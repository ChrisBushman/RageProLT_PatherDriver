/*
 * gld_texture.c - texture upload + bind for the mach64 GL plugin.
 *
 * ARCHITECTURE (recovered by disassembling the reference ATIRage128GLDriver +
 * empirical state-object dumps -- see the project memory):
 *   - GLEngine's gld* texture entry points are pure BOOKKEEPING (set dirty bits
 *     in the texobj); they never hand us pixels.  gldModifyTexture(t, a3&0x80)
 *     just flags the image dirty.
 *   - The real texture image (dims + format + a pointer to GLEngine's own texel
 *     copy) lives in the "state object" at texobj+0x30, populated by draw time.
 *   - So, like the reference, WE do the VRAM upload ourselves at draw time and
 *     program the mach64 texture registers.
 *
 * STATE-OBJECT (texobj+0x30) LAYOUT (confirmed):
 *   state+0x5e (byte)      = base LOD
 *   per-level record @ state + 0x9c + lvl*0x14:
 *     rec+0x00 (u16) width   rec+0x02 (u16) height
 *     rec+0x0c (u16) format  (0x1907 GL_RGB, 0x1908 GL_RGBA)
 *     rec+0x0e (u16) type    (0x1401 GL_UNSIGNED_BYTE)
 *     rec+0x10 (u32) source texel pointer (GLEngine's copy; 32-byte aligned)
 *
 * FIRST CUT: one bound 2D texture, level 0, stored ARGB8888 in VRAM.  The
 * SCALE_3D_CNTL / TEX_CNTL bit values come straight from the reference DRI
 * driver (mesa_mach64_state.c); TEX_SIZE_PITCH and TEX_0_OFF encodings and the
 * S/T convention are best-effort and flagged for hardware validation.
 */
#include <stdlib.h>
#include <pthread.h>  /* g_tex_lock: the VRAM texture heap is process-shared across contexts */
#include <IOKit/IOKitLib.h>   /* IOConnectMethodStructureIStructureO (Path-B selector upload) */
#include "gld.h"
#include "../common/mach64_regs.h"
#include "../common/atiragepro_abi.h"   /* kATIRPro_new_texture (repurposed = VRAM upload) */
#include "../common/mach64_abi.h"        /* Path A user client: kMach64Sel_get_config, MACH64_MEM_* (DMA staging memory) */
#include <libkern/OSByteOrder.h>
#include <mach/mach.h>
#include <mach/mach_time.h>   /* mach64_vram_cpu_begin idle-wait timeout */
#include <stdarg.h>   /* gld_trace_printf */
#include <stdio.h>
#include <unistd.h>   /* access, getpid, fsync */
#include <sys/sysctl.h>
#include <sys/file.h>
#include <fcntl.h>
#include <sys/stat.h>
#include <unistd.h>

/* The texture heap (g_slots / g_heap_next / g_free / g_pending below) is a single
 * process-wide VRAM allocator shared by every GL context.  An app driving multiple
 * contexts from multiple threads can call the upload / delete / dirty paths
 * concurrently, which would corrupt the slot table, free list, and bump pointer.
 * One mutex guards all mutations; it is taken at the PUBLIC entry points only
 * (slot_alloc / texture_deleted / texture_dirtied / set_texheap / the pending drain
 * / the bind-time slot lookup), so the internal helpers heap_alloc / heap_free /
 * slot_find run lock-free with the lock already held.  Uncontended (the common
 * single-threaded case) a mutex is nearly free, and texture alloc is not per-draw. */
static pthread_mutex_t g_tex_lock = PTHREAD_MUTEX_INITIALIZER;
#define TEX_LOCK()   pthread_mutex_lock(&g_tex_lock)
#define TEX_UNLOCK() pthread_mutex_unlock(&g_tex_lock)

/* ---- texture VRAM allocator ------------------------------------------
 * Texels live above the depth buffer (Z @ 0x400000) in the 8 MB VRAM aperture.
 * Each texture gets its own region in [HEAP_BASE, HEAP_END), keyed by its
 * GLEngine state-object pointer (texobj+0x30) -- the one identity we have at
 * BOTH upload time (gldModifyTexture) and draw time (the bound texture).
 *
 * Allocation is a bump pointer (g_heap_next) backed by a free list: delete
 * returns a texture's region to the free list, alloc reuses a free block
 * (best fit) before bumping, adjacent free blocks coalesce, and freeing the
 * block at the top of the heap simply lowers the bump pointer.  So a
 * bind/delete/bind churn no longer leaks VRAM. */
#define MACH64_TEX_HEAP_BASE  0x00600000u
#define MACH64_TEX_HEAP_END   0x00780000u   /* 1.5 MB texture heap */
#define MACH64_TEX_SLOTS      64

/* rsize = bytes actually reserved for this region (>= w*h*4, 32B-aligned).
 * Tracked separately from w/h so a texture re-uploaded SMALLER still frees its
 * full original region instead of leaking the tail.  has_alpha = the texture has
 * a real (GL_RGBA) alpha channel, so TEX_MAP_AEN should be set when it's bound.
 * mip_off[r] = VRAM offset of the mip level whose maxLog2 == r (the register
 * TEX_r_OFF); nlevels = number of loaded mip levels (1 = base only).  clamp_s/t =
 * the app's GL_TEXTURE_WRAP_S/T is a clamp mode (not GL_REPEAT). */
typedef struct {
    uint32_t key, off, w, h, rsize, has_alpha;
    uint32_t mip_off[12];
    int      nlevels;
    uint32_t clamp_s, clamp_t;
    uint32_t bilinear_mag, bilinear_min;   /* GL_LINEAR mag / linear-within-level min filter */
    uint32_t min_filter;                   /* GL_TEXTURE_MIN_FILTER enum */
    uint32_t mipmapped;                    /* MIN filter is one of the four *_MIPMAP_* modes and >1 level is loaded */
    uint32_t lvl_off[12], lvl_w[12], lvl_h[12];   /* per level L (0 = base): VRAM offset and size */
    uint32_t hwfmt, bpt;                   /* hardware texel datatype (6 ARGB8888, 4 RGB565, 3 ARGB1555, 15 ARGB4444) and bytes per texel */
    uint32_t tick;                         /* last-use stamp (LRU eviction) */
    uint32_t h1, h2, hlen;                 /* content hash of the source image(s) at the last upload: an unchanged re-upload is skipped */
    uint32_t gen;                          /* mach64_batch_gen when last bound: still referenced by the pending (unsubmitted) batch if == current */
    void    *texobj;                       /* GLEngine texobj, for re-upload after eviction */
} mach64_tex_slot;
static mach64_tex_slot g_slots[MACH64_TEX_SLOTS];
extern uint32_t mach64_batch_gen;
static int             g_evict_used = 0;               /* set when an eviction victim is referenced by the pending batch */
static uint32_t        g_tick = 0;                     /* LRU clock (one step per bind) */
static uint32_t        g_protect[2] = { 0, 0 };        /* keys of the textures the current draw needs: never evicted */
static uint32_t        g_nevict = 0;                   /* evictions so far (statistics / tests) */
static uint32_t        g_heap_next = MACH64_TEX_HEAP_BASE;
/* Heap window is runtime-relocatable: double-buffered mode needs the VRAM the
 * texture heap normally occupies for the back buffer, so it moves the heap into
 * the smaller tail region.  Must be called before any texture is uploaded. */
static uint32_t        g_heap_base = MACH64_TEX_HEAP_BASE;
static uint32_t        g_heap_end  = MACH64_TEX_HEAP_END;

void mach64_set_texheap(uint32_t base, uint32_t end)
{
    /* The top 2KB of the 8MB aperture is the chip's register window: a texture there is a register write.  Clamp. */
    if (end > MACH64_VRAM_USABLE_END) end = MACH64_VRAM_USABLE_END;
    if (base > end) base = end;
    TEX_LOCK();
    g_heap_base = base; g_heap_end = end; g_heap_next = base;
    TEX_UNLOCK();
    GLD_TRACE("tex: heap set 0x%x-0x%x\n", base, end);
}

typedef struct { uint32_t off, size; } mach64_free_blk;
static mach64_free_blk g_free[MACH64_TEX_SLOTS];
static int             g_nfree;

/* The VRAM under the heap was re-laid out (display mode change): every resident texture's region is gone.  Put each slot in the evicted
 * state (key + texobj kept, rsize 0) so its next bind re-uploads it from GLEngine's copy, forget the content hash so that upload is not
 * skipped, and empty the heap. */
void mach64_tex_invalidate_all(void)
{
    int i;
    TEX_LOCK();
    for (i = 0; i < MACH64_TEX_SLOTS; i++)
        if (g_slots[i].key) { g_slots[i].off = 0; g_slots[i].rsize = 0; g_slots[i].h1 = g_slots[i].h2 = g_slots[i].hlen = 0; }
    g_nfree = 0; g_heap_next = g_heap_base;
    TEX_UNLOCK();
}

static void           *g_pending[16];   /* texobjs image-dirtied, to upload next draw */
static int             g_npending;

static uint32_t tex_align(uint32_t bytes) { return (bytes + 31u) & ~31u; }

static uint32_t tex_key(const void *texobj)
{
    return texobj ? *(const uint32_t *)((const unsigned char *)texobj + 0x30) : 0;
}
static mach64_tex_slot *slot_find(uint32_t key)
{
    int i;
    if (!key) return 0;
    for (i = 0; i < MACH64_TEX_SLOTS; i++) if (g_slots[i].key == key) return &g_slots[i];
    return 0;
}

/* Return [off,off+size) to the heap.  Coalesce with any adjacent free block; if
 * the result sits at the top of the heap, drop the bump pointer instead of
 * listing it (keeps the live region tight so bump allocs keep succeeding). */
static void heap_free(uint32_t off, uint32_t size)
{
    int i;
    if (!size) return;
    for (i = 0; i < g_nfree; ) {
        if (g_free[i].off + g_free[i].size == off) {        /* free block before */
            off = g_free[i].off; size += g_free[i].size;
            g_free[i] = g_free[--g_nfree]; i = 0; continue;
        }
        if (off + size == g_free[i].off) {                  /* free block after  */
            size += g_free[i].size;
            g_free[i] = g_free[--g_nfree]; i = 0; continue;
        }
        i++;
    }
    if (off + size == g_heap_next) { g_heap_next = off; return; }
    if (g_nfree < MACH64_TEX_SLOTS) {
        g_free[g_nfree].off = off; g_free[g_nfree].size = size; g_nfree++;
    } else {
    }
}

/* Reserve `size` bytes: best-fit from the free list (split the remainder back),
 * else bump the heap pointer.  Returns 0 if the heap is exhausted. */
static uint32_t heap_alloc(uint32_t size)
{
    int i, best = -1;
    for (i = 0; i < g_nfree; i++)
        if (g_free[i].size >= size && (best < 0 || g_free[i].size < g_free[best].size))
            best = i;
    if (best >= 0) {
        uint32_t off = g_free[best].off;
        if (g_free[best].size == size) g_free[best] = g_free[--g_nfree];
        else { g_free[best].off += size; g_free[best].size -= size; }
        return off;
    }
    if (g_heap_next + size > g_heap_end) return 0;
    { uint32_t off = g_heap_next; g_heap_next += size; return off; }
}

/* Windowed per-context back+Z VRAM pool.  A PERSISTENT allocator (never reset per-attach, unlike the
 * texture heap) so multiple windowed double-buffered contexts each get their OWN render target instead
 * of sharing one screen-sized back buffer.  It lives in the screen-sized back/Z region the fullscreen
 * layout reserves but windowed mode doesn't use (each windowed ctx takes a window-sized buffer).
 * Lazily initialised on the first windowed attach; guarded by the texture-heap mutex. */
static uint32_t g_winpool_base = 0, g_winpool_end = 0, g_winpool_next = 0;
static mach64_free_blk g_winfree[MACH64_TEX_SLOTS];
static int             g_winnfree = 0;

void mach64_winpool_init(uint32_t base, uint32_t end)
{
    TEX_LOCK();
    if (!g_winpool_base && end > base) {   /* first windowed attach wins; later attaches reuse it */
        g_winpool_base = base; g_winpool_end = end; g_winpool_next = base;
    }
    TEX_UNLOCK();
}

uint32_t mach64_winbuf_alloc(uint32_t bytes)   /* 0 = pool exhausted */
{
    uint32_t sz = (bytes + 31u) & ~31u, off = 0;
    int i, best = -1;
    TEX_LOCK();
    for (i = 0; i < g_winnfree; i++)
        if (g_winfree[i].size >= sz && (best < 0 || g_winfree[i].size < g_winfree[best].size)) best = i;
    if (best >= 0) {
        off = g_winfree[best].off;
        if (g_winfree[best].size == sz) g_winfree[best] = g_winfree[--g_winnfree];
        else { g_winfree[best].off += sz; g_winfree[best].size -= sz; }
    } else if (g_winpool_next && g_winpool_next + sz <= g_winpool_end) {
        off = g_winpool_next; g_winpool_next += sz;
    }
    TEX_UNLOCK();
    return off;
}

void mach64_winbuf_free(uint32_t off, uint32_t bytes)
{
    uint32_t sz = (bytes + 31u) & ~31u;
    if (!off || !sz) return;
    TEX_LOCK();
    if (off + sz == g_winpool_next) g_winpool_next = off;        /* top of pool: drop the bump pointer */
    else if (g_winnfree < MACH64_TEX_SLOTS) { g_winfree[g_winnfree].off = off; g_winfree[g_winnfree].size = sz; g_winnfree++; }
    TEX_UNLOCK();
}

/* Find or allocate a 32-byte-aligned VRAM region of `nbytes` for `key` (a base
 * w*h texture, or a whole mip chain -- caller sizes it).  w/h are the BASE dims
 * stored for the texture registers.  A re-upload reuses its region if the new one
 * still fits; if it grew, the old region is freed before a fresh one is taken. */
static mach64_tex_slot *slot_alloc_locked(uint32_t key, uint32_t w, uint32_t h, uint32_t nbytes)
{
    uint32_t sz = tex_align(nbytes);
    uint32_t off;
    mach64_tex_slot *s = slot_find(key);
    if (s) {
        if (s->rsize >= sz) { s->w = w; s->h = h; return s; }  /* still fits */
        heap_free(s->off, s->rsize);                           /* grew: reclaim */
    } else {
        int i;
        for (i = 0; i < MACH64_TEX_SLOTS; i++) if (!g_slots[i].key) { s = &g_slots[i]; break; }
        if (!s) return 0;
    }
    off = heap_alloc(sz);
    /* Heap full: evict least-recently-used resident textures (never this slot, never the textures the current draw is about to bind) until the request fits.
     * An evicted slot keeps its metadata (key, texobj) with rsize 0; bind_texture re-uploads it from GLEngine's own copy when it is bound again. */
    while (!off) {
        mach64_tex_slot *v = 0; int i;
        for (i = 0; i < MACH64_TEX_SLOTS; i++) {
            mach64_tex_slot *c = &g_slots[i];
            if (!c->key || c == s || !c->rsize) continue;
            if (c->key == g_protect[0] || c->key == g_protect[1]) continue;
            if (!v || c->tick < v->tick) v = c;
        }
        if (!v) break;
        if (v->gen == mach64_batch_gen) g_evict_used = 1;
        heap_free(v->off, v->rsize); v->off = 0; v->rsize = 0; g_nevict++;
        off = heap_alloc(sz);
    }
    if (!off) { GLD_TRACE("tex: heap alloc FAILED %u bytes (heap 0x%x-0x%x next 0x%x)\n", sz, g_heap_base, g_heap_end, g_heap_next); if (s->key != key) s->key = 0; return 0; }
    s->key = key; s->off = off; s->w = w; s->h = h; s->rsize = sz;
    return s;
}

/* Locking wrapper around slot_alloc_locked (single critical section over the slot table
 * + heap).  The returned slot is used unlocked by the caller, which is safe because a slot
 * belongs to one texture key: concurrent uploads target different keys/slots (two threads
 * modifying the SAME bound texture is a GL user error). */
static mach64_tex_slot *slot_alloc(uint32_t key, uint32_t w, uint32_t h, uint32_t nbytes)
{
    mach64_tex_slot *s;
    TEX_LOCK();
    s = slot_alloc_locked(key, w, h, nbytes);
    TEX_UNLOCK();
    return s;
}

/* GLEngine records the currently-bound texture's texobj (our gldCreateTexture
 * result) as a gli field at +0x15748 (recovered by a within-process gli-region
 * diff across two differently-bound draws, tools/mach64_texmulti).  bind_texture
 * reads it to select the right VRAM region. */
#define GLI_BOUND_TEXOBJ  0x15748

/* gldModifyTexture(a3&0x80): queue this texobj for upload at the next draw (its
 * state object isn't populated until then).  gldModifyTexture is NOT a bind hook
 * -- binding is read from the gli at draw time. */
void mach64_texture_dirtied(void *texobj)
{
    int i;
    if (!texobj) return;
    TEX_LOCK();
    for (i = 0; i < g_npending; i++) if (g_pending[i] == texobj) { TEX_UNLOCK(); return; }
    if (g_npending < 16) g_pending[g_npending++] = texobj;
    TEX_UNLOCK();
}
void mach64_texture_bound(void *texobj) { (void)texobj; }   /* no bind hook; see gli */

void mach64_texture_deleted(void *texobj)
{
    mach64_tex_slot *s;
    TEX_LOCK();
    s = slot_find(tex_key(texobj));
    if (s) { heap_free(s->off, s->rsize); s->key = 0; }   /* reclaim VRAM + slot */
    TEX_UNLOCK();
}

static uint32_t ilog2u(uint32_t v) { uint32_t r = 0; while (v > 1) { v >>= 1; r++; } return r; }

/* Texels per host-data submit: the scratch command buffer is 1024 dwords; one
 * blit setup (~13 regs) + count texels + a packet header per 16 + terminator must
 * fit, so cap a chunk at 768 texels (leaves comfortable headroom).  Larger
 * textures are uploaded in several chunks (mach64_upload). */
#define MACH64_BLIT_MAX  768u

/* Convert texel `i` of `src` (in GLEngine's base fmt/type) to native ARGB8888. */
static uint32_t tex_texel_argb(const unsigned char *src, uint32_t i, uint16_t fmt, uint16_t type)
{
    if (type == 0x8363u) {                       /* GL_UNSIGNED_SHORT_5_6_5 (BE u16) */
        unsigned v  = ((unsigned)src[i*2] << 8) | src[i*2 + 1];
        unsigned r5 = (v >> 11) & 0x1fu, g6 = (v >> 5) & 0x3fu, b5 = v & 0x1fu;
        unsigned R  = (r5 << 3) | (r5 >> 2), G = (g6 << 2) | (g6 >> 4), B = (b5 << 3) | (b5 >> 2);
        return 0xff000000u | (R << 16) | (G << 8) | B;
    }
    if (type == 0x8033u) {                       /* GL_UNSIGNED_SHORT_4_4_4_4: R15..12 G B A3..0 */
        unsigned v = ((unsigned)src[i*2] << 8) | src[i*2 + 1];
        return (((v & 0xfu) * 17u) << 24) | ((((v >> 12) & 0xfu) * 17u) << 16) | ((((v >> 8) & 0xfu) * 17u) << 8) | (((v >> 4) & 0xfu) * 17u);
    }
    if (type == 0x8034u) {                       /* GL_UNSIGNED_SHORT_5_5_5_1: R15..11 G B A0 */
        unsigned v = ((unsigned)src[i*2] << 8) | src[i*2 + 1];
        unsigned r5 = (v >> 11) & 0x1fu, g5 = (v >> 6) & 0x1fu, b5 = (v >> 1) & 0x1fu;
        return ((v & 1u) ? 0xff000000u : 0u) | (((r5 << 3) | (r5 >> 2)) << 16) | (((g5 << 3) | (g5 >> 2)) << 8) | ((b5 << 3) | (b5 >> 2));
    }
    switch (fmt) {
    case 0x1908u: { const unsigned char *px = src + i*4;   /* GL_RGBA */
        return ((uint32_t)px[3]<<24)|((uint32_t)px[0]<<16)|((uint32_t)px[1]<<8)|px[2]; }
    case 0x1909u: { unsigned L = src[i];                   /* GL_LUMINANCE */
        return 0xff000000u|(L<<16)|(L<<8)|L; }
    case 0x8049u: { unsigned I = src[i];                   /* GL_INTENSITY */
        return (I<<24)|(I<<16)|(I<<8)|I; }
    case 0x1906u: { unsigned A = src[i];                   /* GL_ALPHA */
        return (A<<24); }
    case 0x190Au: { const unsigned char *px = src + i*2;   /* GL_LUMINANCE_ALPHA */
        unsigned L = px[0], A = px[1]; return (A<<24)|(L<<16)|(L<<8)|L; }
    default:      { const unsigned char *px = src + i*3;   /* GL_RGB (and fallback) */
        return 0xff000000u|((uint32_t)px[0]<<16)|((uint32_t)px[1]<<8)|px[2]; }
    }
}

/* Texel `i` as a hardware 16-bit texel of datatype `hwfmt` (4 RGB565, 15 ARGB4444, 3 ARGB1555).  Packed 16-bit sources whose layout already matches (or only
 * needs a field shuffle) are converted exactly; anything else goes through ARGB8888 and is truncated. */
static uint32_t tex_texel_hw16(const unsigned char *src, uint32_t i, uint16_t fmt, uint16_t type, uint32_t hwfmt)
{
    unsigned v = 0;
    if (hwfmt == 11u || hwfmt == 12u) return (unsigned)src[i*2] | ((unsigned)src[i*2 + 1] << 8);   /* GL_APPLE_ycbcr_422: VRAM bytes = source bytes */
    if (type == 0x8363u && hwfmt == 4u)  { v = ((unsigned)src[i*2] << 8) | src[i*2 + 1]; return v; }                                   /* 5_6_5 -> RGB565: identical */
    if (type == 0x8033u && hwfmt == 15u) { v = ((unsigned)src[i*2] << 8) | src[i*2 + 1];                                              /* RGBA4444 -> ARGB4444 */
        return ((v & 0xfu) << 12) | (((v >> 12) & 0xfu) << 8) | (((v >> 8) & 0xfu) << 4) | ((v >> 4) & 0xfu); }
    if (type == 0x8034u && hwfmt == 3u)  { v = ((unsigned)src[i*2] << 8) | src[i*2 + 1];                                              /* RGB5_A1 -> ARGB1555 */
        return ((v & 1u) << 15) | (((v >> 11) & 0x1fu) << 10) | (((v >> 6) & 0x1fu) << 5) | ((v >> 1) & 0x1fu); }
    {   uint32_t a = tex_texel_argb(src, i, fmt, type), A = a >> 24, R = (a >> 16) & 0xffu, G = (a >> 8) & 0xffu, B = a & 0xffu;
        if (hwfmt == 4u)  return ((R >> 3) << 11) | ((G >> 2) << 5) | (B >> 3);
        if (hwfmt == 15u) return ((A >> 4) << 12) | ((R >> 4) << 8) | ((G >> 4) << 4) | (B >> 4);
        return ((A >> 7) << 15) | ((R >> 3) << 10) | ((G >> 3) << 5) | (B >> 3);                                                     /* ARGB1555 */
    }
}

/* PATH B texel upload: the host-data blit (HOST_DATA0 FIFO feed) can't be expressed by Apple's PIO
 * {reg,val} submit and WEDGES the engine (see the guard note in mach64_bind_texture).  Instead, hand
 * the ARGB texels to the PATCHED new_texture selector (GL sel 10), which the kext turns into a plain
 * VRAM memcpy: struct in = { u32 dst_off; u32 argb[] } -> lfb[dst_off..] = argb.  Chunked to keep the
 * IOKit struct input a sane size.  REQUIRES the upload-patched kext (ATIRagePro.patched2). */
/* CORRECTION (Nov 21, colortest on the MDD): the swap is an EXCHANGE OF THE TWO 16-BIT HALVES of each 32-bit word (0xAARRGGBB is sampled as 0xGGBBAARR), NOT a
 * full byte reversal: reversal only looked right for the symmetric test value 0xffff0000.  Pure red/green/blue RGBA and RGBA4444 textures proved it.
 * OBSERVED (Nov 20, headless setup): the patched kext's upload stores each ARGB word we pass into VRAM swapped relative to what the
 * engine samples (red 0xffff0000 read back as 0x0000ffff).  In that setup the VRAM view handed to the Apple kernel driver comes from RageStubFB,
 * which returns the plain little-endian aperture (BAR0 +0, 8MB).  Why this was not needed with Apple's own framebuffer driver is NOT established
 * (hypothesis: a byte-swapped VRAM view); the swap is therefore applied only when RageStubFB is present, so the Apple-FB path is unchanged. */
static int tex_need_bswap(void)
{
    static int v = -1;
    if (v < 0) {
        io_service_t sv = IOServiceGetMatchingService(kIOMasterPortDefault, IOServiceMatching("RageStubFB"));
        v = (sv != 0); if (sv) IOObjectRelease(sv);
    }
    return v;
}

static void mach64_upload_texels_pathb(gld_context *ctx, uint32_t dst_off,
                                       const unsigned char *src, uint32_t first, uint32_t count,
                                       uint16_t fmt, uint16_t type, uint32_t bpt, uint32_t hwfmt)
{
    io_connect_t gc = ctx->connect;
    uint32_t i = first, end = first + count;
    int swap = tex_need_bswap();
#define TEX_CHUNK 2000u                     /* texels/call -> struct <= 8 + 8000 bytes */
    while (i < end) {
        uint32_t n = end - i; if (n > TEX_CHUNK) n = TEX_CHUNK;
        uint32_t buf[2 + TEX_CHUNK];        /* [0]=op(UPLOAD), [1]=dst byte offset, [2..]=ARGB texels */
        uint32_t out[2] = {0,0}; IOByteCount osz = sizeof out;
        uint32_t k;
        buf[0] = kATIRPro_TEXOP_UPLOAD;
        buf[1] = dst_off + (i - first) * bpt;
        if (bpt == 2u) {   /* two 16-bit texels per word: the hardware sees little-endian u16s, texel 0 in the low half (same word-level swap as ARGB8888) */
            uint32_t nw = (n + 1u) / 2u, base = i;
            for (k = 0; k < nw; k++) {
                uint32_t t0 = tex_texel_hw16(src, base + 2u*k, fmt, type, hwfmt);
                uint32_t t1 = (2u*k + 1u < n) ? tex_texel_hw16(src, base + 2u*k + 1u, fmt, type, hwfmt) : 0u;
                uint32_t a = t0 | (t1 << 16);
                buf[2+k] = swap ? ((a << 16) | (a >> 16)) : a;
            }
            i += n;
            IOConnectMethodStructureIStructureO(gc, kATIRPro_new_texture, (uint32_t)(8u + nw*4u), &osz, buf, out);
            continue;
        }
        if (bpt == 1u) {   /* Y8: four luminance texels per word, texel 0 in the low byte */
            uint32_t nw = (n + 3u) / 4u, base = i, j;
            for (k = 0; k < nw; k++) {
                uint32_t a = 0;
                for (j = 0; j < 4u && 4u*k + j < n; j++) a |= (uint32_t)src[base + 4u*k + j] << (8u*j);
                buf[2+k] = swap ? ((a << 16) | (a >> 16)) : a;
            }
            i += n;
            IOConnectMethodStructureIStructureO(gc, kATIRPro_new_texture, (uint32_t)(8u + nw*4u), &osz, buf, out);
            continue;
        }
        for (k = 0; k < n; k++, i++) {
            uint32_t a = tex_texel_argb(src, i, fmt, type);
            buf[2+k] = swap ? ((a << 16) | (a >> 16)) : a;
        }
        IOConnectMethodStructureIStructureO(gc, kATIRPro_new_texture,
                                            (uint32_t)(8u + n*4u), &osz, buf, out);
    }
#undef TEX_CHUNK
}


/* ---- Bus-mastered texture upload (DEFAULT ON; MACH64_DMA_UPLOAD=0 disables; see docs/re-notes/dma-upload-design.md) ------------------------------
 * The kext-memcpy upload costs the CPU about 5.6 ms per 112 KB; filling an uncached staging buffer costs about 0.5 ms and the Rage Pro's *system* bus-master
 * transfer (Programmer's Guide 8.10.3) then copies it to VRAM without the draw engine or register FIFO (measured: 45 MB/s, safe next to concurrent 2D fills).
 *   staging + descriptor table : the Path A kext's physically contiguous, uncached command buffer (128 KB) and ring, opened as a SECOND connection
 *   kick / completion          : direct register access through that connection's BAR map (BM_SYSTEM_TABLE 0x1bc; done when the table pointer reaches the table end and
 *                                BM_COMMAND shows end-of-list).  Not via Apple's drain: the plugin's drain is one flush per frame (its position in the frame is fixed by the batch/clear ordering), and Apple's kext consumes only the FIRST drain of a frame (measured, dma-upload-design.md section 7), so a mid-frame kick through a drain would be lost; a direct register write does not depend on it.
 *   exclusion                  : flock() around fill -> kick -> completion (the staging buffer and table are global to the kext, so processes must not overlap)
 * Version 1 is synchronous: it returns after the transfer completes, yielding the CPU while it waits.  Any problem (Path A missing, address outside RAM, BUS_CNTL not already
 * bus-master enabled, timeout) disables the path for the process and the caller falls back to the kext memcpy upload. */
#define M64_DMA_STAGE   0x20000u
#define M64_DMA_MINBYTES 4096u            /* smaller uploads are cheaper through the kext call */
#define M64_DMA_BM_SYSTEM_TABLE 0x01bcu
#define M64_DMA_BM_COMMAND      0x0188u
#define M64_DMA_BUS_CNTL        0x04a0u
#define M64_DMA_EOL (1u << 31)
static struct {
    int state;                            /* 0 untried, 1 ready, -1 unavailable */
    io_connect_t conn;
    volatile uint8_t *cmd, *ring, *regs;  /* staging, descriptor table, BAR0 register window (BAR + 0x7ff800) */
    volatile uint8_t *vram;               /* BAR0 itself: the little-endian VRAM view, CPU offset == engine VRAM offset */
    uint32_t cmdbus, ringbus, prev_end;
    int lockfd;
    size_t cmdsz, ringsz;                 /* sizes of the Path A staging/ring maps (the single-thread engine needs the 384 KB / 64 KB kext) */
} g_dma;
/* One process-wide, re-entrant cross-process lock for every bus-master user (texture DMA, the kernel-handler flush, the single-thread engine): the engine holds it while a batch is in
 * flight, and a texture upload in the same process must still be able to run then (the system bus master is safe next to the GUI one). */
static int g_eng_lockfd = -2, g_eng_lockdepth = 0;
void mach64_eng_lock(void)
{
    if (g_eng_lockfd == -2) { g_eng_lockfd = open("/var/tmp/mach64_dma.lock", O_CREAT | O_RDWR, 0666); if (g_eng_lockfd >= 0) fchmod(g_eng_lockfd, 0666); }
    if (g_eng_lockdepth++ == 0 && g_eng_lockfd >= 0) flock(g_eng_lockfd, LOCK_EX);
}
void mach64_eng_unlock(void) { if (g_eng_lockdepth > 0 && --g_eng_lockdepth == 0 && g_eng_lockfd >= 0) flock(g_eng_lockfd, LOCK_UN); }
/* Path A handles for the single-thread engine (gld_engine.c); opens Path A like the texture path does. */
static int dma_ready_pathA(void);
int mach64_pathA(io_connect_t *conn, volatile uint8_t **regs, volatile uint8_t **cmd, volatile uint8_t **ring, uint32_t *cmdbus, uint32_t *ringbus, size_t *cmdsz, size_t *ringsz)
{
    if (!dma_ready_pathA()) return 0;
    *conn = g_dma.conn; *regs = g_dma.regs; *cmd = g_dma.cmd; *ring = g_dma.ring; *cmdbus = g_dma.cmdbus; *ringbus = g_dma.ringbus; *cmdsz = g_dma.cmdsz; *ringsz = g_dma.ringsz;
    return 1;
}
static unsigned dma_rd(uint32_t reg) { return OSReadLittleInt32((void *)g_dma.regs, reg); }
/* Display-mode generation from Mach64Accelerator (odd while a mode change is in progress).  Its own connection: the DMA path can be
 * disabled for the process (dma_disable) and mode tracking must keep working regardless.  An older kext has no selector 19: the call
 * fails and this stays 0, so mode tracking is simply off. */
uint32_t mach64_mode_gen(void)
{
    static io_connect_t mc = 0; static int tried = 0; static uint32_t last = 0xffffffffu;
    uint32_t gen = 0;
    if (!tried) {
        io_service_t sv = IOServiceGetMatchingService(kIOMasterPortDefault, IOServiceMatching("Mach64Accelerator"));
        tried = 1;
        if (sv) { if (IOServiceOpen(sv, mach_task_self(), MACH64_GL_CONNECT_TYPE, &mc)) mc = 0; IOObjectRelease(sv); }
        GLD_TRACE("mode_gen: connection %s\n", mc ? "open" : "UNAVAILABLE");
    }
    if (!mc || IOConnectMethodScalarIScalarO(mc, kMach64Sel_get_mode_gen, 0, 1, &gen) != KERN_SUCCESS) gen = 0;
    if (gen != last) { GLD_TRACE("mode_gen: %u\n", gen); last = gen; }
    return gen;
}

/* ---- opt-in trace ---- */
int g_gld_trace = -1;   /* -1 = not decided yet: the first gld_trace_init() reads MACH64_TRACE / /tmp/gld-trace-on */
static int g_trace_fd = -1;
static void gld_trace_init(void)
{
    const char *e = getenv("MACH64_TRACE");
    char path[64];
    g_gld_trace = (e && atoi(e)) || access("/tmp/gld-trace-on", F_OK) == 0;
    if (!g_gld_trace) return;
    snprintf(path, sizeof path, "/tmp/gld-trace-%d.log", (int)getpid());
    g_trace_fd = open(path, O_CREAT | O_WRONLY | O_APPEND, 0666);
    if (g_trace_fd < 0) g_gld_trace = 0;
}
void gld_trace_printf(const char *fmt, ...)
{
    char buf[512]; int n; va_list ap;
    uint64_t t = mach_absolute_time(); static mach_timebase_info_data_t tb;
    if (g_gld_trace < 0) gld_trace_init();
    if (g_gld_trace <= 0) return;
    if (!tb.denom) mach_timebase_info(&tb);
    n = snprintf(buf, sizeof buf, "%10.3f [%p] ", (double)(t * tb.numer / tb.denom) / 1e6, (void *)pthread_self());
    va_start(ap, fmt); n += vsnprintf(buf + n, sizeof buf - (size_t)n, fmt, ap); va_end(ap);
    if (n > (int)sizeof buf - 1) n = (int)sizeof buf - 1;
    write(g_trace_fd, buf, (size_t)n);
    fsync(g_trace_fd);
}
/* CPU view of VRAM for the pixel paths (glReadPixels / glDrawPixels / glBitmap): BAR0 from Mach64Accelerator, offsets == engine VRAM
 * offsets, usable span [0, MACH64_VRAM_USABLE_END).  NOT ctx->regs -- that is Apple's type-1 COMMAND buffer (~768 KB); treating it as VRAM
 * read past its end (Chess: SIGBUS in glReadPixels) or, for small offsets, overwrote queued engine commands.  Drains every queued and in-flight
 * engine job and waits for the GUI engine to go idle first, so reads see finished pixels and writes are not overdrawn by a pending batch.
 * Returns 0 (caller skips the op) if Path A is unavailable or the engine never goes idle. */
volatile unsigned char *mach64_vram_cpu_begin(void)
{
    extern void mach64_dma_fence(void);
    uint64_t t0;
    unsigned long n = 0;
    if (!dma_ready_pathA()) return 0;
    mach64_dma_fence();
    t0 = mach_absolute_time();
    while ((dma_rd(MACH64_FIFO_STAT) & 0xffffu) || (dma_rd(MACH64_GUI_STAT) & 1u)) {
        if ((++n & 0x3ffu) == 0) {
            static mach_timebase_info_data_t tb;
            if (!tb.denom) mach_timebase_info(&tb);
            if ((mach_absolute_time() - t0) * tb.numer / tb.denom > 1000000000ull) return 0;   /* 1 s: wedged engine -> skip, don't spin forever */
        }
    }
    return (volatile unsigned char *)g_dma.vram;
}
static void dma_wr(uint32_t reg, uint32_t v) { OSWriteLittleInt32((void *)g_dma.regs, reg, v); }
static void dma_mb(void) { __asm__ volatile("sync; eieio" ::: "memory"); }
static int dma_map(io_connect_t c, unsigned type, vm_address_t *a, vm_size_t *sz)
{
    *a = 0; *sz = 0;
    return IOConnectMapMemory(c, type, mach_task_self(), a, sz, kIOMapAnywhere | kIOMapInhibitCache) == KERN_SUCCESS && *a;
}
#define dma_disable() do { GLD_TRACE("dma: DISABLED at %s:%d (BUS_CNTL=0x%08x)\n", __func__, __LINE__, g_dma.regs ? dma_rd(M64_DMA_BUS_CNTL) : 0u); g_dma.state = -1; } while (0)
static int dma_ready(void);
static int dma_ready_pathA(void) { return dma_ready(); }
static int dma_ready(void)
{
    if (g_dma.state) return g_dma.state > 0;
    g_dma.state = -1;
    {   io_service_t sv = IOServiceGetMatchingService(kIOMasterPortDefault, IOServiceMatching("Mach64Accelerator"));
        vm_address_t a; vm_size_t sz; uint32_t cmdbus = 0, ringbus = 0, cfg = 0; unsigned physmem = 0; size_t pl = sizeof physmem;
        if (!sv) return 0;
        if (IOServiceOpen(sv, mach_task_self(), MACH64_GL_CONNECT_TYPE, &g_dma.conn)) { IOObjectRelease(sv); return 0; }
        IOObjectRelease(sv);
        if (!dma_map(g_dma.conn, MACH64_MEM_SURFACE, &a, &sz) || sz < 0x800000u) return 0;
        g_dma.vram = (volatile uint8_t *)a;
        g_dma.regs = (volatile uint8_t *)a + MACH64_REG_WINDOW;
        if (!dma_map(g_dma.conn, MACH64_MEM_CMDBUF, &a, &sz) || sz < M64_DMA_STAGE) return 0;
        g_dma.cmd = (volatile uint8_t *)a; g_dma.cmdsz = sz;
        if (!dma_map(g_dma.conn, MACH64_MEM_RING, &a, &sz) || sz < 0x4000u) return 0;
        g_dma.ring = (volatile uint8_t *)a; g_dma.ringsz = sz;
        if (IOConnectMethodScalarIScalarO(g_dma.conn, kMach64Sel_get_config, 0, 3, &cmdbus, &ringbus, &cfg)) return 0;
        /* the bus addresses must be real RAM: a wrong one makes the chip DMA over arbitrary memory (an earlier test panicked on ~0xd0320000) */
        if (sysctlbyname("hw.physmem", &physmem, &pl, 0, 0) || !physmem) return 0;
        if (cmdbus < 0x1000u || (uint64_t)cmdbus + M64_DMA_STAGE > physmem || ringbus < 0x1000u || (uint64_t)ringbus + 0x4000u > physmem || (ringbus & 0xfu) || (cmdbus & 0xfffu)) {
            return 0; }
        g_dma.cmdbus = cmdbus; g_dma.ringbus = ringbus; g_dma.prev_end = 0;
        GLD_TRACE("dma_ready: maps ok cmdbus=0x%x ringbus=0x%x BUS_CNTL=0x%08x\n", cmdbus, ringbus, dma_rd(M64_DMA_BUS_CNTL));
        g_dma.lockfd = open("/var/tmp/mach64_dma.lock", O_CREAT | O_RDWR, 0666);
        if (g_dma.lockfd >= 0) fchmod(g_dma.lockfd, 0666);   /* a root-created lock (umask 022) must stay usable by every user, else DMA upload silently falls back to the engine path */
        if (g_dma.lockfd < 0) { GLD_TRACE("dma_ready: lock file unusable\n"); return 0; }
        g_dma.state = 1;
    }
    return 1;
}
/* Convert texels [first, first+n) of `src` into the staging buffer (little-endian words: what lands in VRAM is byte-for-byte what the engine reads; the DMA path does not go
 * through any aperture byte swap). */
static void dma_fill(const unsigned char *src, uint32_t first, uint32_t n, uint16_t fmt, uint16_t type, uint32_t bpt, uint32_t hwfmt)
{
    uint32_t k, nw = (n * bpt + 3u) / 4u;
    /* Fast paths for the common formats (the generic per-texel converters dominate the CPU cost once the copy itself is offloaded). */
    if (bpt == 4u && type == 0x1401u && (fmt == 0x1908u || fmt == 0x1907u)) {
        const unsigned char *p = src + (size_t)first * (fmt == 0x1908u ? 4u : 3u);
        if (fmt == 0x1908u) for (k = 0; k < n; k++, p += 4) OSWriteLittleInt32((void *)g_dma.cmd, k * 4u, ((uint32_t)p[3] << 24) | ((uint32_t)p[0] << 16) | ((uint32_t)p[1] << 8) | p[2]);   /* RGBA -> ARGB */
        else                   for (k = 0; k < n; k++, p += 3) OSWriteLittleInt32((void *)g_dma.cmd, k * 4u, 0xff000000u | ((uint32_t)p[0] << 16) | ((uint32_t)p[1] << 8) | p[2]);         /* RGB */
        return;
    }
    if (bpt == 2u && type == 0x8363u && hwfmt == 4u) {                       /* 5_6_5 big-endian u16 -> RGB565 little-endian u16, two texels per word */
        const unsigned char *p = src + (size_t)first * 2u;
        for (k = 0; k < nw; k++, p += 4) {
            uint32_t t0 = ((uint32_t)p[0] << 8) | p[1], t1 = (2u * k + 1u < n) ? (((uint32_t)p[2] << 8) | p[3]) : 0u;
            OSWriteLittleInt32((void *)g_dma.cmd, k * 4u, t0 | (t1 << 16));
        }
        return;
    }
    for (k = 0; k < nw; k++) {
        uint32_t a = 0;
        if (bpt == 4u) a = tex_texel_argb(src, first + k, fmt, type);
        else if (bpt == 2u) {
            uint32_t i0 = first + 2u * k;
            a = tex_texel_hw16(src, i0, fmt, type, hwfmt) | ((2u * k + 1u < n) ? (tex_texel_hw16(src, i0 + 1u, fmt, type, hwfmt) << 16) : 0u);
        } else { uint32_t j; for (j = 0; j < 4u && 4u * k + j < n; j++) a |= (uint32_t)src[first + 4u * k + j] << (8u * j); }
        OSWriteLittleInt32((void *)g_dma.cmd, k * 4u, a);
    }
}
/* One transfer of up to 128 KB: texels [first, first+n) -> VRAM byte offset dst.  Returns 1 on success. */
static int dma_xfer(uint32_t dst, const unsigned char *src, uint32_t first, uint32_t n, uint16_t fmt, uint16_t type, uint32_t bpt, uint32_t hwfmt)
{
    uint32_t bytes = (n * bpt + 3u) & ~3u, nd = (bytes + 4095u) / 4096u, i, tbl, end, spins;
    int ok = 0;
    if (!mach64_vram_span_ok(dst, bytes) || (dst & 3u)) return 0;
    mach64_eng_lock();
    { uint32_t bus = dma_rd(M64_DMA_BUS_CNTL);    /* bus mastering must already be on: we do not touch BUS_CNTL (shared with Apple's kext) */
      if ((bus & (1u << 6)) || !(bus & (1u << 27))) { GLD_TRACE("dma_xfer: bus mastering off (BUS_CNTL=0x%08x)\n", bus); mach64_eng_unlock(); dma_disable(); return 0; } }
    dma_fill(src, first, n, fmt, type, bpt, hwfmt);
    tbl = g_dma.ringbus; if (tbl + nd * 16u == g_dma.prev_end) tbl += 0x2000u;       /* a table end different from the last one, so "pointer at end" cannot be stale */
    end = tbl + nd * 16u;
    for (i = 0; i < nd; i++) {
        uint32_t cnt = (i == nd - 1u) ? bytes - (nd - 1u) * 4096u : 4096u;
        OSWriteLittleInt32((void *)g_dma.ring, (tbl - g_dma.ringbus) + i * 16u + 0, dst + i * 4096u);
        OSWriteLittleInt32((void *)g_dma.ring, (tbl - g_dma.ringbus) + i * 16u + 4, g_dma.cmdbus + i * 4096u);
        OSWriteLittleInt32((void *)g_dma.ring, (tbl - g_dma.ringbus) + i * 16u + 8, cnt | ((i == nd - 1u) ? M64_DMA_EOL : 0u));
        OSWriteLittleInt32((void *)g_dma.ring, (tbl - g_dma.ringbus) + i * 16u + 12, 0);
    }
    dma_mb();
    dma_wr(M64_DMA_BM_SYSTEM_TABLE, tbl);                                      /* SYSTEM_TRIGGER 0: system memory -> frame buffer, starts now */
    for (spins = 0; spins < 200000u; spins++) {
        if (dma_rd(M64_DMA_BM_SYSTEM_TABLE) == end && dma_rd(M64_DMA_BM_COMMAND) == 0x80000000u) { ok = 1; break; }
        if (spins > 50u) usleep(20);                                           /* yield the CPU instead of spinning on PCI reads */
    }
    if (ok) g_dma.prev_end = end;
    mach64_eng_unlock();
    if (!ok) { GLD_TRACE("dma_xfer: TIMEOUT dst=0x%x bytes=%u table=0x%x/0x%x cmd=0x%08x\n", dst, bytes, dma_rd(M64_DMA_BM_SYSTEM_TABLE), end, dma_rd(M64_DMA_BM_COMMAND)); dma_disable(); }
    return ok;
}
/* Upload one whole image through DMA, in <=128 KB transfers.  Returns 1 when the whole image is in VRAM, 0 to let the caller use the kext path. */
static int dma_upload_image(uint32_t dst_off, uint32_t n, const unsigned char *src, uint16_t fmt, uint16_t type, uint32_t bpt, uint32_t hwfmt)
{
    uint32_t per = M64_DMA_STAGE / bpt, first = 0;
    if (n * bpt < M64_DMA_MINBYTES || (bpt != 1u && bpt != 2u && bpt != 4u) || !dma_ready()) return 0;
    while (first < n) {
        uint32_t c = (n - first < per) ? (n - first) : per;
        if (!dma_xfer(dst_off + first * bpt, src, first, c, fmt, type, bpt, hwfmt)) return 0;   /* the caller re-uploads the whole image through the kext (idempotent) */
        first += c;
    }
    return 1;
}
/* One host-data DMA blit of `count` ARGB8888 texels (converted from src[first..])
 * into VRAM at byte offset `dst_off`, laid out `bw` wide x `bh` tall with row
 * pitch `pitch_units`*8 pixels.  count must be <= MACH64_BLIT_MAX. */
static void mach64_blit_texels(gld_context *ctx, uint32_t dst_off, uint32_t pitch_units,
                               uint32_t bw, uint32_t bh, const unsigned char *src,
                               uint32_t first, uint32_t count, uint16_t fmt, uint16_t type, uint32_t bpt, uint32_t hwfmt)
{
    mach64_priv *p = MACH64_PRIV(ctx);
    m64_cmdbuf cb;
    uint32_t i;
    if (p->apple_surf_cn) {                 /* PATH B: host-data blit wedges -> use the kext selector */
        mach64_upload_texels_pathb(ctx, dst_off, src, first, count, fmt, type, bpt, hwfmt);
        return;
    }
    m64_cb_init(&cb, p->scratch, sizeof(p->scratch) / sizeof(p->scratch[0]));
    m64_reg(&cb, MACH64_Z_CNTL,        0);
    m64_reg(&cb, MACH64_SCALE_3D_CNTL, 0);
    m64_reg(&cb, MACH64_SC_LEFT_RIGHT, (8191u << 16));
    m64_reg(&cb, MACH64_SC_TOP_BOTTOM, (16383u << 16));
    m64_reg(&cb, MACH64_CLR_CMP_CNTL,  0);
    m64_reg(&cb, MACH64_GUI_TRAJ_CNTL, (1u << 0) | (1u << 1));
    m64_reg(&cb, MACH64_DP_PIX_WIDTH,  0x60060666u);
    m64_reg(&cb, MACH64_DP_WRITE_MASK, 0xffffffffu);
    m64_reg(&cb, MACH64_DP_MIX,        MACH64_DP_MIX_DS);
    m64_reg(&cb, MACH64_DP_SRC,        MACH64_DP_SRC_HOST);
    m64_reg(&cb, MACH64_DST_OFF_PITCH, (pitch_units << 22) | (dst_off >> 3));
    m64_reg(&cb, MACH64_DST_X_Y,       0);
    m64_reg(&cb, MACH64_DST_WIDTH_HEIGHT, (bh << 16) | bw);   /* starts the blit */
    for (i = first; i < first + count; ) {
        uint32_t chunk[16], k = 0;
        while (k < 16 && i < first + count) {
            uint32_t argb;
            /* GLEngine hands texels in the app's base format, tightly packed (fmt @ level rec+0x0c,
             * the pixel TYPE @ +0x0e).  Convert to native ARGB8888.
             * 1-byte formats (LUMINANCE/ALPHA/INTENSITY) and 2-byte (LUMINANCE_ALPHA) expand to
             * grey/alpha.  Packed types are keyed on `type` (fmt stays the base RGB code). */
            if (type == 0x8363u) {   /* GL_UNSIGNED_SHORT_5_6_5: packed RGB in one native (big-endian) u16 */
                unsigned v  = ((unsigned)src[i*2] << 8) | src[i*2 + 1];
                unsigned r5 = (v >> 11) & 0x1fu, g6 = (v >> 5) & 0x3fu, b5 = v & 0x1fu;
                unsigned R  = (r5 << 3) | (r5 >> 2);   /* 5/6-bit -> 8-bit with bit replication */
                unsigned G  = (g6 << 2) | (g6 >> 4);
                unsigned B  = (b5 << 3) | (b5 >> 2);
                argb = 0xff000000u | (R << 16) | (G << 8) | B;
                chunk[k++] = argb; i++;
                continue;
            }
            switch (fmt) {
            case 0x1908u: { const unsigned char *px = src + i*4;   /* GL_RGBA  */
                argb = ((uint32_t)px[3]<<24)|((uint32_t)px[0]<<16)|((uint32_t)px[1]<<8)|px[2]; break; }
            case 0x1909u: { unsigned L = src[i];                   /* GL_LUMINANCE -> grey, opaque */
                argb = 0xff000000u|(L<<16)|(L<<8)|L; break; }
            case 0x8049u: { unsigned I = src[i];                   /* GL_INTENSITY -> grey, A = I */
                argb = (I<<24)|(I<<16)|(I<<8)|I; break; }
            case 0x1906u: { unsigned A = src[i];                   /* GL_ALPHA -> RGB 0, A */
                argb = (A<<24); break; }
            case 0x190Au: { const unsigned char *px = src + i*2;   /* GL_LUMINANCE_ALPHA */
                unsigned L = px[0], A = px[1]; argb = (A<<24)|(L<<16)|(L<<8)|L; break; }
            default:      { const unsigned char *px = src + i*3;   /* GL_RGB (and fallback) */
                argb = 0xff000000u|((uint32_t)px[0]<<16)|((uint32_t)px[1]<<8)|px[2]; break; }
            }
            chunk[k++] = argb; i++;
        }
        m64_regs(&cb, MACH64_HOST_DATA0, chunk, (int)k);
    }
    mach64_submit(ctx, &cb);
}

/* Upload ONE w x h ARGB image from `src` (contiguous, GLEngine fmt) into VRAM at
 * byte offset dst_off.  mach64 textures are stored LINEARLY at natural width
 * (2^widthLog2); TEX_SIZE_PITCH has no separate pitch, so the unit reads texel
 * (s,t) at base + (t*w + s)*4.  DST_OFF_PITCH's pitch is in 8-pixel units (min 1
 * = 8px), so a rectangular w x h blit only lands rows at the natural stride when
 * w is a multiple of 8 -- true for w>=8; for w<8 blit a linear 1-row run instead
 * (pitch irrelevant, packed at stride w).  Larger-than-one-buffer images are split
 * into <=MACH64_BLIT_MAX chunks, each at its own sub-offset.  Returns 0 if w is
 * too wide to chunk by rows. */
static int mach64_upload_image(gld_context *ctx, uint32_t dst_off,
                               uint32_t w, uint32_t h, const unsigned char *src,
                               uint16_t fmt, uint16_t type, uint32_t bpt, uint32_t hwfmt)
{
    uint32_t n = w * h;
    if (w > MACH64_BLIT_MAX) return 0;
    if (MACH64_PRIV(ctx)->apple_surf_cn && dma_upload_image(dst_off, n, src, fmt, type, bpt, hwfmt)) return 1;   /* bus-mastered path */
    if (w >= 8) {
        uint32_t rows_per = MACH64_BLIT_MAX / w, y0;
        for (y0 = 0; y0 < h; y0 += rows_per) {
            uint32_t rows = (h - y0 < rows_per) ? (h - y0) : rows_per;
            mach64_blit_texels(ctx, dst_off + y0 * w * bpt, w / 8, w, rows,
                               src, y0 * w, rows * w, fmt, type, bpt, hwfmt);
        }
    } else {
        uint32_t start;
        for (start = 0; start < n; start += MACH64_BLIT_MAX) {
            uint32_t cnt = (n - start < MACH64_BLIT_MAX) ? (n - start) : MACH64_BLIT_MAX;
            mach64_blit_texels(ctx, dst_off + start * bpt, 1, cnt, 1, src, start, cnt, fmt, type, bpt, hwfmt);
        }
    }
    return 1;
}

/* Pull the texture's image(s) from the texobj state object and DMA them into VRAM.
 * GLEngine populates a record per mip level at state+0x9c+(baseLOD+L)*0x14 (w@0,
 * h@2, fmt@0xc, src@0x10); we upload the WHOLE chain into one region and record
 * each level's offset in mip_off[maxLog2] for its TEX_[maxLog2]_OFF register.
 *
 * WHY A BLIT (not a CPU poke): ctx->regs is not VRAM at all -- it is Apple's type-1
 * COMMAND buffer, so a wide CPU write there corrupted queued engine commands and
 * panicked the box.  (The real CPU VRAM view is mach64_vram_cpu_begin's BAR0 map.)
 * A 2D host-data blit writes through DST_OFF_PITCH in the
 * ENGINE's own offset space -- exactly where TEX_n_OFF then reads. */
/* Cheap 64-bit content hash of a source image (two independent 32-bit lanes): the game re-uploads ~10 unchanged 256x256 texture pages per frame, each ~10 ms
 * through the kext; hashing the source costs under a millisecond. */
static void tex_hash(const unsigned char *p, uint32_t n, uint32_t *h1, uint32_t *h2)
{
    uint32_t a = 0x811c9dc5u, b = 0x9e3779b9u, i = 0, nw = n / 4u;
    if (((uintptr_t)p & 3u) == 0) {
        const uint32_t *w = (const uint32_t *)p;
        for (i = 0; i < nw; i++) { uint32_t x = w[i]; a = (a ^ x) * 0x01000193u; b = ((b << 5) | (b >> 27)) + x; }
        i = nw * 4u;
    }
    for (; i < n; i++) { a = (a ^ p[i]) * 0x01000193u; b = ((b << 5) | (b >> 27)) + p[i]; }
    *h1 = a; *h2 = b;
}
static uint32_t tex_src_bytes(uint32_t w, uint32_t h, uint16_t fmt, uint16_t type)
{
    uint32_t bpp;
    if (type == 0x8363u || type == 0x8033u || type == 0x8034u) bpp = 2;
    else switch (fmt) { case 0x1908u: bpp = 4; break; case 0x1909u: case 0x1906u: case 0x8049u: bpp = 1; break; case 0x190Au: case 0x85B9u: bpp = 2; break; default: bpp = 3; }
    return w * h * bpp;
}
static int mach64_upload(gld_context *ctx, void *texobj)
{
    mach64_priv *p = MACH64_PRIV(ctx);
    const unsigned char *t = (const unsigned char *)texobj;
    struct { uint32_t w, h, src, fmt, type, bytes; } lvl[12];
    uint32_t state, base_lod, total, cur, hwfmt, bpt;
    const unsigned char *s;
    mach64_tex_slot *slot;
    int nlevels = 0, L, pre_used = 0;
    uint32_t tex_new_h1 = 0, tex_new_h2 = 0, tex_new_hl = 0;   /* hash of the image being uploaded (stored in the slot on success) */

    if (!t) return 0;
    state = *(const uint32_t *)(t + 0x30);
    GLD_TRACE("tex: upload texobj=%p state=0x%x heap 0x%x-0x%x next 0x%x dma=%d\n", texobj, state, g_heap_base, g_heap_end, g_heap_next, g_dma.state);
    if (state < 0x10000 || state > M64_UADDR_MAX) return 0;
    s = (const unsigned char *)(uintptr_t)state;
    base_lod = s[0x5e];

    /* Enumerate the mip chain from the base level down to 1x1 (stops at the first
     * empty/invalid record -- a non-mipmapped texture yields a single level). */
    for (L = 0; L < 12; L++) {
        const unsigned char *r = s + 0x9c + (base_lod + (uint32_t)L) * 0x14;
        uint32_t lw = *(const uint16_t *)(r + 0x00), lh = *(const uint16_t *)(r + 0x02);
        uint32_t lsrc = *(const uint32_t *)(r + 0x10);
        if (!lw || !lh || lsrc < 0x10000 || lsrc > M64_UADDR_MAX) break;
        if (lw > MACH64_BLIT_MAX) { if (L == 0) return 0; break; }
        lvl[nlevels].w = lw; lvl[nlevels].h = lh; lvl[nlevels].src = lsrc;
        lvl[nlevels].fmt  = *(const uint16_t *)(r + 0x0c);
        lvl[nlevels].type = *(const uint16_t *)(r + 0x0e);   /* pixel type (0x1401 UBYTE / 0x8363 5_6_5) */
        lvl[nlevels].bytes = 0;
        nlevels++;
    }
    if (!nlevels) return 0;
    /* Unchanged re-upload of a resident texture: nothing to do. */
    {   uint32_t h1 = 0, h2 = 0, hl = 0, k; mach64_tex_slot *ex;
        {
            for (k = 0; k < (uint32_t)nlevels; k++) {
                uint32_t nb = tex_src_bytes(lvl[k].w, lvl[k].h, (uint16_t)lvl[k].fmt, (uint16_t)lvl[k].type), a1, a2;
                tex_hash((const unsigned char *)(uintptr_t)lvl[k].src, nb, &a1, &a2); h1 = (h1 * 31u) ^ a1; h2 = (h2 * 31u) ^ a2; hl += nb;
            }
            TEX_LOCK(); ex = slot_find(tex_key(texobj));
            if (ex && ex->rsize && ex->hlen == hl && ex->h1 == h1 && ex->h2 == h2 && ex->w == lvl[0].w && ex->h == lvl[0].h && ex->nlevels == nlevels) {
                ex->texobj = texobj; ex->tick = ++g_tick; TEX_UNLOCK();
                return 1;
            }
            TEX_UNLOCK();
            tex_new_h1 = h1; tex_new_h2 = h2; tex_new_hl = hl;
        }
    }

    /* Texel format.  Packed 16-bit sources (5_6_5, 4_4_4_4, 5_5_5_1) are stored natively as RGB565 / ARGB4444 / ARGB1555 (half the VRAM, exact); everything
     * else is ARGB8888 -- and if an ARGB8888 texture does not fit the heap it is retried as 16-bit (RGB565 without alpha, ARGB4444 with), the way a compact texture format
     * would be chosen.  Only the Path B (kext memcpy) upload can pack 16-bit texels. */
    {   uint32_t t0 = lvl[0].type, f0 = lvl[0].fmt;
        int alpha0 = (f0 == 0x1908u || f0 == 0x1906u || f0 == 0x190Au || f0 == 0x8049u);
        int attempt;
        hwfmt = 6u; bpt = 4u; slot = 0;
        { mach64_tex_slot *pre; TEX_LOCK(); pre = slot_find(tex_key(texobj)); pre_used = (pre && pre->rsize && pre->gen == mach64_batch_gen); TEX_UNLOCK(); }
        if (p->apple_surf_cn && lvl[0].w >= 2) {
            if (t0 == 0x8363u) { hwfmt = 4u; bpt = 2u; }
            else if (t0 == 0x8033u) { hwfmt = 15u; bpt = 2u; }
            else if (t0 == 0x8034u) { hwfmt = 3u; bpt = 2u; }
            else if (f0 == 0x85B9u && (t0 == 0x85BAu || t0 == 0x85BBu)) { hwfmt = (t0 == 0x85BAu) ? 12u : 11u; bpt = 2u; }   /* GL_APPLE_ycbcr_422: memory [Cb Y0 Cr Y1] (8_8_APPLE) = hardware datatype 12 (measured: it takes Y from the 2nd byte); _REV [Y0 Cb Y1 Cr] = 11 */
            else if (f0 == 0x1909u && t0 == 0x1401u && lvl[0].w >= 4) { hwfmt = 8u; bpt = 1u; }   /* GL_LUMINANCE -> Y8 (1 byte/texel) */
            else { hwfmt = alpha0 ? 15u : 4u; bpt = 2u; }   /* default: 16-bit (565 / 4444) -- the 1.5 MB heap otherwise thrashes (Sonic R: 6800 evictions/20 s as ARGB8888) */
        }
        for (attempt = 0; attempt < 2; attempt++) {
            total = 0;
            for (L = 0; L < nlevels; L++) { lvl[L].bytes = tex_align(lvl[L].w * lvl[L].h * bpt); total += lvl[L].bytes; }
            g_evict_used = 0;
            slot = slot_alloc(tex_key(texobj), lvl[0].w, lvl[0].h, total);
            if (slot || bpt == 2u || !p->apple_surf_cn || lvl[0].w < 2) break;
            hwfmt = alpha0 ? 15u : 4u; bpt = 2u;                 /* heap full as ARGB8888: retry compact */
        }
    }
    /* Order: pending triangles must run before a texture region they read is rewritten.  Draining the batch on EVERY upload made one drain (= one type-1
     * present/copy opportunity) per texture change -- Sonic R's 6 background tiles landed as 6 partial frames.  Flush only if this upload re-uses a region the
     * pending batch bound (re-upload of a bound slot, or an eviction victim bound in this batch); a fresh region cannot be read by queued triangles. */
    if (pre_used || g_evict_used) mach64_flush_batch(ctx);
    { extern void mach64_dma_fence(void); mach64_dma_fence(); }   /* a real upload drives the engine/aperture: let an async geometry job finish first */
    p->dirty |= MACH64_DIRTY_STATE;   /* the upload's 2D blit clobbers engine state; the next triangle re-emits it */
    if (!slot) return 0;
    slot->hwfmt = hwfmt; slot->bpt = bpt; slot->texobj = texobj; slot->tick = ++g_tick;
    {   uint16_t f0 = (uint16_t)lvl[0].fmt;      /* alpha-bearing: RGBA / ALPHA / LUM_ALPHA / INTENSITY (RGB565 has none, whatever the source) */
        slot->has_alpha = (f0 == 0x1908u || f0 == 0x1906u || f0 == 0x190Au || f0 == 0x8049u) && hwfmt != 4u;
    }
    slot->nlevels   = nlevels;
    /* GL_TEXTURE_WRAP_S/T live in the state-obj at +0x10 as (WRAP_S<<16)|WRAP_T,
     * raw GL enums (recovered via tools/mach64_wrapstate).  Any non-REPEAT (0x2901)
     * mode -- GL_CLAMP/CLAMP_TO_EDGE/CLAMP_TO_BORDER -- maps to hardware clamp. */
    {   uint32_t wrap = *(const uint32_t *)(s + 0x10);
        uint16_t minf = *(const uint16_t *)(s + 0x16);   /* GL_TEXTURE_MIN_FILTER (u16) */
        uint16_t magf = *(const uint16_t *)(s + 0x18);   /* GL_TEXTURE_MAG_FILTER (u16) */
        slot->clamp_s = ((wrap >> 16) != 0x2901u);
        slot->clamp_t = ((wrap & 0xffffu) != 0x2901u);
        /* GL filter enums (state-object offsets: 0x2600 NEAREST,
         * 0x2601 LINEAR, 0x2700..0x2703 the mipmap variants (odd low bit = LINEAR within a level).
         * mag GL_LINEAR -> hardware bilinear (SCALE_3D_CNTL BILINEAR_TEX_EN bit25, HW-verified).
         * NOTE: the Rage LT Pro cannot bilinear-filter ALPHA-BLENDED textures (a HW limitation --
         * grates/fences/smoke stay pixelated); setting the bit is harmless there (HW falls back). */
        slot->bilinear_mag = (magf == 0x2601u);
        slot->bilinear_min = ((minf & 1u) != 0u);   /* 0x2601/0x2701/0x2703 -> linear min */
        slot->min_filter   = minf;
        slot->mipmapped    = (nlevels > 1 && minf >= 0x2700u && minf <= 0x2703u);
    }

    /* Upload each level; index its offset by its maxLog2 (= its TEX_n_OFF reg). */
    cur = slot->off;
    for (L = 0; L < nlevels; L++) {
        uint32_t mx = lvl[L].w > lvl[L].h ? lvl[L].w : lvl[L].h;
        uint32_t r  = ilog2u(mx);
        mach64_upload_image(ctx, cur, lvl[L].w, lvl[L].h,
                            (const unsigned char *)(uintptr_t)lvl[L].src,
                            (uint16_t)lvl[L].fmt, (uint16_t)lvl[L].type, bpt, hwfmt);
        if (r < 12) slot->mip_off[r] = cur;
        slot->lvl_off[L] = cur; slot->lvl_w[L] = lvl[L].w; slot->lvl_h[L] = lvl[L].h;
        cur += lvl[L].bytes;
    }

    p->dirty |= MACH64_DIRTY_STATE;      /* new texels -> re-emit tex state       */
    slot->h1 = tex_new_h1; slot->h2 = tex_new_h2; slot->hlen = tex_new_hl;
    return 1;
}

/* GL_TEXTURE_2D enable lives in the gli as bit 3 of the word at +0x6748 (0x08
 * when enabled, 0 when disabled; recovered via tools/mach64_texstate gli-diff). */
#define GLI_TEX2D_ENABLE   0x6748
#define GLI_TEX2D_BIT      0x00000008u
#define GLI_UNIT_STRIDE    0x7cu       /* per texture unit: enable word, env-mode word */
#define GLI_BOUND_STRIDE   0x14u       /* per texture unit: bound texobj pointer       */

/* GL_TEXTURE_ENV_MODE lives in the very next gli word, at +0x674c, stored as the
 * raw GL enum in the HIGH 16 bits (recovered via a 3-way gli diff
 * MODULATE/REPLACE/DECAL, tools/mach64_texenv): MODULATE=0x2100, REPLACE=0x1e01,
 * DECAL=0x2101, so the field = enum << 16. */
#define GLI_TEX_ENV_MODE   0x674c
#define GL_ENV_MODULATE    0x2100u
#define GL_ENV_REPLACE     0x1e01u
#define GL_ENV_DECAL       0x2101u

/* GL_BLEND enable + glBlendFunc, recovered via a 3-way gli diff (off / srcalpha /
 * additive, tools/mach64_blendstate).  Enable is bit 0x01000000 of gli+0x6304;
 * the blend func is packed (srcRGB<<16)|dstRGB as raw GL factor enums at
 * gli+0x62e8 (mirrored at +0x62ec = the alpha func). */
#define GLI_BLEND_ENABLE   0x6304
#define GLI_BLEND_EN_BIT   0x01000000u
#define GLI_BLEND_FUNC     0x62e8

/* Map a GL blend factor to the mach64 ALPHA_BLEND_SRC field (SCALE_3D_CNTL
 * [18:16]) / ALPHA_BLEND_DST field ([21:19]) (values from mesa_mach64_reg.h).
 * Unsupported factors fall back to opaque (SRC ONE / DST ZERO). */
static uint32_t mach64_blend_src(uint32_t glf)
{
    switch (glf) {
    case 0x0000: return 0u << 16;   /* GL_ZERO                 */
    case 0x0001: return 1u << 16;   /* GL_ONE                  */
    case 0x0306: return 2u << 16;   /* GL_DST_COLOR            */
    case 0x0307: return 3u << 16;   /* GL_ONE_MINUS_DST_COLOR  */
    case 0x0302: return 4u << 16;   /* GL_SRC_ALPHA            */
    case 0x0303: return 5u << 16;   /* GL_ONE_MINUS_SRC_ALPHA  */
    case 0x0304: return 6u << 16;   /* GL_DST_ALPHA            */
    case 0x0305: return 7u << 16;   /* GL_ONE_MINUS_DST_ALPHA  */
    case 0x0308: return (4u << 16) | 0x00002000u;  /* GL_SRC_ALPHA_SATURATE: SRC_ALPHA + ALPHA_BLEND_SAT
                                     * (bit13, from xf86-video-mach64 atiregs.h -- cross-verified vs the
                                     * other SCALE_3D_CNTL bits this driver already uses).  min(As,1-Ad)
                                     * for RGB, 1 for alpha -- the HW SAT bit does exactly this.  SRC-only
                                     * factor per the GL spec, so it is NOT added to mach64_blend_dst. */
    default:     return 1u << 16;
    }
}
static uint32_t mach64_blend_dst(uint32_t glf)
{
    switch (glf) {
    case 0x0000: return 0u << 19;   /* GL_ZERO                 */
    case 0x0001: return 1u << 19;   /* GL_ONE                  */
    case 0x0300: return 2u << 19;   /* GL_SRC_COLOR            */
    case 0x0301: return 3u << 19;   /* GL_ONE_MINUS_SRC_COLOR  */
    case 0x0302: return 4u << 19;   /* GL_SRC_ALPHA            */
    case 0x0303: return 5u << 19;   /* GL_ONE_MINUS_SRC_ALPHA  */
    case 0x0304: return 6u << 19;   /* GL_DST_ALPHA            */
    case 0x0305: return 7u << 19;   /* GL_ONE_MINUS_DST_ALPHA  */
    default:     return 0u << 19;
    }
}

/* GL_FOG (HW-verified 2026-08-25, Picture 40/41).  The mach64 has no fog table/plane
 * the driver solves; the setup engine interpolates a per-vertex FOG FACTOR and blends
 * the fragment against DP_FOG_CLR by it:  pixel = factor*fragment + (1-factor)*fogcolor.
 * TWO findings diverge from the reference DRI mach64UpdateFogAttrib (which is INERT on
 * the LT Pro -- proven: its recipe emitted perfect state but drew no fog):
 *   1. The factor source is the SPECULAR alpha (VERTEX_SPEC_ARGB[31:24]), NOT the
 *      primary vertex alpha the reference assumes.  mach64_read_vertex packs +0x4c
 *      into spec alpha when mach64_fog_on.
 *   2. FOG mode is ALPHA_FOG_EN = bit12 ONLY (0x1000).  The reference's bit11|bit12
 *      (0x1800) was HW-proven to give NO fog; bit12-only is the mode that blends.
 * Kept from the reference: SRC=SRC_ALPHA, DST=INV_SRC_ALPHA, clear TEX_MAP_AEN, and
 * DP_FOG_CLR = GL_FOG_COLOR.  Mutually exclusive with GL_BLEND (both own the alpha
 * channel), so blend wins.
 * GL_FOG enable = gli+0x6380 bit 8 (the ONLY word that changed on
 * glEnable(GL_FOG) was 0x6380: 0x855c0000 -> 0x855c0100).  NOT 0x6354 (dither), NOT
 * bit24.  The per-vertex fog factor (+0x4c) reads 0.0 when fog is OFF, so the pipe MUST
 * be gated on this enable -- an always-on pipe would fog everything (factor 0 = full fog). */
#define GLI_FOG_ENABLE   0x6380u          /* GL_FOG enable word                   */
#define GLI_FOG_EN_BIT   0x00000100u      /* bit 8 (recovered)    */
#define GLI_FOG_COLOR    0x6358u          /* GL_FOG_COLOR RGBA floats (verified)  */

/* These two are "current draw" globals: set in mach64_bind_texture at the START of each primitive
 * batch (from the drawing context's gli state) and consumed in that SAME batch's emit_tri/read_vertex.
 * On the serialized GL thread a draw is atomic, so each draw refreshes them before use -- safe under
 * multiple contexts without per-context storage (unlike the pixel-store gc, which a later pixel op
 * reads and so had to be made per-context). */
uint32_t mach64_tex_vofs[2] = { 0x80u, 0u };   /* see gld.h */
int mach64_spec_mode = 0;
int mach64_fog_on = 0;                    /* consumed by mach64_read_vertex       */
int mach64_flat_on = 0;                   /* glShadeModel(GL_FLAT): emit_tri flattens colour */

/* Clamp a 0..1 float colour channel to a 0..255 byte. */
static uint32_t chan_u8(float f){ return m64_f2u8(f); }

/* Is GL_FOG active for the current draw?  Requires a known enable offset, the
 * enable bit set, and GL_BLEND OFF (blend takes precedence over fog). */
static int mach64_fog_active(gld_context *ctx)
{
    if (!GLI_FOG_ENABLE) return 0;                                   /* dormant   */
    if (mach64_gli_u32(ctx, GLI_BLEND_ENABLE) & GLI_BLEND_EN_BIT) return 0;
    return (mach64_gli_u32(ctx, GLI_FOG_ENABLE) & GLI_FOG_EN_BIT) != 0;
}

/* GL_TEXTURE_LOD_BIAS (glTexEnvf(GL_TEXTURE_FILTER_CONTROL_EXT, GL_TEXTURE_LOD_BIAS_EXT, x), texture unit 0): a float at gli+0x6774.
 * Recovered with tools/mach64_lodscan.c (offscreen software context, gli located in-process and diffed).  Hardware: TEX_CNTL[3:0] = n, 0..15,
 * threshold stride 1 + n/16 (Win98 ATI3DCIF 0x138b0 / SDK "LOD biasing"): the chip switches to the next smaller map LATER, i.e. sharper, which is
 * a NEGATIVE GL bias of -log2(stride) in [-0.954, 0].  A positive GL bias (blurrier) has no hardware field: bind falls back to the software
 * selector for it (whole levels, both signs).  The caller clamps/rounds. */
#define GLI_LOD_BIAS 0x6774u
static float mach64_lod_bias(gld_context *ctx)
{
    uint32_t bits = mach64_gli_u32(ctx, GLI_LOD_BIAS); float f;
    __builtin_memcpy(&f, &bits, 4);
    if (!(f == f) || f > 16.0f || f < -16.0f) return 0.0f;           /* NaN / absurd: ignore */
    return f;
}
static uint32_t mach64_lod_bias_nibble(float b)
{
    static const float lg[16] = { 0.0000f, 0.0875f, 0.1699f, 0.2479f, 0.3219f, 0.3923f, 0.4594f, 0.5236f,
                                  0.5850f, 0.6439f, 0.7004f, 0.7549f, 0.8074f, 0.8580f, 0.9069f, 0.9542f };   /* log2(1 + n/16) */
    float x = -b, best = 1e9f, d; uint32_t n, bn = 0;
    if (x <= 0.0f) return 0;
    for (n = 0; n < 16; n++) { d = x - lg[n]; if (d < 0) d = -d; if (d < best) { best = d; bn = n; } }
    return bn;
}

/* Overlay the fog fields onto a SCALE_3D_CNTL value and latch DP_FOG_CLR from the
 * gli fog colour.  Clears ALPHA_BLEND_SRC[18:16], DST[21:19], ALPHA_FOG_EN[12:11]
 * and TEX_MAP_AEN[30], then sets FOG mode (bit12) + SRC_ALPHA/INV_SRC_ALPHA. */
static uint32_t mach64_apply_fog(gld_context *ctx, m64_state *st, uint32_t sc)
{
    uint32_t r = mach64_gli_u32(ctx, GLI_FOG_COLOR + 0);
    uint32_t g = mach64_gli_u32(ctx, GLI_FOG_COLOR + 4);
    uint32_t b = mach64_gli_u32(ctx, GLI_FOG_COLOR + 8);
    float rf, gf, bf;
    __builtin_memcpy(&rf, &r, 4); __builtin_memcpy(&gf, &g, 4); __builtin_memcpy(&bf, &b, 4);
    st->dp_fog_clr = 0xff000000u                                     /* opaque    */
                   | (chan_u8(rf) << 16) | (chan_u8(gf) << 8) | chan_u8(bf);
    return (sc & ~0x403f1800u)                 /* clear SRC,DST,FOG_EN,TEX_MAP_AEN */
         | (4u << 16) | (5u << 19)             /* SRC_ALPHA | DST_INV_SRC_ALPHA    */
         | (1u << 12);                         /* ALPHA_FOG_EN = FOG (bit12 only)  */
}

/* GL_COLOR_LOGIC_OP (glLogicOp).  gli+0x63a4 packs [31:16] = glLogicOp GL enum
 * (0x1500..0x150f) and bit0 = GL_COLOR_LOGIC_OP enable (recovered
 * state-delta: 0x15030000 -> 0x15060001 on enable+XOR, -> 0x15070001 on GL_OR).
 * The op is a datapath ROP applied via DP_MIX (0x6d4): FRGD_MIX [20:16] + BKGD_MIX
 * [4:0], each a 5-bit code, set to the SAME value = the ROP of source (fragment)
 * vs dest (framebuffer).  The 16 GL ops map onto the 16 basic MIX codes exactly.
 * NOTE the reference DRI driver FALLS BACK TO SOFTWARE for all ops != GL_COPY; we
 * attempt it in hardware (verified below). */
#define GLI_LOGICOP  0x63a4u

/* GL logic op (enum low nibble) -> mach64 MIX code, S=source frag, D=dest fb. */
static const unsigned char mach64_logicop_mix[16] = {
    1,  /* 0x0 CLEAR         -> ZERO             (0)      */
    12, /* 0x1 AND           -> D_AND_S          (s & d)  */
    13, /* 0x2 AND_REVERSE   -> NOT_D_AND_S      (s & ~d) */
    7,  /* 0x3 COPY          -> S                (s)      */
    14, /* 0x4 AND_INVERTED  -> D_AND_NOT_S      (~s & d) */
    3,  /* 0x5 NOOP          -> D                (d)      */
    5,  /* 0x6 XOR           -> D_XOR_S          (s ^ d)  */
    11, /* 0x7 OR            -> D_OR_S           (s | d)  */
    15, /* 0x8 NOR           -> NOT_D_AND_NOT_S  (~(s|d)) */
    6,  /* 0x9 EQUIV         -> NOT_D_XOR_S      (~(s^d)) */
    0,  /* 0xa INVERT        -> NOT_D            (~d)     */
    10, /* 0xb OR_REVERSE    -> NOT_D_OR_S       (s | ~d) */
    4,  /* 0xc COPY_INVERTED -> NOT_S            (~s)     */
    9,  /* 0xd OR_INVERTED   -> D_OR_NOT_S       (~s | d) */
    8,  /* 0xe NAND          -> NOT_D_OR_NOT_S   (~(s&d)) */
    2,  /* 0xf SET           -> ONE              (1)      */
};

/* If a non-trivial colour logic op is active (GL_COLOR_LOGIC_OP on AND op != COPY),
 * write its DP_MIX value and return 1; else return 0 (caller uses the source mix). */
static int mach64_logicop(gld_context *ctx, uint32_t *dp_mix_out)
{
    uint32_t w = mach64_gli_u32(ctx, GLI_LOGICOP);
    uint32_t op, code;
    if (!(w & 1u)) return 0;                 /* GL_COLOR_LOGIC_OP disabled          */
    op = (w >> 16) & 0xfu;                    /* glLogicOp enum low nibble           */
    if (op == 3u) return 0;                   /* GL_COPY (0x1503) = the pass-through  */
    code = mach64_logicop_mix[op];
    *dp_mix_out = (code << 16) | code;        /* FRGD_MIX | BKGD_MIX = the ROP        */
    return 1;
}

/* GL_ALPHA_TEST state (recovered via tools/mach64_alphatest):
 *   gli+0x62e0 = glAlphaFunc REF (float 0..1)
 *   gli+0x62e4 = (GL compare enum << 16) | (bit8 = GL_ALPHA_TEST enabled) */
#define GLI_ALPHA_FUNC   0x62e4
#define GLI_ALPHA_REF    0x62e0
#define GLI_ALPHA_EN_BIT 0x00000100u

/* Build ALPHA_TST_CNTL (0x550).  Disabled default = ALPHA_TEST_ALWAYS |
 * ALPHA_DST_SEL=As (0x470, matches the reference init and our old fixed value).
 * When GL_ALPHA_TEST is on: ALPHA_TST_EN(bit0) | mach64 test func([6:4]) |
 * ALPHA_DST_SEL=As([10:8]=4) | ALPHA_TST_SRC_SEL([12]: 0=texel / 1=source) |
 * REF_ALPHA([23:16]).  GL compare enums (low 3 bits) map to the mach64 test codes
 * (never/less/lequal/equal/gequal/greater/notequal/always) NOT one-to-one. */
static uint32_t mach64_alpha_tst(gld_context *ctx, int use_texel_alpha)
{
    uint32_t af = mach64_gli_u32(ctx, GLI_ALPHA_FUNC);
    if (af & GLI_ALPHA_EN_BIT) {
        static const unsigned char map[8] = { 0,1,3,2,5,6,4,7 };  /* GL func -> mach64 */
        uint32_t func = map[(af >> 16) & 7u];
        float rf; uint32_t rb = mach64_gli_u32(ctx, GLI_ALPHA_REF);
        int c;
        __builtin_memcpy(&rf, &rb, 4);
        c = (int)(rf * 255.0f + 0.5f); if (c < 0) c = 0; else if (c > 255) c = 255;
        return 1u | (func << 4) | (4u << 8)
             | (use_texel_alpha ? 0u : (1u << 12))
             | ((uint32_t)c << 16);
    }
    return (7u << 4) | (4u << 8);   /* ALPHA_TEST_ALWAYS, disabled */
}

/* TEX_CNTL composite bits for the secondary map (DRI reference mach64UpdateTextureUnit, unit 1): COMPOSITE (8), SECONDARY_STW (31), COMP_ALPHA (13) when the map has alpha,
 * filter bits 11 (min) / 12 (mag), per-map clamp 24 (S) / 25 (T); bit 9 (combine = modulate) is already part of the base value. */
static uint32_t mt_cntl(const mach64_tex_slot *b2)
{
    return (1u << 8) | (1u << 31) | (b2->has_alpha ? (1u << 13) : 0u)
         | (b2->bilinear_min ? (1u << 11) : 0u) | (b2->bilinear_mag ? (1u << 12) : 0u)
         | (b2->clamp_s ? (1u << 24) : 0u) | (b2->clamp_t ? (1u << 25) : 0u);
}

/* Called from the draw path.  Uploads any pending image (so the texture is ready
 * in VRAM regardless of enable state), then -- only if the app has GL_TEXTURE_2D
 * enabled -- programs the texture registers into p->hw and enables textured state
 * emission.  Returns 1 if a texture is bound (the primitive should feed S/T). */
int mach64_bind_texture(gld_context *ctx)
{
    mach64_priv *p = MACH64_PRIV(ctx);
    m64_state *st = &p->hw;
    mach64_tex_slot *bound, *bound2;
    /* The textures this draw will use must survive any eviction triggered by an upload below. */
    {   int u; for (u = 0; u < 2; u++) {
            uint32_t en = mach64_gli_u32(ctx, GLI_TEX2D_ENABLE + (uint32_t)u * GLI_UNIT_STRIDE), bt = mach64_gli_u32(ctx, GLI_BOUND_TEXOBJ + (uint32_t)u * GLI_BOUND_STRIDE);
            g_protect[u] = ((en & GLI_TEX2D_BIT) && bt > 0x10000 && bt <= M64_UADDR_MAX) ? *(const uint32_t *)(uintptr_t)(bt + 0x30) : 0;
        } }
    int punit;
    uint32_t wl, hl, tex_on, env, light_fcn, new_scale_3d, blend, lop_mix = 0;
    int changed, lop_on, hw_mip, mt;
    uint32_t lod_n;
    /* Upload any image-dirtied textures (their state objects are populated by
     * draw time); each lands in its own VRAM slot.  Pop the queue under the lock,
     * then upload OUTSIDE it -- mach64_upload -> slot_alloc takes the lock itself,
     * so holding it across the DMA upload would both deadlock and needlessly
     * serialise uploads.  Under Path B the upload routes through the kext selector
     * (mach64_blit_texels), NOT the host-data blit -- so texturing is fully enabled. */
    for (;;) {
        void *pend = 0;
        if (!*(volatile int *)&g_npending) break;     /* nothing queued (the common case): skip the lock round trip, 2 mutex ops per primitive otherwise */
        TEX_LOCK();
        if (g_npending) pend = g_pending[--g_npending];
        TEX_UNLOCK();
        if (!pend) break;
        mach64_upload(ctx, pend);
    }

    /* GL_COLOR_MASK (glColorMask): gli+0x63b4 holds 4 bytes R,G,B,A (each 0/1,
     * recovered by gli diffing).  Map to DP_WRITE_MASK for the 32bpp ARGB8888 target
     * (A[31:24] R[23:16] G[15:8] B[7:0]) -- a cleared channel drops those bits so the
     * pipe leaves them untouched in the framebuffer. */
    {   uint32_t cm = mach64_gli_u32(ctx, 0x63b4);
        uint32_t wm = ((cm >> 24) & 0xffu ? 0x00ff0000u : 0u)   /* R */
                    | ((cm >> 16) & 0xffu ? 0x0000ff00u : 0u)   /* G */
                    | ((cm >>  8) & 0xffu ? 0x000000ffu : 0u)   /* B */
                    | ((cm        & 0xffu) ? 0xff000000u : 0u);  /* A */
        if (st->dp_write_mask != wm) { st->dp_write_mask = wm; p->dirty |= MACH64_DIRTY_STATE; }
    }

    /* glShadeModel (gli+0x6708 bit0: 1 = GL_SMOOTH, 0 = GL_FLAT -- recovered
     * 2026-08-26).  Rather than the mach64 SETUP_CNTL FLAT_SHADE field (bit value uncertain in
     * the spec and didn't take), do flat shading in the driver: emit_tri replicates the
     * provoking (GL default = LAST) vertex's colour onto all three vertices when mach64_flat_on,
     * so the 3D scaler interpolates a constant -> flat.  HW-independent, exact for GL_TRIANGLES. */
    mach64_flat_on = !(mach64_gli_u32(ctx, 0x6708) & 1u);

    /* NOTE on GL_SCISSOR_TEST: Apple's GLEngine does T&L *and clipping* in software, so it
     * clips scissored (and frustum-crossing) primitives itself and re-dispatches the clipped
     * convex polygons through the elements-tris vector (slot 0x48 -> mach64_render_elt_tris)
     * as an array of vertex pointers.  It never relies on the mach64 hardware scissor
     * (SC_LEFT_RIGHT/SC_TOP_BOTTOM stay wide-open), so there is nothing to program here --
     * scissor "works" by our rendering that clipped geometry correctly. */

    /* GL_COLOR_LOGIC_OP (glLogicOp): a datapath ROP via DP_MIX.  Takes precedence
     * over blending AND fog (GL spec: logic op disables blending), so when active we
     * force the opaque SOLID pipe in the branches below and skip fog. */
    lop_on = mach64_logicop(ctx, &lop_mix);
    {   uint32_t dm = lop_on ? lop_mix : MACH64_DP_MIX_SS;
        if (st->dp_mix != dm) { st->dp_mix = dm; p->dirty |= MACH64_DIRTY_STATE; }
    }

    /* GL_POLYGON_STIPPLE (offsets recovered 2026-08-26): enable =
     * gli+0x66ec bit24; the 32x32 pattern is at gli+0x665c, one 32-bit row per word.
     * The mach64 pattern is only 8x8 (PAT_REG0/1), so extract the top-left 8x8 tile --
     * exact for 8x8-periodic stipples (halftone/screen-door), approximate for arbitrary
     * 32x32.  Datapath: DP_SRC = MONO_SRC_PATTERN|FRGD_SRC_3D, DP_MIX = FRGD_MIX_S|
     * BKGD_MIX_D -> the triangle draws where the pattern bit is set and leaves the
     * framebuffer where it is clear.  Mutually exclusive with a logic op (shared DP_SRC/
     * DP_MIX); logic op wins. */
    #define GLI_POLY_STIPPLE_EN  0x66ecu
    #define GLI_POLY_STIPPLE_PAT 0x665cu
    if (!lop_on && (mach64_gli_u32(ctx, GLI_POLY_STIPPLE_EN) & 0x01000000u)) {
        uint32_t r[8], k, p0, p1;
        /* leftmost 8 px (top byte, MSB-first == GL bit order, verified Picture 85) of the
         * pattern rows, but with the ROW ORDER REVERSED: GL's origin is lower-left (row 0
         * = bottom) while the mach64 pattern is top-left, so GL row 0 -> mach64 tile bottom
         * (verified: without the flip a "/" pattern rendered as "\"). */
        for (k = 0; k < 8; k++)
            r[k] = (mach64_gli_u32(ctx, GLI_POLY_STIPPLE_PAT + (7u - k) * 4u) >> 24) & 0xffu;
        p0 = r[0] | (r[1] << 8) | (r[2] << 16) | (r[3] << 24);
        p1 = r[4] | (r[5] << 8) | (r[6] << 16) | (r[7] << 24);
        if (!st->stipple || st->pat_reg0 != p0 || st->pat_reg1 != p1) {
            st->stipple  = 1;
            st->pat_reg0 = p0; st->pat_reg1 = p1;
            st->dp_src   = MACH64_DP_SRC_STIPPLE;
            st->dp_mix   = MACH64_DP_MIX_STIPPLE;
            p->dirty |= MACH64_DIRTY_STATE;
        }
    } else if (st->stipple) {
        st->stipple = 0;
        st->dp_src  = MACH64_DP_SRC_3D;   /* dp_mix already restored by the block above */
        p->dirty |= MACH64_DIRTY_STATE;
    }
    #undef GLI_POLY_STIPPLE_EN
    #undef GLI_POLY_STIPPLE_PAT

    /* GL_FOG: decide once per draw.  Sets mach64_fog_on so mach64_read_vertex packs
     * the fog factor into the vertex alpha, and forces a state re-emit while fogging
     * so DP_FOG_CLR (which aliases the 2D DP_FRGD_CLR a clear/fill may have trampled)
     * is re-latched every fog draw.  A logic op owns the pipe -> no fog. */
    /* GL_SEPARATE_SPECULAR_COLOR (glLightModeli COLOR_CONTROL): gli+0x6380 bit 0 (recovered with tools/mach64_mtexscan; the same word's bit 8 is fog).  GLEngine then
     * delivers the secondary colour as floats R,G,B at gli-vertex +0x40/+0x44/+0x48 (the layout the Radeon driver copies).  Decided below once we know if texturing is on. */
    mach64_spec_mode = (!lop_on && (mach64_gli_u32(ctx, GLI_FOG_ENABLE) & 1u)) ? 2 : 0;
    mach64_fog_on = lop_on ? 0 : mach64_fog_active(ctx);
    if (mach64_fog_on) p->dirty |= MACH64_DIRTY_STATE;
    else               st->dp_fog_clr = 0;   /* leave DP_FRGD_CLR to the 2D engine */

    /* Texture units.  GLEngine keeps unit u's enable word at gli+0x6748+u*0x7c (bit 3), its env mode at +0x674c+u*0x7c and its bound texobj at
     * gli+0x15748+u*0x14 (all recovered with tools/mach64_mtexscan: the software context's gli, diffed while unit 1 was enabled / bound).
     * Both units enabled with textures bound -> ONE-pass compositing (primary = unit 0, secondary = unit 1); only unit 1 enabled -> it simply
     * becomes the primary map (its coordinates are read from the unit-1 vertex fields). */
    mach64_tex_vofs[0] = 0x80u; mach64_tex_vofs[1] = 0u;
    {   mach64_tex_slot *ub[2]; int u;
        for (u = 0; u < 2; u++) {
            uint32_t en = mach64_gli_u32(ctx, GLI_TEX2D_ENABLE + (uint32_t)u * GLI_UNIT_STRIDE);
            uint32_t bt = mach64_gli_u32(ctx, GLI_BOUND_TEXOBJ + (uint32_t)u * GLI_BOUND_STRIDE);
            ub[u] = 0;
            if ((en & GLI_TEX2D_BIT) && bt > 0x10000 && bt <= M64_UADDR_MAX) {
                uint32_t key = *(const uint32_t *)(uintptr_t)(bt + 0x30);
                {   static mach64_tex_slot *last_sl[2];                       /* per-unit cache: the slot table is a static array, a slot's identity is its key, so a hit needs no lock and no search */
                    if (last_sl[u] && last_sl[u]->key == key) ub[u] = last_sl[u];
                    else { TEX_LOCK(); ub[u] = slot_find(key); TEX_UNLOCK(); last_sl[u] = ub[u]; }   /* `bound` is this thread's own bound texture */
                }
                if (ub[u] && !ub[u]->rsize && ub[u]->texobj) {           /* evicted: bring it back from GLEngine's copy */
                    mach64_upload(ctx, ub[u]->texobj);
                    TEX_LOCK(); ub[u] = slot_find(key); TEX_UNLOCK();
                }
                if (ub[u] && !ub[u]->rsize) ub[u] = 0;                   /* could not be made resident: draw untextured */
                if (ub[u]) { ub[u]->tick = ++g_tick; ub[u]->gen = mach64_batch_gen; }
            }
        }
        bound = 0; bound2 = 0; punit = 0;
        if (ub[0]) { bound = ub[0]; bound2 = ub[1]; }
        else if (ub[1]) { bound = ub[1]; punit = 1; }
        if (bound2) mach64_tex_vofs[1] = 0x90u;
        mach64_tex_vofs[0] = 0x80u + (uint32_t)punit * 0x10u;
    }
    tex_on = bound != 0;

    if (!bound || !tex_on) {               /* no bound texture, or texturing off */
        /* Untextured gouraud: honour glEnable(GL_BLEND) on the shade pipe.  The
         * source alpha As is the interpolated VERTEX alpha (now carried in the
         * packed argb, bit 24-31); ALPHA_FOG_EN_ALPHA (bit 11) turns on the alpha
         * channel so As is actually computed.  Replace SOLID's SRC field ([18:16])
         * + DST field ([21:19]) with the app's glBlendFunc factors.  Blend off ->
         * the proven opaque SOLID pipe (SRC_ONE/DST_ZERO). */
        uint32_t sc = MACH64_SCALE_3D_CNTL_SOLID;
        /* A logic op owns the datapath (DP_MIX) and forces the opaque SOLID pipe;
         * blend/fog are skipped (GL: logic op disables blending). */
        if (!lop_on && (mach64_gli_u32(ctx, GLI_BLEND_ENABLE) & GLI_BLEND_EN_BIT)) {
            uint32_t bf = mach64_gli_u32(ctx, GLI_BLEND_FUNC);
            sc = (sc & ~0x003f0800u)
               | mach64_blend_src(bf >> 16) | mach64_blend_dst(bf & 0xffffu)
               | (1u << 11);
        }
        /* GL_FOG (blend off, checked in mach64_fog_active): blend the fragment
         * against DP_FOG_CLR by the interpolated per-vertex fog factor. */
        if (mach64_fog_on) sc = mach64_apply_fog(ctx, st, sc);
        {   /* GL_ALPHA_TEST on untextured geometry: source (vertex) alpha. */
            uint32_t atc = mach64_alpha_tst(ctx, 0);
            if (st->textured || st->scale_3d_cntl != sc || st->alpha_tst_cntl != atc) {
                st->textured       = 0;
                st->scale_3d_cntl  = sc;
                st->alpha_tst_cntl = atc;
                p->dirty |= MACH64_DIRTY_STATE;
            }
        }
        return 0;
    }
    /* Map the app's GL_TEXTURE_ENV_MODE -> mach64 TEX_LIGHT_FCN (SCALE_3D_CNTL
     * [23:22]; mesa_mach64_reg.h): REPLACE=0<<22, MODULATE=1<<22, DECAL->
     * ALPHA_DECAL=2<<22.  For an RGB texture the texel alpha is assumed 0xff
     * (TEX_MAP_AEN off), so ALPHA_DECAL evaluates to Ctex -- identical to REPLACE,
     * exactly GL_DECAL's result for an RGB internal format.  For an RGBA texture
     * TEX_MAP_AEN (bit 30) is set so the real texel alpha feeds the pipeline, and
     * ALPHA_DECAL then blends Ctex over the fragment colour by At. */
    env = mach64_gli_u32(ctx, GLI_TEX_ENV_MODE + (uint32_t)punit * GLI_UNIT_STRIDE) >> 16;
    switch (env) {
        case GL_ENV_REPLACE:  light_fcn = 0u << 22; break;   /* REPLACE     */
        case GL_ENV_MODULATE: light_fcn = 1u << 22; break;   /* MODULATE    */
        case GL_ENV_DECAL:    light_fcn = 2u << 22; break;   /* ALPHA_DECAL (== REPLACE for RGB) */
        /* The mach64 TEX_LIGHT_FCN field is only 2 bits -- 3 real modes.  GL_BLEND (0x0be2),
         * GL_ADD (0x0104), GL_COMBINE (0x8570) are NOT hardware-doable (the DRI reference
         * software-rasterises them via Mesa, which we can't through Apple's GLEngine).  Best
         * hardware approximation is MODULATE.  (See docs/re-notes/texture-blend-audit.md.) */
        case 0x8570:          light_fcn = 0u << 22; break;   /* GL_COMBINE (Sonic R GL_ADD_SIGNED overbright): vertex colour ~0.5 grey => Cp+Ct-0.5 ~= Ct => REPLACE (MODULATE halves the brightness) */
        default:              light_fcn = 1u << 22; break;
    }

    /* Framebuffer alpha blend (SCALE_3D_CNTL ALPHA_BLEND_SRC/DST): follow the
     * app's glEnable(GL_BLEND) + glBlendFunc from the gli.  Off -> SRC_ONE/DST_ZERO
     * (opaque).  With TEX_MAP_AEN set for RGBA, the "source alpha" the SRC/DST
     * factors reference IS the texel alpha. */
    if (!lop_on && (mach64_gli_u32(ctx, GLI_BLEND_ENABLE) & GLI_BLEND_EN_BIT)) {
        uint32_t bf = mach64_gli_u32(ctx, GLI_BLEND_FUNC);
        /* ALPHA_FOG_EN_ALPHA (1<<11) turns ON the alpha channel in the 3D pipe --
         * without it the alpha isn't computed and the blend sees opaque As, so a
         * translucent texture never blends (ref sets it whenever BlendEnabled).  A
         * logic op forces the opaque pipe (blend disabled). */
        blend = mach64_blend_src(bf >> 16) | mach64_blend_dst(bf & 0xffffu)
              | (1u << 11);
    } else {
        blend = 1u << 16;                     /* SRC_ONE | DST_ZERO = opaque */
    }

    new_scale_3d = 0x1u | (1u << 1) | (1u << 3) | (1u << 4)
                 | (2u << 6) | blend | light_fcn
                 | (1u << 24)                               /* MIP_MAP_DISABLE unless hw_mip clears it */
                 | (bound->has_alpha ? (1u << 30) : 0u)     /* TEX_MAP_AEN */
                 | (bound->bilinear_mag ? 0x02000000u : 0u); /* BILINEAR_TEX_EN: GL_LINEAR mag filter */
    /* MIP_MAP_DISABLE starts set; the hw_mip path below clears it for a complete mip chain, and
     * TEX_BLEND_FCN [27:26] (the min/mip filter; BILINEAR_TEX_EN above is the mag filter) is chosen there too. */
    if (bound->bilinear_min && !(bound->mipmapped)) {
        /* GL_LINEAR min filter on a single-level texture: TEX_BLEND_FCN field 2 (2x2 within the map), which the Win98 driver uses for
         * every non-mip texture; field 2 needs BILINEAR_TEX_EN too.  Hardware-verified at 32x32 and 128x128. */
        new_scale_3d |= 0x08000000u | 0x02000000u;
    }

    /* Hardware mipmapping.  Recipe recovered from the Windows 98 ATI3DCIF.DLL
     * (docs/re-notes/win98-mipmap.md): MIP_MAP_DISABLE (SCALE_3D_CNTL bit 24) CLEAR; every TEX_r_OFF, r = 0..maxLog2, holds the offset of
     * the level whose larger dimension is 2^r (the chain must be complete down to 1x1); TEX_SIZE_PITCH describes the BASE map;
     * TEX_BLEND_FCN (bits 27:26) = 0 point, 1 mip-linear (blend between maps), 2 2x2 within a map, 3 tri-linear, where 2 and 3 also
     * need BILINEAR_TEX_EN; and SETUP_CNTL.LOG_MAX_INC_ADJ (bit 6) set, as the Win98 driver's context defaults do.  Hardware-verified on the
     * MDD rig: levels picked per pixel, all four filter fields, no hang.  Requires a COMPLETE chain (1x1 included); anything
     * else falls back to the software selector below. */
    hw_mip = 0;
    if (bound->mipmapped) {
        uint32_t maxl = ilog2u(bound->lvl_w[0] > bound->lvl_h[0] ? bound->lvl_w[0] : bound->lvl_h[0]);
        if ((uint32_t)bound->nlevels == maxl + 1u && maxl <= 10u && mach64_lod_bias(ctx) < 0.5f) {   /* blurrier bias (>= 0.5): software selector, hardware has no such field */
            uint32_t minf = bound->min_filter, field = (minf == 0x2701u) ? 2u : (minf == 0x2702u) ? 1u : (minf == 0x2703u) ? 3u : 0u;
            hw_mip = 1;
            new_scale_3d &= ~((1u << 24) | 0x0c000000u);
            new_scale_3d |= field << 26;
            if (field >= 2u) new_scale_3d |= 0x02000000u;
        }
    }

    /* Two-texture compositing.  Unit 0 runs through TEX_LIGHT_FCN as usual; the secondary map is then combined by the composite stage
     * (TEX_CNTL[8] COMPOSITE, [9] 1 = MODULATE the primary result by the secondary texel).  The hardware cannot REPLACE/DECAL/ADD in the
     * composite stage (the DRI reference falls back to software for those), so every unit-1 env mode other than MODULATE is approximated by
     * MODULATE, the same policy as the single-unit path.  The Mesa reference also sets TEX_BLEND_FCN = trilinear and TEX_CACHE_SPLIT (SCALE_3D_CNTL
     * bit 9, split the texture cache between the two maps) on the primary whenever two maps are active; mipmaps are not combined with
     * compositing here (base level only, software level selection still applies to the primary).  Measured composite MODULATE precision: the
     * secondary texel acts as ((B>>4)+1)/16 (4-bit factor; B=0 stays 0, B=255 exact), so mid greys come out ~+15/255 brighter than the exact product. */
    mt = bound2 != 0;
    if (mt) {
        hw_mip = 0;
        new_scale_3d &= ~((1u << 24) | 0x0c000000u);
        new_scale_3d |= (1u << 24) | (1u << 9);                           /* MIP_MAP_DISABLE, TEX_CACHE_SPLIT */
        new_scale_3d |= 0x0c000000u;                                      /* TEX_BLEND_FCN = trilinear: REQUIRED for compositing -- with point/linear the engine wedges on the first draw (hardware-verified, Nov 20) */
    }
    lod_n = hw_mip ? mach64_lod_bias_nibble(mach64_lod_bias(ctx)) : 0u;

    /* GL_FOG on textured geometry: fog wins the alpha channel (clears TEX_MAP_AEN,
     * "can't use texture alpha when fogging") and blends the textured fragment
     * against DP_FOG_CLR by the per-vertex fog factor. */
    if (mach64_fog_on) new_scale_3d = mach64_apply_fog(ctx, st, new_scale_3d);

    /* Re-emit the texture state when the bound texture OR the env mode changes
     * (not just on the untextured->textured transition) -- otherwise switching
     * texA->texB, or MODULATE->REPLACE, leaves the hardware in the old state. */
    {   /* GL_ALPHA_TEST on textured geometry: the texel alpha (needs TEX_MAP_AEN,
         * set for an RGBA texture) is compared to the reference. */
        uint32_t atc = mach64_alpha_tst(ctx, 1);
        if (mach64_spec_mode) { atc |= 1u << 31; mach64_spec_mode = 1; }     /* SPECULAR_LIGHT_EN: add the interpolated specular rgb to textured pixels */
        changed = (!st->textured || st->tex_mip != hw_mip || st->tex_0_off != bound->off || (st->tex_cntl & 0xfu) != lod_n
                                  || st->tex_fmt != bound->hwfmt || (mt && st->tex2_fmt != bound2->hwfmt)
                                  || st->multitex != mt || (mt && (st->tex2_off != bound2->off || bound2->w != p->tex2_w || bound2->h != p->tex2_h || st->tex2_cntl_bits != mt_cntl(bound2)))
                                  || st->tex_scale_3d != new_scale_3d
                                  || st->alpha_tst_cntl != atc);
        st->alpha_tst_cntl = atc;
    }
    if (changed) p->dirty |= MACH64_DIRTY_STATE;

    p->tex_off = bound->off;
    st->multitex = mt; p->tex2_w = mt ? bound2->w : 0; p->tex2_h = mt ? bound2->h : 0;
    st->tex_fmt = bound->hwfmt; st->tex2_fmt = mt ? bound2->hwfmt : 0u;
    p->tex_w   = bound->w;
    p->tex_h   = bound->h;
    p->tex_slot = bound;                 /* mach64_tex_select_lod may rebind a smaller level per triangle */
    p->tex_lvl  = 0;
    wl = ilog2u(p->tex_w);
    hl = ilog2u(p->tex_h);

    /* Register values taken EXACTLY from the reference DRI driver
     * (mach64_texstate.c mach64UpdateTextureUnit + mach64_tex.c). */

    /* Base texel offset (raw VRAM byte offset).  Emitted to TEX_[maxLog2]_OFF by
     * m64_emit_state (the register is chosen from TEX_SIZE_PITCH), not TEX_0_OFF. */
    st->tex_0_off      = p->tex_off;

    /* TEX_SIZE_PITCH (unit 0): widthLog2 | maxLog2 | heightLog2, where
     * maxLog2 = max(widthLog2, heightLog2) (ref exact). */
    st->tex_size_pitch = ((wl << 0)
                       | ((wl > hl ? wl : hl) << 4)
                       |  (hl << 8));

    /* TEX_CNTL: COMP_COMBINE_MODULATE | TEX_CACHE_FLUSH | CACHE_SIZE_4K, plus
     * TEXTURE_CLAMP_S (1<<17) / TEXTURE_CLAMP_T (1<<18) per the app's actual
     * GL_TEXTURE_WRAP_S/T (ref mach64_texstate.c sets these for GL_CLAMP, nothing
     * for GL_REPEAT).  A REPEAT texture drawn once in [0,1] shows the mach64's
     * edge wrap (a 1px opposite-edge line) -- that IS GL_REPEAT's edge behaviour;
     * apps wanting clean edges use GL_CLAMP. */
    st->tex_cntl       = (1u << 9) | (1u << 23) | (1u << 30)
                       | (mt ? mt_cntl(bound2) : 0u)
                       | (bound->clamp_s ? (1u << 17) : 0u)
                       | (bound->clamp_t ? (1u << 18) : 0u);

    /* SCALE_3D_CNTL textured: the shade default with TEXTURE_DISABLE(bit6)
     * cleared turns FCN_SHADE(3<<6) into FCN_TEXTURE(2<<6).  Fixed set (ref,
     * cpp==4, nearest, no alpha): EXPAND_DYNAMIC | DITHER_2D_TABLE |
     * DITHER_INIT_RESET | ROUND_EN | FCN_TEXTURE | ALPHA_BLEND_SRC_ONE |
     * MIP_MAP_DISABLE, plus the env-mode-dependent TEX_LIGHT_FCN computed above.
     * (BILINEAR/TEX_BLEND=nearest -> clear; TEX_MAP_AEN clear = RGB no alpha.)
     * MODULATE gives the old 0x0141009b. */
    st->tex_scale_3d   = new_scale_3d;

    st->tex_mip     = hw_mip;
    st->tex_cntl    = (st->tex_cntl & ~0xfu) | lod_n;
    if (mt) {   /* unit-1 map: offset, log2 sizes in TEX_SIZE_PITCH[31:16] (width [19:16], max [23:20], height [27:24]) */
        uint32_t w2l = ilog2u(bound2->w), h2l = ilog2u(bound2->h);
        st->tex2_off = bound2->off; st->tex2_cntl_bits = mt_cntl(bound2);
        st->tex2_size_pitch = (w2l << 16) | ((w2l > h2l ? w2l : h2l) << 20) | (h2l << 24);
    } else { st->tex2_off = 0; st->tex2_size_pitch = 0; st->tex2_cntl_bits = 0; }
    st->tex_maxlog2 = (int)(wl > hl ? wl : hl);
    if (hw_mip) {
        int r;
        for (r = 0; r < 12; r++) st->tex_mip_off[r] = bound->mip_off[r <= st->tex_maxlog2 ? r : st->tex_maxlog2];   /* never leave a register pointing at stale memory */
        p->tex_slot = 0;                                 /* the hardware picks the level: no software LOD */
        if (!(st->setup_cntl & 0x40u)) { st->setup_cntl |= 0x40u; p->dirty |= MACH64_DIRTY_STATE; }
    } else if (st->setup_cntl & 0x40u) { st->setup_cntl &= ~0x40u; p->dirty |= MACH64_DIRTY_STATE; }

    if (mt && !(st->setup_cntl & 0x40u)) { st->setup_cntl |= 0x40u; p->dirty |= MACH64_DIRTY_STATE; }   /* LOG_MAX_INC_ADJ, as for the hardware mip path and Win98's context default */
    st->textured = 1;
    return 1;
}

/* Per-triangle mip level and min/mag filter (software LOD, used when the hardware mip chain is not used, see hw_mip in
 * mach64_bind_texture).  The hardware always samples ONE image: the "base" registers.  For a minified triangle we point those
 * registers at a smaller, already-uploaded level (offset + size), which is the same proven single-level path.  Normalised texcoords
 * are unchanged (the hardware scales them by the base size).
 *
 *   lod = 0.5*log2(texel_area / pixel_area), texel_area = uv_area * W0 * H0 at level 0 (affine estimate from the vertices).
 *   lod <= 0 : magnified -> level 0, bilinear iff GL_LINEAR mag.
 *   lod  > 0 : minified  -> for a mipmapped MIN filter the nearest level round(lod) clamped to the chain (no trilinear: blending two
 *              levels needs TEX_BLEND_FCN, which wedges the engine), bilinear iff the MIN filter is a LINEAR-within-level variant;
 *              for a non-mipmapped MIN filter level 0, bilinear iff GL_LINEAR.
 * Only the registers that changed are re-emitted (state goes into the same command stream, in order, before the next triangle). */
void mach64_tex_select_lod(gld_context *ctx, float pix_area, float uv_area)
{
    mach64_priv *p = MACH64_PRIV(ctx);
    const mach64_tex_slot *sl = (const mach64_tex_slot *)p->tex_slot;
    m64_state *st = &p->hw;
    int lvl = 0, bil;
    uint32_t nsc, wl, hl;
    if (!sl || !st->textured || sl->nlevels < 1) return;
    if (pix_area < 0.0f) pix_area = -pix_area;
    if (uv_area  < 0.0f) uv_area  = -uv_area;
    bil = sl->bilinear_mag != 0;
    if (pix_area > 1e-3f && uv_area > 1e-12f) {
        float texels = uv_area * (float)sl->lvl_w[0] * (float)sl->lvl_h[0];             /* ratio = texels / pix_area (texels per pixel, area): compared by multiplying, no fdivs */
        if (texels > pix_area) {                                                         /* minified: lod = 0.5*log2(ratio) */
            float lod = 0.0f, lim = pix_area;
            while (texels >= 4.0f * lim && lod < 12.0f) { lim *= 4.0f; lod += 1.0f; }   /* whole levels */
            if (texels >= 2.0f * lim) { lod += 1.0f; }                                   /* round: 0.5*log2(2..4) is >= 0.5 */
            if (sl->mipmapped) { lvl = (int)lod; if (lvl >= sl->nlevels) lvl = sl->nlevels - 1; }
            bil = sl->bilinear_min != 0;
        }
    }
    if (sl->mipmapped) {   /* GL_TEXTURE_LOD_BIAS, in whole levels (both signs; the hardware path only has sharper-than-neutral) */
        float lb = mach64_lod_bias(ctx);
        int ib = (int)(lb + (lb >= 0.0f ? 0.5f : -0.5f));
        lvl += ib; if (lvl < 0) lvl = 0; if (lvl >= sl->nlevels) lvl = sl->nlevels - 1;
    }
    nsc = (st->tex_scale_3d & ~0x02000000u) | (bil ? 0x02000000u : 0u);
    if (lvl == p->tex_lvl && nsc == st->tex_scale_3d) return;
    {   uint32_t w = sl->lvl_w[lvl], h = sl->lvl_h[lvl], off = sl->lvl_off[lvl];
        if (!w || !h || !off) return;                       /* level never uploaded: stay on the current one */
        p->tex_lvl = lvl; p->tex_off = off; p->tex_w = (int)w; p->tex_h = (int)h;
        wl = ilog2u(w); hl = ilog2u(h);
        st->tex_0_off      = off;
        st->tex_size_pitch = (wl << 0) | ((wl > hl ? wl : hl) << 4) | (hl << 8);
        st->tex_maxlog2    = (int)(wl > hl ? wl : hl);
        st->tex_scale_3d   = nsc;
        p->dirty |= MACH64_DIRTY_STATE;
    }
}

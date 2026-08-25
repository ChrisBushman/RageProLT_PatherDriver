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
#include "gld.h"
#include "../common/mach64_regs.h"

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
} mach64_tex_slot;
static mach64_tex_slot g_slots[MACH64_TEX_SLOTS];
static uint32_t        g_heap_next = MACH64_TEX_HEAP_BASE;
/* Heap window is runtime-relocatable: double-buffered mode needs the VRAM the
 * texture heap normally occupies for the back buffer, so it moves the heap into
 * the smaller tail region.  Must be called before any texture is uploaded. */
static uint32_t        g_heap_base = MACH64_TEX_HEAP_BASE;
static uint32_t        g_heap_end  = MACH64_TEX_HEAP_END;

void mach64_set_texheap(uint32_t base, uint32_t end)
{
    g_heap_base = base; g_heap_end = end; g_heap_next = base;
    gld_log("mach64_set_texheap: heap = [0x%x, 0x%x) (%u KB)\n",
            base, end, (end - base) >> 10);
}

typedef struct { uint32_t off, size; } mach64_free_blk;
static mach64_free_blk g_free[MACH64_TEX_SLOTS];
static int             g_nfree;

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
        gld_log("mach64 tex: free list full, 0x%x (%u B) leaked\n", off, size);
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

/* Find or allocate a 32-byte-aligned VRAM region of `nbytes` for `key` (a base
 * w*h texture, or a whole mip chain -- caller sizes it).  w/h are the BASE dims
 * stored for the texture registers.  A re-upload reuses its region if the new one
 * still fits; if it grew, the old region is freed before a fresh one is taken. */
static mach64_tex_slot *slot_alloc(uint32_t key, uint32_t w, uint32_t h, uint32_t nbytes)
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
        if (!s) { gld_log("mach64 tex: slot table full\n"); return 0; }
    }
    off = heap_alloc(sz);
    if (!off) { gld_log("mach64 tex: heap full\n"); if (s->key != key) s->key = 0; return 0; }
    s->key = key; s->off = off; s->w = w; s->h = h; s->rsize = sz;
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
    for (i = 0; i < g_npending; i++) if (g_pending[i] == texobj) return;
    if (g_npending < 16) g_pending[g_npending++] = texobj;
}
void mach64_texture_bound(void *texobj) { (void)texobj; }   /* no bind hook; see gli */

void mach64_texture_deleted(void *texobj)
{
    mach64_tex_slot *s = slot_find(tex_key(texobj));
    if (s) { heap_free(s->off, s->rsize); s->key = 0; }   /* reclaim VRAM + slot */
}

static uint32_t ilog2u(uint32_t v) { uint32_t r = 0; while (v > 1) { v >>= 1; r++; } return r; }

/* Texels per host-data submit: the scratch command buffer is 1024 dwords; one
 * blit setup (~13 regs) + count texels + a packet header per 16 + terminator must
 * fit, so cap a chunk at 768 texels (leaves comfortable headroom).  Larger
 * textures are uploaded in several chunks (mach64_upload). */
#define MACH64_BLIT_MAX  768u

/* One host-data DMA blit of `count` ARGB8888 texels (converted from src[first..])
 * into VRAM at byte offset `dst_off`, laid out `bw` wide x `bh` tall with row
 * pitch `pitch_units`*8 pixels.  count must be <= MACH64_BLIT_MAX. */
static void mach64_blit_texels(gld_context *ctx, uint32_t dst_off, uint32_t pitch_units,
                               uint32_t bw, uint32_t bh, const unsigned char *src,
                               uint32_t first, uint32_t count, uint16_t fmt)
{
    mach64_priv *p = MACH64_PRIV(ctx);
    m64_cmdbuf cb;
    uint32_t i;
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
            if (fmt == 0x1908) { const unsigned char *px = src + i*4;
                argb = ((uint32_t)px[3]<<24)|((uint32_t)px[0]<<16)|((uint32_t)px[1]<<8)|px[2];
            } else { const unsigned char *px = src + i*3;
                argb = 0xff000000u|((uint32_t)px[0]<<16)|((uint32_t)px[1]<<8)|px[2];
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
                               uint32_t w, uint32_t h, const unsigned char *src, uint16_t fmt)
{
    uint32_t n = w * h;
    if (w > MACH64_BLIT_MAX) return 0;
    if (w >= 8) {
        uint32_t rows_per = MACH64_BLIT_MAX / w, y0;
        for (y0 = 0; y0 < h; y0 += rows_per) {
            uint32_t rows = (h - y0 < rows_per) ? (h - y0) : rows_per;
            mach64_blit_texels(ctx, dst_off + y0 * w * 4u, w / 8, w, rows,
                               src, y0 * w, rows * w, fmt);
        }
    } else {
        uint32_t start;
        for (start = 0; start < n; start += MACH64_BLIT_MAX) {
            uint32_t cnt = (n - start < MACH64_BLIT_MAX) ? (n - start) : MACH64_BLIT_MAX;
            mach64_blit_texels(ctx, dst_off + start * 4u, 1, cnt, 1, src, start, cnt, fmt);
        }
    }
    return 1;
}

/* Pull the texture's image(s) from the texobj state object and DMA them into VRAM.
 * GLEngine populates a record per mip level at state+0x9c+(baseLOD+L)*0x14 (w@0,
 * h@2, fmt@0xc, src@0x10); we upload the WHOLE chain into one region and record
 * each level's offset in mip_off[maxLog2] for its TEX_[maxLog2]_OFF register.
 *
 * WHY A BLIT (not a CPU poke): the userspace VRAM aperture (ctx->regs) is NOT
 * offset-aligned with the engine's VRAM offset space (a wide CPU write there
 * panics the box).  A 2D host-data blit writes through DST_OFF_PITCH in the
 * ENGINE's own offset space -- exactly where TEX_n_OFF then reads. */
static int mach64_upload(gld_context *ctx, void *texobj)
{
    mach64_priv *p = MACH64_PRIV(ctx);
    const unsigned char *t = (const unsigned char *)texobj;
    mach64_flush_batch(ctx);   /* order: pending triangles before texture VRAM changes */
    struct { uint32_t w, h, src, fmt, bytes; } lvl[12];
    uint32_t state, base_lod, total, cur;
    const unsigned char *s;
    mach64_tex_slot *slot;
    int nlevels = 0, L;

    if (!t) return 0;
    state = *(const uint32_t *)(t + 0x30);
    if (state < 0x10000 || state >= 0x40000000) return 0;
    s = (const unsigned char *)(uintptr_t)state;
    base_lod = s[0x5e];

    /* Enumerate the mip chain from the base level down to 1x1 (stops at the first
     * empty/invalid record -- a non-mipmapped texture yields a single level). */
    for (L = 0; L < 12; L++) {
        const unsigned char *r = s + 0x9c + (base_lod + (uint32_t)L) * 0x14;
        uint32_t lw = *(const uint16_t *)(r + 0x00), lh = *(const uint16_t *)(r + 0x02);
        uint32_t lsrc = *(const uint32_t *)(r + 0x10);
        if (!lw || !lh || lsrc < 0x10000 || lsrc >= 0x40000000) break;
        if (lw > MACH64_BLIT_MAX) { if (L == 0) { gld_log("mach64 tex: %ux%u too wide\n", lw, lh); return 0; } break; }
        lvl[nlevels].w = lw; lvl[nlevels].h = lh; lvl[nlevels].src = lsrc;
        lvl[nlevels].fmt = *(const uint16_t *)(r + 0x0c);
        lvl[nlevels].bytes = tex_align(lw * lh * 4u);
        nlevels++;
    }
    if (!nlevels) return 0;

    total = 0;
    for (L = 0; L < nlevels; L++) total += lvl[L].bytes;

    slot = slot_alloc(tex_key(texobj), lvl[0].w, lvl[0].h, total);
    if (!slot) return 0;
    slot->has_alpha = (lvl[0].fmt == 0x1908);   /* GL_RGBA -> real alpha channel */
    slot->nlevels   = nlevels;
    /* GL_TEXTURE_WRAP_S/T live in the state-obj at +0x10 as (WRAP_S<<16)|WRAP_T,
     * raw GL enums (recovered via tools/mach64_wrapstate).  Any non-REPEAT (0x2901)
     * mode -- GL_CLAMP/CLAMP_TO_EDGE/CLAMP_TO_BORDER -- maps to hardware clamp. */
    {   uint32_t wrap = *(const uint32_t *)(s + 0x10);
        slot->clamp_s = ((wrap >> 16) != 0x2901u);
        slot->clamp_t = ((wrap & 0xffffu) != 0x2901u);
    }

    /* Upload each level; index its offset by its maxLog2 (= its TEX_n_OFF reg). */
    cur = slot->off;
    for (L = 0; L < nlevels; L++) {
        uint32_t mx = lvl[L].w > lvl[L].h ? lvl[L].w : lvl[L].h;
        uint32_t r  = ilog2u(mx);
        mach64_upload_image(ctx, cur, lvl[L].w, lvl[L].h,
                            (const unsigned char *)(uintptr_t)lvl[L].src, (uint16_t)lvl[L].fmt);
        if (r < 12) slot->mip_off[r] = cur;
        cur += lvl[L].bytes;
    }

    p->dirty |= MACH64_DIRTY_STATE;      /* new texels -> re-emit tex state       */
    gld_log("mach64 texture upload: %ux%u fmt=0x%x -> VRAM 0x%x (key 0x%x) %d level%s\n",
            lvl[0].w, lvl[0].h, lvl[0].fmt, slot->off, slot->key,
            nlevels, nlevels > 1 ? "s (mip)" : "");
    return 1;
}

/* GL_TEXTURE_2D enable lives in the gli as bit 3 of the word at +0x6748 (0x08
 * when enabled, 0 when disabled; recovered via tools/mach64_texstate gli-diff). */
#define GLI_TEX2D_ENABLE   0x6748
#define GLI_TEX2D_BIT      0x00000008u

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
 * GL_FOG enable = gli+0x6380 bit 8 (GLD_FOGSCAN: the ONLY word that changed on
 * glEnable(GL_FOG) was 0x6380: 0x855c0000 -> 0x855c0100).  NOT 0x6354 (dither), NOT
 * bit24.  The per-vertex fog factor (+0x4c) reads 0.0 when fog is OFF, so the pipe MUST
 * be gated on this enable -- an always-on pipe would fog everything (factor 0 = full fog). */
#define GLI_FOG_ENABLE   0x6380u          /* GL_FOG enable word                   */
#define GLI_FOG_EN_BIT   0x00000100u      /* bit 8 (recovered via GLD_FOGSCAN)    */
#define GLI_FOG_COLOR    0x6358u          /* GL_FOG_COLOR RGBA floats (verified)  */

int mach64_fog_on = 0;                    /* consumed by mach64_read_vertex       */

/* Clamp a 0..1 float colour channel to a 0..255 byte. */
static uint32_t chan_u8(float f){ int c=(int)(f*255.0f+0.5f); return c<0?0u:(c>255?255u:(uint32_t)c); }

/* Is GL_FOG active for the current draw?  Requires a known enable offset, the
 * enable bit set, and GL_BLEND OFF (blend takes precedence over fog). */
static int mach64_fog_active(gld_context *ctx)
{
    if (!GLI_FOG_ENABLE) return 0;                                   /* dormant   */
    if (mach64_gli_u32(ctx, GLI_BLEND_ENABLE) & GLI_BLEND_EN_BIT) return 0;
    return (mach64_gli_u32(ctx, GLI_FOG_ENABLE) & GLI_FOG_EN_BIT) != 0;
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
 * (0x1500..0x150f) and bit0 = GL_COLOR_LOGIC_OP enable (recovered via GLD_FOGSCAN
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

/* GL_ALPHA_TEST state (recovered via tools/mach64_alphatest -DGLD_ATSCAN):
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

/* Called from the draw path.  Uploads any pending image (so the texture is ready
 * in VRAM regardless of enable state), then -- only if the app has GL_TEXTURE_2D
 * enabled -- programs the texture registers into p->hw and enables textured state
 * emission.  Returns 1 if a texture is bound (the primitive should feed S/T). */
int mach64_bind_texture(gld_context *ctx)
{
    mach64_priv *p = MACH64_PRIV(ctx);
    m64_state *st = &p->hw;
    mach64_tex_slot *bound;
    uint32_t wl, hl, tex_on, env, light_fcn, new_scale_3d, blend, lop_mix = 0;
    int changed, lop_on;

    /* Upload any image-dirtied textures (their state objects are populated by
     * draw time); each lands in its own VRAM slot. */
    while (g_npending) mach64_upload(ctx, g_pending[--g_npending]);

#ifdef GLD_FOGSCAN
    /* Recover the GL_FOG-enable gli offset (0x6354 was disproven = dither).  Snapshot
     * the whole pipeline-state region silently on the first draw, then print ONLY the
     * words that changed on later draws.  tools/mach64_fogscan draws a fog-OFF frame
     * then a fog-ON frame changing nothing else, so the delta pinpoints the enable
     * word (a 0x01000000-style flag) alongside the fog colour at +0x6358. */
    {   static uint32_t fs_snap[(0x6780u - 0x6200u) / 4];
        static int fs_inited = 0;
        uint32_t o; int any = 0;
        for (o = 0x6200u; o < 0x6780u; o += 4) {
            uint32_t val = mach64_gli_u32(ctx, o), idx = (o - 0x6200u) / 4;
            if (fs_inited && val != fs_snap[idx]) {
                if (!any) { gld_log("FOGSCAN delta:\n"); any = 1; }
                gld_log("  gli+0x%04x: 0x%08x -> 0x%08x\n", o, fs_snap[idx], val);
            }
            fs_snap[idx] = val;
        }
        fs_inited = 1;
    }
#endif

    /* GL_COLOR_MASK (glColorMask): gli+0x63b4 holds 4 bytes R,G,B,A (each 0/1,
     * recovered via GLD_CMSCAN).  Map to DP_WRITE_MASK for the 32bpp ARGB8888 target
     * (A[31:24] R[23:16] G[15:8] B[7:0]) -- a cleared channel drops those bits so the
     * pipe leaves them untouched in the framebuffer. */
    {   uint32_t cm = mach64_gli_u32(ctx, 0x63b4);
        uint32_t wm = ((cm >> 24) & 0xffu ? 0x00ff0000u : 0u)   /* R */
                    | ((cm >> 16) & 0xffu ? 0x0000ff00u : 0u)   /* G */
                    | ((cm >>  8) & 0xffu ? 0x000000ffu : 0u)   /* B */
                    | ((cm        & 0xffu) ? 0xff000000u : 0u);  /* A */
        if (st->dp_write_mask != wm) { st->dp_write_mask = wm; p->dirty |= MACH64_DIRTY_STATE; }
    }

    /* GL_COLOR_LOGIC_OP (glLogicOp): a datapath ROP via DP_MIX.  Takes precedence
     * over blending AND fog (GL spec: logic op disables blending), so when active we
     * force the opaque SOLID pipe in the branches below and skip fog. */
    lop_on = mach64_logicop(ctx, &lop_mix);
    {   uint32_t dm = lop_on ? lop_mix : MACH64_DP_MIX_SS;
        if (st->dp_mix != dm) { st->dp_mix = dm; p->dirty |= MACH64_DIRTY_STATE; }
    }

    /* GL_FOG: decide once per draw.  Sets mach64_fog_on so mach64_read_vertex packs
     * the fog factor into the vertex alpha, and forces a state re-emit while fogging
     * so DP_FOG_CLR (which aliases the 2D DP_FRGD_CLR a clear/fill may have trampled)
     * is re-latched every fog draw.  A logic op owns the pipe -> no fog. */
    mach64_fog_on = lop_on ? 0 : mach64_fog_active(ctx);
    if (mach64_fog_on) p->dirty |= MACH64_DIRTY_STATE;
    else               st->dp_fog_clr = 0;   /* leave DP_FRGD_CLR to the 2D engine */

    /* Follow the app's glEnable/glDisable(GL_TEXTURE_2D). */
    tex_on = (mach64_gli_u32(ctx, GLI_TEX2D_ENABLE) & GLI_TEX2D_BIT) != 0;

    /* Select the CURRENTLY-BOUND texture: GLEngine stores its texobj at
     * gli+0x15748; our slot is keyed by texobj[0x30] (the state object). */
    {   uint32_t bt = mach64_gli_u32(ctx, GLI_BOUND_TEXOBJ);
        uint32_t key = (bt > 0x10000 && bt < 0x40000000)
                     ? *(const uint32_t *)(uintptr_t)(bt + 0x30) : 0;
        bound = slot_find(key);
    }

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
    env = mach64_gli_u32(ctx, GLI_TEX_ENV_MODE) >> 16;
    switch (env) {
        case GL_ENV_REPLACE: light_fcn = 0u << 22; break;
        case GL_ENV_DECAL:   light_fcn = 2u << 22; break;
        default:             light_fcn = 1u << 22; break;   /* MODULATE + fallback */
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
                 | (bound->nlevels > 1 ? 0u : (1u << 24))   /* MIP_MAP_DISABLE unless mipped */
                 | (bound->has_alpha ? (1u << 30) : 0u);   /* TEX_MAP_AEN */

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
        changed = (!st->textured || st->tex_0_off != bound->off
                                  || st->tex_scale_3d != new_scale_3d
                                  || st->alpha_tst_cntl != atc);
        st->alpha_tst_cntl = atc;
    }
    if (changed) p->dirty |= MACH64_DIRTY_STATE;

    p->tex_off = bound->off;
    p->tex_w   = bound->w;
    p->tex_h   = bound->h;
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

    /* Mipmapping: hand emit the per-level offsets so it programs one TEX_r_OFF per
     * level and (via the cleared MIP_MAP_DISABLE above) lets the HW select the LOD.
     * A non-mipped texture keeps the single-offset path (tex_mip = 0). */
    st->tex_mip     = (bound->nlevels > 1);
    st->tex_maxlog2 = (int)(wl > hl ? wl : hl);
    if (st->tex_mip) {
        int r;
        for (r = 0; r < 12; r++) st->tex_mip_off[r] = bound->mip_off[r];
    }

    st->textured = 1;
    if (changed)
        gld_log("mach64 tex: env=0x%x light_fcn=%u alpha=%u blend=0x%x off=0x%x %ux%u sp=0x%x\n",
                env, light_fcn >> 22, bound->has_alpha, blend, p->tex_off,
                p->tex_w, p->tex_h, st->tex_size_pitch);
    return 1;
}

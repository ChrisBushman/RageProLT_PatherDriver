/*
 * gld_texture.c - texture upload/bind and the per-draw state translation (blend, fog, alpha, logic-op, filters, mip LOD) on the standalone client.
 *
 * Texture memory comes from the kernel allocator (one process-wide connection owns it); texels are converted on the CPU into a staging image and
 * copied into VRAM by the kernel (rgb_upload: validated span, engine drained first, byte-order flags).  Offsets reach the engine only through
 * rgb_flush, which routes TEX_n_OFF / the texel formats to the kernel's SET_TEXTURE.  GLEngine's texobj/state layouts and the gli offsets are the
 * recovered ones of the old bundle (docs/re-notes/).  Single-threaded by design (design 3.3): no locks.
 */
#include <stdlib.h>
#include <stdio.h>
#include "gld.h"

#define TEX_LOCK()
#define TEX_UNLOCK()
#define GLD_TRACE(...) ((void)0)
#define MACH64_TEX_SLOTS      64
#define MACH64_BLIT_MAX       768u            /* (kept: largest texture width the hardware path was proven for) */

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
    int      noinset;                      /* bordered CLAMP texture: the coordinates are already exact texel positions, skip the texel-centre inset */
    float    wscale[2], wbias[2];          /* S/T coordinate map (s' = s * scale + bias) for wrap modes the chip lacks (mirrored repeat, mirror clamp, clamp to border) */
    uint32_t bilinear_mag, bilinear_min;   /* GL_LINEAR mag / linear-within-level min filter */
    uint32_t min_filter;                   /* GL_TEXTURE_MIN_FILTER enum */
    uint32_t mipmapped;                    /* MIN filter is one of the four *_MIPMAP_* modes and >1 level is loaded */
    uint32_t lvl_off[12], lvl_w[12], lvl_h[12];   /* per level L (0 = base): VRAM offset and size */
    uint32_t hwfmt, bpt;                   /* hardware texel datatype (6 ARGB8888, 4 RGB565, 3 ARGB1555, 15 ARGB4444) and bytes per texel */
    uint32_t tick;                         /* last-use stamp (LRU eviction) */
    uint32_t *cpu;                         /* lazily decoded ARGB8888 copy of level 0 for the CPU rasteriser (mach64_cpu_env); freed on re-upload/delete */
    uint32_t h1, h2, hlen;                 /* content hash of the source image(s) at the last upload: an unchanged re-upload is skipped */
    uint32_t gen;                          /* mach64_batch_gen when last bound: still referenced by the pending (unsubmitted) batch if == current */
    void    *texobj;                       /* GLEngine texobj, for re-upload after eviction */
    rgb_t   *owner;                        /* the context connection that owns the kernel allocation (0 = none) */
} mach64_tex_slot;
static mach64_tex_slot g_slots[MACH64_TEX_SLOTS];
uint32_t mach64_batch_gen = 1;   /* bumped by every real submit; slots stamp the generation they were last bound in */
static int             g_evict_used = 0;
static uint32_t        g_tick = 0;
static uint32_t        g_protect[2] = { 0, 0 };
static uint32_t        g_nevict = 0;

/* ---- texture VRAM: the kernel allocator.  A block belongs to the context connection that allocated it (the kernel validates TEX offsets against the
 * owner), so a texture is resident for ONE context at a time: a draw from another context re-uploads it into that context's own block. ---- */
static rgb_t *g_cur;                       /* the connection of the context being served */
static int tex_conn(void) { return g_cur != 0; }
static uint32_t heap_alloc(uint32_t size)
{
    uint32_t off = 0;
    if (!g_cur) return 0;
    if (rgb_tex_alloc(g_cur, size, &off)) return 0;
    return off;
}
static void heap_free(rgb_t *owner, uint32_t off) { if (off && owner) rgb_tex_free(owner, off); }

/* a context is going away: the kernel frees its blocks; its textures become non-resident (key + texobj kept) */
void mach64_tex_ctx_gone(rgb_t *rb)
{
    int i;
    for (i = 0; i < MACH64_TEX_SLOTS; i++)
        if (g_slots[i].key && g_slots[i].owner == rb) { g_slots[i].off = 0; g_slots[i].rsize = 0; g_slots[i].owner = 0; g_slots[i].h1 = g_slots[i].h2 = g_slots[i].hlen = 0; }
    if (g_cur == rb) g_cur = 0;
}

/* A display mode change dropped the kernel's allocations: every resident texture is evicted (key + texobj kept) and re-uploads on its next bind. */
void mach64_tex_invalidate_all(void)
{
    int i;
    for (i = 0; i < MACH64_TEX_SLOTS; i++)
        if (g_slots[i].key) { heap_free(g_slots[i].owner, g_slots[i].off); g_slots[i].off = 0; g_slots[i].rsize = 0; g_slots[i].owner = 0; g_slots[i].h1 = g_slots[i].h2 = g_slots[i].hlen = 0; }
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
        if (s->rsize >= sz && s->owner == g_cur) { s->w = w; s->h = h; return s; }  /* still fits, and ours */
        heap_free(s->owner, s->off); s->off = 0; s->rsize = 0; s->owner = 0;       /* grew or another context's: reclaim */
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
        heap_free(v->owner, v->off); v->off = 0; v->rsize = 0; v->owner = 0; g_nevict++;
        off = heap_alloc(sz);
    }
    if (!off) { GLD_TRACE("tex: heap alloc FAILED %u bytes (heap 0x%x-0x%x next 0x%x)\n", sz, g_heap_base, g_heap_end, g_heap_next); if (s->key != key) s->key = 0; return 0; }
    s->key = key; s->off = off; s->w = w; s->h = h; s->rsize = sz; s->owner = g_cur;
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
static void cube_cache_drop(uint32_t state);
static void vol_cache_drop(uint32_t state);

void mach64_texture_deleted(void *texobj)
{
    mach64_tex_slot *s;
    TEX_LOCK();
    s = slot_find(tex_key(texobj));
    cube_cache_drop(tex_key(texobj)); vol_cache_drop(tex_key(texobj));
    if (s) { heap_free(s->owner, s->off); s->key = 0; s->owner = 0; s->rsize = 0; free(s->cpu); s->cpu = 0; }   /* reclaim VRAM + slot */
    TEX_UNLOCK();
}

static mach64_tex_slot *g_cpu_slot; static uint32_t g_cpu_unit; static int g_cpu_multi;
static uint32_t potge(uint32_t v) { uint32_t r = 1; while (r < v) r <<= 1; return r; }
static uint32_t ilog2u(uint32_t v) { uint32_t r = 0; while (v > 1) { v >>= 1; r++; } return r; }

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

/* Convert one level into a staging image (the hardware's texel layout, as the bytes VRAM must hold) and copy it in through the kernel.
 * 32-bit texels: host ARGB word, SWAP32 puts the little-endian bytes in VRAM; 16-bit: host halfword values, SWAP16; 8-bit: bytes as they are. */
static int tex_upload_image(uint32_t dst_off, uint32_t w, uint32_t h, const unsigned char *src, uint16_t fmt, uint16_t type, uint32_t bpt, uint32_t hwfmt)
{
    uint32_t n = w * h, i, bytes = (n * bpt + 3u) & ~3u, done = 0;
    unsigned char *buf; int rc = 0, flags = 0;
    if (!tex_conn()) return 0;
    buf = (unsigned char *)calloc(1, bytes);
    if (!buf) return 0;
    if (bpt == 4u) { uint32_t *d = (uint32_t *)buf; for (i = 0; i < n; i++) d[i] = tex_texel_argb(src, i, fmt, type); flags = 2; }
    else if (bpt == 2u) { uint16_t *d = (uint16_t *)buf; for (i = 0; i < n; i++) d[i] = (uint16_t)tex_texel_hw16(src, i, fmt, type, hwfmt); flags = 1; }
    else for (i = 0; i < n; i++) buf[i] = src[i];
    while (done < bytes && !rc) {
        uint32_t c = bytes - done; if (c > 0x80000u) c = 0x80000u;
        rc = rgb_upload2(g_cur, dst_off + done, buf + done, c, flags);
        done += c;
    }
    free(buf);
    return !rc;
}

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
    if (fmt >= 0x83F0u && fmt <= 0x83F3u) return ((w + 3u) / 4u) * ((h + 3u) / 4u) * (fmt == 0x83F0u || fmt == 0x83F1u ? 8u : 16u);     /* S3TC blocks */
    if (type == 0x8363u || type == 0x8033u || type == 0x8034u) bpp = 2;
    else switch (fmt) { case 0x1900u: bpp = 1; break; case 0x1908u: bpp = 4; break; case 0x1909u: case 0x1906u: case 0x8049u: bpp = 1; break; case 0x190Au: case 0x85B9u: bpp = 2; break; default: bpp = 3; }
    return w * h * bpp;
}
/* S3TC (DXT1/3/5) block decode to RGBA bytes (R,G,B,A order = GL_RGBA / UNSIGNED_BYTE).  Integer maths. */
static void s3tc_565(uint32_t c, int *r, int *g, int *b) { int r5 = (int)((c >> 11) & 31u), g6 = (int)((c >> 5) & 63u), b5 = (int)(c & 31u); *r = (r5 << 3) | (r5 >> 2); *g = (g6 << 2) | (g6 >> 4); *b = (b5 << 3) | (b5 >> 2); }
static unsigned char *s3tc_decode(const unsigned char *src, uint32_t w, uint32_t h, uint32_t fmt)
{
    uint32_t bx, by, bw = (w + 3u) / 4u, bh = (h + 3u) / 4u, bsz = (fmt == 0x83F0u || fmt == 0x83F1u) ? 8u : 16u;
    unsigned char *out = (unsigned char *)malloc((size_t)w * h * 4u);
    if (!out) return 0;
    for (by = 0; by < bh; by++) for (bx = 0; bx < bw; bx++) {
        const unsigned char *b = src + (size_t)(by * bw + bx) * bsz, *cb = b + (bsz == 16u ? 8 : 0);
        uint32_t c0 = ((uint32_t)cb[0] << 8) | cb[1], c1 = ((uint32_t)cb[2] << 8) | cb[3], idx, x, y;
        int pal[4][4], i;
        /* the colour endpoints are little-endian 16-bit words in the stream: byte 0 is the low byte */
        c0 = (uint32_t)cb[0] | ((uint32_t)cb[1] << 8); c1 = (uint32_t)cb[2] | ((uint32_t)cb[3] << 8);
        idx = (uint32_t)cb[4] | ((uint32_t)cb[5] << 8) | ((uint32_t)cb[6] << 16) | ((uint32_t)cb[7] << 24);
        s3tc_565(c0, &pal[0][0], &pal[0][1], &pal[0][2]); s3tc_565(c1, &pal[1][0], &pal[1][1], &pal[1][2]); pal[0][3] = pal[1][3] = 255;
        if (c0 > c1 || bsz == 16u) { for (i = 0; i < 3; i++) { pal[2][i] = (2 * pal[0][i] + pal[1][i]) / 3; pal[3][i] = (pal[0][i] + 2 * pal[1][i]) / 3; } pal[2][3] = pal[3][3] = 255; }
        else { for (i = 0; i < 3; i++) { pal[2][i] = (pal[0][i] + pal[1][i]) / 2; pal[3][i] = 0; } pal[2][3] = 255; pal[3][3] = 0; }
        for (y = 0; y < 4; y++) for (x = 0; x < 4; x++) {
            uint32_t px = bx * 4u + x, py = by * 4u + y; int a = pal[(idx >> (2u * (y * 4u + x))) & 3u][3];
            unsigned char *o;
            if (px >= w || py >= h) continue;
            if (fmt == 0x83F2u) { uint32_t k = y * 4u + x; a = (int)((b[k >> 1] >> ((k & 1u) * 4u)) & 15u) * 17; }
            else if (fmt == 0x83F3u) {
                int a0 = b[0], a1 = b[1]; uint64_t bits = 0; uint32_t sel, k = y * 4u + x; int j;
                for (j = 0; j < 6; j++) bits |= (uint64_t)b[2 + j] << (8 * j);
                sel = (uint32_t)((bits >> (3u * k)) & 7u);
                if (sel == 0) a = a0; else if (sel == 1) a = a1;
                else if (a0 > a1) a = ((8 - (int)sel) * a0 + ((int)sel - 1) * a1) / 7;
                else if (sel == 6) a = 0; else if (sel == 7) a = 255; else a = ((6 - (int)sel) * a0 + ((int)sel - 1) * a1) / 5;
            }
            o = out + ((size_t)py * w + px) * 4u;
            o[0] = (unsigned char)pal[(idx >> (2u * (y * 4u + x))) & 3u][0]; o[1] = (unsigned char)pal[(idx >> (2u * (y * 4u + x))) & 3u][1]; o[2] = (unsigned char)pal[(idx >> (2u * (y * 4u + x))) & 3u][2]; o[3] = (unsigned char)a;
        }
    }
    return out;
}
/* one texel of the border colour in the source image's own format (UNSIGNED_BYTE RGB/RGBA/LUMINANCE/ALPHA/LUMINANCE_ALPHA/INTENSITY or a packed 16-bit type) */
static void tex_border_pixel(uint16_t fmt, uint16_t type, const float *c, unsigned char *o)
{
    uint32_t r, g, b, a, l; float cl[4]; int i;
    for (i = 0; i < 4; i++) cl[i] = c[i] < 0.0f ? 0.0f : c[i] > 1.0f ? 1.0f : c[i];
    r = (uint32_t)(cl[0] * 255.0f + 0.5f); g = (uint32_t)(cl[1] * 255.0f + 0.5f); b = (uint32_t)(cl[2] * 255.0f + 0.5f); a = (uint32_t)(cl[3] * 255.0f + 0.5f); l = r;
    if (type == 0x8363u) { uint32_t v = ((r >> 3) << 11) | ((g >> 2) << 5) | (b >> 3); o[0] = (unsigned char)(v >> 8); o[1] = (unsigned char)v; return; }       /* big-endian host: the short as stored */
    if (type == 0x8033u) { uint32_t v = ((r >> 4) << 12) | ((g >> 4) << 8) | ((b >> 4) << 4) | (a >> 4); o[0] = (unsigned char)(v >> 8); o[1] = (unsigned char)v; return; }
    if (type == 0x8034u) { uint32_t v = ((r >> 3) << 11) | ((g >> 3) << 6) | ((b >> 3) << 1) | (a >> 7); o[0] = (unsigned char)(v >> 8); o[1] = (unsigned char)v; return; }
    switch (fmt) {
    case 0x1908u: o[0] = (unsigned char)r; o[1] = (unsigned char)g; o[2] = (unsigned char)b; o[3] = (unsigned char)a; break;
    case 0x1909u: o[0] = (unsigned char)l; break;
    case 0x1906u: o[0] = (unsigned char)a; break;
    case 0x8049u: o[0] = (unsigned char)a; break;
    case 0x190Au: case 0x85B9u: o[0] = (unsigned char)l; o[1] = (unsigned char)a; break;
    default: o[0] = (unsigned char)r; o[1] = (unsigned char)g; o[2] = (unsigned char)b; break;
    }
}
/* EXT_paletted_texture: GLEngine keeps the palette (glColorTableEXT) as the bytes the app gave, pointed to by state+0x98, its internal format in the low half of state+0x54
 * (0x8051 RGB8 -> 3 bytes per entry, 0x8058 RGBA8 -> 4).  Returns the entry size, 0 if unsupported. */
static uint32_t pal_info(const unsigned char *s, const unsigned char **pal)
{
    uint32_t pp = *(const uint32_t *)(s + 0x98), f = *(const uint16_t *)(s + 0x56);
    if (pp < 0x10000u || pp > M64_UADDR_MAX) return 0;
    *pal = (const unsigned char *)(uintptr_t)pp;
    return f == 0x8051u ? 3u : f == 0x8058u ? 4u : 0u;
}
static int mach64_upload(gld_context *ctx, void *texobj)
{
    mach64_priv *p = MACH64_PRIV(ctx);
    g_cur = &p->rb;
    const unsigned char *t = (const unsigned char *)texobj;
    struct { uint32_t w, h, src, fmt, type, bytes; } lvl[12];
    uint32_t state, base_lod, total, cur, hwfmt, bpt;
    const unsigned char *s;
    mach64_tex_slot *slot;
    int nlevels = 0, L, pre_used = 0, maxlim = 12, bclamp_s = 0, bclamp_t = 0; uint32_t bord = 0, bw0 = 0, bh0 = 0;
    uint32_t tex_new_h1 = 0, tex_new_h2 = 0, tex_new_hl = 0;   /* hash of the image being uploaded (stored in the slot on success) */
    int rect = 0, wmode_s = 0, wmode_t = 0; float wscale_s = 1.0f, wscale_t = 1.0f, wbias_s = 0.0f, wbias_t = 0.0f; unsigned char *mir_bufs[12] = { 0 }, *dec_bufs[12] = { 0 };

    if (!t) return 0;
    state = *(const uint32_t *)(t + 0x30);
    if (state < 0x10000 || state > M64_UADDR_MAX) return 0;
    s = (const unsigned char *)(uintptr_t)state;
    if ((((*(const uint32_t *)s) >> 16) & 0xfu) == 0u) { cube_cache_drop(state); return 0; }
    if (*(const uint16_t *)(s + 0x9c + 4) > 1u) { vol_cache_drop(state); return 0; }                 /* 3D texture (level 0 depth > 1): CPU-sampled volume, no VRAM copy */      /* cube map (state word 0 target nibble 0): shaded by the CPU, no VRAM copy */
    base_lod = s[0x5e];

    /* Enumerate the mip chain from the base level down to 1x1 (stops at the first
     * empty/invalid record -- a non-mipmapped texture yields a single level). */
    {   /* GLEngine's texture validate (0xd284c) derives the effective base level (state +0x5e) and max level (+0x5f), the latter clamped to the complete chain and >= base;
         * the raw GL_TEXTURE_MAX_LEVEL (default 1000) is the halfword at +0x3e. */
        uint32_t effmax = s[0x5f];
        maxlim = effmax >= base_lod ? (int)(effmax - base_lod) + 1 : 1; if (maxlim > 12) maxlim = 12; }
    for (L = 0; L < maxlim; L++) {
        const unsigned char *r = s + 0x9c + (base_lod + (uint32_t)L) * 0x14;
        uint32_t lw = *(const uint16_t *)(r + 0x00), lh = *(const uint16_t *)(r + 0x02);
        uint32_t lsrc = *(const uint32_t *)(r + 0x10);
        if (!lw || !lh || lsrc < 0x10000 || lsrc > M64_UADDR_MAX) break;
        if (lw > 1024u) { if (L == 0) return 0; break; }
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
            if (lvl[0].fmt == 0x1900u) { const unsigned char *pl; uint32_t be = pal_info(s, &pl), a1, a2; if (be) { tex_hash(pl, 256u * be, &a1, &a2); h1 ^= a1 * 31u; h2 ^= a2; } }
            h1 ^= *(const uint32_t *)(s + 0x10) * 0x9e3779b1u; h1 ^= (*(const uint32_t *)(s + 0x1c) ^ (*(const uint32_t *)(s + 0x20) << 3) ^ (*(const uint32_t *)(s + 0x24) << 6) ^ (*(const uint32_t *)(s + 0x28) << 9)) * 0x85ebca6bu;   /* the wrap modes and border colour shape the stored image */
            TEX_LOCK(); ex = slot_find(tex_key(texobj));
            if (ex && ex->rsize && ex->owner == g_cur && ex->hlen == hl && ex->h1 == h1 && ex->h2 == h2 && ex->w == lvl[0].w && ex->h == lvl[0].h && ex->nlevels == nlevels) {
                ex->texobj = texobj; ex->tick = ++g_tick; TEX_UNLOCK();
                return 1;
            }
            TEX_UNLOCK();
            tex_new_h1 = h1; tex_new_h2 = h2; tex_new_hl = hl;
        }
    }

    if (lvl[0].fmt == 0x1900u && lvl[0].type == 0x1401u) {            /* colour-index levels: expand through the palette to RGB / RGBA bytes */
        const unsigned char *pl; uint32_t be = pal_info(s, &pl);
        if (be) {
            for (L = 0; L < nlevels; L++) {
                uint32_t n = lvl[L].w * lvl[L].h, i, k; const unsigned char *ix = (const unsigned char *)(uintptr_t)lvl[L].src; unsigned char *d = (unsigned char *)malloc((size_t)n * be);
                if (!d) { int q; for (q = 0; q < 12; q++) free(dec_bufs[q]); return 0; }
                for (i = 0; i < n; i++) for (k = 0; k < be; k++) d[i * be + k] = pl[(uint32_t)ix[i] * be + k];
                lvl[L].src = (uint32_t)(uintptr_t)d; lvl[L].fmt = be == 3u ? 0x1907u : 0x1908u; dec_bufs[L] = d;
            }
        }
    }
    for (L = 0; L < nlevels; L++) {          /* compressed (S3TC) levels: decode to RGBA bytes; the rest of the upload sees an ordinary GL_RGBA image */
        if (lvl[L].fmt >= 0x83F0u && lvl[L].fmt <= 0x83F3u) {
            unsigned char *d = s3tc_decode((const unsigned char *)(uintptr_t)lvl[L].src, lvl[L].w, lvl[L].h, lvl[L].fmt);
            if (!d) { int k; for (k = 0; k < 12; k++) { free(dec_bufs[k]); } return 0; }
            lvl[L].src = (uint32_t)(uintptr_t)d; lvl[L].fmt = (lvl[L].fmt == 0x83F0u) ? 0x1907u : 0x1908u; lvl[L].type = 0x1401u; dec_bufs[L] = d;
            if (lvl[L].fmt == 0x1907u) {                         /* RGB: drop the (opaque) alpha bytes into a packed RGB array */
                uint32_t n = lvl[L].w * lvl[L].h, i; for (i = 0; i < n; i++) { d[i * 3] = d[i * 4]; d[i * 3 + 1] = d[i * 4 + 1]; d[i * 3 + 2] = d[i * 4 + 2]; }
            }
        }
    }
    /* Texture borders (glTexImage border = 1..): GLEngine stores the full size including the border in the level record (+0,+2) and the border width in the byte at +6
     * (glGetTexLevelParameter reads it from there, GLEngine 0x487fc..0x48808); the image data includes the border texels.  Per axis: GL_CLAMP keeps the border texels (they are
     * what an out-of-range or edge-filtered fetch sees) and is padded/clamped like a non-power-of-two image with the coordinates remapped onto the interior; every other
     * wrap mode samples the interior only, so the image is cropped to it. */
    bord = s[0x9c + 6]; bclamp_s = bclamp_t = 0; bw0 = lvl[0].w; bh0 = lvl[0].h;
    if (bord && bord < 8u) {
        uint32_t wrapb = *(const uint32_t *)(s + 0x10);
        bclamp_s = (wrapb >> 16) == 0x2900u; bclamp_t = (wrapb & 0xffffu) == 0x2900u;
        for (L = 0; L < nlevels; L++) {
            uint32_t bpp = tex_src_bytes(1, 1, (uint16_t)lvl[L].fmt, (uint16_t)lvl[L].type), w = lvl[L].w, h = lvl[L].h, x0 = bclamp_s ? 0u : bord, y0 = bclamp_t ? 0u : bord, y;
            uint32_t nw = bclamp_s ? w : (w > 2u * bord ? w - 2u * bord : 1u), nh = bclamp_t ? h : (h > 2u * bord ? h - 2u * bord : 1u);
            const unsigned char *src = (const unsigned char *)(uintptr_t)lvl[L].src; unsigned char *d;
            if (nw == w && nh == h) continue;
            d = (unsigned char *)malloc((size_t)nw * nh * bpp);
            if (!d) break;
            for (y = 0; y < nh; y++) memcpy(d + (size_t)y * nw * bpp, src + ((size_t)(y + y0) * w + x0) * bpp, (size_t)nw * bpp);
            free(dec_bufs[L]); dec_bufs[L] = d; lvl[L].src = (uint32_t)(uintptr_t)d; lvl[L].w = nw; lvl[L].h = nh;
        }
    }
    /* Wrap modes the chip lacks (it repeats or clamps only): the stored image is rebuilt so plain REPEAT/CLAMP gives the GL result, and the coordinates are mapped by
     * a per-axis scale and bias at draw time (mach64_tex_mscale/mbias).
     *   GL_MIRRORED_REPEAT: [image | mirror], REPEAT, s' = s / 2
     *   GL_MIRROR_CLAMP_EXT: [mirror | image], CLAMP, s' = s / 2 + 1/2
     *   GL_CLAMP_TO_BORDER_ARB: the image inside a ring of border colour (padded to a power of two), CLAMP, s' = (s * w + 1) / W   (single level only)
     *   non-power-of-two sizes (ARB_texture_non_power_of_two): the chip needs power-of-two images.  CLAMP axes: the image padded to the next power of two with its edge texels, CLAMP,
     *   s' = s * w / W; REPEAT (and any mirror/border mode) axes: the image stretched to the next power of two (nearest), plain REPEAT.
     * Skipped (plain repeat / edge clamp) when the stored size would exceed 1024. */
    wmode_s = wmode_t = 0;
    {   uint32_t wrap = *(const uint32_t *)(s + 0x10), ws = wrap >> 16, wt = wrap & 0xffffu;
        wmode_s = ws == 0x8370u ? 1 : ws == 0x8742u ? 3 : ws == 0x812du ? 2 : 0; wmode_t = wt == 0x8370u ? 1 : wt == 0x8742u ? 3 : wt == 0x812du ? 2 : 0;
        rect = (((*(const uint32_t *)s) >> 16) & 0xfu) == 2u;                 /* GL_TEXTURE_RECTANGLE_EXT (state word 0 bits 16..19: 3 = 2D, 2 = rectangle): texel-unit coordinates, never repeats */
        if (rect) wmode_s = wmode_t = 0;
        if (lvl[0].w & (lvl[0].w - 1u)) wmode_s = (!rect && (wmode_s || ws == 0x2901u)) ? 5 : 4;
        if (lvl[0].h & (lvl[0].h - 1u)) wmode_t = (!rect && (wmode_t || wt == 0x2901u)) ? 5 : 4;
        if (bclamp_s) wmode_s = 4; if (bclamp_t) wmode_t = 4;          /* bordered CLAMP axes: pad with the edge (= border) texels */
        if (nlevels > 1 && (wmode_s == 2 || wmode_t == 2)) { if (wmode_s == 2) wmode_s = 0; if (wmode_t == 2) wmode_t = 0; }
        if ((wmode_s || wmode_t)) {
            uint32_t W0 = lvl[0].w, H0 = lvl[0].h, nw0 = W0, nh0 = H0;
            if (wmode_s == 1 || wmode_s == 3) nw0 = W0 * 2u; else if (wmode_s == 2) { nw0 = 4u; while (nw0 < W0 + 2u) nw0 <<= 1; } else if (wmode_s >= 4) nw0 = potge(W0);
            if (wmode_t == 1 || wmode_t == 3) nh0 = H0 * 2u; else if (wmode_t == 2) { nh0 = 4u; while (nh0 < H0 + 2u) nh0 <<= 1; } else if (wmode_t >= 4) nh0 = potge(H0);
            if (nw0 > 1024u || nh0 > 1024u) wmode_s = wmode_t = 0;
        }
        if (wmode_s || wmode_t) {
            float bcf[4]; unsigned char bpix[8]; uint32_t k;
            for (k = 0; k < 4; k++) { uint32_t b = *(const uint32_t *)(s + 0x1c + k * 4); memcpy(&bcf[k], &b, 4); }
            for (L = 0; L < nlevels; L++) {
                uint32_t bpp = tex_src_bytes(1, 1, (uint16_t)lvl[L].fmt, (uint16_t)lvl[L].type), w = lvl[L].w, h = lvl[L].h, nw = w, nh = h, x, y;
                const unsigned char *src = (const unsigned char *)(uintptr_t)lvl[L].src; unsigned char *dst;
                if (wmode_s == 1 || wmode_s == 3) nw = w * 2u; else if (wmode_s == 2) { nw = 4u; while (nw < w + 2u) nw <<= 1; } else if (wmode_s >= 4) nw = potge(w);
                if (wmode_t == 1 || wmode_t == 3) nh = h * 2u; else if (wmode_t == 2) { nh = 4u; while (nh < h + 2u) nh <<= 1; } else if (wmode_t >= 4) nh = potge(h);
                tex_border_pixel((uint16_t)lvl[L].fmt, (uint16_t)lvl[L].type, bcf, bpix);
                dst = (unsigned char *)malloc((size_t)nw * nh * bpp);
                if (!dst) { wmode_s = wmode_t = 0; break; }
                for (y = 0; y < nh; y++) {
                    int64_t sy = y; int by = 0;
                    if (wmode_t == 1) sy = y >= h ? (int64_t)(2u * h - 1u - y) : (int64_t)y;
                    else if (wmode_t == 3) sy = y < h ? (int64_t)(h - 1u - y) : (int64_t)(y - h);
                    else if (wmode_t == 2) { sy = (int64_t)y - 1; by = (sy < 0 || sy >= (int64_t)h); }
                    else if (wmode_t == 4) sy = y < h ? (int64_t)y : (int64_t)(h - 1u);
                    else if (wmode_t == 5) sy = (int64_t)(((uint64_t)y * h) / nh);
                    for (x = 0; x < nw; x++) {
                        int64_t sx = x; int bx = 0;
                        if (wmode_s == 1) sx = x >= w ? (int64_t)(2u * w - 1u - x) : (int64_t)x;
                        else if (wmode_s == 3) sx = x < w ? (int64_t)(w - 1u - x) : (int64_t)(x - w);
                        else if (wmode_s == 2) { sx = (int64_t)x - 1; bx = (sx < 0 || sx >= (int64_t)w); }
                        else if (wmode_s == 4) sx = x < w ? (int64_t)x : (int64_t)(w - 1u);
                        else if (wmode_s == 5) sx = (int64_t)(((uint64_t)x * w) / nw);
                        if (bx || by) memcpy(dst + ((size_t)y * nw + x) * bpp, bpix, bpp);
                        else memcpy(dst + ((size_t)y * nw + x) * bpp, src + ((size_t)sy * w + (size_t)sx) * bpp, bpp);
                    }
                }
                if (L == 0) {
                wscale_s = wmode_s == 1 || wmode_s == 3 ? 0.5f : (wmode_s == 2 || wmode_s == 4) ? (float)w / (float)nw : 1.0f; wbias_s = wmode_s == 3 ? 0.5f : wmode_s == 2 ? 1.0f / (float)nw : 0.0f;
                wscale_t = wmode_t == 1 || wmode_t == 3 ? 0.5f : (wmode_t == 2 || wmode_t == 4) ? (float)h / (float)nh : 1.0f; wbias_t = wmode_t == 3 ? 0.5f : wmode_t == 2 ? 1.0f / (float)nh : 0.0f;
                }
                lvl[L].w = nw; lvl[L].h = nh; lvl[L].src = (uint32_t)(uintptr_t)dst; mir_bufs[L] = dst;
                if (L == 0 && wmode_s == 0 && wmode_t == 0) break;
            }
        }
    }
    if (bord && bord < 8u) {          /* bordered CLAMP: s' = (b + s * interior) / padded width */
        if (bclamp_s && wmode_s == 4) { wscale_s = (float)(bw0 - 2u * bord) / (float)lvl[0].w; wbias_s = (float)bord / (float)lvl[0].w; }
        if (bclamp_t && wmode_t == 4) { wscale_t = (float)(bh0 - 2u * bord) / (float)lvl[0].h; wbias_t = (float)bord / (float)lvl[0].h; }
    }
    if (rect) { wscale_s = 1.0f / (float)lvl[0].w; wscale_t = 1.0f / (float)lvl[0].h; wbias_s = wbias_t = 0.0f; }    /* texel units -> normalised over the stored (padded) size */
    /* Texel format.  Packed 16-bit sources (5_6_5, 4_4_4_4, 5_5_5_1) are stored natively as RGB565 / ARGB4444 / ARGB1555 (half the VRAM, exact); everything
     * else is ARGB8888 -- and if an ARGB8888 texture does not fit the heap it is retried as 16-bit (RGB565 without alpha, ARGB4444 with), the way a compact texture format
     * would be chosen.  Only the Path B (kext memcpy) upload can pack 16-bit texels. */
    {   uint32_t t0 = lvl[0].type, f0 = lvl[0].fmt;
        int alpha0 = (f0 == 0x1908u || f0 == 0x1906u || f0 == 0x190Au || f0 == 0x8049u);
        if (f0 == 0x1908u && t0 == 0x1401u) {        /* an RGBA image whose alpha is 255 everywhere (Chess board) carries no alpha: store it as RGB565 so the blend source alpha is the vertex alpha */
            const unsigned char *px = (const unsigned char *)(uintptr_t)lvl[0].src; uint32_t n = lvl[0].w * lvl[0].h, i;
            for (i = 0; i < n && px[i * 4 + 3] == 0xffu; i++) {}
            if (i == n) alpha0 = 0;
        }
        int attempt;
        hwfmt = 6u; bpt = 4u; slot = 0;
        { mach64_tex_slot *pre; TEX_LOCK(); pre = slot_find(tex_key(texobj)); pre_used = (pre && pre->rsize && pre->owner == g_cur && pre->gen == mach64_batch_gen); TEX_UNLOCK(); }
        if (lvl[0].w >= 1) {
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
            if (slot || bpt == 2u || lvl[0].w < 2) break;
            hwfmt = alpha0 ? 15u : 4u; bpt = 2u;                 /* heap full as ARGB8888: retry compact */
        }
    }
    /* Order: pending triangles must run before a texture region they read is rewritten.  Draining the batch on EVERY upload made one drain (= one type-1
     * present/copy opportunity) per texture change -- Sonic R's 6 background tiles landed as 6 partial frames.  Flush only if this upload re-uses a region the
     * pending batch bound (re-upload of a bound slot, or an eviction victim bound in this batch); a fresh region cannot be read by queued triangles. */
    if (pre_used || g_evict_used) mach64_flush_batch(ctx);
    p->dirty |= MACH64_DIRTY_STATE;   /* the upload's 2D blit clobbers engine state; the next triangle re-emits it */
    if (!slot) { int k; for (k = 0; k < 12; k++) { free(mir_bufs[k]); free(dec_bufs[k]); } return 0; }
    free(slot->cpu); slot->cpu = 0;
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
        slot->noinset = (bord && bord < 8u && ((bclamp_s && wmode_s == 4) || (bclamp_t && wmode_t == 4)));
        slot->wscale[0] = wscale_s; slot->wscale[1] = wscale_t; slot->wbias[0] = wbias_s; slot->wbias[1] = wbias_t;
        if (rect) { slot->clamp_s = slot->clamp_t = 1; }
        if (wmode_s == 1 || wmode_s == 5) slot->clamp_s = 0; else if (wmode_s == 2 || wmode_s == 3 || wmode_s == 4) slot->clamp_s = 1;
        if (wmode_t == 1 || wmode_t == 5) slot->clamp_t = 0; else if (wmode_t == 2 || wmode_t == 3 || wmode_t == 4) slot->clamp_t = 1;
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
        tex_upload_image(cur, lvl[L].w, lvl[L].h,
                         (const unsigned char *)(uintptr_t)lvl[L].src,
                         (uint16_t)lvl[L].fmt, (uint16_t)lvl[L].type, bpt, hwfmt);
        if (r < 12) slot->mip_off[r] = cur;
        slot->lvl_off[L] = cur; slot->lvl_w[L] = lvl[L].w; slot->lvl_h[L] = lvl[L].h;
        cur += lvl[L].bytes;
    }

    p->dirty |= MACH64_DIRTY_STATE;      /* new texels -> re-emit tex state       */
    slot->h1 = tex_new_h1; slot->h2 = tex_new_h2; slot->hlen = tex_new_hl;
    { int k; for (k = 0; k < 12; k++) { free(mir_bufs[k]); free(dec_bufs[k]); } }
    return 1;
}

/* GL_TEXTURE_2D enable lives in the gli as bit 3 of the word at +0x6748 (0x08
 * when enabled, 0 when disabled; recovered via tools/mach64_texstate gli-diff). */
#define GLI_TEX2D_ENABLE   0x6748
#define GLI_TEX2D_BIT      0x00000008u
#define GLI_TEXRECT_BIT    0x00000004u    /* GL_TEXTURE_RECTANGLE_EXT enabled (its bound texobj is the word BEFORE the 2D one: targets are 1D, rect, 2D, 3D, cube at 4-byte steps from 0x15744) */
#define GLI_TEXCUBE_BIT    0x00000001u     /* GL_TEXTURE_CUBE_MAP enabled; its bound texobj is the word at 0x1573c, 12 bytes before the 2D one */
#define GLI_TEX3D_BIT      0x00000002u     /* GL_TEXTURE_3D enabled; its bound texobj is the word at 0x15740 (cube 0x1573c, 3D, rect, 2D, 1D at 4-byte steps); CPU-sampled, never on the chip */
#define GLI_TEX1D_BIT      0x00000010u     /* GL_TEXTURE_1D enabled; its bound texobj is the word AFTER the 2D one (found with RAGE_ENDUMP) */
#define GLI_TEXANY_BITS    (GLI_TEX2D_BIT | GLI_TEXRECT_BIT | GLI_TEXCUBE_BIT | GLI_TEX1D_BIT)
#define GLI_UNIT_STRIDE    0x7cu       /* per texture unit: enable word, env-mode word */
#define GLI_BOUND_STRIDE   0x14u       /* per texture unit: bound texobj pointer       */
static uint32_t unit_bound(gld_context *ctx, uint32_t u, uint32_t en)      /* the bound texobj of the highest-priority enabled target (cube > 2D > rectangle) */
{
    uint32_t base = GLI_BOUND_TEXOBJ + u * GLI_BOUND_STRIDE;
    if (en & GLI_TEXCUBE_BIT) return mach64_gli_u32(ctx, base - 12u);
    if (en & GLI_TEX3D_BIT) return mach64_gli_u32(ctx, base - 8u);
    if (en & GLI_TEX2D_BIT) return mach64_gli_u32(ctx, base);
    if (en & GLI_TEXRECT_BIT) return mach64_gli_u32(ctx, base - 4u);
    if (en & GLI_TEX1D_BIT) return mach64_gli_u32(ctx, base + 4u);
    return 0;
}

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
int mach64_tex_cube = 0;       /* a cube map is bound on unit 0: CPU-shaded (gld_stencil.c), the vertex carries the third coordinate */
static uint32_t g_cube_state;
static uint32_t g_vol_state;   /* GL_TEXTURE_3D bound on unit 0 (also sets mach64_tex_cube: the third coordinate rides in the vertex, the CPU shades it) */
static void vol_cache_drop(uint32_t state);  /* its state object (level records at +0x9c + face * 0x12c) */
int mach64_tex_noinset;
float mach64_tex_mscale[2] = { 1.0f, 1.0f }, mach64_tex_mbias[2] = { 0.0f, 0.0f };      /* per-draw S/T coordinate map of the bound texture (see mach64_upload) */
int mach64_spec_mode = 0;
int mach64_env_add = 0;       /* GL_TEXTURE_ENV_MODE GL_ADD on this draw: primary colour rides the specular add, the diffuse colour becomes white */
int mach64_fog_on = 0;                    /* consumed by mach64_read_vertex       */
int mach64_flat_on = 0;                   /* glShadeModel(GL_FLAT): emit_tri flattens colour */

/* Clamp a 0..1 float colour channel to a 0..255 byte. */
static uint32_t chan_u8(float f){ return rage_f2byte(gld_bits(f)); }

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
    g_cur = &p->rb;
    m64_state *st = &p->hw;
    mach64_tex_slot *bound, *bound2;
    /* The textures this draw will use must survive any eviction triggered by an upload below. */
    {   int u; for (u = 0; u < 2; u++) {
            uint32_t en = mach64_gli_u32(ctx, GLI_TEX2D_ENABLE + (uint32_t)u * GLI_UNIT_STRIDE), bt = unit_bound(ctx, (uint32_t)u, en);
            g_protect[u] = ((en & GLI_TEXANY_BITS) && bt > 0x10000 && bt <= M64_UADDR_MAX) ? *(const uint32_t *)(uintptr_t)(bt + 0x30) : 0;
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
        /* 16 bpp target: RGB565 R[15:11] G[10:5] B[4:0], or 1555 R[14:10] G[9:5] B[4:0] (rb.fmt555); no usable alpha channel in the buffer */
        uint32_t wm = p->rb.fmt555 ? (0x8000u | ((cm >> 24) & 0xffu ? 0x7c00u : 0u) | ((cm >> 16) & 0xffu ? 0x03e0u : 0u) | ((cm >> 8) & 0xffu ? 0x001fu : 0u))
                                   : (((cm >> 24) & 0xffu ? 0xf800u : 0u) | ((cm >> 16) & 0xffu ? 0x07e0u : 0u) | ((cm >> 8) & 0xffu ? 0x001fu : 0u));
        wm |= 0xffff0000u;
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
     * delivers the secondary colour as floats R,G,B at gli-vertex +0x50/+0x54/+0x58 (real GLEngine output, Chess; the Radeon reference layout +0x40 is the normal).  Decided below once we know if texturing is on. */
    mach64_spec_mode = (!lop_on && (mach64_gli_u32(ctx, GLI_FOG_ENABLE) & 1u)) ? 2 : 0;
    mach64_env_add = 0;
    mach64_fog_on = lop_on ? 0 : mach64_fog_active(ctx);
    if (mach64_fog_on) p->dirty |= MACH64_DIRTY_STATE;
    else               st->dp_fog_clr = 0;   /* leave DP_FRGD_CLR to the 2D engine */

    /* Texture units.  GLEngine keeps unit u's enable word at gli+0x6748+u*0x7c (bit 3), its env mode at +0x674c+u*0x7c and its bound texobj at
     * gli+0x15748+u*0x14 (all recovered with tools/mach64_mtexscan: the software context's gli, diffed while unit 1 was enabled / bound).
     * Both units enabled with textures bound -> ONE-pass compositing (primary = unit 0, secondary = unit 1); only unit 1 enabled -> it simply
     * becomes the primary map (its coordinates are read from the unit-1 vertex fields). */
    mach64_tex_vofs[0] = 0x80u; mach64_tex_vofs[1] = 0u; mach64_tex_mscale[0] = mach64_tex_mscale[1] = 1.0f; mach64_tex_mbias[0] = mach64_tex_mbias[1] = 0.0f; mach64_tex_noinset = 0; mach64_tex_cube = 0; g_cube_state = 0; g_vol_state = 0;
    {   mach64_tex_slot *ub[2]; int u;
        for (u = 0; u < 2; u++) {
            uint32_t en = mach64_gli_u32(ctx, GLI_TEX2D_ENABLE + (uint32_t)u * GLI_UNIT_STRIDE);
            uint32_t bt = unit_bound(ctx, (uint32_t)u, en);
            ub[u] = 0;
            if (u == 0 && (en & GLI_TEXCUBE_BIT) && bt > 0x10000 && bt <= M64_UADDR_MAX) {          /* cube map: never on the chip; the CPU shades it from GLEngine's six face images */
                uint32_t cs = *(const uint32_t *)(uintptr_t)(bt + 0x30);
                if (cs > 0x10000 && cs <= M64_UADDR_MAX) { mach64_tex_cube = 1; g_cube_state = cs; }
                continue;
            }
            if (u == 0 && (en & GLI_TEX3D_BIT) && !(en & GLI_TEXCUBE_BIT) && bt > 0x10000 && bt <= M64_UADDR_MAX) {            /* 3D texture: never on the chip; the CPU shades it from GLEngine's volume image */
                uint32_t vs = *(const uint32_t *)(uintptr_t)(bt + 0x30);
                if (vs > 0x10000 && vs <= M64_UADDR_MAX) { mach64_tex_cube = 1; g_vol_state = vs; }
                continue;
            }
            if ((en & GLI_TEXANY_BITS) && bt > 0x10000 && bt <= M64_UADDR_MAX) {
                uint32_t key = *(const uint32_t *)(uintptr_t)(bt + 0x30);
                {   static mach64_tex_slot *last_sl[2];                       /* per-unit cache: the slot table is a static array, a slot's identity is its key, so a hit needs no lock and no search */
                    if (last_sl[u] && last_sl[u]->key == key) ub[u] = last_sl[u];
                    else { TEX_LOCK(); ub[u] = slot_find(key); TEX_UNLOCK(); last_sl[u] = ub[u]; }   /* `bound` is this thread's own bound texture */
                }
                if (!ub[u]) {                                                                       /* never uploaded: GLEngine stored the image without a dirty call (Chess board) -> upload from its level record */
                    const unsigned char *s0 = (key > 0x10000 && key <= M64_UADDR_MAX) ? (const unsigned char *)(uintptr_t)key : 0;
                    if (s0) {
                        const unsigned char *r0 = s0 + 0x9c + (uint32_t)s0[0x5e] * 0x14;
                        uint32_t sig = (uint32_t)*(const uint16_t *)r0 | ((uint32_t)*(const uint16_t *)(r0 + 2) << 16), src = *(const uint32_t *)(r0 + 0x10);
                        if (sig && src > 0x10000) {
                            static struct { uint32_t key, sig, src; } neg[16]; static int nneg; int k, known = 0;
                            for (k = 0; k < 16; k++) if (neg[k].key == key && neg[k].sig == sig && neg[k].src == src) { known = 1; break; }
                            if (!known) {
                                mach64_upload(ctx, (void *)(uintptr_t)bt);
                                TEX_LOCK(); ub[u] = slot_find(key); TEX_UNLOCK();
                                if (!ub[u]) { neg[nneg & 15].key = key; neg[nneg & 15].sig = sig; neg[nneg & 15].src = src; nneg++; }   /* unusable as it stands: do not retry until the record changes */
                            }
                        }
                    }
                }
                if (ub[u] && (!ub[u]->rsize || ub[u]->owner != g_cur) && ub[u]->texobj) {           /* evicted: bring it back from GLEngine's copy */
                    mach64_upload(ctx, ub[u]->texobj);
                    TEX_LOCK(); ub[u] = slot_find(key); TEX_UNLOCK();
                }
                if (ub[u] && (!ub[u]->rsize || ub[u]->owner != g_cur)) ub[u] = 0;                   /* could not be made resident: draw untextured */
                if (ub[u]) { ub[u]->tick = ++g_tick; ub[u]->gen = mach64_batch_gen; }
            }
        }
        bound = 0; bound2 = 0; punit = 0;
        if (ub[0]) { bound = ub[0]; bound2 = ub[1]; }
        else if (ub[1]) { bound = ub[1]; punit = 1; }
        if (bound2) mach64_tex_vofs[1] = 0x90u;
        mach64_tex_vofs[0] = 0x80u + (uint32_t)punit * 0x10u;
        if (bound) { mach64_tex_noinset = bound->noinset; mach64_tex_mscale[0] = bound->wscale[0]; mach64_tex_mscale[1] = bound->wscale[1]; mach64_tex_mbias[0] = bound->wbias[0]; mach64_tex_mbias[1] = bound->wbias[1]; }
    }
    tex_on = bound != 0;
    g_cpu_slot = bound; g_cpu_unit = (uint32_t)punit; g_cpu_multi = bound2 != 0;

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
        case 0x0104:          light_fcn = 1u << 22; mach64_env_add = 1; break;   /* GL_ADD: Cp + Ct = Ct * white + specular(Cp) */
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
        if (mach64_spec_mode || mach64_env_add) { atc |= 1u << 31; if (mach64_spec_mode) mach64_spec_mode = 1; }     /* SPECULAR_LIGHT_EN: add the interpolated specular rgb to textured pixels */
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


/* ---- CPU texturing support (gld_stencil.c's compositing rasteriser) ------------------------------------------------------------------------------------------ */
#define ENV_REPLACE 0x1e01u
#define ENV_MODULATE 0x2100u
#define ENV_DECAL 0x2101u
#define ENV_BLEND 0x0be2u
#define ENV_ADD 0x0104u
#define ENV_COMBINE 0x8570u
/* Does the bound texture environment need the CPU?  gli words of unit 0 (see docs): mode 0x674c >> 16; combine: rgb func 0x6750 hi, alpha func lo, sources 0x6754/58/5c, operands 0x6760/64/68, scales 0x676c/0x6770. */
int mach64_env_needs_cpu(uint32_t gli)
{
    const uint32_t *w = (const uint32_t *)(uintptr_t)gli;
    uint32_t en = w[0x6748 / 4], mode = w[0x674c / 4] >> 16;
    if (!(en & (GLI_TEXANY_BITS | GLI_TEX3D_BIT)) || (w[(0x6748 + GLI_UNIT_STRIDE) / 4] & GLI_TEXANY_BITS)) return 0;
    if (en & (GLI_TEXCUBE_BIT | GLI_TEX3D_BIT)) return 1;       /* off, or two units (one-pass hardware compositing) */
    if (mode == ENV_BLEND) return 1;
    if (mode != ENV_COMBINE) return 0;
    {   uint32_t rf = w[0x6750 / 4] >> 16, af = w[0x6750 / 4] & 0xffffu;
#ifndef GLD_EXACT_ADD_SIGNED
        if (rf == 0x8574u && (af == ENV_MODULATE || af == ENV_REPLACE)) return 0;     /* Sonic R's overbright ADD_SIGNED stays on the hardware approximation (see bind_texture): the CPU path would cost its frame rate */
#endif
        (void)af; (void)rf;
        return 1;
    }
}
static uint32_t fl_to_q8(uint32_t bits) { float f; __builtin_memcpy(&f, &bits, 4); return f <= 0.0f ? 0u : f >= 16.0f ? 16u * 256u : (uint32_t)(f * 256.0f + 0.5f); }
static void cpu_decode(const mach64_tex_slot *sl, const unsigned char *raw, uint32_t n, uint32_t *out)
{
    uint32_t i;
    for (i = 0; i < n; i++) {
        uint32_t v, a = 255, r, g, b;
        switch (sl->hwfmt) {
        case 4u:  v = ((uint32_t)raw[i*2] << 8) | raw[i*2+1]; r = (v >> 11) & 31u; g = (v >> 5) & 63u; b = v & 31u; r = (r << 3) | (r >> 2); g = (g << 2) | (g >> 4); b = (b << 3) | (b >> 2); break;
        case 15u: v = ((uint32_t)raw[i*2] << 8) | raw[i*2+1]; a = ((v >> 12) & 15u) * 17u; r = ((v >> 8) & 15u) * 17u; g = ((v >> 4) & 15u) * 17u; b = (v & 15u) * 17u; break;
        case 3u:  v = ((uint32_t)raw[i*2] << 8) | raw[i*2+1]; a = (v & 0x8000u) ? 255u : 0u; r = (v >> 10) & 31u; g = (v >> 5) & 31u; b = v & 31u; r = (r << 3) | (r >> 2); g = (g << 3) | (g >> 2); b = (b << 3) | (b >> 2); break;
        case 8u:  r = g = b = raw[i]; break;
        default:  v = ((uint32_t)raw[i*4] << 24) | ((uint32_t)raw[i*4+1] << 16) | ((uint32_t)raw[i*4+2] << 8) | raw[i*4+3]; out[i] = v; continue;
        }
        out[i] = (a << 24) | (r << 16) | (g << 8) | b;
    }
}
/* Cube map faces for the CPU shader: six ARGB images decoded from GLEngine's level records (state + 0x9c + face * 0x12c, level 0), cached until the texture is re-uploaded or deleted. */
static struct { uint32_t state; uint32_t *img[6]; int w, h, has_a; } g_cube;
static void cube_cache_drop(uint32_t state)
{
    int i;
    if (g_cube.state != state) return;
    for (i = 0; i < 6; i++) { free(g_cube.img[i]); g_cube.img[i] = 0; }
    g_cube.state = 0;
}
static int cube_env(gld_context *ctx, m64_cpuenv *o)
{
    int f; const unsigned char *s = (const unsigned char *)(uintptr_t)g_cube_state; uint32_t u = g_cpu_unit * GLI_UNIT_STRIDE, k; (void)ctx;
    if (g_cube.state != g_cube_state) {
        uint32_t w, h, fmt, type;
        cube_cache_drop(g_cube.state);
        w = *(const uint16_t *)(s + 0x9c); h = *(const uint16_t *)(s + 0x9e); fmt = *(const uint16_t *)(s + 0x9c + 0x0c); type = *(const uint16_t *)(s + 0x9c + 0x0e);
        if (!w || !h) return 0;
        for (f = 0; f < 6; f++) {
            const unsigned char *r = s + 0x9c + (uint32_t)f * 0x12cu; uint32_t src = *(const uint32_t *)(r + 0x10), i, n = w * h;
            if (src < 0x10000u || src > M64_UADDR_MAX) { cube_cache_drop(g_cube_state); g_cube.state = g_cube_state; for (i = 0; i < 6; i++) { free(g_cube.img[i]); g_cube.img[i] = 0; } g_cube.state = 0; return 0; }
            g_cube.img[f] = (uint32_t *)malloc((size_t)n * 4u);
            if (!g_cube.img[f]) { for (i = 0; i < 6; i++) { free(g_cube.img[i]); g_cube.img[i] = 0; } return 0; }
            for (i = 0; i < n; i++) g_cube.img[f][i] = tex_texel_argb((const unsigned char *)(uintptr_t)src, i, (uint16_t)fmt, (uint16_t)type);
        }
        g_cube.state = g_cube_state; g_cube.w = (int)w; g_cube.h = (int)h; g_cube.has_a = (fmt == 0x1908u || fmt == 0x1906u || fmt == 0x190Au || fmt == 0x8049u);
    }
    o->cube = 1; for (f = 0; f < 6; f++) o->face[f] = g_cube.img[f];
    o->img = g_cube.img[0]; o->w = g_cube.w; o->h = g_cube.h; o->wsh = (int)ilog2u((uint32_t)o->w); o->clamp_s = o->clamp_t = 1; o->has_a = g_cube.has_a;
    o->bil = *(const uint16_t *)(s + 0x18) == 0x2601u;
    o->mode = mach64_gli_u32(ctx, GLI_TEX_ENV_MODE + u) >> 16;
    { uint32_t a = mach64_gli_u32(ctx, 0x6750u + u); o->rgbf = a >> 16; o->alf = a & 0xffffu; }
    { uint32_t a = mach64_gli_u32(ctx, 0x6754u + u), b = mach64_gli_u32(ctx, 0x6758u + u), c = mach64_gli_u32(ctx, 0x675cu + u);
      o->src[0] = a >> 16; o->src[1] = a & 0xffffu; o->src[2] = b >> 16; o->asrc[0] = b & 0xffffu; o->asrc[1] = c >> 16; o->asrc[2] = c & 0xffffu; }
    { uint32_t a = mach64_gli_u32(ctx, 0x6760u + u), b = mach64_gli_u32(ctx, 0x6764u + u), c = mach64_gli_u32(ctx, 0x6768u + u);
      o->op[0] = a >> 16; o->op[1] = a & 0xffffu; o->op[2] = b >> 16; o->aop[0] = b & 0xffffu; o->aop[1] = c >> 16; o->aop[2] = c & 0xffffu; }
    o->rscale = (int)((fl_to_q8(mach64_gli_u32(ctx, 0x676cu + u)) + 128u) >> 8); o->ascale = (int)((fl_to_q8(mach64_gli_u32(ctx, 0x6770u + u)) + 128u) >> 8);
    if (o->rscale < 1) o->rscale = 1; if (o->ascale < 1) o->ascale = 1;
    for (k = 0; k < 4; k++) { uint32_t q = fl_to_q8(mach64_gli_u32(ctx, 0x6738u + u + k * 4u)); o->ec[k] = (int)(q > 255u ? 255u : q); }
    return 1;
}

/* 3D texture for the CPU shader: GLEngine's level-0 volume image (state + 0x9c: w, h, depth u16 at +0, +2, +4; format/type at +0xc/+0xe; source pointer +0x10, slices of w*h texels
 * one after another) decoded to ARGB8888 and cached until the texture is re-uploaded or deleted.  Wrap S/T = the halves of the word at state+0x10, wrap R = the high half of the
 * word at state+0x14 (glTexParameter 0x8072 stores at texobj+0x38 = state+0x14, GLEngine 0x4922c); 0x2900 GL_CLAMP and 0x812f CLAMP_TO_EDGE clamp, anything else repeats. */
static struct { uint32_t state; uint32_t *img; int w, h, d, has_a; } g_vol;
static void vol_cache_drop(uint32_t state)
{
    if (g_vol.state != state || !state) return;
    free(g_vol.img); g_vol.img = 0; g_vol.state = 0;
}
static int vol_env(gld_context *ctx, m64_cpuenv *o)
{
    const unsigned char *s = (const unsigned char *)(uintptr_t)g_vol_state; uint32_t u = g_cpu_unit * GLI_UNIT_STRIDE, k; uint32_t ws, wt, wr;
    if (g_vol.state != g_vol_state) {
        uint32_t w = *(const uint16_t *)(s + 0x9c), h = *(const uint16_t *)(s + 0x9e), d = *(const uint16_t *)(s + 0x9c + 4), fmt = *(const uint16_t *)(s + 0x9c + 0x0c), type = *(const uint16_t *)(s + 0x9c + 0x0e);
        uint32_t src = *(const uint32_t *)(s + 0x9c + 0x10), i, n = w * h * d;
        vol_cache_drop(g_vol.state);
        if (!w || !h || !d || src < 0x10000u || src > M64_UADDR_MAX || n > (1u << 24)) return 0;
        g_vol.img = (uint32_t *)malloc((size_t)n * 4u);
        if (!g_vol.img) return 0;
        for (i = 0; i < n; i++) g_vol.img[i] = tex_texel_argb((const unsigned char *)(uintptr_t)src, i, (uint16_t)fmt, (uint16_t)type);
        g_vol.state = g_vol_state; g_vol.w = (int)w; g_vol.h = (int)h; g_vol.d = (int)d; g_vol.has_a = (fmt == 0x1908u || fmt == 0x1906u || fmt == 0x190Au || fmt == 0x8049u);
    }
    ws = *(const uint16_t *)(s + 0x10); wt = *(const uint16_t *)(s + 0x12); wr = *(const uint16_t *)(s + 0x14);
    o->vol = 1; o->vimg = g_vol.img; o->w = g_vol.w; o->h = g_vol.h; o->depth = g_vol.d; o->img = g_vol.img; o->has_a = g_vol.has_a;
    o->clamp_s = (ws == 0x2900u || ws == 0x812fu); o->clamp_t = (wt == 0x2900u || wt == 0x812fu); o->clamp_r = (wr == 0x2900u || wr == 0x812fu);
    o->bil = *(const uint16_t *)(s + 0x18) == 0x2601u;
    o->mode = mach64_gli_u32(ctx, GLI_TEX_ENV_MODE + u) >> 16;
    { uint32_t a = mach64_gli_u32(ctx, 0x6750u + u); o->rgbf = a >> 16; o->alf = a & 0xffffu; }
    { uint32_t a = mach64_gli_u32(ctx, 0x6754u + u), b = mach64_gli_u32(ctx, 0x6758u + u), c = mach64_gli_u32(ctx, 0x675cu + u);
      o->src[0] = a >> 16; o->src[1] = a & 0xffffu; o->src[2] = b >> 16; o->asrc[0] = b & 0xffffu; o->asrc[1] = c >> 16; o->asrc[2] = c & 0xffffu; }
    { uint32_t a = mach64_gli_u32(ctx, 0x6760u + u), b = mach64_gli_u32(ctx, 0x6764u + u), c = mach64_gli_u32(ctx, 0x6768u + u);
      o->op[0] = a >> 16; o->op[1] = a & 0xffffu; o->op[2] = b >> 16; o->aop[0] = b & 0xffffu; o->aop[1] = c >> 16; o->aop[2] = c & 0xffffu; }
    o->rscale = (int)((fl_to_q8(mach64_gli_u32(ctx, 0x676cu + u)) + 128u) >> 8); o->ascale = (int)((fl_to_q8(mach64_gli_u32(ctx, 0x6770u + u)) + 128u) >> 8);
    if (o->rscale < 1) o->rscale = 1; if (o->ascale < 1) o->ascale = 1;
    for (k = 0; k < 4; k++) { uint32_t q = fl_to_q8(mach64_gli_u32(ctx, 0x6738u + u + k * 4u)); o->ec[k] = (int)(q > 255u ? 255u : q); }
    return 1;
}
/* Fill *o for the CPU rasteriser: the bound texture's level 0 as ARGB8888 and the environment.  Returns 0 if the texture cannot be sampled on the CPU. */
int mach64_cpu_env(gld_context *ctx, m64_cpuenv *o)
{
    mach64_priv *p = MACH64_PRIV(ctx); mach64_tex_slot *sl = g_cpu_slot; uint32_t u = g_cpu_unit * GLI_UNIT_STRIDE, k;
    memset(o, 0, sizeof *o);
    if (g_cube_state) return cube_env(ctx, o);
    if (g_vol_state) return vol_env(ctx, o);
    if (!sl || g_cpu_multi || !sl->rsize) return 0;
    if (sl->hwfmt == 11u || sl->hwfmt == 12u) return 0;
    if (!sl->cpu) {
        uint32_t w = sl->lvl_w[0], h = sl->lvl_h[0], n = w * h, bytes = (n * sl->bpt + 3u) & ~3u, done = 0; unsigned char *raw; int rc = 0;
        if (!w || !h || !sl->bpt) return 0;
        raw = (unsigned char *)malloc(bytes); sl->cpu = (uint32_t *)malloc((size_t)n * 4u);
        if (!raw || !sl->cpu) { free(raw); free(sl->cpu); sl->cpu = 0; return 0; }
        mach64_flush_batch(ctx);
        while (done < bytes && !rc) { uint32_t c = bytes - done; if (c > 0x80000u) c = 0x80000u; rc = rgb_readback2(&p->rb, sl->lvl_off[0] + done, raw + done, c, sl->bpt == 4u ? 2u : sl->bpt == 2u ? 1u : 0u); done += c; }
        if (rc) { free(raw); free(sl->cpu); sl->cpu = 0; return 0; }
        cpu_decode(sl, raw, n, sl->cpu); free(raw);
    }
    o->img = sl->cpu; o->w = (int)sl->lvl_w[0]; o->h = (int)sl->lvl_h[0]; o->wsh = (int)ilog2u((uint32_t)o->w); o->clamp_s = (int)sl->clamp_s; o->clamp_t = (int)sl->clamp_t; o->bil = (int)sl->bilinear_mag; o->has_a = (int)sl->has_alpha;
    o->mode = mach64_gli_u32(ctx, GLI_TEX_ENV_MODE + u) >> 16;
    { uint32_t f = mach64_gli_u32(ctx, 0x6750u + u); o->rgbf = f >> 16; o->alf = f & 0xffffu; }
    { uint32_t a = mach64_gli_u32(ctx, 0x6754u + u), b = mach64_gli_u32(ctx, 0x6758u + u), c = mach64_gli_u32(ctx, 0x675cu + u);
      o->src[0] = a >> 16; o->src[1] = a & 0xffffu; o->src[2] = b >> 16; o->asrc[0] = b & 0xffffu; o->asrc[1] = c >> 16; o->asrc[2] = c & 0xffffu; }
    { uint32_t a = mach64_gli_u32(ctx, 0x6760u + u), b = mach64_gli_u32(ctx, 0x6764u + u), c = mach64_gli_u32(ctx, 0x6768u + u);
      o->op[0] = a >> 16; o->op[1] = a & 0xffffu; o->op[2] = b >> 16; o->aop[0] = b & 0xffffu; o->aop[1] = c >> 16; o->aop[2] = c & 0xffffu; }
    o->rscale = (int)((fl_to_q8(mach64_gli_u32(ctx, 0x676cu + u)) + 128u) >> 8); o->ascale = (int)((fl_to_q8(mach64_gli_u32(ctx, 0x6770u + u)) + 128u) >> 8);
    if (o->rscale < 1) o->rscale = 1; if (o->ascale < 1) o->ascale = 1;
    for (k = 0; k < 4; k++) { uint32_t q = fl_to_q8(mach64_gli_u32(ctx, 0x6738u + u + k * 4u)); o->ec[k] = (int)(q > 255u ? 255u : q); }
    return 1;
}

/* glCopyTexSubImage (dispatch slot 0x74; reference glCopyTexSubImage_impl(ctx, texobj, face, level, xoff, yoff, zoff, srcx, srcy, w, h)): copy a rectangle of the colour buffer into a
 * level of the texture.  The rectangle is written into GLEngine's own image of that level (so the texture survives eviction and glGetTexImage) in its stored format, and the texture
 * is queued for re-upload.  GL_RGB / GL_RGBA / GL_LUMINANCE with GL_UNSIGNED_BYTE levels; other formats are left alone. */
int mach64_copy_tex_sub(gld_context *ctx, void *texobj, int face, uint32_t level, uint32_t xoff, uint32_t yoff, int zoff, uint32_t srcx, uint32_t srcy, int w, int h)
{
    mach64_priv *p = MACH64_PRIV(ctx); const unsigned char *t = (const unsigned char *)texobj, *s, *r; uint32_t state, lw, lh, fmt, type, bpp, dst; int H, i, j, fr0, nr; void *cbw;
    (void)face; (void)zoff;
    if (!p || !p->attached || !t || w <= 0 || h <= 0) return 0;
    state = *(const uint32_t *)(t + 0x30);
    if (state < 0x10000u || state > M64_UADDR_MAX) return 0;
    s = (const unsigned char *)(uintptr_t)state; r = s + 0x9c + ((uint32_t)s[0x5e] + level) * 0x14;
    lw = *(const uint16_t *)(r + 0); lh = *(const uint16_t *)(r + 2); fmt = *(const uint16_t *)(r + 0x0c); type = *(const uint16_t *)(r + 0x0e); dst = *(const uint32_t *)(r + 0x10);
    if (!lw || !lh || dst < 0x10000u || dst > M64_UADDR_MAX || type != 0x1401u) return 0;
    bpp = fmt == 0x1907u ? 3u : fmt == 0x1908u ? 4u : fmt == 0x1909u ? 1u : 0u;
    if (!bpp) return 0;
    if (xoff + (uint32_t)w > lw || yoff + (uint32_t)h > lh) return 0;
    H = (int)p->rb.h;
    mach64_flush_batch(ctx);
    fr0 = H - (int)(srcy + (uint32_t)h); nr = h; if (fr0 < 0) { nr += fr0; fr0 = 0; }
    if (fr0 + nr > H) nr = H - fr0;
    if (nr <= 0 || !(cbw = mach64_cb_open(ctx, fr0, nr))) return 0;
    for (j = 0; j < h; j++) for (i = 0; i < w; i++) {
        int fbrow = H - 1 - (int)(srcy + (uint32_t)j); uint16_t *px = mach64_cb_px(cbw, fbrow, (int)(srcx + (uint32_t)i)); unsigned R, G, B; unsigned char *o;
        if (!px) continue;
        mach64_cb_unpack(ctx, *px, &R, &G, &B);
        o = (unsigned char *)(uintptr_t)dst + ((size_t)(yoff + (uint32_t)j) * lw + xoff + (uint32_t)i) * bpp;
        if (bpp == 1u) o[0] = (unsigned char)R; else { o[0] = (unsigned char)R; o[1] = (unsigned char)G; o[2] = (unsigned char)B; if (bpp == 4u) o[3] = 255; }
    }
    mach64_cb_close(ctx, cbw, 0);
    mach64_texture_dirtied(texobj);
    return 0;
}

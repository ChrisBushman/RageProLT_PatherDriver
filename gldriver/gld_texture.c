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
 * full original region instead of leaking the tail. */
typedef struct { uint32_t key, off, w, h, rsize; } mach64_tex_slot;
static mach64_tex_slot g_slots[MACH64_TEX_SLOTS];
static uint32_t        g_heap_next = MACH64_TEX_HEAP_BASE;

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
    if (g_heap_next + size > MACH64_TEX_HEAP_END) return 0;
    { uint32_t off = g_heap_next; g_heap_next += size; return off; }
}

/* Find or allocate a 32-byte-aligned region for `key` sized for w*h ARGB8888
 * texels.  A re-upload reuses its region if the new image still fits; if it
 * grew, the old region is freed back to the heap before a fresh one is taken. */
static mach64_tex_slot *slot_alloc(uint32_t key, uint32_t w, uint32_t h)
{
    uint32_t sz = tex_align(w * h * 4u);
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

/* Pull the base-level image from the texobj state object and DMA it (converted
 * to ARGB8888) into VRAM via a host-data DMA blit.  Returns 1 on success.
 *
 * WHY A BLIT (not a CPU poke): the userspace VRAM aperture (ctx->regs) is NOT
 * offset-aligned with the engine's VRAM offset space (a wide CPU write there
 * panics the box, and pokes to ctx->regs+0x600000 never appear at engine offset
 * 0x600000).  A 2D host-data blit writes through DST_OFF_PITCH in the ENGINE's
 * own offset space -- exactly where TEX_0_OFF then reads -- so it is guaranteed
 * to land.  Sequence + register values are from the reference mach64_dma_blit. */
static int mach64_upload(gld_context *ctx, void *texobj)
{
    mach64_priv *p = MACH64_PRIV(ctx);
    const unsigned char *t = (const unsigned char *)texobj;
    uint32_t state, srcp, w, h, i, n, pitch_units, dst_w, dst_h;
    const unsigned char *s, *rec, *src;
    uint16_t fmt;
    mach64_tex_slot *slot;
    m64_cmdbuf cb;

    if (!t) return 0;
    state = *(const uint32_t *)(t + 0x30);
    if (state < 0x10000 || state >= 0x40000000) return 0;
    s   = (const unsigned char *)(uintptr_t)state;
    rec = s + 0x9c + (uint32_t)s[0x5e] * 0x14;          /* base-level record   */
    w   = *(const uint16_t *)(rec + 0x00);
    h   = *(const uint16_t *)(rec + 0x02);
    fmt = *(const uint16_t *)(rec + 0x0c);
    srcp = *(const uint32_t *)(rec + 0x10);
    if (!w || !h || srcp < 0x10000 || srcp >= 0x40000000) return 0;
    src = (const unsigned char *)(uintptr_t)srcp;
    n = w * h;
    /* Command budget: state (~15 regs) + n host-data dwords + packet headers.
     * scratch is 1024 dwords -> cap the texel count so we never overflow the
     * single-submit staging (larger textures need chunked blits -- TODO). */
    if (n > 900) { gld_log("mach64 texture upload: %ux%u too big for one blit\n", w, h); return 0; }

    /* Allocate this texture's own VRAM region (keyed by its state-obj pointer). */
    slot = slot_alloc(tex_key(texobj), w, h);
    if (!slot) return 0;

    /* mach64 textures are stored LINEARLY at their natural width (2^widthLog2);
     * TEX_SIZE_PITCH carries no separate pitch, so the texture unit reads texel
     * (s,t) at base + (t*w + s)*4.  The DST_OFF_PITCH pitch field is in 8-pixel
     * units (min 1 = 8 px), so a rectangular w x h blit only lands rows at the
     * natural stride when w is a multiple of 8.  For w >= 8 (every power-of-two
     * width >= 8) that holds -- keep the proven rectangular blit.  For w < 8
     * (1/2/4) the 8-px min pitch would over-stride each row; instead blit the
     * whole image as a SINGLE linear row of w*h texels -- height 1 makes the
     * pitch irrelevant and packs the texels contiguously at stride w, exactly the
     * layout the texture unit samples.  (Both forms write the identical bytes for
     * w >= 8, so this only changes the narrow case.) */
    if (w >= 8) { pitch_units = w / 8; dst_w = w;     dst_h = h; }
    else        { pitch_units = 1;     dst_w = w * h; dst_h = 1; }

    /* Host-data DMA blit: stream the real per-texel ARGB8888 data into VRAM at
     * the texture base, through the engine's own offset space (DST_OFF_PITCH).
     * Now that the base offset lands in TEX_[maxLog2]_OFF this is finally
     * testable.  Sequence from the reference mach64_dma_blit. */
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
    m64_reg(&cb, MACH64_DST_OFF_PITCH, (pitch_units << 22) | (slot->off >> 3));
    m64_reg(&cb, MACH64_DST_X_Y,       0);
    m64_reg(&cb, MACH64_DST_WIDTH_HEIGHT, (dst_h << 16) | dst_w);  /* starts blit */
    for (i = 0; i < n; ) {
        uint32_t chunk[16], k = 0;
        while (k < 16 && i < n) {
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

    p->dirty |= MACH64_DIRTY_STATE;      /* new texels -> re-emit tex state       */
    gld_log("mach64 texture upload: %ux%u fmt=0x%x -> VRAM 0x%x (key 0x%x) blit %ux%u\n",
            w, h, fmt, slot->off, slot->key, dst_w, dst_h);
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

/* Called from the draw path.  Uploads any pending image (so the texture is ready
 * in VRAM regardless of enable state), then -- only if the app has GL_TEXTURE_2D
 * enabled -- programs the texture registers into p->hw and enables textured state
 * emission.  Returns 1 if a texture is bound (the primitive should feed S/T). */
int mach64_bind_texture(gld_context *ctx)
{
    mach64_priv *p = MACH64_PRIV(ctx);
    m64_state *st = &p->hw;
    mach64_tex_slot *bound;
    uint32_t wl, hl, tex_on, env, light_fcn, new_scale_3d;

    /* Upload any image-dirtied textures (their state objects are populated by
     * draw time); each lands in its own VRAM slot. */
    while (g_npending) mach64_upload(ctx, g_pending[--g_npending]);

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
        if (st->textured) { st->textured = 0; p->dirty |= MACH64_DIRTY_STATE; }
        return 0;
    }
    /* Map the app's GL_TEXTURE_ENV_MODE -> mach64 TEX_LIGHT_FCN (SCALE_3D_CNTL
     * [23:22]; mesa_mach64_reg.h): REPLACE=0<<22, MODULATE=1<<22, DECAL->
     * ALPHA_DECAL=2<<22.  For our RGB textures the texel alpha is assumed 0xff
     * (TEX_MAP_AEN off), so ALPHA_DECAL evaluates to Ctex -- identical to REPLACE,
     * which is exactly GL_DECAL's defined result for an RGB internal format. */
    env = mach64_gli_u32(ctx, GLI_TEX_ENV_MODE) >> 16;
    switch (env) {
        case GL_ENV_REPLACE: light_fcn = 0u << 22; break;
        case GL_ENV_DECAL:   light_fcn = 2u << 22; break;
        default:             light_fcn = 1u << 22; break;   /* MODULATE + fallback */
    }
    new_scale_3d = 0x1u | (1u << 1) | (1u << 3) | (1u << 4)
                 | (2u << 6) | (1u << 16) | light_fcn | (1u << 24);

    /* Re-emit the texture state when the bound texture OR the env mode changes
     * (not just on the untextured->textured transition) -- otherwise switching
     * texA->texB, or MODULATE->REPLACE, leaves the hardware in the old state. */
    if (!st->textured || st->tex_0_off != bound->off
                      || st->tex_scale_3d != new_scale_3d) {
        p->dirty |= MACH64_DIRTY_STATE;
        gld_log("mach64 tex: env=0x%x light_fcn=%u off=0x%x\n",
                env, light_fcn >> 22, bound->off);
    }

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

    /* TEX_CNTL (ref init): COMP_COMBINE_MODULATE | TEX_CACHE_FLUSH |
     * CACHE_SIZE_4K.  GL_REPEAT sets NO clamp/wrap bits (mach64 default is
     * repeat); nearest filter -> COMP_BLEND/FILTER bits clear. */
    st->tex_cntl       = (1u << 9) | (1u << 23) | (1u << 30);   /* 0x40800200 */

    /* SCALE_3D_CNTL textured: the shade default with TEXTURE_DISABLE(bit6)
     * cleared turns FCN_SHADE(3<<6) into FCN_TEXTURE(2<<6).  Fixed set (ref,
     * cpp==4, nearest, no alpha): EXPAND_DYNAMIC | DITHER_2D_TABLE |
     * DITHER_INIT_RESET | ROUND_EN | FCN_TEXTURE | ALPHA_BLEND_SRC_ONE |
     * MIP_MAP_DISABLE, plus the env-mode-dependent TEX_LIGHT_FCN computed above.
     * (BILINEAR/TEX_BLEND=nearest -> clear; TEX_MAP_AEN clear = RGB no alpha.)
     * MODULATE gives the old 0x0141009b. */
    st->tex_scale_3d   = new_scale_3d;

    st->textured = 1;
    return 1;
}

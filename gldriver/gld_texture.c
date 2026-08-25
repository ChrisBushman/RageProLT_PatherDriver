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

/* Texels live above the depth buffer (Z at 0x400000) in the 8 MB VRAM aperture
 * (ctx->regs = whole framebuffer, CPU-mapped uncached).  2 MB is plenty for the
 * small textures we handle; a real allocator comes with multi-texture support. */
#define MACH64_TEX_VRAM_BASE  0x00600000u

static void *g_pending_tex;    /* texobj GLEngine last image-dirtied (a3&0x80) */

/* gldModifyTexture(a3&0x80) records the image-dirtied texobj here; the next
 * draw's mach64_bind_texture() performs the actual VRAM upload. */
void mach64_texture_dirtied(void *texobj) { g_pending_tex = texobj; }

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
    uint32_t state, srcp, w, h, i, n, pitch_units;
    const unsigned char *s, *rec, *src;
    uint16_t fmt;
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

    /* DST_OFF_PITCH pitch field is in 8-pixel units; at 32bpp a texel is a pixel.
     * 8x8 -> 1 unit.  (w<8 would need a padded pitch -- TODO; test is 8x8.) */
    pitch_units = (w >= 8) ? (w / 8) : 1;

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
    m64_reg(&cb, MACH64_DST_OFF_PITCH, (pitch_units << 22) | (MACH64_TEX_VRAM_BASE >> 3));
    m64_reg(&cb, MACH64_DST_X_Y,       0);
    m64_reg(&cb, MACH64_DST_WIDTH_HEIGHT, (h << 16) | w);          /* starts blit */
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

    p->tex_obj = texobj;
    p->tex_off = MACH64_TEX_VRAM_BASE;
    p->tex_w = w; p->tex_h = h;
    p->dirty |= MACH64_DIRTY_STATE;
    gld_log("mach64 texture upload: %ux%u fmt=0x%x -> VRAM 0x%x\n",
            w, h, fmt, MACH64_TEX_VRAM_BASE);
    return 1;
}

/* GL_TEXTURE_2D enable lives in the gli as bit 3 of the word at +0x6748 (0x08
 * when enabled, 0 when disabled; recovered via tools/mach64_texstate gli-diff). */
#define GLI_TEX2D_ENABLE   0x6748
#define GLI_TEX2D_BIT      0x00000008u

/* Called from the draw path.  Uploads any pending image (so the texture is ready
 * in VRAM regardless of enable state), then -- only if the app has GL_TEXTURE_2D
 * enabled -- programs the texture registers into p->hw and enables textured state
 * emission.  Returns 1 if a texture is bound (the primitive should feed S/T). */
int mach64_bind_texture(gld_context *ctx)
{
    mach64_priv *p = MACH64_PRIV(ctx);
    m64_state *st = &p->hw;
    uint32_t wl, hl, tex_on;

    if (g_pending_tex) { mach64_upload(ctx, g_pending_tex); g_pending_tex = 0; }

    /* Follow the app's glEnable/glDisable(GL_TEXTURE_2D). */
    tex_on = (mach64_gli_u32(ctx, GLI_TEX2D_ENABLE) & GLI_TEX2D_BIT) != 0;

    if (!p->tex_off || !tex_on) {          /* nothing uploaded, or texturing off */
        if (st->textured) { st->textured = 0; p->dirty |= MACH64_DIRTY_STATE; }
        return 0;
    }
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
     * cleared turns FCN_SHADE(3<<6) into FCN_TEXTURE(2<<6).  Full set (ref,
     * cpp==4, nearest, no alpha): EXPAND_DYNAMIC | DITHER_2D_TABLE |
     * DITHER_INIT_RESET | ROUND_EN | FCN_TEXTURE | ALPHA_BLEND_SRC_ONE |
     * TEX_LIGHT_FCN_MODULATE | MIP_MAP_DISABLE.  (BILINEAR/TEX_BLEND=nearest
     * -> clear; TEX_MAP_AEN clear = RGB no alpha.) */
    st->tex_scale_3d   = 0x1u | (1u << 1) | (1u << 3) | (1u << 4)
                       | (2u << 6) | (1u << 16) | (1u << 22) | (1u << 24);
                                                              /* = 0x0141009b */

    if (!st->textured) { st->textured = 1; p->dirty |= MACH64_DIRTY_STATE; }
    return 1;
}

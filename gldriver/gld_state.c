/*
 * gld_state.c - translate GL raster state into the mach64 register shadow.
 *
 * Apple's GLEngine owns the GL state machine and, on gleUpdateState, calls our
 * dispatch hooks (wired by gldUpdateDispatch) when relevant state changes.  Each
 * hook here maps a GL knob to bits in mach64_priv.hw and marks the state dirty;
 * the next draw re-emits the block (see gld_render.c:mach64_draw_triangle).
 *
 * Only the untextured-gouraud subset we PROVED on hardware is wired up; the
 * values come straight from docs/re-notes/3d-triangle.md.  Texture/fog/blend
 * knobs are stubbed with TODOs.
 *
 * Needs the 10.3 headers to build against the real bundle; this is the porting
 * skeleton.  The register bit values live in ../common/mach64_regs.h.
 */
#include "gld.h"
#include "../common/mach64_regs.h"

/* GL depth-func enums (avoid pulling <GL/gl.h> into the skeleton). */
#define GLD_NEVER    0x0200
#define GLD_LESS     0x0201
#define GLD_EQUAL    0x0202
#define GLD_LEQUAL   0x0203
#define GLD_GREATER  0x0204
#define GLD_NOTEQUAL 0x0205
#define GLD_GEQUAL   0x0206
#define GLD_ALWAYS   0x0207

/* Initialize the shadow to the proven opaque untextured gouraud state. */
void mach64_init_state(mach64_priv *p)
{
    m64_state *s = &p->hw;
    s->scale_3d_cntl = MACH64_SCALE_3D_CNTL_SOLID;  /* 0x100c1 shade pipe   */
    s->setup_cntl    = MACH64_SETUP_CNTL_SHADE;     /* 0  (0x40 breaks it)  */
    s->z_cntl        = 0;                           /* depth off            */
    s->dp_pix_width  = 0x60060666;                  /* ARGB8888             */
    s->dst_off_pitch = 0;                           /* set per drawable     */
    s->sc_left_right = 0x1fff0000;                  /* scissor wide open    */
    s->sc_top_bottom = 0x3fff0000;
    s->dp_write_mask = 0xffffffff;
    s->clr_cmp_cntl  = 0;
    s->dp_mix        = MACH64_DP_MIX_SS;            /* 0x00070007           */
    s->dp_src        = MACH64_DP_SRC_3D;            /* gouraud (3D scaler)  */
    s->alpha_tst_cntl = (7u << 4) | (4u << 8);      /* 0x470: test ALWAYS, off */
    p->dirty        |= MACH64_DIRTY_STATE;
}

/* Render target = the current GL drawable's back/front surface in VRAM. */
void mach64_set_render_target(gld_context *ctx, uint32_t off, uint32_t pitch_px)
{
    mach64_priv *p = MACH64_PRIV(ctx);
    p->target_off   = off;
    p->target_pitch = pitch_px;
    p->hw.dst_off_pitch = ((pitch_px / 8) << 22) | (off >> 3);
    p->dirty |= MACH64_DIRTY_STATE;
}

/* glShadeModel(GL_SMOOTH|GL_FLAT).  Smooth = interpolate the 3D scaler's
 * per-vertex colour; flat = solid colour from DP_FRGD_CLR (provoking vertex,
 * which the caller must also load into DP_FRGD_CLR). */
void mach64_set_shade_model(gld_context *ctx, int smooth)
{
    mach64_priv *p = MACH64_PRIV(ctx);
    p->hw.dp_src     = smooth ? MACH64_DP_SRC_3D : MACH64_DP_SRC_FLAT;
    p->hw.setup_cntl = MACH64_SETUP_CNTL_SHADE;   /* 0 for both (see notes) */
    p->dirty |= MACH64_DIRTY_STATE;
}

/* Bind the depth buffer.  off = byte offset in VRAM, pitch_px = width in
 * 16-bit Z pixels.  Z_OFF_PITCH uses the same encoding as DST_OFF_PITCH. */
void mach64_set_zbuffer(gld_context *ctx, uint32_t off, uint32_t pitch_px)
{
    mach64_priv *p = MACH64_PRIV(ctx);
    p->z_off   = off;
    p->z_pitch = pitch_px;
    p->hw.z_off_pitch = ((pitch_px / 8) << 22) | (off >> 3);
    p->dirty |= MACH64_DIRTY_STATE;
}

/* glEnable/glDisable(GL_DEPTH_TEST) + glDepthFunc.  Z_EN is bit 0; the test
 * function is bits 1-3 (Z_TEST_*).  Needs a bound depth buffer
 * (mach64_set_zbuffer) and depth-carrying vertices (pack_vertex now packs z). */
void mach64_set_depth_test(gld_context *ctx, int enable, uint32_t gl_func)
{
    mach64_priv *p = MACH64_PRIV(ctx);
    uint32_t z = 0;
    if (enable) {
        z = MACH64_Z_EN | MACH64_Z_MASK_EN;   /* test + write the Z buffer */
        switch (gl_func) {
        case GLD_LESS:    z |= MACH64_Z_TEST_LESS;     break;
        case GLD_LEQUAL:  z |= MACH64_Z_TEST_LEQUAL;   break;
        case GLD_ALWAYS:  z |= MACH64_Z_TEST_ALWAYS;   break;
        case GLD_EQUAL:   z |= MACH64_Z_TEST_EQUAL;    break;
        case GLD_GEQUAL:  z |= MACH64_Z_TEST_GEQUAL;   break;
        case GLD_GREATER: z |= MACH64_Z_TEST_GREATER;  break;
        case GLD_NOTEQUAL:z |= MACH64_Z_TEST_NOTEQUAL; break;
        case GLD_NEVER:   z |= MACH64_Z_TEST_NEVER;    break;
        default:          z |= MACH64_Z_TEST_LESS;     break;
        }
    }
    p->hw.z_cntl = z;
    p->dirty |= MACH64_DIRTY_STATE;
}

/* glScissor: SC_LEFT_RIGHT = (right<<16)|left, SC_TOP_BOTTOM = (bottom<<16)|top.
 * GL gives (x,y,w,h) with y up from bottom-left; the caller converts to the
 * drawable's top-left pixel box first (drawable height needed -- TODO). */
void mach64_set_scissor(gld_context *ctx, int x, int y, int w, int h)
{
    mach64_priv *p = MACH64_PRIV(ctx);
    int left = x, right = x + w, top = y, bottom = y + h;
    if (left   < 0) left = 0;
    if (top    < 0) top  = 0;
    p->hw.sc_left_right = ((uint32_t)(right  & 0x1fff) << 16) | (uint32_t)(left & 0x1fff);
    p->hw.sc_top_bottom = ((uint32_t)(bottom & 0x1fff) << 16) | (uint32_t)(top  & 0x1fff);
    p->dirty |= MACH64_DIRTY_STATE;
}

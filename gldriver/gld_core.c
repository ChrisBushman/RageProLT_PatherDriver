/*
 * gld_core.c - the bundle's draw path on the standalone kernel client (design section 4).
 *
 * What changed from the Apple-kext bundle: there is no Apple surface, no kernel peek, no worker thread and no mapped register window.
 * The colour target and Z buffer are kernel-allocated VRAM blocks; every draw is a validated batch (rgb_flush); the frame is presented
 * by a kernel blit from the target to the scanout (rgb_present).  The kernel serialises: a batch returns without waiting, the next
 * kernel call waits for it.  All numeric work on vertices is integer (rage_vmath.h) -- single-float arithmetic only where noted.
 */
#include "gld.h"

#define GLI_CLEAR_COLOR 0x6320
extern uint32_t mach64_batch_gen;
extern void mach64_tex_invalidate_all(void);
extern void mach64_tex_ctx_gone(rgb_t *rb);

/* ---- geometry -------------------------------------------------------- */
/* post-T&L float vertex -> hardware m64_vertex.  X/Y in 2 sub-pixel bits (exact integer conversion), colour/depth as in the old driver.
 * (Bit-exact against the float casts: tests/vmath_test.c.) */
void mach64_pack_vertex(m64_vertex *out, float x, float y, float z, float w, uint32_t argb)
{
    int32_t xi = rage_f2q(gld_bits(x)), yi = rage_f2q(gld_bits(y));
    int32_t zi = rage_f2q(gld_bits((z * 65535.0f + 0.5f) * 0.25f));      /* (int)(z*65535+0.5) via the exact x4 conversion */
    uint32_t wb = (w != 0.0f) ? gld_bits(w) : gld_bits(1.0f);
    if (zi < 0) zi = 0; else if (zi > 0xffff) zi = 0xffff;
    out->x_y = (int32_t)((((uint32_t)xi & 0xffffu) << 16) | ((uint32_t)yi & 0xffffu));
    out->z   = (int32_t)((uint32_t)zi << 15);                            /* 17.15: 16-bit depth in [30:15] */
    out->w   = (int32_t)wb;
    out->s = out->t = 0;
    out->s2 = out->t2 = out->w2 = 0;
    out->spec_argb = 0;
    out->argb = argb;
}

/* ---- the gli (GLEngine's per-context state block) --------------------
 * ctx+0x10 points into it; the block is found by the back-pointer at +0x7898 (user space only, bounded). */
static gld_context *g_gli_ctx; static uint32_t g_gli_p10, g_gli_g;
void mach64_gli_forget(gld_context *ctx) { if (g_gli_ctx == ctx) { g_gli_ctx = 0; g_gli_g = 0; } }
static uint32_t gli_find(gld_context *ctx)
{
    uint32_t p10 = *(const uint32_t *)((const char *)ctx + 0x10), off;
    if (g_gli_ctx == ctx && g_gli_p10 == p10 && g_gli_g && *(const uint32_t *)(uintptr_t)(g_gli_g + 0x7898) == (uint32_t)(uintptr_t)ctx) return g_gli_g;
    for (off = 0; off <= 0x8000; off += 4) {
        uint32_t g = p10 - off;
        if (g < 0x10000 || g > M64_UADDR_MAX) break;
        if (*(const uint32_t *)(uintptr_t)(g + 0x7898) == (uint32_t)(uintptr_t)ctx) { g_gli_ctx = ctx; g_gli_p10 = p10; g_gli_g = g; return g; }
    }
    return 0;
}
uint32_t mach64_gli_u32(gld_context *ctx, uint32_t off)
{
    uint32_t g = gli_find(ctx);
    return g ? *(const uint32_t *)(uintptr_t)(g + off) : 0;
}

/* GL depth state -> Z_CNTL.  gli+0x6340 bit24 = GL_DEPTH_TEST, +0x633c >> 16 = glDepthFunc, +0x63b8 bit24 = glDepthMask. */
void mach64_sync_depth_from_gli(gld_context *ctx)
{
    mach64_priv *p = MACH64_PRIV(ctx);
    uint32_t gli = gli_find(ctx), z = 0;
    if (!gli) return;
    if ((*(const uint32_t *)(uintptr_t)(gli + 0x6340)) & 0x01000000u) {
        uint32_t func = (*(const uint32_t *)(uintptr_t)(gli + 0x633c)) >> 16;
        uint32_t mask = (*(const uint32_t *)(uintptr_t)(gli + 0x63b8)) & 0x01000000u;
        z = MACH64_Z_EN;
        switch (func) {
        case 0x0200: z |= MACH64_Z_TEST_NEVER;    break;
        case 0x0201: z |= MACH64_Z_TEST_LESS;     break;
        case 0x0202: z |= MACH64_Z_TEST_EQUAL;    break;
        case 0x0203: z |= MACH64_Z_TEST_LEQUAL;   break;
        case 0x0204: z |= MACH64_Z_TEST_GREATER;  break;
        case 0x0205: z |= MACH64_Z_TEST_NOTEQUAL; break;
        case 0x0206: z |= MACH64_Z_TEST_GEQUAL;   break;
        case 0x0207: z |= MACH64_Z_TEST_ALWAYS;   break;
        default:     z |= MACH64_Z_TEST_LESS;     break;
        }
        if (mask) z |= MACH64_Z_MASK_EN;
    }
    if (p->hw.z_cntl != z) { p->hw.z_cntl = z; p->dirty |= MACH64_DIRTY_STATE; }
}

/* ---- state ----------------------------------------------------------- */
void mach64_init_state(mach64_priv *p)
{
    m64_state *s = &p->hw;
    memset(s, 0, sizeof *s);
    s->scale_3d_cntl = MACH64_SCALE_3D_CNTL_SOLID;
    s->setup_cntl    = MACH64_SETUP_CNTL_SHADE;
    s->z_cntl        = 0;
    s->dp_pix_width  = 0x60040464u;                 /* RGB565 (the kernel owns the real value; kept for the shadow) */
    s->sc_left_right = 0x1fff0000;
    s->sc_top_bottom = 0x3fff0000;
    s->dp_write_mask = 0xffffffffu;
    s->clr_cmp_cntl  = 0;
    s->dp_mix        = MACH64_DP_MIX_SS;
    s->dp_src        = MACH64_DP_SRC_3D;
    s->alpha_tst_cntl = (7u << 4) | (4u << 8);
    p->cbpp = 2;
    p->dirty |= MACH64_DIRTY_STATE;
}

/* ---- surface --------------------------------------------------------- */
/* (Re)build the kernel render target (+ Z) for the drawable size.  Called at attach and again after a display mode change. */
int mach64_build_surface(gld_context *ctx)
{
    mach64_priv *p = MACH64_PRIV(ctx);
    int rc;
    if (!p->fb_w || !p->fb_h) return -1;
    rc = rgb_surface(&p->rb, p->fb_w, p->fb_h, 2, 1);
    if (rc) { p->attached = 0; return rc; }
    p->rb.gen = rgb_gen(&p->rb);
    p->mode_gen = p->rb.gen;
    p->attached = 1;
    p->frame_new = 1;
    p->dirty |= MACH64_DIRTY_STATE;
    return 0;
}

/* 1 = fine to draw; 0 = a mode change is in progress or the surface could not be rebuilt: draw/present nothing. */
int mach64_mode_check(gld_context *ctx)
{
    mach64_priv *p = MACH64_PRIV(ctx);
    uint32_t g;
    if (!p || !p->attached) return 0;
    g = rgb_gen(&p->rb);
    if (g & 1u) return 0;
    if (g != p->mode_gen) {
        p->batch.n = 0;                         /* anything queued targeted the old surface */
        p->rb.col_off = p->rb.z_off = 0;        /* the kernel dropped them with the old mode */
        mach64_tex_invalidate_all();
        if (mach64_build_surface(ctx)) return 0;
    }
    return 1;
}

/* ---- batching -------------------------------------------------------- */
void mach64_flush_batch(gld_context *ctx)
{
    mach64_priv *p = MACH64_PRIV(ctx);
    if (!p || !p->batch.buf || p->batch.n == 0) return;
    mach64_batch_gen++;
    rgb_flush(&p->rb, &p->batch);
    m64_cb_init(&p->batch, p->batchbuf, GLD_BATCH_DWORDS);
    p->dirty |= MACH64_DIRTY_STATE;          /* the next batch re-emits the render state */
}

static void colour_clear(gld_context *ctx)
{
    mach64_priv *p = MACH64_PRIV(ctx);
    uint32_t gli = gli_find(ctx), c565 = 0;
    if (gli) {
        uint32_t r = rage_f2byte(mach64_gli_u32(ctx, GLI_CLEAR_COLOR + 0));
        uint32_t g = rage_f2byte(mach64_gli_u32(ctx, GLI_CLEAR_COLOR + 4));
        uint32_t b = rage_f2byte(mach64_gli_u32(ctx, GLI_CLEAR_COLOR + 8));
        c565 = ((r >> 3) << 11) | ((g >> 2) << 5) | (b >> 3);
    }
    mach64_flush_batch(ctx);
    rgb_fill(&p->rb, 0, 0, (int)p->fb_w, (int)p->fb_h, c565);
    p->dirty |= MACH64_DIRTY_STATE;                /* the kernel fill reprogrammed the engine for 2D */
}

void mach64_clear_color(gld_context *ctx) { colour_clear(ctx); }
void mach64_clear_zbuffer(gld_context *ctx)
{
    mach64_priv *p = MACH64_PRIV(ctx);
    if (!p->rb.z_off) return;
    mach64_flush_batch(ctx);
    rgb_clear_z(&p->rb, 0xffffu);
    p->dirty |= MACH64_DIRTY_STATE;
}

/* The deferred per-frame clear (armed by frame_new; GLEngine dispatches no clear entry point for our surface).  Colour on the frame's first draw;
 * Z on the first draw that actually uses depth (a frame's first draws are often depth-off). */
void mach64_frame_start(gld_context *ctx)
{
    mach64_priv *p = MACH64_PRIV(ctx);
    if (p->frame_new) {
        if (!mach64_mode_check(ctx)) return;
        colour_clear(ctx);
        p->z_clear_pending = 1;
        p->frame_new = 0;
    }
    if (p->z_clear_pending && p->rb.z_off && (p->hw.z_cntl & MACH64_Z_EN)) {
        mach64_flush_batch(ctx);
        rgb_clear_z(&p->rb, 0xffffu);
        p->dirty |= MACH64_DIRTY_STATE;
        p->z_clear_pending = 0;
    }
}

int mach64_draw_triangle(gld_context *ctx, const m64_vertex *v0, const m64_vertex *v1, const m64_vertex *v2)
{
    mach64_priv *p = MACH64_PRIV(ctx);
    size_t need;
    if (!p->attached) return 0;
    mach64_frame_start(ctx);
    if (!p->attached) return 0;
    if (!p->batch.buf) m64_cb_init(&p->batch, p->batchbuf, GLD_BATCH_DWORDS);
    need = ((p->dirty & MACH64_DIRTY_STATE) ? 96u : 0u) + 64u;
    if (p->batch.n + need > GLD_BATCH_LIMIT) mach64_flush_batch(ctx);        /* a full buffer is a normal mid-frame flush now (no present semantics) */
    if (p->dirty & MACH64_DIRTY_STATE) { m64_emit_state(&p->batch, &p->hw); p->dirty &= ~MACH64_DIRTY_STATE; }
    p->batch.multitex = p->hw.textured && p->hw.multitex;
    /* Normalise the perspective W so the triangle's largest is 1.0: S,T are raw and the engine computes interp(S*W)/interp(W), so a common scale leaves
     * the result unchanged, but a tiny W (a far-plane 2D background: W = 1/3000) underflows the setup engine's fixed-point range -> black texture. */
    if (p->hw.textured) {
        float wa = gld_flt((uint32_t)v0->w), wb = gld_flt((uint32_t)v1->w), wc = gld_flt((uint32_t)v2->w), mx = wa > wb ? wa : wb;
        if (wc > mx) mx = wc;
        if (mx > 0.0f && mx < 0.5f) {
            m64_vertex a = *v0, b = *v1, c = *v2;
            float k = 1.0f / mx;
            a.w = (int32_t)gld_bits(wa * k); b.w = (int32_t)gld_bits(wb * k); c.w = (int32_t)gld_bits(wc * k);
            a.w2 = a.w; b.w2 = b.w; c.w2 = c.w;
            if (!m64_emit_triangle(&p->batch, &a, &b, &c)) return 0;
            p->triangles++;
            return 1;
        }
    }
    if (!m64_emit_triangle(&p->batch, v0, v1, v2)) return 0;
    p->triangles++;
    return 1;
}

/* ---- present --------------------------------------------------------- */
/* Everything drawn so far -> the scanout: a kernel blit (clipped to the display) from the target at (0,0) to the window origin. */
void mach64_present(gld_context *ctx)
{
    mach64_priv *p = MACH64_PRIV(ctx);
    if (!p || !p->attached) return;
    mach64_flush_batch(ctx);
    if (!mach64_mode_check(ctx)) return;
    if (p->win_mode && p->cgs_win) {
        /* a window: follow it (it may have moved) and copy only the part no window in front covers */
        int wx = 0, wy = 0, ww = 0, wh = 0, n, i; short vr[16][4];
        if (mach64_cgs_resolve_bounds(p->cgs_win, p->cgs_surf, 0, &wx, &wy, &ww, &wh) && ww == (int)p->fb_w && wh == (int)p->fb_h) {
            p->win_x = (uint32_t)wx; p->win_y = (uint32_t)wy;
        }
        n = mach64_cgs_visible_rects(p->cgs_win, p->cgs_surf, (int)p->win_x, (int)p->win_y, (int)p->fb_w, (int)p->fb_h, vr, 16);
        if (n == 0) return;                                           /* completely covered */
        if (n > 0) {
            for (i = 0; i < n; i++)
                rgb_present(&p->rb, vr[i][0] - (int)p->win_x, vr[i][1] - (int)p->win_y, vr[i][2], vr[i][3], vr[i][0], vr[i][1]);
            p->dirty |= MACH64_DIRTY_STATE;
            return;
        }
    }
    rgb_present(&p->rb, 0, 0, (int)p->fb_w, (int)p->fb_h, (int)p->win_x, (int)p->win_y);
    p->dirty |= MACH64_DIRTY_STATE;                /* the kernel blit reprogrammed the engine for 2D */
}

int gldFlush(gld_context *ctx)
{
    mach64_priv *p = MACH64_PRIV(ctx);
    if (!p) return 0;
    mach64_flush_batch(ctx);
    if (!p->dbuf && p->attached) mach64_present(ctx);                    /* single-buffered: the scanout copy is the only visible result */
    return 0;
}

int gldFinish(gld_context *ctx)
{
    mach64_priv *p = MACH64_PRIV(ctx);
    if (!p) return 0;
    mach64_flush_batch(ctx);
    if (p->attached) {
        rgb_fence(&p->rb);
        mach64_present(ctx);
        rgb_fence(&p->rb);
    }
    p->frame_new = 1;
    return 0;
}

void *gldAllocVertexBuffer(gld_context *ctx, uint32_t size) { (void)ctx; (void)size; return 0; }
int gldCompleteVertexBuffer(gld_context *ctx, void *vb) { (void)ctx; (void)vb; return 0; }
void gldFreeVertexBuffer(gld_context *ctx, void *vb) { (void)ctx; (void)vb; }

/* ---- control slots ---------------------------------------------------- */
int mach64_ctl00(gld_context *c, uint32_t a, uint32_t b, uint32_t d, uint32_t e, uint32_t f, uint32_t g)
{ (void)c; (void)a; (void)b; (void)d; (void)e; (void)f; (void)g; return 0; }

/* table[0x04] = the STATE flush: sync the app's depth state into Z_CNTL before the batch renders. */
int mach64_flush(gld_context *ctx, uint32_t a2, uint32_t a3, uint32_t a4, uint32_t a5, uint32_t a6, uint32_t a7)
{
    (void)a2; (void)a3; (void)a4; (void)a5; (void)a6; (void)a7;
    mach64_sync_depth_from_gli(ctx);
    return 0;
}

void mach64_dispatch_complete(gld_context *ctx, const void *v, uint32_t n) { (void)ctx; (void)v; (void)n; }

/* Dispatch slot 0x60 = GLEngine's once-per-frame render-control / buffer-swap hook: flush, present, arm the next frame's clears. */
void mach64_dispatch_render_ctl(gld_context *ctx, uint32_t a4, uint32_t a5, uint32_t a6, uint32_t a7, uint32_t a8)
{
    mach64_priv *p = MACH64_PRIV(ctx);
    (void)a4; (void)a5; (void)a6; (void)a7; (void)a8;
    if (!p || !p->attached) return;
    mach64_frame_start(ctx);                    /* a clear-only frame still presents its clear colour */
    mach64_present(ctx);
    p->frame_new = 1;
}

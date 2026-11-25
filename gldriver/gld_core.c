/*
 * gld_core.c - the bundle's draw path on the standalone kernel client (design section 4).
 *
 * What changed from the Apple-kext bundle: there is no Apple surface, no kernel peek, no worker thread and no mapped register window.
 * The colour target and Z buffer are kernel-allocated VRAM blocks; every draw is a validated batch (rgb_flush); the frame is presented
 * by a kernel blit from the target to the scanout (rgb_present).  The kernel serialises: a batch returns without waiting, the next
 * kernel call waits for it.  All numeric work on vertices is integer (rage_vmath.h) -- single-float arithmetic only where noted.
 */
#include <sys/time.h>
#include <unistd.h>
#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
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

float mach64_zforce = -1.0f;

/* GL depth + stencil state -> Z_CNTL and the stencil/depth emulation (gld_stencil.c).  gli+0x6340 bit24 = GL_DEPTH_TEST, +0x633c >> 16 = glDepthFunc,
 * +0x63b8 bit24 = glDepthMask; the stencil words are documented in gld_stencil.c. */
void mach64_sync_depth_from_gli(gld_context *ctx)
{
    uint32_t gli = gli_find(ctx);
    if (!gli) return;
    mach64_zs_sync(ctx, gli);
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
    mach64_zs_reset(p);
    p->frame_new = 1;
    p->init_clear = 1;
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
    mach64_zs_clear_depth(ctx);                       /* lazy unless the chip's Z currently holds the depth plane */
}

/* The deferred per-frame clear (armed by frame_new; GLEngine dispatches no clear entry point for our surface).  Colour on the frame's first draw;
 * Z on the first draw that actually uses depth (a frame's first draws are often depth-off). */
void mach64_frame_start(gld_context *ctx)
{
    mach64_priv *p = MACH64_PRIV(ctx);
    if (p->k2d_touched) { p->k2d_touched = 0; p->dirty |= MACH64_DIRTY_STATE; }       /* the idle-repaint thread blitted in between */
    if (p->frame_new) {
        if (!mach64_mode_check(ctx)) return;
        /* glClear reaches the driver through slot 0x04 (mach64_flush, a2 = the GL_*_BUFFER_BIT mask, found in GLEngine's glClear at 0x4530 which
         * tail-calls ctx[0x78ac]); an app that never clears (Flurry fades the last frame with a quad) keeps its colour buffer, so the frame start
         * only clears a new, undefined surface. */
        if (p->init_clear) { colour_clear(ctx); mach64_zs_clear_stencil(ctx, mach64_gli_u32(ctx, 0x6334)); }
        p->init_clear = 0;
        p->z_clear_pending = 1;
        p->frame_new = 0;
    }
    if (p->z_clear_pending && p->rb.z_off) {
        p->z_clear_pending = 0;
        mach64_zs_clear_depth(ctx);
    }
}

int mach64_draw_triangle(gld_context *ctx, const m64_vertex *v0, const m64_vertex *v1, const m64_vertex *v2)
{
    mach64_priv *p = MACH64_PRIV(ctx);
    if (!p->attached) return 0;
    mach64_frame_start(ctx);
    if (!p->attached) return 0;
    mach64_zs_prepare(ctx);                          /* put the stencil/depth planes where this draw needs them */
    if (p->zs_mode == 2) return mach64_zs_cpu_draw(ctx, v0, v1, v2);
    mach64_zs_note_hw(ctx);
    return mach64_hw_tri(ctx, v0, v1, v2);
}

/* One triangle through the hardware (the batch, the sampler-change split, the state block). */
int mach64_hw_tri(gld_context *ctx, const m64_vertex *v0, const m64_vertex *v1, const m64_vertex *v2)
{
    mach64_priv *p = MACH64_PRIV(ctx);
    size_t need;
    if (!p->batch.buf) m64_cb_init(&p->batch, p->batchbuf, GLD_BATCH_DWORDS);
    need = ((p->dirty & MACH64_DIRTY_STATE) ? 96u : 0u) + 64u;
    if (p->batch.n + need > GLD_BATCH_LIMIT) mach64_flush_batch(ctx);        /* a full buffer is a normal mid-frame flush now (no present semantics) */
    if ((p->dirty & MACH64_DIRTY_STATE) && p->hw.textured) {
        /* The engine reads the texture registers while the previous triangles are still being drawn: a texture offset/size/format/filter change inside a
         * batch makes them sample the NEW texture (Chess board: every dark square drawn light).  Start a new submit whenever the sampler setup changes. */
        uint32_t sig[9]; const m64_state *s = &p->hw; int i;
        sig[0] = s->tex_0_off; sig[1] = s->tex_size_pitch; sig[2] = s->tex_fmt; sig[3] = s->tex_cntl; sig[4] = s->tex_scale_3d; sig[5] = s->tex2_off; sig[6] = s->tex2_size_pitch;
        sig[7] = 0; for (i = 0; i < 11; i++) sig[7] = sig[7] * 31u + s->tex_mip_off[i];
        sig[8] = (uint32_t)s->tex_mip;
        if (p->batch.n && p->tsig_valid && __builtin_memcmp(sig, p->tsig, sizeof sig)) mach64_flush_batch(ctx);
        __builtin_memcpy(p->tsig, sig, sizeof sig); p->tsig_valid = 1;
    }
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
static const char *g_ptrace_fn = (const char *)1;
static void ptrace_log(const mach64_priv *p, int n, short (*vr)[4])
{
    FILE *f = fopen(g_ptrace_fn, "a"); struct timeval tv; int i;
    if (!f) return;
    gettimeofday(&tv, 0);
    fprintf(f, "%ld.%06d pid=%d win=%u/%u at %u,%u %ux%u rects=%d", (long)tv.tv_sec, (int)tv.tv_usec, (int)getpid(), p->cgs_win, p->cgs_surf, p->win_x, p->win_y, p->fb_w, p->fb_h, n);
    for (i = 0; i < n && i < 4; i++) fprintf(f, " [%d,%d %dx%d]", vr[i][0], vr[i][1], vr[i][2], vr[i][3]);
    fprintf(f, "\n"); fclose(f);
}
/* Blit the target to the scanout: only the part of a window that no window in front covers (CGS z-order), at the window's current position.
 * Returns 1 if the kernel blit reprogrammed the engine. */
static int present_rects(mach64_priv *p)
{
    if (g_ptrace_fn == (const char *)1) g_ptrace_fn = getenv("RAGE_PTRACE");        /* debug: RAGE_PTRACE=<file> logs every present (checked once) */
    if (p->win_mode && p->cgs_win) {
        int wx = 0, wy = 0, ww = 0, wh = 0, n, i; short vr[16][4];
        if (mach64_cgs_resolve_bounds(p->cgs_win, p->cgs_surf, 0, &wx, &wy, &ww, &wh) && ww == (int)p->fb_w && wh == (int)p->fb_h) {
            p->win_x = (uint32_t)wx; p->win_y = (uint32_t)wy;
        }
        n = mach64_cgs_visible_rects(p->cgs_win, p->cgs_surf, (int)p->win_x, (int)p->win_y, (int)p->fb_w, (int)p->fb_h, vr, 16);
        if (g_ptrace_fn) ptrace_log(p, n, vr);
        if (n == 0) return 0;                                         /* completely covered */
        if (n > 0) {
            for (i = 0; i < n; i++)
                rgb_present(&p->rb, vr[i][0] - (int)p->win_x, vr[i][1] - (int)p->win_y, vr[i][2], vr[i][3], vr[i][0], vr[i][1]);
            return 1;
        }
    }
    rgb_present(&p->rb, 0, 0, (int)p->fb_w, (int)p->fb_h, (int)p->win_x, (int)p->win_y);
    return 1;
}

void mach64_present(gld_context *ctx)
{
    mach64_priv *p = MACH64_PRIV(ctx);
    if (!p || !p->attached) return;
    mach64_flush_batch(ctx);
    if (!mach64_mode_check(ctx)) return;
    if (present_rects(p)) p->dirty |= MACH64_DIRTY_STATE;                /* the kernel blit reprogrammed the engine for 2D */
    p->presents++;
}

/* The idle-repaint thread (see mach64_priv).  Every 100 ms: if the context did nothing for three ticks, copy the target again. */
static void *repaint_thread(void *arg)
{
    mach64_priv *p = (mach64_priv *)arg; uint32_t last = 0xffffffffu; int idle = 0;
    while (p->rp_run) {
        uint32_t act;
        usleep(100000);
        if (!p->rp_run) break;
        act = p->triangles + p->presents;
        if (act != last) { last = act; idle = 0; continue; }
        if (++idle < 3 || !p->attached || !p->win_mode) continue;
        idle = 0;
        if (present_rects(p)) p->k2d_touched = 1;
    }
    return 0;
}
void mach64_repaint_start(mach64_priv *p)
{
    pthread_t t;
    if (p->rp_started || !p->win_mode) return;
    p->rp_run = 1;
    if (pthread_create(&t, 0, repaint_thread, p) == 0) { p->rp_thread = (unsigned long)t; p->rp_started = 1; } else p->rp_run = 0;
}
void mach64_repaint_stop(mach64_priv *p)
{
    if (!p->rp_started) return;
    p->rp_run = 0;
    pthread_join((pthread_t)p->rp_thread, 0);
    p->rp_started = 0;
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
    mach64_priv *p = MACH64_PRIV(ctx);
    (void)a3; (void)a4; (void)a5; (void)a6; (void)a7;
    mach64_sync_depth_from_gli(ctx);
    if ((a2 & 0x4500u) && p && p->attached) {
        mach64_frame_start(ctx);                     /* arm the frame (mode check, undefined-surface clear) before the app's own clear */
        if (!p->attached) return 0;
        if (a2 & 0x4000u) colour_clear(ctx);
        if (a2 & 0x0100u) { p->z_clear_pending = 0; if (p->rb.z_off) mach64_zs_clear_depth(ctx); else p->z_clear_pending = 1; }
        if (a2 & 0x0400u) mach64_zs_clear_stencil(ctx, mach64_gli_u32(ctx, 0x6334));
    }
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

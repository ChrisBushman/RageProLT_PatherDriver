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
    out->s2 = out->t2 = out->w2 = 0; out->r = 0;
    out->spec_argb = 0;
    out->argb = argb;
}

/* ---- the gli (GLEngine's per-context state block) --------------------
 * ctx+0x10 points into it; the block is found by the back-pointer at +0x7898 (user space only, bounded). */
gld_context *g_gli_ctx; uint32_t g_gli_p10, g_gli_g;
void mach64_gli_forget(gld_context *ctx) { if (g_gli_ctx == ctx) { g_gli_ctx = 0; g_gli_g = 0; } }
static uint32_t gli_find_slow(gld_context *ctx);
static inline uint32_t gli_find(gld_context *ctx)
{
    uint32_t p10 = *(const uint32_t *)((const char *)ctx + 0x10);
    if (g_gli_ctx == ctx && g_gli_p10 == p10 && g_gli_g && *(const uint32_t *)(uintptr_t)(g_gli_g + 0x7898) == (uint32_t)(uintptr_t)ctx) return g_gli_g;       /* the hit: one compare chain, inlined */
    return gli_find_slow(ctx);
}
static uint32_t gli_find_slow(gld_context *ctx)
{
    uint32_t p10 = *(const uint32_t *)((const char *)ctx + 0x10), off;
    for (off = 0; off <= 0x8000; off += 4) {
        uint32_t g = p10 - off;
        if (g < 0x10000 || g > M64_UADDR_MAX) break;
        if (*(const uint32_t *)(uintptr_t)(g + 0x7898) == (uint32_t)(uintptr_t)ctx) { g_gli_ctx = ctx; g_gli_p10 = p10; g_gli_g = g; return g; }
    }
    return 0;
}
uint32_t mach64_gli_u32_slow(gld_context *ctx, uint32_t off)
{
    uint32_t g = gli_find(ctx);
    return g ? *(const uint32_t *)(uintptr_t)(g + off) : 0;
}

static void glidiff(gld_context *ctx, const char *tag)
{
    static uint32_t snap[0x2000]; static int have; static const char *fn; static int fn_set; uint32_t g, o, n = 0; FILE *f;
    if (!fn_set) { fn = getenv("RAGE_GLIDIFF"); fn_set = 1; }         /* read once: this runs on every draw */
    if (!fn) return;
    g = gli_find(ctx); if (!g) return;
    f = fopen(fn, "a"); if (!f) return;
    for (o = 0x300; o < 0x7e00; o += 4) { uint32_t v = *(const uint32_t *)(uintptr_t)(g + o), i = o / 4 - 0xc0; if (i >= sizeof snap / 4) continue;
        if (have && snap[i] != v) { if (!n++) fprintf(f, "-- %s\n", tag); fprintf(f, "  %04x: %08x -> %08x\n", o, snap[i], v); } snap[i] = v; }
    have = 1; fclose(f);
}
float mach64_zbias = 0.0f;
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
/* Windowed contexts of this process.  VRAM is small (about 4.4 MB free at 1024x768x32): an app that creates a second GL view while an earlier one's window is not on
 * screen (Chess: a 740x680 view that is never shown, then the real 630x418 one) would fail with kCGLBadAlloc.  When a surface cannot be built, the surfaces of the other
 * contexts whose windows are not on screen are freed first; each is rebuilt on its next draw/present (mach64_mode_check). */
#define SURF_REG_MAX 32
static mach64_priv *g_surf_reg[SURF_REG_MAX];
static void surf_register(mach64_priv *p)
{
    int i;
    for (i = 0; i < SURF_REG_MAX; i++) if (g_surf_reg[i] == p) return;
    for (i = 0; i < SURF_REG_MAX; i++) if (!g_surf_reg[i]) { g_surf_reg[i] = p; return; }
}
void mach64_surf_unregister(mach64_priv *p)
{
    int i;
    for (i = 0; i < SURF_REG_MAX; i++) if (g_surf_reg[i] == p) g_surf_reg[i] = 0;
}
static int evict_offscreen(mach64_priv *self)
{
    int i, n = 0;
    for (i = 0; i < SURF_REG_MAX; i++) {
        mach64_priv *q = g_surf_reg[i]; short vr[16][4];
        if (!q || q == self || !q->attached || !q->rb.col_off || !q->win_mode || !q->cgs_win) continue;
        if (mach64_cgs_visible_rects(q->cgs_win, q->cgs_surf, (int)q->win_x, (int)q->win_y, (int)q->fb_w, (int)q->fb_h, vr, 16) != -2) continue;
        q->batch.n = 0;                                   /* queued draws targeted the surface that is going away */
        rgb_fence(&q->rb);
        rgb_release(&q->rb);
        q->surf_evicted = 1;
        n++;
    }
    return n;
}

/* (Re)build the kernel render target (+ Z) for the drawable size.  Called at attach and again after a display mode change. */
int mach64_build_surface(gld_context *ctx)
{
    mach64_priv *p = MACH64_PRIV(ctx);
    int rc;
    if (!p->fb_w || !p->fb_h) return -1;
    {   /* the colour target has the scanout's pixel size (the kernel's present is a raw copy): 32 bpp when the scanout is 32 bpp and the VRAM budget allows, else 16 bpp
         * (which cannot be presented to a 32 bpp scanout until the textured-quad present, design 5 / L4) */
        uint32_t dw = 0, dh = 0, dbpp = 2u, want = 2u;
        if (!rgb_display(&p->rb, &dw, &dh, &dbpp) && dbpp == 4u) want = 4u;
        p->scr_w = dw; p->scr_h = dh;
        p->fs_scale = (!p->win_mode && dw && dh && (p->fb_w < dw || p->fb_h < dh)) ? 1u : 0u;      /* FS_SCALE: a smaller full-screen drawable is stretched to the scanout */
        if (p->fs_scale) want = 2u;                                                                    /* a 4 KB-pitch 32 bpp texture is ~30x slower to sample (387 ms vs 13 ms per 1024x768, measured): stretch from a 16 bpp target */
        p->use_ovl = (p->fs_scale && rgb_overlay_wanted(&p->rb, p->fb_w)) ? 1u : 0u;                  /* overlay present: no engine time per frame; falls back to the quad if the kernel refuses */
        p->rb.dbl = (int)p->use_ovl; p->rb.ovl_fail = 0;
        p->rb.pot_pitch = p->fs_scale ? 1u : 0u;                                                      /* the quad samples the target as a power-of-two texture */
        {   int attempt;
            for (attempt = 0; attempt < 2; attempt++) {
                p->rb.pot_pitch = p->fs_scale ? 1u : 0u;
                rc = rgb_surface(&p->rb, p->fb_w, p->fb_h, want, 1);
                if (rc && want == 4u) { p->rb.pot_pitch = 1; rc = rgb_surface(&p->rb, p->fb_w, p->fb_h, 2, 1); }
                if (!rc || attempt || !p->win_mode || !evict_offscreen(p)) break;       /* out of VRAM: free the off-screen windows' surfaces and try once more */
            }
        }
        { static const char *sl = (const char *)1; if (sl == (const char *)1) sl = getenv("RAGE_SURF_LOG");       /* debug: RAGE_SURF_LOG=1 logs every surface build to stderr */
          if (sl) fprintf(stderr, "RAGE_SURF: ctx %p drawable %ux%u win_mode %d scanout %ux%u bpp %u -> target bpp %u rc %d (fs_scale %u)\n", (void *)ctx, p->fb_w, p->fb_h, (int)p->win_mode, dw, dh, dbpp, p->rb.bpp, rc, p->fs_scale); }   /* 16 bpp target on a 32 bpp scanout: presented through the 3D engine, so power-of-two pitch */
        p->cbpp = p->rb.bpp ? p->rb.bpp : 2u;
    }
    if (rc) { p->attached = 0; return rc; }
    p->rb.gen = rgb_gen(&p->rb);
    p->mode_gen = p->rb.gen;
    p->attached = 1; p->surf_evicted = 0;
    if (p->win_mode) surf_register(p);
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
    if (p->surf_evicted && !p->rb.col_off) {                 /* freed for another context: build it again now */
        if (mach64_build_surface(ctx)) { p->attached = 1; return 0; }           /* still no room: draw nothing, try again next time */
    }
    g = rgb_gen(&p->rb);
    if (g & 1u) return 0;
    if (g != p->mode_gen) {
        p->batch.n = 0;                         /* anything queued targeted the old surface */
        p->rb.col_off = p->rb.z_off = p->rb.col_alt = 0;      /* the kernel dropped them with the old mode (the overlay was disabled by its will-change hook) */
        p->rb.ovl_on = 0;
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
    if (gli && p->rb.bpp == 4u) {                  /* ARGB8888 target: the kernel fill takes 0xAARRGGBB */
        uint32_t r = rage_f2byte(mach64_gli_u32(ctx, GLI_CLEAR_COLOR + 0)), g = rage_f2byte(mach64_gli_u32(ctx, GLI_CLEAR_COLOR + 4));
        uint32_t b = rage_f2byte(mach64_gli_u32(ctx, GLI_CLEAR_COLOR + 8)), a = rage_f2byte(mach64_gli_u32(ctx, GLI_CLEAR_COLOR + 12));
        c565 = (a << 24) | (r << 16) | (g << 8) | b;
    } else if (p->rb.bpp == 4u) c565 = 0;
    else if (gli) {
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
    glidiff(ctx, "frame_start");
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
        if (!p->rb.col_off) return 0;                                /* surface freed for another context */
        int wx = 0, wy = 0, ww = 0, wh = 0, n, i; short vr[16][4];
        if (mach64_cgs_resolve_bounds(p->cgs_win, p->cgs_surf, 0, &wx, &wy, &ww, &wh) && ww == (int)p->fb_w && wh == (int)p->fb_h) {
            p->win_x = (uint32_t)wx; p->win_y = (uint32_t)wy;
        }
        n = mach64_cgs_visible_rects(p->cgs_win, p->cgs_surf, (int)p->win_x, (int)p->win_y, (int)p->fb_w, (int)p->fb_h, vr, 16);
        if (g_ptrace_fn) ptrace_log(p, n, vr);
        if (n == 0 || n == -2) return 0;                              /* completely covered, or the window is not on screen (blitting it would leave stale pixels the window server never repaints) */
        if (n > 0) {
            for (i = 0; i < n; i++)
                rgb_present(&p->rb, vr[i][0] - (int)p->win_x, vr[i][1] - (int)p->win_y, vr[i][2], vr[i][3], vr[i][0], vr[i][1]);
            return 1;
        }
    }
    if (p->fs_scale && !p->win_mode && p->use_ovl && !rgb_overlay_present(&p->rb, p->scr_w, p->scr_h)) return 1;   /* overlay: one engine blit (the copy into the other block) reprogrammed the engine for 2D */
    if (p->fs_scale && !p->win_mode) {                                  /* FS_SCALE: stretch the whole target over the scanout (bilinear); fall back to a 1:1 copy if the kernel refuses */
        if (!rgb_present_scaled(&p->rb, 0, 0, (int)p->fb_w, (int)p->fb_h, 0, 0, (int)p->scr_w, (int)p->scr_h, 1)) return 1;
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
        if (++idle < 3 || !p->attached || !p->win_mode || !p->rb.col_off) continue;
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

/* table[0x04] = the STATE flush: sync the app's depth state into Z_CNTL before the batch renders. */
int mach64_flush(gld_context *ctx, uint32_t a2, uint32_t a3, uint32_t a4, uint32_t a5, uint32_t a6, uint32_t a7)
{
    mach64_priv *p = MACH64_PRIV(ctx);
    (void)a3; (void)a4; (void)a5; (void)a6; (void)a7;
    mach64_sync_depth_from_gli(ctx);
    if ((a2 & 0x4700u) && p && p->attached) {
        mach64_frame_start(ctx);                     /* arm the frame (mode check, undefined-surface clear) before the app's own clear */
        if (!p->attached) return 0;
        if (a2 & 0x4000u) colour_clear(ctx);
        if (a2 & 0x0100u) { p->z_clear_pending = 0; if (p->rb.z_off) mach64_zs_clear_depth(ctx); else p->z_clear_pending = 1; }
        if (a2 & 0x0200u) mach64_accum_clear(ctx);
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

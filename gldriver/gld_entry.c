/*
 * gld_entry.c - the remaining gld* entry points GLEngine looks up (~53 total).
 *
 * Implements the CORE set needed to drive a context to a drawable, and STUBS
 * every optional feature entry point so none of GLEngine's symbol lookups come
 * back NULL (a NULL it then calls would crash the GL app).  Stubs return 0
 * (NULL/success) and are only reached if an app uses that feature.
 *
 * gldAttachDrawable is reversed from the reference (ctx, type, drawable, flags):
 * it derives a surface struct from Apple's CGS drawable descriptor and calls a
 * kext selector (set_surface) that binds the render target.  That descriptor
 * format is the remaining unknown -> we log what the framework passes and use a
 * placeholder target for now, to be refined once we see real drawables.
 */
#include "gld.h"
#include "../common/mach64_abi.h"   /* kMach64Sel_* selector indices */
#include "../common/atiragepro_abi.h"   /* Path-B command/context region types */
#include <stdio.h>
#include <stdarg.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>   /* fsync (MACH64_GLD_FSYNC crash logging) */

/* ---- diagnostic log -------------------------------------------------
 * Open the log ONCE and keep the FILE* -- writes are stdio-buffered and flushed on the app's
 * exit().  The previous fopen("a")+vfprintf+fclose PER CALL cost ~1ms each on the Lombard's
 * IDE disk; at the ~300 log lines/frame a real app (atlantis) produces that alone capped it
 * at ~2fps (the rendering was never the bottleneck).  Set MACH64_GLD_NOLOG=1 to compile-free
 * disable logging entirely (skips even the vfprintf formatting) for pure perf runs. */
void gld_log(const char *fmt, ...)
{
    static FILE *f = NULL;
    static int  tried = 0, do_fsync = 0;
    va_list ap;
    if (!tried) {
        tried = 1;
        do_fsync = (getenv("MACH64_GLD_FSYNC") != 0);   /* force-to-disk each line (survives a panic) */
        if (!getenv("MACH64_GLD_NOLOG")) {
            /* Log to $HOME, NOT /tmp: Panther WIPES /private/tmp on (re)boot, so a kernel
             * panic destroys the log that would diagnose it.  $HOME survives the reboot. */
            const char *home = getenv("HOME");
            char path[1024];
            if (home && (int)strlen(home) < 1000) {
                snprintf(path, sizeof(path), "%s/mach64gld.log", home);
                f = fopen(path, "w");
            }
            if (!f) f = fopen("/tmp/mach64gld.log", "w");   /* fallback */
            if (f) setvbuf(f, NULL, _IONBF, 0);   /* unbuffered: survive a crash/hang mid-frame */
        }
    }
    if (!f) return;
    va_start(ap, fmt);
    vfprintf(f, fmt, ap);
    va_end(ap);
    fflush(f);                                    /* flush every line so the crash point is visible */
    if (do_fsync) fsync(fileno(f));               /* push to disk so a KERNEL PANIC can't lose the tail */
}

/* ---- gldAttachDrawable: bind the app's drawable as the render target -
 * type: 0x50 window / 0x5a fullscreen / 0x36 (pbuffer?) -- same tokens as the
 * pixel-format parser.  drawable: Apple CGS descriptor; the reference reads
 * drawable[0/4/8] and calls a kext set_surface selector (ctx+0x04 connection).
 * TODO: reverse the CGS format -> real VRAM offset/pitch + our set_surface. */
int gldAttachDrawable(gld_context *ctx, int type, void *drawable, uint32_t flags)
{
    gld_log("gldAttachDrawable(ctx=%p type=0x%x drawable=%p flags=0x%x)\n",
            (void*)ctx, type, drawable, flags);
    if (!ctx) return 0x271e;
    if ((type == 0x50 || type == 0x5a || type == 0x36) && !drawable)
        return 0x2715;
    /* detach: nothing to restore.  Present is now a back->front BLIT (no CRTC page-flip),
     * so the CRTC never leaves the desktop front buffer -- the desktop is intact when the
     * app exits.  (Previously the flip-present moved the CRTC to our back buffer, which had
     * to be flipped back here.) */
    if (type == 0 || !drawable) {
        if (ctx->backend && MACH64_PRIV(ctx)->win_buf_off) {   /* release the per-context windowed buffer */
            mach64_winbuf_free(MACH64_PRIV(ctx)->win_buf_off, MACH64_PRIV(ctx)->win_buf_size);
            MACH64_PRIV(ctx)->win_buf_off = 0; MACH64_PRIV(ctx)->win_buf_size = 0;
            MACH64_PRIV(ctx)->win_mode = 0;
        }
        /* Return the Apple-allocated Z + texture-heap VRAM so it isn't leaked (VRAM is never
         * reclaimed until reboot otherwise). */
        if (MACH64_PRIV(ctx)->z_record)   { mach64_vram_free(ctx, MACH64_PRIV(ctx)->z_record);   MACH64_PRIV(ctx)->z_record = 0; }
        if (MACH64_PRIV(ctx)->tex_record) { mach64_vram_free(ctx, MACH64_PRIV(ctx)->tex_record); MACH64_PRIV(ctx)->tex_record = 0; }
        return 0;
    }

    {
        const uint32_t *d = (const uint32_t *)drawable;
        uint8_t *c = (uint8_t *)ctx;
        uint32_t width  = d[1];        /* windowed descriptor: [1]=width      */
        uint32_t height = d[2];        /*                      [2]=height     */
        uint32_t base_bytes  = 0x600000u;   /* fallback: off-screen scratch  */
        uint32_t pitch_bytes;
        uint32_t fb_pitch_px;

        /* Path B: the drawable descriptor carries NO window geometry (windowed.md) -- d[1]/d[2]
         * are a CGS window-id/surface-id pair, NOT width/height (this run: d=8f17 a2 104, rest 0).
         * Resolve the TRUE content bounds via CGS ONCE, up front, and treat them as authoritative:
         * Apple's surface, our render target, GLEngine's drawable dims and the present blit must
         * ALL agree on WxH.  Previously width/height fell through as the garbage d[1]/d[2] (162x260)
         * because the CRTC get_surface_size returns 0 on Apple's kext; that tiny surface then
         * scissored all geometry to a corner, AND the real 480-wide window was rejected downstream
         * by the `ww <= fb_pitch_px(=162)` gate -- a circular size dependency.  Resolve once (the
         * CGS position drifts per call -- windowed.md -- so we must NOT re-resolve later). */
        {   mach64_priv *pv0 = MACH64_PRIV(ctx);
            int wx = 0, wy = 0, ww = 0, wh = 0;
            if (pv0->dbuf && mach64_cgs_resolve_bounds(d[1], d[2], &wx, &wy, &ww, &wh)
                && ww > 0 && wh > 0 && ww <= 2048 && wh <= 2048) {
                width = (uint32_t)ww; height = (uint32_t)wh;
                pv0->win_mode = 1;
                pv0->win_x = (uint32_t)wx; pv0->win_y = (uint32_t)wy;
                pv0->fb_w  = (uint32_t)ww; pv0->fb_h  = (uint32_t)wh;
                gld_log("  [pathb] CGS bounds -> surface %dx%d at (%d,%d) [authoritative]\n",
                        ww, wh, wx, wy);
            }
        }

        /* Bind the LIVE display framebuffer as the render target (full-screen): read the
         * mach64 CRTC via the kext -> DMA lands on screen.  Take the render dims from the
         * CRTC HARDWARE (pitch = width, V_DISP+1 = height), NOT the drawable descriptor:
         * GLEngine's fullscreen descriptor has been observed pointing at reused heap (d[2]
         * = ASCII "path", d[1] a growing garbage value) which would leave ctx+0x1f4 junk
         * and render everything offscreen.  The CRTC is the authoritative fullscreen size.
         * type 0x50 is included: a GLUT game-mode (fullscreen) app attaches its drawable
         * as 0x50 with a bogus descriptor size (atlantis: d[1]=441 d[2]=258), so it too
         * takes the live CRTC scanout base + dims.  (Real WINDOWED 0x50 support -- parsing
         * the descriptor for a sub-rect base/pitch -- is a separate, later task.) */
        if ((type == 0x36 || type == 0x50 || type == 0x5a) && ctx->connect) {
            uint32_t crtc = 0, base = 0, pitch = 0, hi = 0;
            IOConnectMethodScalarIScalarO(ctx->connect, kMach64Sel_get_surface_size,
                                          0, 4, &crtc, &base, &pitch, &hi);
            gld_log("  framebuffer CRTC_OFF_PITCH=0x%x -> base=0x%x pitch=%upx v_disp=%u\n",
                    crtc, base, pitch, hi);
            if (pitch) { base_bytes = base; width = pitch;
                         if (hi >= 16 && hi <= 4096) height = hi; }
            else if (!MACH64_PRIV(ctx)->win_mode) {
                /* FULLSCREEN (no CRTC size from Apple's kext AND the CGS resolve found no real window
                 * -> win_mode==0).  The descriptor dims are garbage (d[1]/d[2] -> a 64px-wide surface
                 * = black screen), so shape to the REAL main-display size.  GATED on !win_mode so a
                 * WINDOWED 0x50 drawable (win_mode==1, CGS resolved its true sub-window bounds) is NEVER
                 * overridden -- fullscreen 0x36/0x50 and windowed 0x50 share the type, only the CGS
                 * resolve tells them apart. */
                int dw = 0, dh = 0;
                if (mach64_display_size(&dw, &dh)) {
                    width = (uint32_t)dw; height = (uint32_t)dh;
                    gld_log("  [pathb] fullscreen (no window) -> display size %ux%u\n", width, height);
                }
            }
        }
        pitch_bytes = width * 4;
        fb_pitch_px = width;

        gld_log("  drawable[0..2] = 0x%x 0x%x 0x%x  -> surface %ux%u target base=0x%x\n",
                d[0], d[1], d[2], width, height, base_bytes);
        /* WINDOWED-MODE PROBE: dump the raw drawable descriptor so we can reverse the window
         * geometry (offset/pitch/bounds) for windowed support.  A windowed context attaches as
         * type 0x50 with a distinctively-sized test window, so we can spot its width/height/x/y
         * in these words.  16 words = 64 bytes; Apple's CGS surface descriptor is larger. */
        gld_log("  descdump[0..15] = %08x %08x %08x %08x %08x %08x %08x %08x %08x %08x %08x %08x %08x %08x %08x %08x\n",
                d[0],d[1],d[2],d[3],d[4],d[5],d[6],d[7],d[8],d[9],d[10],d[11],d[12],d[13],d[14],d[15]);

        /* Populate the ctx surface fields GLEngine reads to consider the drawable
         * renderable -- WITHOUT these it culls all geometry (no dispatch).
         * Offsets from the reference gldAttachDrawable (ati128gld 0x4f38):
         * 0x20=drawable, 0x24/0x28=flags, 0x94=target base, 0x98=pitch,
         * 0x1f0/0x1f4=width/height, 0x64=surface handle, 0x68=byte size.
         * These land in unused/_tail/extra ctx space (we don't map registers). */
        *(uint32_t *)(c + 0x20)  = (uint32_t)drawable;
        *(uint32_t *)(c + 0x24)  = (flags >> 8) & 0xff;
        *(uint32_t *)(c + 0x28)  = flags & 0xff;
        *(uint32_t *)(c + 0x64)  = 1;                      /* non-null handle  */
        *(uint32_t *)(c + 0x68)  = pitch_bytes * height;   /* surface size     */
        *(uint32_t *)(c + 0x94)  = base_bytes;             /* target base      */
        *(uint32_t *)(c + 0x98)  = pitch_bytes;            /* target pitch     */
        /* 0x9c..0xa8: ref context-init defaults (ati128gld 0x4b30) that
         * gldInitDispatch reports to GLEngine's out-struct.  Without these they
         * are malloc garbage. */
        *(uint32_t *)(c + 0x9c)  = 0;
        *(uint32_t *)(c + 0xa0)  = 0;
        *(uint32_t *)(c + 0xa4)  = 0x32;
        *(uint32_t *)(c + 0xa8)  = 0;
        *(uint32_t *)(c + 0x1f0) = width;
        *(uint32_t *)(c + 0x1f4) = height;

        /* DOUBLE-BUFFER DISPATCH GATE (reference RE, ATIRage128GLDriver gldAttach-
         * Drawable type-0x36 path -> sub_a524(ctx,&caps,r5=0)).  The reference does
         * NOT route DB geometry through a special dispatch slot -- slot 0x60 is a
         * no-op.  Instead it sets a FLAG in the GLEngine object at *(ctx+0x14) (= a7
         * from gldCreateContext): word0 |= 2.  GLEngine reads that flag to decide the
         * context has a bound back-buffer surface and dispatches primitives normally
         * instead of falling back to slot 0x60.  It also sets ctx+0x1e4=1 and requires
         * caps bit 0x20 (ctx+0x2c) CLEAR.
         * The gate is a property of the DOUBLE-BUFFERED CONTEXT, not the drawable
         * type: gate on pv->dbuf (armed by the DB pixel format in gldCreateContext),
         * not type==0x36.  A real GLUT app (atlantis) attaches its DB drawable as
         * type 0x50 -- with the old type==0x36 gate that path was skipped, so
         * GLEngine fell back to slot 0x60 and every frame rendered blank. */
        if (MACH64_PRIV(ctx)->dbuf) {
            uint32_t a7p = *(uint32_t *)(c + 0x14);
            *(uint32_t *)(c + 0x2c) &= ~0x20u;      /* caps 0x20 clear (DB dispatch gate) */
            *(uint32_t *)(c + 0x1e4) = 1;
            if (a7p > 0x10000 && a7p < 0x40000000) {
                uint32_t w0 = *(uint32_t *)(uintptr_t)a7p;
                *(uint32_t *)(uintptr_t)a7p = w0 | 2u;
                gld_log("  [DB-gate] a7=0x%x word0 0x%x -> 0x%x, ctx+0x2c=0x%x ctx+0x1e4=1\n",
                        a7p, w0, w0 | 2u, *(uint32_t *)(c + 0x2c));
            } else {
                gld_log("  [DB-gate] a7=0x%x NOT a pointer -- flag NOT set\n", a7p);
            }
        }

        mach64_set_render_target(ctx, base_bytes, fb_pitch_px);

        {   mach64_priv *pv = MACH64_PRIV(ctx);
            uint32_t surf  = fb_pitch_px * 4u * height;   /* bytes per colour surface */
            uint32_t z_off = 0x400000u;   /* single-buffered: Z past the front buffer */

            /* Double-buffered (dbuf armed by the DB pixel format in gldCreateContext):
             * render off-screen to a back buffer and blit it to the front on present.
             * 8MB VRAM map: front | back | Z | texture heap, packed to the top --
             *   front 0x1000..            scanout, 3MB @ 1024x768x32
             *   back  front+surf..        off-screen render target (3MB)
             *   Z     back+surf..         16-bit depth (1.5MB)
             *   heap  Z+zsize..0x800000   ~0.5MB (shrunk from 1.5MB to make room).
             * GLEngine's glClear hits the FRONT, so the driver clears the back. */
            if (pv->dbuf) {
                uint32_t back  = base_bytes + surf;
                uint32_t zsize = fb_pitch_px * 2u * height;
                z_off = back + surf;
                pv->front_off = base_bytes;
                pv->back_off  = back;
                pv->render_off = back;   /* page-flip: front is displayed, render into back */
                pv->fb_w = width; pv->fb_h = height; pv->fb_pitch = fb_pitch_px;
                mach64_set_texheap(z_off + zsize, 0x800000u);
                mach64_set_render_target(ctx, back, fb_pitch_px);   /* draw to back */
                gld_log("  [dbuf] front=0x%x back=0x%x z=0x%x %ux%u pitch=%upx -> render->back\n",
                        base_bytes, back, z_off, width, height, fb_pitch_px);
            }

            /* DEPTH: bind the 16-bit Z buffer (single-buf: 0x400000 past the front;
             * double-buf: above the back buffer).  Real per-vertex Z comes from the
             * NDC z in mach64_read_vertex; Z_CNTL follows the app's GL depth state,
             * synced per-draw in mach64_flush / render_tri_strip -- not forced here. */
            mach64_set_zbuffer(ctx, z_off, fb_pitch_px);

            /* WINDOWED coexistence probe: env MACH64_WIN_RECT="x,y,w,h" (screen pixels).  Keep
             * the front/back/pitch from the dbuf block (screen-sized), but override the RENDER
             * dims to the window w x h and mark win_mode.  render_ctl then renders w x h into the
             * back buffer's top-left and BLITs it to the live front at (x,y) -- no CRTC page-flip,
             * so the desktop stays visible and the WindowServer keeps compositing.  That is the
             * engine-coexistence test: our bus-master blit + geometry DMA run concurrently with
             * the live WS.  (Geometry is bounded by the w x h viewport the app sets.) */
            {   int wx = 0, wy = 0, ww = 0, wh = 0, got = 0;
                /* Reuse the bounds already resolved ONCE up front (pv->win_mode/fb_w/fb_h): the CGS
                 * resolver drifts the reported position per call (windowed.md), so re-resolving here
                 * would disagree with the surface we already shaped.  env MACH64_WIN_RECT stays the
                 * manual fallback for when the early CGS resolve found nothing. */
                if (pv->win_mode) {
                    wx = (int)pv->win_x; wy = (int)pv->win_y;
                    ww = (int)pv->fb_w;  wh = (int)pv->fb_h; got = 1;
                }
                if (!got) {
                    const char *wr = getenv("MACH64_WIN_RECT");
                    if (pv->dbuf && wr && sscanf(wr, "%d,%d,%d,%d", &wx, &wy, &ww, &wh) == 4)
                        got = 1;
                }
                /* Activate windowed blit only for a real SUB-screen / offset window (a fullscreen
                 * drawable -- full-width at the origin -- keeps the CRTC page-flip path). */
                if (got && ww > 0 && wh > 0 && ww <= (int)fb_pitch_px && wh <= 2048 &&
                    (ww < (int)fb_pitch_px || wx > 0 || wy > 0)) {
                    /* PER-CONTEXT back+Z: allocate a window-sized (screen-PITCHED, wh rows) back+Z from
                     * the windowed VRAM pool so multiple windowed contexts render independently instead
                     * of sharing the one screen-sized back the dbuf block set.  The pool occupies that
                     * reserved-but-unused screen back/Z region.  Falls back to the shared back if the
                     * pool is exhausted.  Screen-pitched -> the present blit stays single-pitch. */
                    uint32_t zsize = fb_pitch_px * 2u * height;
                    mach64_winpool_init(base_bytes + surf, z_off + zsize);
                    if (pv->win_buf_off) {                     /* re-attach: release the old allocation */
                        mach64_winbuf_free(pv->win_buf_off, pv->win_buf_size);
                        pv->win_buf_off = 0; pv->win_buf_size = 0;
                    }
                    {   uint32_t bsz = fb_pitch_px * 4u * (uint32_t)wh;   /* back: wh rows @ screen pitch */
                        uint32_t zsz = fb_pitch_px * 2u * (uint32_t)wh;   /* Z: 16-bit */
                        uint32_t buf = mach64_winbuf_alloc(bsz + zsz);
                        if (buf) {
                            pv->win_buf_off = buf; pv->win_buf_size = bsz + zsz;
                            pv->back_off = pv->render_off = buf;
                            mach64_set_render_target(ctx, buf, fb_pitch_px);
                            mach64_set_zbuffer(ctx, buf + bsz, fb_pitch_px);
                            gld_log("  [win] per-ctx back=0x%x z=0x%x (%u KB)\n",
                                    buf, buf + bsz, (bsz + zsz) >> 10);
                        }  /* else keep the shared screen back from the dbuf block */
                    }
                    pv->win_mode = 1; pv->win_x = (uint32_t)wx; pv->win_y = (uint32_t)wy;
                    pv->fb_w = (uint32_t)ww; pv->fb_h = (uint32_t)wh;
                    *(uint32_t *)(c + 0x1f0) = (uint32_t)ww;
                    *(uint32_t *)(c + 0x1f4) = (uint32_t)wh;
                    gld_log("  [win] WINDOWED %dx%d at (%d,%d): render->back, blit to LIVE front\n",
                            ww, wh, wx, wy);
                }
            }
        }
        MACH64_PRIV(ctx)->frame_new = 1;   /* clear Z (+ back) before the first frame */
#ifdef GLD_APPLE_PROBE
        /* Path B Phase 2: the drawable descriptor's word[0] is the CGS surface ID (e.g. 0x8013).
         * Test whether Apple's kext resolves it + binds -- the pivotal port question.  Uses the
         * window bounds if windowed, else the fullscreen dims.  No region mapping (safe). */
        {   mach64_priv *pv2 = MACH64_PRIV(ctx);
            uint32_t sx = pv2->win_mode ? pv2->win_x : 0;
            uint32_t sy = pv2->win_mode ? pv2->win_y : 0;
            uint32_t sw = pv2->win_mode ? pv2->fb_w  : width;
            uint32_t sh = pv2->win_mode ? pv2->fb_h  : height;
            /* Path B (VALIDATED sequence, tools/mach64_surfscan.c): register + shape the surface on
             * Apple's accelerator, then the GL set_surface binds it.  mode 0x24 = 8888 | windowed
             * (windowed path stores idx unchanged at surf+0x98, accepts the CGS-sized handle);
             * make_surface now also set_shapes with the window dims to allocate the backing that
             * set_surface's bind requires. */
            pv2->apple_surf_cn = mach64_apple_make_surface(d[0], 0x24u, sw, sh);
            /* NOTE: IOConnectAddClient (connectClient) PANICS here -- it does release/retain via
             * virtual calls on ctx+0x88 (the IOATIRProShared object), which is NULL on our bare
             * connections.  The share group needs an IOATIRProShared created first (reverse the
             * setup OFFLINE before trying again).  Reverted -- do NOT re-add without that. */
            mach64_apple_bind_surface(pv2->apple_cn, d[0], sx, sy, sw, sh);

            /* Path B: set_surface bound the surface (ctx+0x10c), so it is NOW safe to
             * map the command region (type 1 -> ctx->regs@0xb8, holds the
             * VendorCommandBufferHeader mach64_submit writes) and the context buffer
             * (type 2 -> ctx->fb@0xd0).  Mapping BEFORE the bind NULL-derefs in
             * clientMemoryForType.  Map once. */
            if (pv2->apple_surf_cn && !ctx->regs) {
                IOConnectMapMemory(ctx->connect, ATIRPRO_MEM_COMMAND, mach_task_self(),
                                   (vm_address_t *)&ctx->regs, (IOByteCount *)&ctx->regs_size,
                                   kIOMapAnywhere | kIOMapInhibitCache);
                IOConnectMapMemory(ctx->connect, ATIRPRO_MEM_CONTEXT, mach_task_self(),
                                   (vm_address_t *)&ctx->fb, (IOByteCount *)&ctx->fb_size,
                                   kIOMapAnywhere | kIOMapInhibitCache);
                gld_log("  [pathb] mapped cmd(type1)=%p sz=%u  ctxbuf(type2)=%p sz=%u\n",
                        ctx->regs, ctx->regs_size, ctx->fb, ctx->fb_size);
                /* Path B: prime the surface (alloc VRAM render buffer + clear dirty bit) and set
                 * p->target_off/pitch = surf[0x12c][0x28] so clears/geometry render into it. */
                mach64_pathb_setup_target(ctx);
            }
        }
#endif

        /* EXPERIMENT (root-cause validation): GLEngine leaves its drawable
         * descriptor gli[0x789c] ALL-ZEROS for our HW surface (surface-binding
         * contract incomplete), so glViewport clamps the viewport to
         * desc[0xc]/desc[0x10] = 0 -> 0x0 viewport -> ALL geometry culled before
         * any primitive dispatch.  The software renderer's descriptor is fully
         * populated (desc = {4, 4e7fffff, 3, 0x800, 0x800, ...}).  Populate it
         * ourselves to test whether that is the ONLY gate.  ctx+0x10 = the gli
         * context (a8 from gldCreateContext); desc = gli[0x789c]. */
        {   uint32_t p10 = *(uint32_t *)(c + 0x10);   /* points into gli (+0x3610) */
            uint32_t gli = 0, off;
            /* Find the gli base: the object whose [+0x7898] back-points to ctx. */
            for (off = 0; off <= 0x8000; off += 4) {
                uint32_t g = p10 - off;
                if (g < 0x10000 || g >= 0x40000000) break;
                if (*(uint32_t *)(uintptr_t)(g + 0x7898) == (uint32_t)(uintptr_t)ctx) {
                    gli = g; break;
                }
            }
            gld_log("  [desc-exp] ctx+0x10=0x%x -> gli base=0x%x (off 0x%x)\n", p10, gli, off);
            if (gli) {
                uint32_t descp = *(uint32_t *)(uintptr_t)(gli + 0x789c);
                gld_log("  [desc-exp] gli[0x789c]=desc=0x%x  desc[0xc]=0x%x desc[0x10]=0x%x\n",
                        descp,
                        descp>0x10000?*(uint32_t*)(uintptr_t)(descp+0xc):0,
                        descp>0x10000?*(uint32_t*)(uintptr_t)(descp+0x10):0);
                if (descp > 0x10000 && descp < 0x40000000) {
                    uint32_t *dd = (uint32_t *)(uintptr_t)descp;
                    dd[0x00/4] = 0x00000004;
                    dd[0x04/4] = 0x4e7fffff;
                    dd[0x08/4] = 0x00000003;
                    dd[0x0c/4] = 0x00000800;   /* width  (viewport clamp max) */
                    dd[0x10/4] = 0x00000800;   /* height (viewport clamp max) */
                    dd[0x1c/4] = 0x01000000;
                    dd[0x24/4] = 0x00000008;
                    dd[0x28/4] = 0x00000008;
                    dd[0x2c/4] = 0x00000008;
                    dd[0x34/4] = 0x00000020;
                    /* GL_MAX_TEXTURE_SIZE: GLEngine's glGetIntegerv worker reads
                     * it as a HALFWORD from descriptor+0xb2 (traced: GLEngine
                     * 0xb6e6c -> lhz r0,0xb2(gli[0x789c])).  Zero here made every
                     * glTexImage2D fail GL_INVALID_VALUE.  mach64 texture unit
                     * maxes at 1024x1024 -> advertise 1024 (power-of-two). */
                    *(uint16_t *)(uintptr_t)(descp + 0xb2) = 1024;
                    /* GL_MAX_TEXTURE_UNITS: read as a halfword from descriptor+0xac
                     * (traced: GLEngine 0xb8204 -> lhz r0,0xac(gli[0x789c])).  We
                     * support one texture unit; advertise 1 (was 0). */
                    *(uint16_t *)(uintptr_t)(descp + 0xac) = 1;
                    gld_log("  [desc-exp] populated descriptor (maxtex=1024, texunits=1)\n");
                }
            }
        }
    }
    return 0;
}

/* ---- renderer strings / queries ------------------------------------- */
const char *gldGetString(gld_context *ctx, uint32_t name)
{
    (void)ctx;
    gld_log("gldGetString(name=0x%x)\n", name);
    switch (name) {
    case 0x1f00: return "RageProLT Project";              /* GL_VENDOR    */
    case 0x1f01: return "ATI Rage LT Pro (mach64)";       /* GL_RENDERER  */
    case 0x1f02: return "1.1 Mach64-0.1";                 /* GL_VERSION   */
    case 0x1f03: return "";                               /* GL_EXTENSIONS*/
    default:     return "";
    }
}

/* gldGetVersion: GLEngine looks this up by name IMMEDIATELY after
 * gldInitializeLibrary and BAILS (never enumerating the renderer) if it is
 * absent -- this was THE discovery gate.  Fills 4 version-component outputs;
 * values taken from the reference bundle (2, 4, 0xb, 0x1000). */
int gldGetVersion(int *v0, int *v1, int *v2, int *v3)
{
    gld_log("gldGetVersion()\n");
    if (v0) *v0 = 2;
    if (v1) *v1 = 4;
    if (v2) *v2 = 0xb;
    if (v3) *v3 = 0x1000;
    return 1;
}

int gldGetError(gld_context *ctx) { (void)ctx; gld_log("gldGetError()\n"); return 0; }

int gldGetInteger(gld_context *ctx, uint32_t pname, int *params)
{
    /* GLEngine never calls this for the texture caps (GL_MAX_TEXTURE_SIZE comes
     * from the drawable descriptor +0xb2, set in gldAttachDrawable). */
    (void)ctx;
    gld_log("gldGetInteger(pname=0x%x)\n", pname);
    if (params) *params = 0;
    return 0;
}

int gldSetInteger(gld_context *ctx, uint32_t pname, int value)
{ (void)ctx; gld_log("gldSetInteger(pname=0x%x val=%d)\n", pname, value); return 0; }

/* gldInitDispatch: initial dispatch-table setup (pairs with gldUpdateDispatch).
 * Install our render slots so the table is usable from the start. */
void gldInitDispatch(gld_context *ctx, void *table, void *outinfo)
{
    gld_log("gldInitDispatch(table=%p outinfo=%p)\n", table, outinfo);
    /* CONTRACT (ref gldInitDispatch 0x60a60): arg3(r5) is an OUTPUT struct that
     * GLEngine reads back for the drawable geometry.  The reference copies
     * ctx[0x94..0xa8] into outinfo[0..0x14]; we ignored it, leaving GLEngine a
     * garbage drawable -> geometry culled before any primitive dispatch.  Port
     * it verbatim. */
    if (outinfo) {
        const uint32_t *c = (const uint32_t *)((const unsigned char *)ctx);
        uint32_t *o = (uint32_t *)outinfo;
        o[0] = c[0x94/4];   /* target base   */
        o[1] = c[0x98/4];   /* target pitch  */
        o[2] = c[0x9c/4];
        o[3] = c[0xa0/4];
        o[4] = c[0xa4/4];
        o[5] = c[0xa8/4];
        gld_log("  outinfo <= ctx[0x94..0xa8]: %08x %08x %08x %08x %08x %08x\n",
                o[0],o[1],o[2],o[3],o[4],o[5]);
    }
    /* Reference gldInitDispatch installs the driver's control/flush slots that
     * GLEngine calls to render.  table[0x04] is THE flush entry.  Store the table
     * ptr in ctx+0x1c (reference: stw r4, 0x1c(ctx)) and install our flush. */
    *(void **)((unsigned char *)ctx + 0x1c)  = table;
    /* table[0x08] = GLEngine's glReadPixels readback hook; it's called with GLEngine's rb_ctx, not
     * our gld_context, so stash ours for mach64_ctl08 to read the back-buffer VRAM.  The GLEngine gc
     * holds GL_PACK_* state; our table sits at gc+0x78a8 (table[0x08]=gc+0x78b0), so gc=table-0x78a8. */
    g_gld_readback_ctx = ctx;                        /* global fallback (ctl08 prefers its rb_ctx arg) */
    g_gld_gc = (unsigned char *)table - 0x78a8;       /* global fallback for the pixel-store readers   */
    if (ctx->backend)                                 /* per-context gc = the hardened path pixel ops use */
        MACH64_PRIV(ctx)->gc = (unsigned char *)table - 0x78a8;
    *(void **)((unsigned char *)table + 0x00) = (void *)mach64_ctl00;
    *(void **)((unsigned char *)table + 0x04) = (void *)mach64_flush;
    *(void **)((unsigned char *)table + 0x08) = (void *)mach64_ctl08;
    *(void **)((unsigned char *)table + 0x0c) = (void *)mach64_ctl0c;
    *(void **)((unsigned char *)table + 0x10) = (void *)mach64_ctl10;
    *(void **)((unsigned char *)table + 0x14) = (void *)mach64_ctl14;
    gldUpdateDispatch(ctx, table, (uint32_t)(uintptr_t)outinfo);
}

/* gldFlushBuffer: swap/present the drawable.  Submits are synchronous today, so
 * fence.  TODO: real front/back flip once double-buffered surfaces are bound. */
int gldFlushBuffer(gld_context *ctx, void *drawable)
{
    uint8_t *c = (uint8_t *)ctx;
    gld_log("gldFlushBuffer(ctx=%p drawable=%p) cbpp(ctx+0x5f)=%u target=0x%x pitch=0x%x\n",
            (void*)ctx, drawable, (unsigned)c[0x5f],
            *(uint32_t *)(c + 0x94), *(uint32_t *)(c + 0x98));
    /* FINISH FIRST, then present.  gldFinish flushes the triangle batch, submits the
     * command buffer and waits for idle -> the back buffer is fully rendered.  Only
     * then is it safe to blit back->front.  (Presenting first blits an un-rendered
     * back buffer, so a swap without a preceding glFinish showed black -- the
     * animation swap-chain bug.  A single glFinish-before-swap test hid it.) */
    {   int r = gldFinish(ctx);
        /* NB: the Path-B present runs at dispatch slot 0x60 (mach64_dispatch_render_ctl),
         * GLEngine's once-per-frame hook -- NOT here (gldFlushBuffer is not called per swap
         * on this GLEngine).  Legacy back->front blit kept only for the non-surface path. */
        if (!MACH64_PRIV(ctx)->apple_surf_cn && MACH64_PRIV(ctx)->dbuf) mach64_present(ctx);
        MACH64_PRIV(ctx)->frame_new = 1;                   /* next frame: clear Z + back */
        return r;
    }
}

/* ---- feature stubs: present so GLEngine's lookups are non-NULL; only called
 * if the app uses the feature.  Return 0 (NULL/success). --------------- */
#define GLD_STUB(name) int name(void) { gld_log(#name "()\n"); return 0; }
GLD_STUB(gldCreateBuffer)          GLD_STUB(gldDestroyBuffer)
GLD_STUB(gldCreateFence)           GLD_STUB(gldDestroyFence)
GLD_STUB(gldCreatePipelineProgram) GLD_STUB(gldDestroyPipelineProgram)
GLD_STUB(gldModifyPipelineProgram) GLD_STUB(gldRelatePipelineProgram)
GLD_STUB(gldGetPipelineProgramInfo)
GLD_STUB(gldCreateQuery)           GLD_STUB(gldDestroyQuery)
GLD_STUB(gldGetQueryInfo)

/* gldCreateShared / gldDestroyShared -- the context-level SHARED state object (the shared texture-name /
 * display-list namespace that sibling GL contexts inherit).  These are REAL in the reference (ati128gld
 * @0x4d00 / @0x4d60), not stubs: CreateShared allocs a 44-byte struct, zero-inits it, and writes the
 * pointer to *out; DestroyShared frees it.  Our old GLD_STUB took void and NEVER wrote *out, so GLEngine
 * kept an UNINITIALIZED driver shared-handle (garbage it may later dereference) -- a latent corruption
 * source.  We don't share texture storage at the driver level (our VRAM heap is already process-global),
 * so the struct is opaque bookkeeping GLEngine holds and hands back to us; matching the reference's
 * alloc/free contract is what matters.  Signature per the reference: r3 = void **out (no ctx arg). */
int gldCreateShared(void **out)
{
    void *s;
    if (!out) { gld_log("gldCreateShared(out=NULL)\n"); return 0x271e; }  /* ref: error on NULL out */
    *out = NULL;
    s = calloc(1, 0x2c);                                    /* 44-byte shared struct, zero-inited (ref) */
    if (!s) { gld_log("gldCreateShared: calloc failed\n"); return 0x2720; }
    *out = s;
    gld_log("gldCreateShared() -> shared=%p\n", s);
    return 0;
}
int gldDestroyShared(void *shared)
{
    if (!shared) { gld_log("gldDestroyShared(NULL)\n"); return 0x2714; }  /* ref: error on NULL */
    gld_log("gldDestroyShared(%p)\n", shared);
    free(shared);
    return 0;
}
/* ---- Texture path (INVESTIGATION) --------------------------------------
 * Return a valid 0x40-byte texture-object (faithful to the reference
 * gldCreateTexture, ati128gld 0x6838) so GLEngine sets up texturing and emits
 * per-vertex texcoords we can locate; log every texture call + its args to map
 * how GLEngine delivers pixels.  Not a real upload yet. */
void gldCreateTexture(void *ctx, void **out, void *shared)
{
    unsigned char *t = (unsigned char *)calloc(1, 0x40);
    (void)ctx;
    if (t) {
        *(uint32_t *)(t + 0x04) = 3;
        *(uint32_t *)(t + 0x30) = (uint32_t)(uintptr_t)shared;   /* format/shared */
        *(uint32_t *)(t + 0x18) = (uint32_t)(uintptr_t)(t + 0x1c);  /* list head */
        *(uint32_t *)(t + 0x20) = (uint32_t)(uintptr_t)(t + 0x18);
        t[0x38] = 0xff; t[0x39] = 0x23;
    }
    gld_log("gldCreateTexture(out=%p shared=%p) -> texobj=%p\n", (void*)out, shared, (void*)t);
    if (out) *out = t;
}
int gldDeleteTexture(void *ctx, void *t)
{ (void)ctx; gld_log("gldDeleteTexture(%p)\n", t); if (t) { mach64_texture_deleted(t); free(t); } return 0; }
int gldCreateTextureLevel(void *ctx, void *t, uint32_t a3, uint32_t lvl, uint32_t a5, uint32_t a6, uint32_t a7)
{ (void)ctx; gld_log("gldCreateTextureLevel(t=%p a3=0x%x lvl=%u a5=0x%x a6=0x%x a7=0x%x)\n", t,a3,lvl,a5,a6,a7); return 0; }
int gldDeleteTextureLevel(void *ctx, void *t, uint32_t lvl)
{ (void)ctx; gld_log("gldDeleteTextureLevel(t=%p lvl=%u)\n", t, lvl); return 0; }
int gldModifyTexture(void *ctx, void *t, uint32_t a3)
{
    (void)ctx;
    gld_log("gldModifyTexture(t=%p a3=0x%x)\n", t, a3);
    mach64_texture_bound(t);                /* this texture is now the current one */
    if (a3 & 0x80)                          /* image dirty: upload at next draw    */
        mach64_texture_dirtied(t);
    return 0;
}
int gldModifyTextureLevel(void *ctx, void *t, uint32_t a3, uint32_t lvl, uint32_t a5, uint32_t a6, uint32_t a7)
{ (void)ctx; gld_log("gldModifyTextureLevel(t=%p a3=0x%x lvl=%u a5=0x%x a6=0x%x a7=0x%x)\n", t,a3,lvl,a5,a6,a7); return 0; }
int gldGetTextureLevel(void *ctx, void *t, uint32_t a3, uint32_t a4, uint32_t a5, uint32_t a6, uint32_t a7)
{ (void)ctx; gld_log("gldGetTextureLevel(t=%p a3=0x%x a4=0x%x a5=0x%x a6=0x%x a7=0x%x)\n", t,a3,a4,a5,a6,a7); return 0; }
/* glGetTexLevelParameter for the driver.  a3=level, a5=pname (GL enum), a6=out (write the value here),
 * a7=the candidate dimension for a proxy level.
 *
 * ROOT CAUSE of the Chess hang (HW-confirmed): the caller (gluBuild2DMipmaps' proxy probe) seeds *out=0
 * and spins `while(width==0)` until we write a NONZERO width.  Unlike the reference @0x69f4 -- where
 * GLEngine answers WIDTH/HEIGHT from its own bookkeeping and only forwards format/size pnames to the
 * driver -- our GLEngine path forwards the WIDTH query to us AND does not route the proxy glTexImage2D
 * through gldModifyTexture, so the level record (state+0x9c) reads w=h=0.  The candidate size is instead
 * passed in a7 (observed a7=0x100=256 for Chess's 256x256 board texture; the state object holds no dims).
 *   The earlier stub wrote *out=0 (width 0), so every proxy size looked "rejected" and the shrink-loop
 * never terminated (1.07M identical GL_TEXTURE_WIDTH polls, app hung).  Fix: answer WIDTH/HEIGHT with the
 * level record when populated, else the a7 candidate; reject (write 0) only a proxy level that exceeds the
 * Rage Pro's 1024x1024 limit; answer INTERNAL_FORMAT / component sizes explicitly.
 * Level record: t+0x30 -> state; rec at state+0x9c+(baseLOD[s+0x5e]+level)*0x14 (w@0x00 h@0x02 fmt@0x0c);
 * proxy flag = bit 0x100 of *(u16*)state. */
int gldGetTextureLevelInfo(void *ctx, void *t, uint32_t a3, uint32_t a4, uint32_t a5, uint32_t a6, uint32_t a7)
{
    uint32_t state, w = 0, h = 0; uint16_t fmt = 0, flags = 0;
    (void)ctx; (void)a4; (void)a7;
    state = t ? *(const uint32_t *)((const unsigned char *)t + 0x30) : 0;
    if (state >= 0x10000u && state < 0x40000000u) {
        const unsigned char *s = (const unsigned char *)(uintptr_t)state;
        const unsigned char *r = s + 0x9c + ((uint32_t)s[0x5e] + a3) * 0x14;
        flags = *(const uint16_t *)s;                       /* proxy flag lives in bit 0x100 */
        w     = *(const uint16_t *)(r + 0x00);
        h     = *(const uint16_t *)(r + 0x02);
        fmt   = *(const uint16_t *)(r + 0x0c);
    }
    if (a6 < 0x10000u) return 0;                            /* no out pointer -> nothing to do */

    /* GLEngine seeds *out=0 and the caller (gluBuild2DMipmaps' proxy check) spins `while(width==0)`, so we
     * MUST write a nonzero width.  For a PROXY query GLEngine does NOT route the glTexImage2D through our
     * gldModifyTexture, so the level record is empty (w=h=0) and the candidate dimension is passed in a7. */
    { uint32_t cand = a7 ? a7 : 1u;                          /* candidate size for a proxy level (lives in a7) */
      uint32_t rw = w ? w : cand, rh = h ? h : cand;

      /* Proxy fit reject: too big for the Rage Pro's 1024x1024 limit -> write 0 so the caller shrinks. */
      if ((flags & 0x100u) && (rw > 1024u || rh > 1024u)) {
          *(uint32_t *)(uintptr_t)a6 = 0;
          return 0;
      }
      switch (a5) {
      case 0x1000u: *(uint32_t *)(uintptr_t)a6 = rw; break;                   /* GL_TEXTURE_WIDTH  */
      case 0x1001u: *(uint32_t *)(uintptr_t)a6 = rh; break;                   /* GL_TEXTURE_HEIGHT */
      case 0x8071u: *(uint32_t *)(uintptr_t)a6 = 1u; break;                   /* GL_TEXTURE_DEPTH  */
      case 0x1005u: *(uint32_t *)(uintptr_t)a6 = 0u; break;                   /* GL_TEXTURE_BORDER */
      case 0x1003u: *(uint32_t *)(uintptr_t)a6 = fmt ? fmt : 0x1908u; break;  /* INTERNAL_FORMAT (dflt GL_RGBA) */
      case 0x805cu: case 0x805du: case 0x805eu: case 0x805fu:                 /* R/G/B/A_SIZE = 8 bpp */
          *(uint32_t *)(uintptr_t)a6 = 8u; break;
      case 0x8060u: case 0x8061u:                                            /* LUMINANCE/INTENSITY_SIZE */
          *(uint32_t *)(uintptr_t)a6 = 0u; break;
      default: *(uint32_t *)(uintptr_t)a6 = 0u; break;
      }
    }
    return 0;
}
int gldIsTextureResident(void *ctx, void *t)
{ (void)ctx; gld_log("gldIsTextureResident(%p)\n", t); return 1; }
GLD_STUB(gldCreateVertexArray)     GLD_STUB(gldModifyVertexArray)
GLD_STUB(gldFlushVertexArray)      GLD_STUB(gldDestroyVertexArray)
GLD_STUB(gldDestroyMemoryPluginData) GLD_STUB(gldGetMemoryPluginData)
GLD_STUB(gldSetMemoryPluginData)   GLD_STUB(gldFinishMemoryPluginData)
GLD_STUB(gldTestMemoryPluginData)
GLD_STUB(gldFinishObject)          GLD_STUB(gldTestObject)

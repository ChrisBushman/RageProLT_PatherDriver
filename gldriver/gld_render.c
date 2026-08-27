/*
 * gld_render.c - geometry translation + command submission for the mach64 GL
 * plugin.  This is where a GL primitive becomes a kicked DMA batch, reusing the
 * proven pieces:
 *   - mach64_emit.c builds the register-write stream (state + vertices + trigger
 *     + SRC_CNTL=0 terminator),
 *   - m64_cb_write_le() packs it little-endian into the type-4 command buffer
 *     (ctx->fb) -- the big-endian PPC byte-swap boundary,
 *   - new_vertex_buffer(size) hands it to the kext, which drives the ring
 *     (Mach64Accelerator::rotateCommandBuffer),
 *   - wait_for_stamp() fences (gldFinish).
 *
 * This mirrors tools/mach64_ringtest.c exactly, restructured as the gld* path.
 * Apple's GLEngine does T&L and feeds us post-transform vertices; we only pack,
 * assemble triangles, and rasterize.
 *
 * Porting skeleton: needs the 10.3 IOKit headers to build against the bundle.
 */
#include <IOKit/IOKitLib.h>
#include "gld.h"
#include "../common/mach64_abi.h"
#include "../common/mach64_regs.h"

#ifdef GLD_PERF
/* Frame-time profiler: split each frame into GPU/DMA time (blocked in mach64_submit waiting
 * for the engine) vs wait-flip (vblank) vs the remainder (CPU building triangles).  Prints to
 * the app's stderr (survives MACH64_GLD_NOLOG) once every 15 frames from render_ctl. */
#include <sys/time.h>
#include <stdio.h>
unsigned long g_perf_submit_us = 0, g_perf_wait_us = 0, g_perf_copy_us = 0,
              g_perf_dispatch_us = 0, g_perf_present_us = 0;
unsigned long perf_now_us(void)   /* non-static: gld_prims.c times emit_tri into g_perf_dispatch_us */
{ struct timeval t; gettimeofday(&t, 0); return (unsigned long)t.tv_sec * 1000000UL + t.tv_usec; }
#endif

/* ---- geometry: post-T&L float vertex -> hardware m64_vertex ----------
 * GLEngine gives window-space x,y (pixels, top-left origin), z, w, and a packed
 * colour.  The setup engine wants x,y as *4 fixed point (2 sub-pixel bits) and
 * W as float bits (1.0 for the untextured 2D case we support today). */
void mach64_pack_vertex(m64_vertex *out, float x, float y, float z,
                        float w, uint32_t argb)
{
    int xi = (int)(x * 4.0f);
    int yi = (int)(y * 4.0f);
    int zi = (int)(z * 65535.0f + 0.5f);        /* depth [0,1] -> 16-bit Z */
    float wf = (w != 0.0f) ? w : 1.0f;
    if (zi < 0) zi = 0; else if (zi > 0xffff) zi = 0xffff;
    out->x_y = (int32_t)(((xi & 0xffff) << 16) | (yi & 0xffff));
    out->z   = (int32_t)((uint32_t)zi << 15);   /* 17.15: 16-bit depth in [30:15] */
    __builtin_memcpy(&out->w, &wf, 4);
    out->s = out->t = 0;
    out->spec_argb = 0;
    out->argb = argb;
}

/* ---- submission: flush a built command buffer to the kext ------------
 * Appends the terminator, packs LE into the shared type-4 buffer, and kicks the
 * ring via new_vertex_buffer.  Returns the completion stamp. */
uint32_t mach64_submit(gld_context *ctx, m64_cmdbuf *cb)
{
    mach64_priv *p = MACH64_PRIV(ctx);
    uint32_t bufferID = 0;
    size_t bytes;

    m64_emit_terminator(cb);
    if (cb->overflow)              /* stream didn't fit -> drop, don't submit */
        return p->last_stamp;

#ifdef GLD_PERF
    { unsigned long a = perf_now_us();
      bytes = m64_cb_write_le(cb, ctx->fb);   /* uncached type-4 buffer: the suspect copy */
      g_perf_copy_us += perf_now_us() - a; }
#else
    bytes = m64_cb_write_le(cb, ctx->fb);   /* ctx->fb = type-4 command buffer */
#endif

    /* new_vertex_buffer(size) -> kext rotateCommandBuffer builds the descriptor,
     * quiesces, activates bus-mastering, kicks, waits for idle, restores. */
#ifdef GLD_PERF
    { unsigned long a = perf_now_us();
      IOConnectMethodScalarIScalarO(ctx->connect, kMach64Sel_new_vertex_buffer,
                                    1, 1, (int)bytes, &bufferID);
      g_perf_submit_us += perf_now_us() - a; }
#else
    IOConnectMethodScalarIScalarO(ctx->connect, kMach64Sel_new_vertex_buffer,
                                  1, 1, (int)bytes, &bufferID);
#endif
    p->last_stamp = bufferID;
    return bufferID;
}

/* Clear the bound Z buffer to far (0xffff) via a 2D solid fill through the DMA
 * path (proven mach64_fill register sequence).  The 16-bit Z buffer is filled
 * as 32bpp 0xffffffff (= two 0xffff Z values), so the fill is z_pitch/2 wide.
 * Marks the 3D state dirty so the next triangle re-emits the render state. */
void mach64_clear_zbuffer(gld_context *ctx)
{
    mach64_priv *p = MACH64_PRIV(ctx);
    m64_cmdbuf cb;
    uint32_t zoff = p->z_off;
    uint32_t w32  = p->z_pitch / 2;                 /* 16-bit px -> 32-bit px */
    uint32_t h    = *(const uint32_t *)((const char *)ctx + 0x1f4);  /* height */
    /* Skip the Z clear (and its whole DMA submit) when depth testing is off -- there is
     * no depth buffer in use to clear, so it is pure per-frame overhead. */
    if (!(p->hw.z_cntl & MACH64_Z_EN)) return;
    if (!zoff || !w32 || !h) return;
    mach64_flush_batch(ctx);   /* order: pending triangles before the Z clear */
    m64_cb_init(&cb, p->scratch, sizeof(p->scratch) / sizeof(p->scratch[0]));
    m64_reg(&cb, MACH64_Z_CNTL,          0);              /* Z off for the fill */
    m64_reg(&cb, MACH64_SCALE_3D_CNTL,   0);
    m64_reg(&cb, MACH64_GUI_TRAJ_CNTL,   (1u<<0) | (1u<<1));   /* L->R, T->B */
    m64_reg(&cb, MACH64_DP_PIX_WIDTH,    0x60060666u);   /* 32bpp */
    m64_reg(&cb, MACH64_DP_FRGD_CLR,     0xffffffffu);   /* Z far */
    m64_reg(&cb, MACH64_DP_WRITE_MASK,   0xffffffffu);
    m64_reg(&cb, MACH64_DP_MIX,          (3u<<0) | (7u<<16));  /* BKGD_MIX_D|FRGD_MIX_S */
    m64_reg(&cb, MACH64_DP_SRC,          (1u<<0) | (1u<<8));   /* both srcs = FRGD_CLR */
    m64_reg(&cb, MACH64_SC_LEFT_RIGHT,   (0x1fffu<<16) | 0);
    m64_reg(&cb, MACH64_SC_TOP_BOTTOM,   (0x1fffu<<16) | 0);
    m64_reg(&cb, MACH64_CLR_CMP_CNTL,    0);
    m64_reg(&cb, MACH64_DST_OFF_PITCH,   ((w32 / 8) << 22) | (zoff >> 3));
    m64_reg(&cb, MACH64_DST_X_Y,         0);
    m64_reg(&cb, MACH64_DST_WIDTH_HEIGHT, (h << 16) | w32);    /* triggers the fill */
    mach64_submit(ctx, &cb);
    p->dirty |= MACH64_DIRTY_STATE;
}

/* Clear the colour buffer (current render target) to the app's glClearColor.
 * GLEngine never clears our hardware surface (it dispatches no clear entry point --
 * verified with tools/mach64_clearscan), so the driver owns the colour clear just
 * like the Z clear.  glClearColor lives in the gli as RGBA floats at +0x6320
 * (offset recovered by mach64_clearscan).  target_off is the front buffer (single-
 * buffered) or the back buffer (double-buffered); dims are ctx+0x1f0/0x1f4. */
#define GLI_CLEAR_COLOR 0x6320
static uint32_t mach64_gli(gld_context *ctx);   /* defined below */
static uint32_t clampc(float f){ int c=(int)(f*255.0f+0.5f); return c<0?0:(c>255?255:(uint32_t)c); }
void mach64_clear_color(gld_context *ctx)
{
    mach64_priv *p = MACH64_PRIV(ctx);
    uint32_t gli = mach64_gli(ctx);
    uint32_t w = *(const uint32_t *)((const char *)ctx + 0x1f0);
    uint32_t h = *(const uint32_t *)((const char *)ctx + 0x1f4);
    uint32_t col = 0;
    if (!p->target_off || !w || !h) return;
    if (gli) {
        float rf, gf, bf;
        uint32_t rb = mach64_gli_u32(ctx, GLI_CLEAR_COLOR + 0);
        uint32_t gb = mach64_gli_u32(ctx, GLI_CLEAR_COLOR + 4);
        uint32_t bb = mach64_gli_u32(ctx, GLI_CLEAR_COLOR + 8);
        __builtin_memcpy(&rf, &rb, 4);
        __builtin_memcpy(&gf, &gb, 4);
        __builtin_memcpy(&bf, &bb, 4);
        col = (clampc(rf) << 16) | (clampc(gf) << 8) | clampc(bf);
    }
    mach64_fill_rect(ctx, p->target_off, 0, 0, w, h, p->target_pitch, col);
}

/* Emit the colour clear (a full-render-target 2D fill) directly INTO the persistent
 * triangle batch instead of doing its own DMA submit.  Each mach64_submit quiesces and
 * waits the engine, so folding the per-frame clear into the batch means a frame's
 * clear + geometry go out as ONE submit -- a big win for the page-flip present loop.
 * The trailing triangles re-emit full 3D state (p->dirty set), so the 2D->3D transition
 * inside the single command stream is handled. */
void mach64_batch_clear_color(gld_context *ctx)
{
    mach64_priv *p = MACH64_PRIV(ctx);
    uint32_t gli = mach64_gli(ctx);
    uint32_t w = *(const uint32_t *)((const char *)ctx + 0x1f0);
    uint32_t h = *(const uint32_t *)((const char *)ctx + 0x1f4);
    uint32_t off = p->target_off, op = (p->target_pitch / 8) << 22;
    uint32_t col = 0;
    if (!off || !w || !h) return;
    if (gli) {
        float rf, gf, bf;
        uint32_t rb = mach64_gli_u32(ctx, GLI_CLEAR_COLOR + 0);
        uint32_t gb = mach64_gli_u32(ctx, GLI_CLEAR_COLOR + 4);
        uint32_t bb = mach64_gli_u32(ctx, GLI_CLEAR_COLOR + 8);
        __builtin_memcpy(&rf, &rb, 4);
        __builtin_memcpy(&gf, &gb, 4);
        __builtin_memcpy(&bf, &bb, 4);
        col = (clampc(rf) << 16) | (clampc(gf) << 8) | clampc(bf);
    }
    if (!p->batch.buf)
        m64_cb_init(&p->batch, p->batchbuf,
                    sizeof(p->batchbuf) / sizeof(p->batchbuf[0]));
    m64_reg(&p->batch, MACH64_Z_CNTL,           0);
    m64_reg(&p->batch, MACH64_SCALE_3D_CNTL,    0);
    m64_reg(&p->batch, MACH64_GUI_TRAJ_CNTL,    (1u<<0) | (1u<<1));
    m64_reg(&p->batch, MACH64_DP_PIX_WIDTH,     0x60060666u);
    m64_reg(&p->batch, MACH64_DP_FRGD_CLR,      col);
    m64_reg(&p->batch, MACH64_DP_WRITE_MASK,    0xffffffffu);
    m64_reg(&p->batch, MACH64_DP_MIX,           (3u<<0) | (7u<<16));
    m64_reg(&p->batch, MACH64_DP_SRC,           (1u<<0) | (1u<<8));
    m64_reg(&p->batch, MACH64_SC_LEFT_RIGHT,    (0x1fffu<<16) | 0);
    m64_reg(&p->batch, MACH64_SC_TOP_BOTTOM,    (0x1fffu<<16) | 0);
    m64_reg(&p->batch, MACH64_CLR_CMP_CNTL,     0);
    m64_reg(&p->batch, MACH64_DST_OFF_PITCH,    op | (off >> 3));
    m64_reg(&p->batch, MACH64_DST_X_Y,          0);
    m64_reg(&p->batch, MACH64_DST_WIDTH_HEIGHT, (h << 16) | w);
    p->dirty |= MACH64_DIRTY_STATE;
}

/* ---- solid-colour rectangle fill via the 2D GUI engine --------------
 * Fill a w x h pixel box at (x,y) in `off` (byte offset, pitch in 32-bit px) with
 * `color` (0x00RRGGBB).  Same engine setup as the Z clear but with a caller colour
 * and an arbitrary dest.  Used to clear a double-buffer back buffer (GLEngine's own
 * colour clear targets the front) and to paint test rectangles. */
void mach64_fill_rect(gld_context *ctx, uint32_t off, uint32_t x, uint32_t y,
                      uint32_t w, uint32_t h, uint32_t pitch_px, uint32_t color)
{
    mach64_priv *p = MACH64_PRIV(ctx);
    m64_cmdbuf cb;
    uint32_t op = (pitch_px / 8) << 22;
    if (!off || !w || !h) return;
    mach64_flush_batch(ctx);   /* order: pending triangles before the fill */
    m64_cb_init(&cb, p->scratch, sizeof(p->scratch) / sizeof(p->scratch[0]));
    m64_reg(&cb, MACH64_Z_CNTL,           0);
    m64_reg(&cb, MACH64_SCALE_3D_CNTL,    0);
    m64_reg(&cb, MACH64_GUI_TRAJ_CNTL,    (1u<<0) | (1u<<1));    /* L->R, T->B */
    m64_reg(&cb, MACH64_DP_PIX_WIDTH,     0x60060666u);          /* 32bpp */
    m64_reg(&cb, MACH64_DP_FRGD_CLR,      color);
    m64_reg(&cb, MACH64_DP_WRITE_MASK,    0xffffffffu);
    m64_reg(&cb, MACH64_DP_MIX,           (3u<<0) | (7u<<16));   /* BKGD_MIX_D|FRGD_MIX_S */
    m64_reg(&cb, MACH64_DP_SRC,           (1u<<0) | (1u<<8));    /* both srcs = FRGD_CLR */
    m64_reg(&cb, MACH64_SC_LEFT_RIGHT,    (0x1fffu<<16) | 0);
    m64_reg(&cb, MACH64_SC_TOP_BOTTOM,    (0x1fffu<<16) | 0);
    m64_reg(&cb, MACH64_CLR_CMP_CNTL,     0);
    m64_reg(&cb, MACH64_DST_OFF_PITCH,    op | (off >> 3));
    m64_reg(&cb, MACH64_DST_X_Y,          (x << 16) | y);
    m64_reg(&cb, MACH64_DST_WIDTH_HEIGHT, (h << 16) | w);        /* triggers the fill */
    mach64_submit(ctx, &cb);
    p->dirty |= MACH64_DIRTY_STATE;
}

/* Present a double-buffered frame: copy the whole back buffer to the front
 * (scanout) surface with the GUI-engine blit.  No-op when single-buffered.
 * (Superseded by mach64_flip for DB presents -- kept for the fill/blit test path.) */
void mach64_present(gld_context *ctx)
{
    mach64_priv *p = MACH64_PRIV(ctx);
    if (!p->dbuf || !p->back_off || !p->front_off) return;
    mach64_blit_surface(ctx, p->back_off, p->front_off, 0, 0, 0, 0,
                        p->fb_w, p->fb_h, p->fb_pitch);
}

/* Page-flip: switch the CRTC scanout base to the colour buffer at byte offset `off`.
 * The mach64 CRTC offset is hardware double-buffered (loads at vblank), so this is a
 * tear-free swap with NO copy -- unlike mach64_present's blit.  Goes through the kext
 * (selector 0, repurposed to regWrite CRTC_OFF_PITCH); encoding matches DST_OFF_PITCH:
 * (pitch/8 << 22) | (off >> 3). */
void mach64_flip(gld_context *ctx, uint32_t off)
{
    mach64_priv *p = MACH64_PRIV(ctx);
    uint32_t val;
    if (!ctx->connect || !p->fb_pitch || !off) return;
    val = ((p->fb_pitch / 8u) << 22) | (off >> 3);
    IOConnectMethodScalarIScalarO(ctx->connect, kMach64Sel_set_surface, 1, 0, val);
}

/* Block until the pending page flip has retired (loaded at vblank) so the buffer it
 * freed is safe to reuse.  mach64_flip no longer waits -- it just writes CRTC_OFF_PITCH
 * and records the flip pending; this wait is called at the START of the next frame,
 * AFTER GLEngine has already dispatched that frame's geometry, so the ~vblank wait
 * overlaps that dispatch instead of stalling inside the flip. */
void mach64_wait_flip(gld_context *ctx)
{
    if (!ctx->connect) return;
#ifdef GLD_PERF
    { unsigned long a = perf_now_us();
      IOConnectMethodScalarIScalarO(ctx->connect, kMach64Sel_wait_flip, 0, 0);
      g_perf_wait_us += perf_now_us() - a; }
#else
    IOConnectMethodScalarIScalarO(ctx->connect, kMach64Sel_wait_flip, 0, 0);
#endif
}

/* ---- screen-to-screen (VRAM->VRAM) blit via the 2D GUI engine --------
 * Copy a w x h pixel box: source box at (sx,sy) in src_off -> dest box at
 * (dx,dy) in dst_off, both byte offsets in VRAM, pitch in 32-bit pixels.  Recipe
 * ported from the DRI reference swap (drm_mach64_state.c "dispatch swap"):
 * FRGD_SRC_BLIT + FRGD_MIX_S copies the source pixels; DST_WIDTH_HEIGHT triggers.
 * Used for the double-buffer back->front present. */
void mach64_blit_surface(gld_context *ctx, uint32_t src_off, uint32_t dst_off,
                         uint32_t sx, uint32_t sy, uint32_t dx, uint32_t dy,
                         uint32_t w, uint32_t h, uint32_t pitch_px)
{
    mach64_priv *p = MACH64_PRIV(ctx);
    m64_cmdbuf cb;
    uint32_t op = (pitch_px / 8) << 22;
    if (!src_off || !dst_off || !w || !h) return;
    mach64_flush_batch(ctx);   /* order: pending triangles before the blit */
    m64_cb_init(&cb, p->scratch, sizeof(p->scratch) / sizeof(p->scratch[0]));
    m64_reg(&cb, MACH64_Z_CNTL,           0);               /* Z off for the blit */
    m64_reg(&cb, MACH64_SCALE_3D_CNTL,    0);
    m64_reg(&cb, MACH64_GUI_TRAJ_CNTL,    (1u<<0) | (1u<<1));    /* L->R, T->B */
    m64_reg(&cb, MACH64_DP_PIX_WIDTH,     0x60060666u);          /* 32bpp */
    m64_reg(&cb, MACH64_DP_WRITE_MASK,    0xffffffffu);
    m64_reg(&cb, MACH64_DP_MIX,           MACH64_DP_MIX_DS);     /* BKGD_MIX_D|FRGD_MIX_S */
    m64_reg(&cb, MACH64_DP_SRC,           MACH64_DP_SRC_BLIT);   /* FRGD_SRC_BLIT */
    m64_reg(&cb, MACH64_SC_LEFT_RIGHT,    (0x1fffu<<16) | 0);
    m64_reg(&cb, MACH64_SC_TOP_BOTTOM,    (0x1fffu<<16) | 0);
    m64_reg(&cb, MACH64_CLR_CMP_CNTL,     0);
    m64_reg(&cb, MACH64_SRC_OFF_PITCH,    op | (src_off >> 3));
    m64_reg(&cb, MACH64_DST_OFF_PITCH,    op | (dst_off >> 3));
    m64_reg(&cb, MACH64_SRC_WIDTH1,       w);
    m64_reg(&cb, MACH64_SRC_Y_X,          (sx << 16) | sy);
    m64_reg(&cb, MACH64_DST_Y_X,          (dx << 16) | dy);
    m64_reg(&cb, MACH64_DST_WIDTH_HEIGHT, (h << 16) | w);        /* triggers the blit */
    mach64_submit(ctx, &cb);
    p->dirty |= MACH64_DIRTY_STATE;   /* 3D state clobbered -> re-emit next tri */
}

/* ---- one triangle: the core draw path -------------------------------
 * Re-emit the state block if dirty, emit the 3 vertices + the reciprocal-area
 * trigger, then submit.  Returns 0 on a degenerate triangle (dropped).
 *
 * TODO batching: today each triangle is its own DMA kick (correct, matches
 * ringtest).  For throughput, accumulate many triangles into one command buffer
 * and submit once per flush -- the emit core already supports that; just move
 * the submit out of here and flush on buffer-full / gldFlush. */
/* Submit the accumulated triangle batch (if any) as ONE DMA kick, then reset it.
 * Called at frame boundaries (gldFlush/Finish/FlushBuffer) and before any operation
 * that must observe the triangles (fill/blit/clear/texture upload). */
void mach64_flush_batch(gld_context *ctx)
{
    mach64_priv *p = MACH64_PRIV(ctx);
    if (!p->batch.buf || p->batch.n == 0) return;
    mach64_submit(ctx, &p->batch);
    m64_cb_init(&p->batch, p->batchbuf,
                sizeof(p->batchbuf) / sizeof(p->batchbuf[0]));
    /* A submit only guarantees the emitted stream ran; the next batch must re-emit the
     * render state (don't assume it survives the submit's engine quiesce/restore).  Needed
     * now that large geometry splits across several submits within one frame. */
    p->dirty |= MACH64_DIRTY_STATE;
}

int mach64_draw_triangle(gld_context *ctx, const m64_vertex *v0,
                         const m64_vertex *v1, const m64_vertex *v2)
{
    mach64_priv *p = MACH64_PRIV(ctx);
    size_t limit, need;

    /* Frame start (first triangle of a new frame; frame_new armed at attach and at
     * each swap/finish).  The driver owns the clears GLEngine won't do for us --
     * it dispatches no clear entry point, so neither colour nor Z is cleared for
     * our HW surface unless we do it:
     *  - Colour: to the app's glClearColor (front buffer, or the back buffer when
     *    double-buffered -- GLEngine's own clear only ever hits the front).
     *  - Z: GLEngine never touches our depth buffer.
     * (Assumes the app issues glClear once per frame -- the standard pattern.)
     * The clears flush any pending batch first, so ordering holds. */
    if (p->frame_new) {
        mach64_clear_color(ctx);
        if (p->z_off && (p->hw.z_cntl & MACH64_Z_EN))
            mach64_clear_zbuffer(ctx);   /* separate submit; also marks state dirty */
        p->frame_new = 0;
    }

    if (!p->batch.buf)
        m64_cb_init(&p->batch, p->batchbuf,
                    sizeof(p->batchbuf) / sizeof(p->batchbuf[0]));

    /* Flush before this triangle if the state block (~96 dwords when dirty) + the
     * triangle (~64 dwords) might not fit.  The 12-bit bus-master byte-count limit no longer
     * caps the BATCH: the kext now CHAINS the submit across multiple <=4092-byte descriptors
     * (submitCommandBuffer), so a batch can fill the whole command buffer and still DMA-kick
     * ONCE.  Bound the batch by the smaller of the command buffer (fb_size/4) and the batch
     * staging (batchbuf), leaving headroom for the terminator + a final state+triangle.
     * (Was capped at 960 dwords -> ~80 kicks/frame -> ~3fps, almost all submit overhead.) */
    limit = ctx->fb_size / 4;
    if (limit > 32000) limit = 32000;  /* <= batchbuf[32768], with headroom */
    if (limit < 256) limit = 256;
    need = ((p->dirty & MACH64_DIRTY_STATE) ? 96u : 0u) + 64u;
    if (p->batch.n + need > limit - 8)
        mach64_flush_batch(ctx);

    if (p->dirty & MACH64_DIRTY_STATE) {
        m64_emit_state(&p->batch, &p->hw);
        p->dirty &= ~MACH64_DIRTY_STATE;
    }
    if (!m64_emit_triangle(&p->batch, v0, v1, v2))
        return 0;                  /* degenerate: nothing emitted */
    return 1;
}

/* ---- gld vertex-buffer + flush hooks ---------------------------------
 * GLEngine renders through a dispatch table (installed by gldUpdateDispatch).
 * Its primitive callbacks hand us runs of post-T&L vertices; the fan/strip
 * decomposition into triangles + mach64_draw_triangle lives behind those
 * callbacks.  gldAllocVertexBuffer/gldCompleteVertexBuffer are the buffered
 * variant; sketched here against the same submit path. */

void *gldAllocVertexBuffer(gld_context *ctx, uint32_t size)
{
    gld_log("gldAllocVertexBuffer(size=%u)\n", size);
    /* The type-4 command buffer IS our vertex/command staging.  A real impl
     * returns a sub-allocation to fill; single-buffer synchronous today, so we
     * just hand back the base and rely on mach64_submit to serialize. */
    return ctx->fb;
}

int gldCompleteVertexBuffer(gld_context *ctx, void *vb)
{
    gld_log("gldCompleteVertexBuffer(vb=%p)\n", vb);
    (void)vb;
    /* GLEngine has finished filling the primitive; flush whatever the dispatch
     * callbacks assembled.  With per-triangle submit this is already kicked, so
     * this is a no-op fence point today. */
    (void)ctx;
    return 0;
}

void gldFreeVertexBuffer(gld_context *ctx, void *vb) { (void)ctx; (void)vb; }

/* glFlush: ensure submitted work is kicked (already synchronous today). */
int gldFlush(gld_context *ctx) { gld_log("gldFlush()\n"); mach64_flush_batch(ctx); return 0; }

/* glFinish: block until the newest submission has retired. */
int gldFinish(gld_context *ctx)
{
    mach64_priv *p = MACH64_PRIV(ctx);
    gld_log("gldFinish() last_stamp=%u\n", (unsigned)p->last_stamp);
    mach64_flush_batch(ctx);   /* submit any pending triangles before fencing */
    IOConnectMethodScalarIStructureI(ctx->connect, kMach64Sel_wait_for_stamp,
                                     1, 0, (int)p->last_stamp, NULL);
    /* Double-buffered: single-buffer apps end a frame with glFinish (which lands
     * here), so present the back buffer to the front on each finish as well as on
     * the explicit swap (gldFlushBuffer).  Fence again so the blit completes before
     * the app draws the next frame over the back buffer. */
    if (p->dbuf) {
        gld_log("  [dbuf] present back=0x%x -> front=0x%x on gldFinish\n",
                p->back_off, p->front_off);
        mach64_present(ctx);
        IOConnectMethodScalarIStructureI(ctx->connect, kMach64Sel_wait_for_stamp,
                                         1, 0, (int)p->last_stamp, NULL);
    }
    /* End-of-frame for single-buffered apps: arm the per-frame Z clear so the
     * next frame's first triangle resets the depth buffer.  (Double-buffered
     * apps also arm it at the swap in gldFlushBuffer.) */
    p->frame_new = 1;
    return 0;
}

/* Control-slot probes: the reference installs table[0x00,0x08,0x0c,0x10,0x14] as
 * its vertex/render engine (0x10=primitive-mode dispatcher, 0x08=vertex process).
 * We install none, so GLEngine's render calls no-op.  Log which one carries the
 * vertex batch (dump ptr-looking args' memory). */
static void ctl_dump(const char *w, gld_context *ctx, uint32_t a2, uint32_t a3,
                     uint32_t a4, uint32_t a5, uint32_t a6, uint32_t a7)
{
    int i;
    gld_log(">>> %s(ctx=%p a2=0x%x a3=0x%x a4=0x%x a5=0x%x a6=0x%x a7=0x%x)\n",
            w, (void *)ctx, a2, a3, a4, a5, a6, a7);
    { uint32_t v[6]; int k=0; v[k++]=a2; v[k++]=a3; v[k++]=a4; v[k++]=a5; v[k++]=a6; v[k++]=a7;
      for (k=0;k<6;k++) if (v[k]>0x10000 && v[k]<0x40000000) {
        const uint32_t *p=(const uint32_t *)v[k];
        gld_log("     arg%d=0x%x -> %08x %08x %08x %08x\n", k+2, v[k], p[0],p[1],p[2],p[3]);
      } }
    (void)i;
}
int mach64_ctl00(gld_context *c,uint32_t a,uint32_t b,uint32_t d,uint32_t e,uint32_t f,uint32_t g){ctl_dump("table[0x00]",c,a,b,d,e,f,g);return 0;}

/* table[0x08] = GLEngine's per-renderer glReadPixels READBACK hook.  glReadPixels validation
 * (GLEngine+0x2dc4c, verified via tools/mach64_rpdisp) tail-calls [gc+0x78b0] = this, as
 * (rb_ctx, x, y, w, h, format, type, pixels).  It was a no-op stub (7-arg ctl shape that also
 * dropped `pixels` in r10) -> glReadPixels silently did nothing, glGetError==0, px untouched.
 * STEP 1: real 8-arg signature + log the args (confirm the mechanism).  STEP 2 (TODO): read our
 * back-buffer VRAM (g_gld_readback_ctx render_off) at (x,y,w,h), Y-flip, convert to format/type,
 * write `pixels`. */
gld_context *g_gld_readback_ctx = 0;
int mach64_ctl08(void *rb_ctx, int x, int y, int w, int h,
                 unsigned format, unsigned type, void *pixels)
{
    /* GLEngine passes OUR gld_context as rb_ctx (confirmed: rb==g_gld_readback_ctx). */
    gld_context *gc = rb_ctx ? (gld_context *)rb_ctx : g_gld_readback_ctx;
    mach64_priv *p = gc ? MACH64_PRIV(gc) : 0;
    uint32_t height = gc ? *(uint32_t *)((unsigned char *)gc + 0x1f4) : 0;   /* drawable height (gldAttachDrawable) */
    uint32_t toff  = p ? p->target_off   : 0;
    uint32_t tpitch= p ? p->target_pitch : 0;    /* pixels */
    gld_log(">>> READBACK x=%d y=%d w=%d h=%d fmt=0x%x type=0x%x px=%p  target_off=0x%x pitch=%u H=%u vram=%p\n",
            x, y, w, h, format, type, pixels, toff, tpitch, height, gc ? (void *)gc->regs : 0);
    if (!p || !gc->regs || !toff || !tpitch || !height) return 0;   /* can't locate the buffer */
    if (format != 0x1908u /*GL_RGBA*/ || type != 0x1401u /*GL_UNSIGNED_BYTE*/) return 0;  /* RGBA8 only for now */

    mach64_flush_batch(gc);   /* submit any residual geometry; the app's glFinish already fenced */

    /* Read our render target from the whole-VRAM CPU map (gc->regs).  Framebuffer is ARGB8888
     * (dp_pix_width 0x60060666), stored little-endian -> bytes [B,G,R,A].  glReadPixels uses a
     * bottom-left origin, so flip Y against the drawable height. */
    {   const volatile unsigned char *vram = (const volatile unsigned char *)gc->regs;
        unsigned char *out = (unsigned char *)pixels;
        uint32_t pitch_b = tpitch * 4u;
        int row, col;
        for (row = 0; row < h; row++) {
            int fbrow = (int)height - 1 - (y + row);
            if (fbrow < 0) fbrow = 0;
            for (col = 0; col < w; col++) {
                const volatile unsigned char *s =
                    vram + toff + (uint32_t)fbrow * pitch_b + (uint32_t)(x + col) * 4u;
                *out++ = s[2];   /* R */
                *out++ = s[1];   /* G */
                *out++ = s[0];   /* B */
                *out++ = s[3];   /* A */
            }
        }
    }
    gld_log("     READBACK filled %dx%d from target_off=0x%x pitch=%u H=%u\n", w, h, toff, tpitch, height);
    return 0;
}
int mach64_ctl0c(gld_context *c,uint32_t a,uint32_t b,uint32_t d,uint32_t e,uint32_t f,uint32_t g){ctl_dump("table[0x0c]",c,a,b,d,e,f,g);return 0;}
int mach64_ctl10(gld_context *c,uint32_t a,uint32_t b,uint32_t d,uint32_t e,uint32_t f,uint32_t g){ctl_dump("table[0x10]",c,a,b,d,e,f,g);return 0;}
int mach64_ctl14(gld_context *c,uint32_t a,uint32_t b,uint32_t d,uint32_t e,uint32_t f,uint32_t g){ctl_dump("table[0x14]",c,a,b,d,e,f,g);return 0;}

/* table[0x04] = THE flush entry.  GLEngine's render flow calls this to dispatch
 * accumulated geometry (ref gldInitDispatch installs it -> sub_cf68 -> sub_c3c0,
 * the driver's flush engine).  Our gldInitDispatch stubbed it, so GLEngine hit a
 * no-op and nothing rendered.  First: log that it's called + its args, to find
 * the vertex batch GLEngine passes; then wire it to our DMA path. */
/* Find the gli context base from the driver ctx: ctx+0x10 points into the gli
 * (~+0x3610); scan back for the object whose [+0x7898] back-points to ctx. */
static uint32_t mach64_gli(gld_context *ctx)
{
    uint32_t p10 = *(const uint32_t *)((const char *)ctx + 0x10), off;
    for (off = 0; off <= 0x8000; off += 4) {
        uint32_t g = p10 - off;
        if (g < 0x10000 || g >= 0x40000000) break;
        if (*(const uint32_t *)(uintptr_t)(g + 0x7898) == (uint32_t)(uintptr_t)ctx)
            return g;
    }
    return 0;
}

/* Read a 32-bit gli field (0 if the gli can't be located). */
uint32_t mach64_gli_u32(gld_context *ctx, uint32_t off)
{
    uint32_t g = mach64_gli(ctx);
    return g ? *(const uint32_t *)(uintptr_t)(g + off) : 0;
}

/* Follow the app's GL depth state.  GLEngine stores it in the gli (recovered by
 * diffing depth configs, tools/mach64_depthstate):
 *   gli+0x6340 bit24 = GL_DEPTH_TEST enable
 *   gli+0x633c >>16  = glDepthFunc GL enum (0x0200..0x0207)
 *   gli+0x63b8 bit24 = glDepthMask
 * Translate to Z_CNTL (mach64 test func at [6:4], Z_MASK_EN bit8). */
void mach64_sync_depth_from_gli(gld_context *ctx)
{
    mach64_priv *p = MACH64_PRIV(ctx);
    uint32_t gli = mach64_gli(ctx);
    uint32_t z = 0;
    if (!gli) return;
    if ((*(const uint32_t *)(uintptr_t)(gli + 0x6340)) & 0x01000000u) {   /* enabled */
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

/* table[0x04] = the STATE flush.  Geometry flows through the primitive slots;
 * here we sync the app's depth state (glEnable/glDepthFunc/glDepthMask) into
 * Z_CNTL before the batch renders. */
int mach64_flush(gld_context *ctx, uint32_t a2, uint32_t a3, uint32_t a4,
                 uint32_t a5, uint32_t a6, uint32_t a7)
{
    (void)a2; (void)a3; (void)a4; (void)a5; (void)a6; (void)a7;
    mach64_sync_depth_from_gli(ctx);
    return 0;
}

/* Dispatch slots 0x4c (buffer-complete) and 0x60 (render-control) -- the
 * reference installs these; we didn't, and immediate-mode geometry isn't
 * reaching our primitive slots.  Log them to map what GLEngine calls to flush. */
void mach64_dispatch_complete(gld_context *ctx, const void *v, uint32_t n)
{
    gld_log("mach64_dispatch_complete(0x4c) v=%p n=%u\n", v, n);
    (void)ctx;
}
/* Dispatch slot 0x60 = GLEngine's per-frame render-control / BUFFER-SWAP hook.
 * aglSwapBuffers routes here once per frame (args are a fixed control block -- identical
 * every call, NOT geometry: the transformed geometry now reaches our per-primitive slots
 * via the DB-gate in gldAttachDrawable).  The reference driver no-ops this slot and relies
 * on the WindowServer to page-flip; our minimal kext can't, so we drive the DOUBLE-BUFFER
 * present cycle HERE -- exactly once per frame.
 * (This present USED to run in gldUpdateDispatch on the assumption that gUD was the frame
 * boundary.  True for our single-state test tools -- gUD fired ~1x/frame -- but a real app
 * like atlantis re-installs the dispatch table per state change, ~29-48x/frame, so we
 * page-flipped + waited vblank dozens of times per frame and the demo crawled at <1fps.
 * Slot 0x60 is the real once-per-frame signal.) */
void mach64_dispatch_render_ctl(gld_context *ctx, uint32_t a4, uint32_t a5,
                                uint32_t a6, uint32_t a7, uint32_t a8)
{
    (void)a4; (void)a5; (void)a6; (void)a7; (void)a8;
    {   mach64_priv *p = MACH64_PRIV(ctx);
        if (p && p->dbuf && p->back_off) {
#ifdef GLD_PERF
            unsigned long _pres0 = perf_now_us();
#endif
            /* DEFERRED-WAIT page-flip present (see the retired gldUpdateDispatch comment):
             * wait the previous frame's flip here (overlapped with GLEngine's dispatch of
             * this frame), submit this frame's clear+geometry to render_off, flip the CRTC
             * to it (loads at vblank, tear-free, zero-copy), swap roles, then clear the
             * newly-hidden buffer for the next frame. */
            if (p->win_mode) {
                /* WINDOWED present (coexistence probe): flush the w x h scene to the back
                 * buffer, then BLIT it into the LIVE front at (win_x,win_y).  No page-flip --
                 * the desktop stays visible and the WindowServer keeps compositing while our
                 * bus-master blit + geometry DMA run.  We always render into `back`, never swap. */
                mach64_flush_batch(ctx);
                mach64_blit_surface(ctx, p->back_off, p->front_off,
                                    0, 0, p->win_x, p->win_y, p->fb_w, p->fb_h, p->fb_pitch);
                if (p->z_off) mach64_clear_zbuffer(ctx);
                mach64_batch_clear_color(ctx);
                p->frame_new = 0;
            } else {
                mach64_wait_flip(ctx);
                mach64_flush_batch(ctx);
                mach64_flip(ctx, p->render_off);
                p->render_off = (p->render_off == p->front_off) ? p->back_off
                                                                : p->front_off;
                mach64_set_render_target(ctx, p->render_off, p->fb_pitch);
                if (p->z_off) mach64_clear_zbuffer(ctx);   /* own submit: overlaps vblank dead time */
                mach64_batch_clear_color(ctx);             /* folded into next frame's batch */
                p->frame_new = 0;
            }
#ifdef GLD_PERF
            {   static unsigned long last = 0; static int fr = 0;
                unsigned long now = perf_now_us();
                g_perf_present_us += now - _pres0;   /* our present cycle (flip+clears+waitflip) */
                if (last) {
                    unsigned long wall = now - last;
                    unsigned long ours = g_perf_dispatch_us + g_perf_present_us;   /* all OUR driver code */
                    unsigned long gle  = wall > ours ? wall - ours : 0;            /* the rest = GLEngine T&L */
                    if ((++fr % 5) == 0)
                        fprintf(stderr,
                            "PERF wall=%lu OUR[emit=%lu present=%lu] GLengine(T&L)=%lu us [GL %lu%%] (copy=%lu submit=%lu wait=%lu)\n",
                            wall, g_perf_dispatch_us, g_perf_present_us, gle,
                            wall ? (gle * 100UL / wall) : 0,
                            g_perf_copy_us, g_perf_submit_us, g_perf_wait_us);
                }
                last = now;
                g_perf_dispatch_us = 0; g_perf_present_us = 0;
                g_perf_submit_us = 0; g_perf_wait_us = 0; g_perf_copy_us = 0;
            }
#endif
        }
    }
}

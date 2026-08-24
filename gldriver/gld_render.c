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

/* ---- geometry: post-T&L float vertex -> hardware m64_vertex ----------
 * GLEngine gives window-space x,y (pixels, top-left origin), z, w, and a packed
 * colour.  The setup engine wants x,y as *4 fixed point (2 sub-pixel bits) and
 * W as float bits (1.0 for the untextured 2D case we support today). */
void mach64_pack_vertex(m64_vertex *out, float x, float y, float z,
                        float w, uint32_t argb)
{
    int xi = (int)(x * 4.0f);
    int yi = (int)(y * 4.0f);
    float wf = (w != 0.0f) ? w : 1.0f;
    (void)z;                       /* depth off for now (see gld_state.c) */
    out->x_y = (int32_t)(((xi & 0xffff) << 16) | (yi & 0xffff));
    out->z   = 0;                  /* TODO: ((int)(z*ZSCALE))<<15 with depth */
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

    bytes = m64_cb_write_le(cb, ctx->fb);   /* ctx->fb = type-4 command buffer */

    /* new_vertex_buffer(size) -> kext rotateCommandBuffer builds the descriptor,
     * quiesces, activates bus-mastering, kicks, waits for idle, restores. */
    IOConnectMethodScalarIScalarO(ctx->connect, kMach64Sel_new_vertex_buffer,
                                  1, 1, (int)bytes, &bufferID);
    p->last_stamp = bufferID;
    return bufferID;
}

/* ---- one triangle: the core draw path -------------------------------
 * Re-emit the state block if dirty, emit the 3 vertices + the reciprocal-area
 * trigger, then submit.  Returns 0 on a degenerate triangle (dropped).
 *
 * TODO batching: today each triangle is its own DMA kick (correct, matches
 * ringtest).  For throughput, accumulate many triangles into one command buffer
 * and submit once per flush -- the emit core already supports that; just move
 * the submit out of here and flush on buffer-full / gldFlush. */
int mach64_draw_triangle(gld_context *ctx, const m64_vertex *v0,
                         const m64_vertex *v1, const m64_vertex *v2)
{
    mach64_priv *p = MACH64_PRIV(ctx);
    m64_cmdbuf cb;

    m64_cb_init(&cb, p->scratch, sizeof(p->scratch) / sizeof(p->scratch[0]));

    if (p->dirty & MACH64_DIRTY_STATE) {
        m64_emit_state(&cb, &p->hw);
        p->dirty &= ~MACH64_DIRTY_STATE;
    }
    if (!m64_emit_triangle(&cb, v0, v1, v2))
        return 0;                  /* degenerate: nothing emitted worth kicking */

    mach64_submit(ctx, &cb);
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
int gldFlush(gld_context *ctx) { gld_log("gldFlush()\n"); (void)ctx; return 0; }

/* glFinish: block until the newest submission has retired. */
int gldFinish(gld_context *ctx)
{
    mach64_priv *p = MACH64_PRIV(ctx);
    gld_log("gldFinish() last_stamp=%u\n", (unsigned)p->last_stamp);
    IOConnectMethodScalarIStructureI(ctx->connect, kMach64Sel_wait_for_stamp,
                                     1, 0, (int)p->last_stamp, NULL);
    return 0;
}

/* table[0x04] = THE flush entry.  GLEngine's render flow calls this to dispatch
 * accumulated geometry (ref gldInitDispatch installs it -> sub_cf68 -> sub_c3c0,
 * the driver's flush engine).  Our gldInitDispatch stubbed it, so GLEngine hit a
 * no-op and nothing rendered.  First: log that it's called + its args, to find
 * the vertex batch GLEngine passes; then wire it to our DMA path. */
int mach64_flush(gld_context *ctx, uint32_t a2, uint32_t a3, uint32_t a4,
                 uint32_t a5, uint32_t a6, uint32_t a7)
{
    gld_log("*** mach64_flush (table[0x04]) CALLED  ctx=%p args=0x%x 0x%x 0x%x 0x%x 0x%x 0x%x\n",
            (void *)ctx, a2, a3, a4, a5, a6, a7);
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
void mach64_dispatch_render_ctl(gld_context *ctx, const void *v, uint32_t n)
{
    gld_log("mach64_dispatch_render_ctl(0x60) v=%p n=%u\n", v, n);
    (void)ctx;
}

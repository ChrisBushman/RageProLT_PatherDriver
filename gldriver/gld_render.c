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

    bytes = m64_cb_write_le(cb, ctx->fb);   /* ctx->fb = type-4 command buffer */

    /* new_vertex_buffer(size) -> kext rotateCommandBuffer builds the descriptor,
     * quiesces, activates bus-mastering, kicks, waits for idle, restores. */
    IOConnectMethodScalarIScalarO(ctx->connect, kMach64Sel_new_vertex_buffer,
                                  1, 1, (int)bytes, &bufferID);
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
    if (!zoff || !w32 || !h) return;
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

    /* Per-frame Z clear: on the first triangle of a new frame, clear the depth
     * buffer to far.  frame_new is armed at drawable attach and at each buffer
     * swap (gldFlushBuffer).  GLEngine clears colour itself but never touches
     * our Z buffer, so the driver owns the depth clear. */
    if (p->z_off && p->frame_new) {
        mach64_clear_zbuffer(ctx);   /* separate submit; also marks state dirty */
        p->frame_new = 0;
    }

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
int mach64_ctl08(gld_context *c,uint32_t a,uint32_t b,uint32_t d,uint32_t e,uint32_t f,uint32_t g){ctl_dump("table[0x08]",c,a,b,d,e,f,g);return 0;}
int mach64_ctl0c(gld_context *c,uint32_t a,uint32_t b,uint32_t d,uint32_t e,uint32_t f,uint32_t g){ctl_dump("table[0x0c]",c,a,b,d,e,f,g);return 0;}
int mach64_ctl10(gld_context *c,uint32_t a,uint32_t b,uint32_t d,uint32_t e,uint32_t f,uint32_t g){ctl_dump("table[0x10]",c,a,b,d,e,f,g);return 0;}
int mach64_ctl14(gld_context *c,uint32_t a,uint32_t b,uint32_t d,uint32_t e,uint32_t f,uint32_t g){ctl_dump("table[0x14]",c,a,b,d,e,f,g);return 0;}

/* table[0x04] = THE flush entry.  GLEngine's render flow calls this to dispatch
 * accumulated geometry (ref gldInitDispatch installs it -> sub_cf68 -> sub_c3c0,
 * the driver's flush engine).  Our gldInitDispatch stubbed it, so GLEngine hit a
 * no-op and nothing rendered.  First: log that it's called + its args, to find
 * the vertex batch GLEngine passes; then wire it to our DMA path. */
int mach64_flush(gld_context *ctx, uint32_t a2, uint32_t a3, uint32_t a4,
                 uint32_t a5, uint32_t a6, uint32_t a7)
{
    m64_vertex v0, v1, v2;
    int i;
    (void)a6; (void)a7;
    gld_log("*** mach64_flush args: a2=0x%x a3=0x%x a4=0x%x a5=0x%x a6=0x%x a7=0x%x\n",
            a2, a3, a4, a5, a6, a7);
    /* Decisive: dump EACH arg (r4..r9) deref'd as hex AND float.  The reference
     * sub_c3c0 reads its r4 as `lfs 0(r4)` -- a float vertex coord.  Find which
     * arg holds recognizable geometry: NDC -0.5=0xbf000000 0.5=0x3f000000 0.0=0,
     * or screen 256=0x43800000 512=0x44000000 768=0x44400000. */
    {   uint32_t args[6]; int k; args[0]=a2;args[1]=a3;args[2]=a4;
        args[3]=a5;args[4]=a6;args[5]=a7;
        for (k = 0; k < 6; k++) { uint32_t val = args[k];
            if (val > 0x2000 && val < 0x40000000) {
                const uint32_t *p = (const uint32_t *)val;
                const float    *f = (const float *)val;
                gld_log("  arg%d=0x%x deref:\n", k+2, val);
                for (i = 0; i < 6; i++)
                    gld_log("      [%d] hex=0x%08x  float=%d.%04d\n", i, p[i],
                            (int)f[i], (int)((f[i]<0?-f[i]:f[i])*10000)%10000);
            } else gld_log("  arg%d=0x%x (not a ptr)\n", k+2, val);
        }
    }
    { uint32_t o10 = *(const uint32_t *)((const char *)ctx + 0x10);
      gld_log("   ctx+0x10(gli)=0x%x\n", o10); }
    /* table[0x04] is the STATE flush.  Real geometry now flows through the
     * primitive slots (render_tri_strip -> emit_tri -> mach64_read_vertex ->
     * mach64_draw_triangle), so the hard-coded milestone triangle is retired. */
    (void)v0; (void)v1; (void)v2;
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

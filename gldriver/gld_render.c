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
 * (scanout) surface with the GUI-engine blit.  No-op when single-buffered. */
void mach64_present(gld_context *ctx)
{
    mach64_priv *p = MACH64_PRIV(ctx);
    if (!p->dbuf || !p->back_off || !p->front_off) return;
    mach64_blit_surface(ctx, p->back_off, p->front_off, 0, 0, 0, 0,
                        p->fb_w, p->fb_h, p->fb_pitch);
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
int mach64_draw_triangle(gld_context *ctx, const m64_vertex *v0,
                         const m64_vertex *v1, const m64_vertex *v2)
{
    mach64_priv *p = MACH64_PRIV(ctx);
    m64_cmdbuf cb;

    /* Frame start (first triangle of a new frame; frame_new armed at attach and at
     * each swap/finish).  The driver owns two clears GLEngine won't do for us:
     *  - Z buffer: GLEngine never touches our depth buffer.
     *  - Colour, but ONLY when double-buffered: GLEngine's glClear targets the
     *    FRONT surface, so our off-screen back buffer would otherwise blit stale
     *    VRAM as the background (verified: garbage background, Picture 23).  Clear
     *    it to black.  TODO: read the app's real glClearColor from the gli instead
     *    of assuming black. */
    if (p->frame_new &&
        (p->dbuf || (p->z_off && (p->hw.z_cntl & MACH64_Z_EN)))) {
        if (p->dbuf)
            mach64_fill_rect(ctx, p->back_off, 0, 0, p->fb_w, p->fb_h,
                             p->fb_pitch, 0x00000000u);
        if (p->z_off && (p->hw.z_cntl & MACH64_Z_EN))
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
int mach64_ctl08(gld_context *c,uint32_t a,uint32_t b,uint32_t d,uint32_t e,uint32_t f,uint32_t g){ctl_dump("table[0x08]",c,a,b,d,e,f,g);return 0;}
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
/* Dispatch slot 0x60 = the SOLE render entry GLEngine uses for a DOUBLE-BUFFERED
 * context (the per-primitive slots 0x18-0x3c and control slots 0x00-0x14 never
 * fire for DB -- verified).  RE status (tools/mach64_dbufanim, -DGLD_DBUF):
 *   r4 = a state buffer (const across frames though the geometry moves -> NOT the
 *        vertex data; no heap pointer inside it),
 *   r5 = a small count (4,6,8 seen),  r6 = 0xa8,  r7 = 0xc03,
 *   r8 = a GLEngine FUNCTION pointer (PPC code: mflr/mtlr/blr) -- a callback.
 * So the transformed geometry is reached via the r8 callback / an internal buffer,
 * not the args -- recovering it needs GLEngine disassembly.  Until then a DB
 * context renders nothing; DB advertising stays gated OFF (see gld_pixelformat). */
void mach64_dispatch_render_ctl(gld_context *ctx, uint32_t a4, uint32_t a5,
                                uint32_t a6, uint32_t a7, uint32_t a8)
{
    gld_log("render_ctl(0x60) r4=0x%x r5=0x%x r6=0x%x r7=0x%x r8=0x%x (DB render -- TODO)\n",
            a4, a5, a6, a7, a8);
    (void)ctx;
}

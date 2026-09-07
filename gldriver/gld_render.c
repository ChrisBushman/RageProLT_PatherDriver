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
#include <mach/mach.h>   /* mach_task_self for the MapMemory submit trigger */
#include "gld.h"
#include "../common/mach64_abi.h"
#include "../common/atiragepro_abi.h"   /* Path-B: cmdbuf header + finish selector */
#include "../common/mach64_regs.h"
#include <string.h>   /* memset (mach64_pathb_present) */

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

/* ---- submission (PATH B): drain a command buffer through Apple's ATIRagePro kext ----
 * Our m64_cmdbuf is the mach64 DMA PACKET format {header=ADRINDEX(reg)|(n-1)<<16, n vals}.
 * Apple's submit_buffer (@0x12730) PIO-drains flat {reg,val} u32 PAIRS instead -- reg is
 * the RAW byte offset, and the kext stwbrx-byte-reverses val into the little-endian
 * register window.  So we CONVERT the packet stream to flat pairs (undoing ADRINDEX),
 * write them into the type-1 command region's VendorCommandBufferHeader body[], set
 * count + the SUBMIT bit, and call finish() (sel 8) -- which triggers the drain and
 * fences on engine-idle.  Values go in NATIVE (big-endian) order: the kext does the
 * byte-swap, NOT us (unlike the old DMA path's m64_cb_write_le).
 * ADRINDEX inverse: block1 (reg 0x000-0x3ff) -> adr has bit 0x100, reg=(adr&0xff)<<2;
 *                   block0 (reg 0x400-0x7ff) -> no 0x100 bit, reg=((adr&0xff)<<2)+0x400. */
uint32_t mach64_submit(gld_context *ctx, m64_cmdbuf *cb)
{
    mach64_priv *p = MACH64_PRIV(ctx);
    /* HW-PROVEN Path-B recipe (2026-09-06):
     *  - the REAL command buffer is the type-2 map (ATIRPRO_MEM_CONTEXT -> ctx->fb == kext ctx[0xdc]),
     *    NOT type-1 (which is the surface buffer).
     *  - Apple's submit_buffer writes MMIO[this[0x550]+reg] and this[0x550] == real_reg_base+0x400,
     *    so the {reg,val} offset for register R is (R - 0x400).
     *  - the DRAIN is triggered by IOConnectMapMemory(type-1) (clientMemoryForType ->
     *    submit_context_buffer -> submit_buffer PIO), NOT finish(sel8) (which only fences). */
    volatile ATIRProCmdBufHeader *hdr = (volatile ATIRProCmdBufHeader *)ctx->fb;
    volatile uint32_t *body;
    uint32_t cap, out = 0;
    size_t i = 0;
    vm_address_t sub = 0; vm_size_t subsz = 0;

    m64_emit_terminator(cb);
    if (cb->overflow || !hdr)      /* stream overflowed, or command buffer not mapped yet */
        return p->last_stamp;

    body = hdr->body;
    cap  = hdr->capacity;          /* body capacity in DWORDS = (regionsize-0x20)/4 */
    while (i < cb->n) {
        uint32_t h   = cb->buf[i++];
        uint32_t adr = h & 0xffffu;
        uint32_t cnt = (h >> 16) + 1, k;
        uint32_t base = (adr & 0x100u) ? ((adr & 0xffu) << 2)
                                       : (((adr & 0xffu) << 2) + 0x400u);
        for (k = 0; k < cnt && i < cb->n; k++) {
            /* `reg` is our DRI-header register offset (0x000-0x7fc).  Apple's submit_buffer does
             * `stwbrx val, this[0x550], off`, and this[0x550] == the mach64 register BAR + 0x400
             * (the GUI/block-1 sub-aperture base) -- HW-PROVEN by the working submits: Z_CNTL is
             * DRI 0x54c and lands via off 0x14c (=0x54c-0x400); SCALE_3D_CNTL DRI 0x5fc via off
             * 0x1fc.  So the UNIVERSAL offset is off = reg - 0x400 for EVERY register.  For the 3D
             * SETUP/VERTEX block-0 registers (DRI 0x240-0x304, below 0x400) this yields a value
             * like 0x240-0x400 = 0xFFFFFE40; PPC stwrx adds it mod 2^32 so the EA wraps to
             * this[0x550]-0x1c0 = BAR+0x240 = real VERTEX_1_S -- IN the register BAR, not before
             * it (the earlier "block-0 unreachable/panic" fear assumed base 0x000; base is 0x400,
             * so the wrap is safe).  The prior `reg>=0x400?reg-0x400:reg` wrote block-0 regs at
             * this[0x550]+0x240 = real 0x640 (WRONG registers) -> the setup engine got no vertex
             * data -> the triangle was submitted but rasterized nothing (2D fills still worked). */
            uint32_t reg = base + k * 4u;
            if (out + 2 > cap) { cb->overflow = 1; break; }   /* body full -> drop rest */
            body[out++] = reg - 0x400u;    /* Apple MMIO offset (base = BAR+0x400) */
            body[out++] = cb->buf[i++];    /* val (kext byte-reverses to the LE regs) */
        }
        if (cb->overflow) break;
    }
    hdr->count  = out;                                 /* # body dwords for the kext to drain */
    hdr->flags |= ATIRPRO_CMDBUF_FLAG_SUBMIT;          /* bit0 = VALID/SUBMIT */
    { static int slog = 0; if (slog < 5) {
        gld_log("  [submit #%d] out=%u dw (cap=%u) off0=0x%x val0=0x%x -> drain...\n",
                slog, out, cap, out ? body[0] : 0, out ? body[1] : 0); slog++; } }
    /* SUBMIT = map type-1 (surface buffer): clientMemoryForType drains our command buffer through
     * the engine.  Map+unmap (the drain is the side effect of the map call). */
    if (!IOConnectMapMemory(ctx->connect, ATIRPRO_MEM_COMMAND, mach_task_self(),
                            &sub, &subsz, kIOMapAnywhere | kIOMapInhibitCache) && sub)
        IOConnectUnmapMemory(ctx->connect, ATIRPRO_MEM_COMMAND, mach_task_self(), sub);
    p->last_stamp = hdr->stamp;                        /* kext-updated submit stamp */
    { static int slog2 = 0; if (slog2 < 5) {
        gld_log("  [submit #%d] drained OK stamp=0x%x\n", slog2, p->last_stamp); slog2++; } }
    return p->last_stamp;
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

/* Batched Z-buffer clear (companion to mach64_batch_clear_color): append a 2D solid fill of the
 * 16-bit depth buffer to 0xffff (far) INTO p->batch, so the colour clear + Z clear + geometry all
 * drain as ONE command stream / ONE type-1 trigger.  (mach64_clear_zbuffer does its own submit --
 * using it inside frame_start would split the frame into multiple submits and let the trailing
 * backing copy clobber the render, the black-screen failure mode.)  The 16-bit Z is filled 32bpp
 * as 0xffffffff (two 0xffff values), so the fill is z_pitch/2 wide. */
void mach64_batch_clear_zbuffer(gld_context *ctx)
{
    mach64_priv *p = MACH64_PRIV(ctx);
    uint32_t zoff = p->z_off;
    uint32_t w32  = p->z_pitch / 2;                 /* 16-bit px -> 32-bit px */
    uint32_t h    = *(const uint32_t *)((const char *)ctx + 0x1f4);
    if (!zoff || !w32 || !h) return;
    if (!p->batch.buf)
        m64_cb_init(&p->batch, p->batchbuf,
                    sizeof(p->batchbuf) / sizeof(p->batchbuf[0]));
    m64_reg(&p->batch, MACH64_Z_CNTL,           0);            /* Z off for the fill itself */
    m64_reg(&p->batch, MACH64_SCALE_3D_CNTL,    0);
    m64_reg(&p->batch, MACH64_GUI_TRAJ_CNTL,    (1u<<0) | (1u<<1));
    m64_reg(&p->batch, MACH64_DP_PIX_WIDTH,     0x60060666u);
    m64_reg(&p->batch, MACH64_DP_FRGD_CLR,      0xffffffffu);  /* Z far */
    m64_reg(&p->batch, MACH64_DP_WRITE_MASK,    0xffffffffu);
    m64_reg(&p->batch, MACH64_DP_MIX,           (3u<<0) | (7u<<16));
    m64_reg(&p->batch, MACH64_DP_SRC,           (1u<<0) | (1u<<8));
    m64_reg(&p->batch, MACH64_SC_LEFT_RIGHT,    (0x1fffu<<16) | 0);
    m64_reg(&p->batch, MACH64_SC_TOP_BOTTOM,    (0x1fffu<<16) | 0);
    m64_reg(&p->batch, MACH64_CLR_CMP_CNTL,     0);
    m64_reg(&p->batch, MACH64_DST_OFF_PITCH,    ((w32 / 8) << 22) | (zoff >> 3));
    m64_reg(&p->batch, MACH64_DST_X_Y,          0);
    m64_reg(&p->batch, MACH64_DST_WIDTH_HEIGHT, (h << 16) | w32);
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

/* IOAccelSurfaceData returned by surface_write_lock (layout LIVE-confirmed in
 * apple-glcontext-protocol.md: +00 userspace-writable backing ptr, +10 pitch bytes,
 * +14 w, +18 h, +1c bpp). */
typedef struct {
    uint32_t address;                 /* +00 directly-writable userspace backing ptr */
    uint32_t r04, r08, r0c;           /* +04..+0f                                    */
    uint32_t pitch;                   /* +10 pitch in bytes                          */
    uint32_t width;                   /* +14                                         */
    uint32_t height;                  /* +18                                         */
    uint32_t bpp;                     /* +1c bytes/pixel                             */
    uint32_t tail[16];                /* +20.. (idx@+48 etc.)                        */
} apple_surf_data;

/* PATH B present: composite the bound surface to the live scanout via Apple's
 * surface_flush (surface sel 10) -- the LIVE-confirmed, race-free PIO 2D blit
 * (build_swap, backing->scanout, NO CRTC touch).  write_lock/unlock bracket the
 * surface's CPU-coherence protocol and hand back the backing geometry.  For now this
 * also LOGS the backing so the first HW run resolves the open question: is a GL-bound
 * surface's backing VRAM (engine renders straight in) or system memory (needs a
 * render->backing copy bridge)?  The pixel bridge is finished once HW answers that. */
/* Extract the app's glClearColor from the gli as 0xAARRGGBB (alpha forced 0xff). */
static uint32_t mach64_clear_argb(gld_context *ctx)
{
    uint32_t gli = mach64_gli(ctx);
    float rf = 0.0f, gf = 0.0f, bf = 0.0f;
    if (gli) {
        uint32_t rb = mach64_gli_u32(ctx, GLI_CLEAR_COLOR + 0);
        uint32_t gb = mach64_gli_u32(ctx, GLI_CLEAR_COLOR + 4);
        uint32_t bb = mach64_gli_u32(ctx, GLI_CLEAR_COLOR + 8);
        __builtin_memcpy(&rf, &rb, 4); __builtin_memcpy(&gf, &gb, 4); __builtin_memcpy(&bf, &bb, 4);
    }
    return 0xff000000u | (clampc(rf) << 16) | (clampc(gf) << 8) | clampc(bf);
}

/* PATH B present (Increment 2, HW-PROVEN 2026-09-06): the engine has ALREADY rendered into the
 * surface's VRAM buffer (surf[0x12c] == p->target_off) via mach64_submit.  Present = surface_flush
 * ONLY: flush_surface -> build_swap blits the buffer to the live scanout.  Do NOT write_lock /
 * CPU-fill the backing -- that sets the surface dirty bit, so clientMemoryForType/flush would copy
 * the backing OVER our engine render and clobber it (the prime cleared that bit; keep it clear). */
void mach64_pathb_present(gld_context *ctx)
{
    mach64_priv *p = MACH64_PRIV(ctx);
    io_connect_t surf = (io_connect_t)p->apple_surf_cn;
    if (!surf) return;
    IOConnectMethodScalarIScalarO(surf, kATIRProSurf_surface_flush, 2, 0, 1u, 0u);
}

/* After the surface is bound: (1) PRIME it -- surface write_lock(14)+unlock(15)+flush(10) allocates
 * the VRAM render buffer AND clears the surface dirty bit (so no backing->buffer copy clobbers our
 * engine renders).  (2) Fetch the render-target off_pitch surf[0x12c][0x28] and decode it into
 * p->target_off (byte offset) / p->target_pitch (pixels) so the clears/fills target it.
 * NB: rt is read via the sel-14 dual-peek RE patch (this=ATIR3GLContext, ctx[0x10c]=surf); a shipping
 * build should expose it via a dedicated get_render_target selector instead. */
/* Allocate `size` bytes of VRAM from Apple's own best-fit allocator via the multiplexed new_texture
 * selector (sel 10, op=ALLOC).  The kext wraps the shared-heap best-fit in accel[0x544] mutex_lock,
 * so it can't race the WindowServer (the v3 unlocked allocator paniced).  Returns the VRAM byte offset
 * (0 on failure) and stores the opaque kernel record handle in *record_out (free it later with
 * mach64_vram_free).  Collision-free at any surface size, unlike the small-window guessed placement.
 * On a kext WITHOUT the allocator patch, op=ALLOC just runs the upload memcpy path with a tiny size and
 * returns out=[0,0] -> rejected -> caller falls back to guessed placement. */
uint32_t mach64_vram_alloc(gld_context *ctx, uint32_t size, uint32_t *record_out)
{
    uint32_t in[2], out[2] = {0,0};
    IOByteCount osz = sizeof out;
    kern_return_t k;
    if (record_out) *record_out = 0;
    in[0] = kATIRPro_TEXOP_ALLOC; in[1] = size;
    gld_log("[vram] -> calling new_texture ALLOC size=%u (sel 10 op=1)...\n", size);   /* fsync'd: last line if it panics */
    k = IOConnectMethodStructureIStructureO(ctx->connect, kATIRPro_new_texture,
                                            sizeof in, &osz, in, out);
    gld_log("[vram] <- alloc returned kr=0x%x out=[0x%x,0x%x]\n", (unsigned)k, out[0], out[1]);
    if (k != 0) { gld_log("[vram] alloc(%u) kr=0x%x (allocator selector absent)\n", size, (unsigned)k); return 0; }
    if (!out[0] || out[0] >= 0x800000u || (out[0] & 7u)) {   /* insane -> reject (OOM or wrong kext) */
        gld_log("[vram] alloc(%u) -> bad off=0x%x rec=0x%x -- reject\n", size, out[0], out[1]);
        if (out[1]) { uint32_t f[2]={kATIRPro_TEXOP_FREE,out[1]}, o2[2]; IOByteCount z=sizeof o2;   /* free the record */
            IOConnectMethodStructureIStructureO(ctx->connect, kATIRPro_new_texture, sizeof f, &z, f, o2); }
        return 0;
    }
    if (record_out) *record_out = out[1];
    gld_log("[vram] alloc(%u) -> off=0x%x record=0x%x\n", size, out[0], out[1]);
    return out[0];
}

void mach64_vram_free(gld_context *ctx, uint32_t record)
{
    uint32_t in[2], out[2]; IOByteCount osz = sizeof out;
    if (!record || !ctx->connect) return;
    in[0] = kATIRPro_TEXOP_FREE; in[1] = record;
    IOConnectMethodStructureIStructureO(ctx->connect, kATIRPro_new_texture, sizeof in, &osz, in, out);
}

void mach64_pathb_setup_target(gld_context *ctx)
{
    mach64_priv *p = MACH64_PRIV(ctx);
    io_connect_t sc = (io_connect_t)p->apple_surf_cn;
    io_connect_t gc = ctx->connect;
    uint32_t surf = 0, desc = 0, rt = 0;
    if (sc) {
        apple_surf_data sd; IOByteCount ol = sizeof sd; memset(&sd, 0, sizeof sd);
        IOConnectMethodScalarIStructureO(sc, kATIRProSurf_surface_write_lock, 0, &ol, &sd);
        IOConnectMethodScalarIScalarO(sc, kATIRProSurf_surface_write_unlock, 0, 0);
        IOConnectMethodScalarIScalarO(sc, kATIRProSurf_surface_flush, 2, 0, 1u, 0u);
    }
    /* KERNEL-PEEK HAZARD: these are absolute kernel reads (0x80000000 flag).  Dereferencing a
     * bogus pointer PANICS the machine (docs hazard).  VALIDATE each peeked pointer is in the
     * kernel-heap range before feeding it to the next absolute peek -- a garbage value (0,
     * ~0, a userspace address) is skipped, leaving target_off 0 (caller renders nothing) rather
     * than panicking.  Observed-good: surf~0x1d84400, desc~0x1d8449c (kernel zalloc zone). */
#define PEEK_OK(pp)  ((pp) > 0x10000u && (pp) < 0x40000000u)
    IOConnectMethodScalarIScalarO(gc, kATIRPro_scale_surface, 1, 1, 0x10cu, &surf);
    if (PEEK_OK(surf)) IOConnectMethodScalarIScalarO(gc, kATIRPro_scale_surface, 1, 1, 0x80000000u | (surf + 0x12cu), &desc);
    if (PEEK_OK(desc)) IOConnectMethodScalarIScalarO(gc, kATIRPro_scale_surface, 1, 1, 0x80000000u | (desc + 0x28u), &rt);
#undef PEEK_OK
    if (rt) {
        uint32_t toff   = (rt & 0x3fffffu) << 3;         /* VRAM byte offset */
        uint32_t tpitch = ((rt >> 22) & 0x3ffu) << 3;    /* pitch in pixels  */
        /* Sync BOTH the 2D-fill target (p->target_off, used by the clear/fill path) AND the
         * 3D render state (p->hw.dst_off_pitch, emitted by m64_emit_state for triangles) to
         * Apple's bound surface.  Previously only target_off was set, so the 2D clear hit the
         * surface (0x301000, presented) but triangles used the stale hw.dst_off_pitch left by
         * the winpool set_render_target (0x6bb800) -- rendered into a buffer never presented,
         * so geometry was invisible while the clear showed. */
        mach64_set_render_target(ctx, toff, tpitch);
        /* Depth buffer: place it in VRAM immediately AFTER our colour surface, overriding the
         * winpool guess (0x6bb800+).  [HW] the card is 8MB (ATY,MEMSIZE=0x800000), but the winpool
         * region collides with VRAM Apple actively uses (WindowServer backing / cursor) -> Z writes
         * there corrupt the display.  The tail of OUR just-allocated colour surface is the region
         * most likely free.  z = toff + (visible_h * pitch * 4); a 16-bit Z of the same pixel width.
         * Keep the drawable SMALL so colour(0x301000..) + Z stay below Apple's desktop surfaces:
         * a 320x240 window's colour(0x4b000)+Z(0x25800) end ~0x372000, well under 0x400000. */
        {   uint32_t vis_h = *(const uint32_t *)((const char *)ctx + 0x1f4);
            uint32_t color_bytes = tpitch * 4u * vis_h;
            uint32_t zsize   = tpitch * 2u * vis_h;               /* 16-bit Z, same pixel width */
            uint32_t texsize = 0x180000u;                         /* 1.5 MB texture heap        */
            uint32_t zoff = 0, theap = 0;
            /* PREFERRED: allocate Z + texture heap from Apple's OWN VRAM allocator (sel 10 op=ALLOC on
             * the v4 kext) -- collision-free at ANY surface size, so large/fullscreen textures+depth
             * work.  Falls back to the small-window guessed placement if the allocator isn't present
             * (unpatched kext) or is out of VRAM. */
            /* OPT-IN (MACH64_VRAM_ALLOC=1): the v3 allocator PANICKED because it drove the shared VRAM
             * heap UNLOCKED, racing the WindowServer.  The v4 kext wraps the best-fit in the
             * accel[0x544] mutex (as the stock methods do), which should fix it -- but it still runs on
             * EVERY attach, so keep it gated until validated on hardware.  Without the env var, use the
             * proven small-window guessed placement. */
            int use_alloc = (getenv("MACH64_VRAM_ALLOC") != 0);
            if (p->z_record)   { mach64_vram_free(ctx, p->z_record);   p->z_record = 0; }   /* re-attach: no leak */
            if (p->tex_record) { mach64_vram_free(ctx, p->tex_record); p->tex_record = 0; }
            if (vis_h && use_alloc) {
                zoff  = mach64_vram_alloc(ctx, zsize,   &p->z_record);
                theap = mach64_vram_alloc(ctx, texsize, &p->tex_record);
            }
            if (zoff) {
                mach64_set_zbuffer(ctx, zoff, tpitch);
                gld_log("[pathb] z-buffer -> off=0x%x pitch=%u (Apple allocator)\n", p->z_off, p->z_pitch);
            } else if (vis_h) {
                /* FALLBACK: gap past the colour surface (small windows only -- see the historical
                 * white-clobber note; writing Z inside the surface's tracked range clobbers present). */
                mach64_set_zbuffer(ctx, toff + 2u * color_bytes, tpitch);
                gld_log("[pathb] z-buffer -> off=0x%x pitch=%u (guessed gap; small window only)\n",
                        p->z_off, p->z_pitch);
            }
            if (theap) {
                mach64_set_texheap(theap, theap + texsize);       /* Apple-allocated -> any size safe */
            } else if (vis_h && p->z_off) {
                uint32_t g = p->z_off + zsize;                    /* fallback: gap after Z, < 0x400000 */
                if (g < 0x3f0000u) mach64_set_texheap(g, 0x400000u);
                else gld_log("[pathb] texheap: no safe room (0x%x) + no allocator -- textures unsafe\n", g);
            }
        }
    }
    gld_log("[pathb] setup target: surf=0x%x desc=0x%x rt=0x%08x -> off=0x%x pitch=%u\n",
            surf, desc, rt, p->target_off, p->target_pitch);
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

/* Wait for the vblank leading edge (kext waitFlip, repurposed).  Called right before the
 * fullscreen present blit so the blit runs during retrace and outruns the scan beam --
 * tear-free.  (Formerly waited for a CRTC page-flip to retire; the flip is gone.) */
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

/* ---- triangle batching ----------------------------------------------
 * mach64_draw_triangle (below) accumulates state + geometry into the persistent
 * per-context command buffer p->batch instead of submitting per-triangle; the whole
 * batch is DMA-kicked ONCE by mach64_flush_batch at frame boundaries or on overflow
 * (the kext chains the submit across descriptors, so a full buffer still kicks once).
 * This replaced the old submit-per-triangle path (~80 kicks/frame, ~3fps of pure
 * submit overhead). */
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

/* Do the deferred per-frame clear (armed by glClear -> frame_new) if it hasn't fired yet.  GLEngine
 * dispatches no clear entry point for our HW surface, so the driver owns it: colour to the app's
 * glClearColor (front buffer, or the back buffer when double-buffered), and Z (GLEngine never touches
 * our depth buffer).  It is LAZY -- normally the first triangle of the frame triggers it -- so any op
 * that starts drawing (a triangle OR a pixel op) must call this; otherwise a frame that does only
 * glClear + glDrawPixels/glBitmap/glCopyPixels never clears.  Assumes glClear once per frame (the
 * standard pattern); the clears flush any pending batch first, so ordering holds. */
static void mach64_frame_start(gld_context *ctx)
{
    mach64_priv *p = MACH64_PRIV(ctx);
    if (p->frame_new) {
        /* Batch the colour clear into p->batch (NOT a separate mach64_submit) so the clear and the
         * frame's triangles drain as ONE command stream -> ONE IOConnectMapMemory(type1) trigger ->
         * ONE backing->buffer copy opportunity, in the correct order (clear first, geometry on top).
         * With the clear as its OWN submit, each frame did multiple type-1 drains and the trailing
         * copy could clobber the just-rendered geometry with the (black) system backing -> the black
         * screen.  batch_clear_color marks state dirty so the following triangle re-emits full 3D
         * state after the 2D clear regs. */
        mach64_batch_clear_color(ctx);
        if (p->z_off && (p->hw.z_cntl & MACH64_Z_EN))
            mach64_batch_clear_zbuffer(ctx);   /* batched (NOT a separate submit): keep the frame
                                                * a single drain so the backing copy can't clobber */
        p->frame_new = 0;
    }
}

int mach64_draw_triangle(gld_context *ctx, const m64_vertex *v0,
                         const m64_vertex *v1, const m64_vertex *v2)
{
    mach64_priv *p = MACH64_PRIV(ctx);
    size_t limit, need;

    mach64_frame_start(ctx);   /* deferred glClear -> colour/Z clear on the frame's first draw */

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
    { static int tlog = 0; if (tlog < 3) {
        gld_log("  [tri #%d] v0 xy=0x%08x argb=0x%08x | v1 xy=0x%08x argb=0x%08x | v2 xy=0x%08x argb=0x%08x\n",
                tlog, (unsigned)v0->x_y, v0->argb, (unsigned)v1->x_y, v1->argb, (unsigned)v2->x_y, v2->argb);
        gld_log("  [tri #%d] scale3d=0x%x setup=0x%x zc=0x%x dstop=0x%x sc_lr=0x%x sc_tb=0x%x dp_mix=0x%x dp_src=0x%x wmask=0x%x\n",
                tlog, p->hw.scale_3d_cntl, p->hw.setup_cntl, p->hw.z_cntl, p->hw.dst_off_pitch,
                p->hw.sc_left_right, p->hw.sc_top_bottom, p->hw.dp_mix, p->hw.dp_src, p->hw.dp_write_mask);
        tlog++; } }
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
 * (rb_ctx, x, y, w, h, format, type, pixels).  It began as a no-op stub (7-arg ctl shape that
 * dropped `pixels` in r10) -> glReadPixels silently did nothing.  Now fully implemented in
 * mach64_ctl08 below: reads the back-buffer VRAM (g_gld_readback_ctx render_off) for colour, or the
 * 16-bit Z buffer for GL_DEPTH_COMPONENT, at (x,y,w,h), Y-flips, converts to format/type, and writes
 * `pixels`. */
gld_context *g_gld_readback_ctx = 0;
unsigned char *g_gld_gc = 0;   /* GLEngine's gc (dispatch table - 0x78a8); holds GL_PACK_* state */

/* Read GL_PACK_* state from GLEngine's gc (default-safe).  Offsets reversed from glPixelStorei:
 * ALIGNMENT@0x63d8, ROW_LENGTH@0x63c4, SKIP_PIXELS@0x63d0, SKIP_ROWS@0x63cc, SWAP_BYTES@0x63dc. */
/* The GLEngine gc (GL_PACK / GL_UNPACK pixel-store + raster state) for a SPECIFIC context.
 * Per-context via mach64_priv.gc (set in gldUpdateDispatch), so a pixel op reads the state of the
 * context it runs on instead of whichever context last updated dispatch.  Falls back to the
 * last-dispatched global if a context has no backend/gc yet (defensive). */
static unsigned char *mach64_ctx_gc(gld_context *ctx)
{
    if (ctx && ctx->backend && MACH64_PRIV(ctx)->gc) return MACH64_PRIV(ctx)->gc;
    return g_gld_gc;
}

static void mach64_pack_state(gld_context *ctx, int *align, int *rowlen, int *skippix,
                              int *skiprows, int *swap)
{
    unsigned char *g = mach64_ctx_gc(ctx);
    *align = 4; *rowlen = 0; *skippix = 0; *skiprows = 0; *swap = 0;
    if (g) {
        int a = *(int *)(g + 0x63d8);
        if (a == 1 || a == 2 || a == 4 || a == 8) *align = a;
        *rowlen   = *(int *)(g + 0x63c4);
        *skippix  = *(int *)(g + 0x63d0);
        *skiprows = *(int *)(g + 0x63cc);
        *swap     = g[0x63dc] ? 1 : 0;
        if (*rowlen   < 0) *rowlen   = 0;
        if (*skippix  < 0) *skippix  = 0;
        if (*skiprows < 0) *skiprows = 0;
    }
}

/* GL_UNPACK_* state (for glDrawPixels/glBitmap) -- same layout as PACK, +0x1c (verified live via
 * tools/mach64_unpackprobe): ALIGNMENT@0x63f4 ROW_LENGTH@0x63e0 SKIP_PIXELS@0x63ec SKIP_ROWS@0x63e8
 * SWAP_BYTES@0x63f8 LSB_FIRST@0x63f9 (bytes).  Defaults 4/0/0/0/0/0. */
static void mach64_unpack_state(gld_context *ctx, int *align, int *rowlen, int *skippix,
                                int *skiprows, int *swap, int *lsbfirst)
{
    unsigned char *g = mach64_ctx_gc(ctx);
    *align = 4; *rowlen = 0; *skippix = 0; *skiprows = 0; *swap = 0; *lsbfirst = 0;
    if (g) {
        int a = *(int *)(g + 0x63f4);
        if (a == 1 || a == 2 || a == 4 || a == 8) *align = a;
        *rowlen   = *(int *)(g + 0x63e0);
        *skippix  = *(int *)(g + 0x63ec);
        *skiprows = *(int *)(g + 0x63e8);
        *swap     = g[0x63f8] ? 1 : 0;
        *lsbfirst = g[0x63f9] ? 1 : 0;
        if (*rowlen   < 0) *rowlen   = 0;
        if (*skippix  < 0) *skippix  = 0;
        if (*skiprows < 0) *skiprows = 0;
    }
}

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
    if (!p || !gc->regs || !height) return 0;

    /* DEPTH readback: GL_DEPTH_COMPONENT from our 16-bit Z buffer (p->z_off/z_pitch, Z16 stored LE).
     * GLEngine already rejected this if no depth buffer is bound, but guard anyway. */
    if (format == 0x1902u) {
        uint32_t zoff = p ? p->z_off : 0, zpitch = p ? p->z_pitch : 0;   /* zpitch: 16-bit Z pixels */
        int elem;
        switch (type) {
            case 0x1403u: elem = 2; break;   /* GL_UNSIGNED_SHORT */
            case 0x1405u: elem = 4; break;   /* GL_UNSIGNED_INT   */
            case 0x1406u: elem = 4; break;   /* GL_FLOAT          */
            default: return 0;
        }
        if (!zoff || !zpitch) return 0;      /* no depth buffer */
        mach64_flush_batch(gc);
        {   const volatile unsigned char *vram = (const volatile unsigned char *)gc->regs;
            unsigned char *base = (unsigned char *)pixels;
            uint32_t zpitch_b = zpitch * 2u;
            int p_align, p_rowlen, p_skippix, p_skiprows, p_swap, l, row_stride, row, col;
            mach64_pack_state(gc, &p_align, &p_rowlen, &p_skippix, &p_skiprows, &p_swap);
            l = p_rowlen > 0 ? p_rowlen : w;
            row_stride = (elem >= p_align) ? (l * elem)
                                           : (p_align * ((l * elem + p_align - 1) / p_align));
            base += p_skiprows * row_stride + p_skippix * elem;
            for (row = 0; row < h; row++) {
                int fbrow = (int)height - 1 - (y + row);
                unsigned char *o;
                if (fbrow < 0) fbrow = 0;
                o = base + row * row_stride;
                for (col = 0; col < w; col++) {
                    const volatile unsigned char *s =
                        vram + zoff + (uint32_t)fbrow * zpitch_b + (uint32_t)(x + col) * 2u;
                    unsigned z16 = (unsigned)s[0] | ((unsigned)s[1] << 8);   /* Z16 little-endian */
                    if (type == 0x1403u) {              /* GL_UNSIGNED_SHORT */
                        uint16_t v = (uint16_t)z16;
                        if (p_swap) v = (uint16_t)((v >> 8) | (v << 8));
                        *(uint16_t *)o = v; o += 2;
                    } else {                            /* GL_UNSIGNED_INT / GL_FLOAT */
                        union { float f; uint32_t u; } out;
                        if (type == 0x1405u) out.u = (z16 << 16) | z16;       /* Z16 -> full range */
                        else                 out.f = (float)z16 / 65535.0f;   /* GL_FLOAT [0,1]    */
                        if (p_swap) out.u = (out.u>>24)|((out.u>>8)&0xff00u)|((out.u<<8)&0xff0000u)|(out.u<<24);
                        *(uint32_t *)o = out.u; o += 4;
                    }
                }
            }
        }
        gld_log("     READBACK depth %dx%d type=0x%x z_off=0x%x zpitch=%u H=%u\n",
                w, h, type, zoff, zpitch, height);
        return 0;
    }

    if (!toff || !tpitch) return 0;   /* color path needs the color render target */
    /* Decode (format, type).  Our framebuffer is X8R8G8B8 (no real dst alpha) -> a missing-alpha
     * buffer reads back alpha = 1.0 (opaque) per the GL spec.  For the packed INT_8_8_8_8[_REV]
     * types we build a native uint32 by BIT position and store it natively -- so the layout is
     * correct on this BIG-ENDIAN PPC (NOT the same byte order an x86 host would produce). */
    {   int is_bgr, has_a, pk;   /* pk: 0 = ubyte bytes, 1 = INT_8_8_8_8, 2 = INT_8_8_8_8_REV */
        switch (format) {
            case 0x1908u: is_bgr = 0; has_a = 1; break;   /* GL_RGBA */
            case 0x80E1u: is_bgr = 1; has_a = 1; break;   /* GL_BGRA */
            case 0x1907u: is_bgr = 0; has_a = 0; break;   /* GL_RGB  */
            case 0x80E0u: is_bgr = 1; has_a = 0; break;   /* GL_BGR  */
            default: return 0;                            /* unsupported format */
        }
        switch (type) {
            case 0x1401u: pk = 0; break;                         /* GL_UNSIGNED_BYTE */
            case 0x8035u: pk = 1; if (!has_a) return 0; break;   /* GL_UNSIGNED_INT_8_8_8_8 (RGBA/BGRA) */
            case 0x8367u: pk = 2; if (!has_a) return 0; break;   /* GL_UNSIGNED_INT_8_8_8_8_REV */
            default: return 0;                                   /* unsupported type */
        }
        mach64_flush_batch(gc);   /* submit any residual geometry; the app's glFinish already fenced */

        /* Read our render target from the whole-VRAM CPU map (gc->regs).  ARGB8888 stored LE ->
         * bytes [B,G,R,A].  glReadPixels origin is bottom-left (flip Y vs drawable height H).
         * Honour GL_PACK_* state from GLEngine's gc (default-safe fallback): ALIGNMENT@0x63d8,
         * ROW_LENGTH@0x63c4, SKIP_PIXELS@0x63d0, SKIP_ROWS@0x63cc, SWAP_BYTES@0x63dc (byte). */
        {   const volatile unsigned char *vram = (const volatile unsigned char *)gc->regs;
            unsigned char *base = (unsigned char *)pixels;
            uint32_t pitch_b = tpitch * 4u;
            int bpp_out = (pk || has_a) ? 4 : 3;
            int elem = pk ? 4 : 1;                                   /* element size (bytes) */
            int p_align, p_rowlen, p_skippix, p_skiprows, p_swap, l, row_stride, row, col;
            mach64_pack_state(gc, &p_align, &p_rowlen, &p_skippix, &p_skiprows, &p_swap);
            l = p_rowlen > 0 ? p_rowlen : w;
            row_stride = (elem >= p_align) ? (l * bpp_out)
                                           : (p_align * ((l * bpp_out + p_align - 1) / p_align));
            base += p_skiprows * row_stride + p_skippix * bpp_out;
            for (row = 0; row < h; row++) {
                int fbrow = (int)height - 1 - (y + row);
                unsigned char *o;
                if (fbrow < 0) fbrow = 0;
                o = base + row * row_stride;
                for (col = 0; col < w; col++) {
                    const volatile unsigned char *s =
                        vram + toff + (uint32_t)fbrow * pitch_b + (uint32_t)(x + col) * 4u;
                    unsigned R = s[2], G = s[1], B = s[0], A = 0xFFu;   /* X8 -> opaque */
                    if (pk == 0) {
                        if (is_bgr) { *o++ = (unsigned char)B; *o++ = (unsigned char)G; *o++ = (unsigned char)R; }
                        else        { *o++ = (unsigned char)R; *o++ = (unsigned char)G; *o++ = (unsigned char)B; }
                        if (has_a)  *o++ = (unsigned char)A;
                    } else {
                        /* components in FORMAT order (first..last); 8_8_8_8 puts the first in the
                         * MSB, _REV in the LSB.  GL_PACK_SWAP_BYTES reverses the 4 element bytes. */
                        unsigned c0, c1, c2, c3, v;
                        if (is_bgr) { c0 = B; c1 = G; c2 = R; c3 = A; }
                        else        { c0 = R; c1 = G; c2 = B; c3 = A; }
                        v = (pk == 1) ? ((c0 << 24) | (c1 << 16) | (c2 << 8) | c3)
                                      : ( c0        | (c1 << 8)  | (c2 << 16) | (c3 << 24));
                        if (p_swap) v = (v >> 24) | ((v >> 8) & 0xff00u) | ((v << 8) & 0xff0000u) | (v << 24);
                        *(uint32_t *)o = v;
                        o += 4;
                    }
                }
            }
        }
    }
    gld_log("     READBACK filled %dx%d fmt=0x%x from target_off=0x%x pitch=%u H=%u\n",
            w, h, format, toff, tpitch, height);
    return 0;
}
/* glDrawPixels.  GLEngine's glDrawPixels impl (GLEngine+0xe688) validates format/type then tail-calls
 * this renderer hook (dispatch table[0x0c] = gc+0x78b4) with the RAW client pixels -- no pre-convert:
 *   a = float*{dst_x,dst_y,dst_z}  destination window coords (the raster position)
 *   b = width, d = height          image dims (pixels)
 *   e = format (0x1908 RGBA / 0x80E1 BGRA / 0x1907 RGB / 0x80E0 BGR)
 *   f = type   (UBYTE 0x1401 / INT_8_8_8_8 0x8035 / INT_8_8_8_8_REV 0x8367)
 *   g = pixels (client CPU pointer to the source image)
 * We convert each client pixel to native ARGB8888 (inverse of the glReadPixels/mach64_ctl08 path) and
 * write it to the render target via the whole-VRAM CPU map (c->regs), Y-flipping GL's bottom-left
 * origin (image row 0 = bottom = raster y).  Honours GL_UNPACK_ALIGNMENT/ROW_LENGTH/SKIP_ROWS/
 * SKIP_PIXELS/SWAP_BYTES (mach64_unpack_state) and glPixelZoom (gc+0x63bc/0x63c0): each source pixel
 * fills its zoomed destination rectangle (fragment-center rule; negative zoom flips that axis). */

/* Fragment-center ceil: a framebuffer pixel n is covered by window span [lo,hi) when its centre n+0.5
 * falls inside, so the covered integer range is [iceil(lo-0.5), iceil(hi-0.5)).  Handles negatives. */
static int iceil(float f) { int i = (int)f; return (f > (float)i) ? i + 1 : i; }

/* Decode one glDrawPixels source pixel to R/G/B/A.  Shared by the 1:1 and pixel-zoom paths.
 * pk: 0=byte components, 1=INT_8_8_8_8, 2=_REV.  u_swap = GL_UNPACK_SWAP_BYTES (reverses the 4-byte
 * packed word; a no-op for 1-byte components, so it only applies in the packed branch). */
static void mach64_decode_pixel(const unsigned char *s, int pk, int is_bgr, int has_a, int u_swap,
                                unsigned char *R, unsigned char *G, unsigned char *B, unsigned char *A)
{
    if (pk == 0) {
        if (is_bgr) { *B = s[0]; *G = s[1]; *R = s[2]; }
        else        { *R = s[0]; *G = s[1]; *B = s[2]; }
        *A = has_a ? s[3] : 0xFFu;
    } else {
        uint32_t v = *(const uint32_t *)s, c0, c1, c2, c3;
        if (u_swap) v = (v>>24) | ((v>>8)&0xff00u) | ((v<<8)&0xff0000u) | (v<<24);
        if (pk == 1) { c0=(v>>24)&0xffu; c1=(v>>16)&0xffu; c2=(v>>8)&0xffu; c3=v&0xffu; }
        else         { c0=v&0xffu; c1=(v>>8)&0xffu; c2=(v>>16)&0xffu; c3=(v>>24)&0xffu; }
        if (is_bgr) { *B=(unsigned char)c0; *G=(unsigned char)c1; *R=(unsigned char)c2; }
        else        { *R=(unsigned char)c0; *G=(unsigned char)c1; *B=(unsigned char)c2; }
        *A = (unsigned char)c3;
    }
}
int mach64_ctl0c(gld_context *c, uint32_t a, uint32_t b, uint32_t d,
                 uint32_t e, uint32_t f, uint32_t g)
{
    mach64_priv *p = c ? MACH64_PRIV(c) : 0;
    const float *rp = (const float *)(uintptr_t)a;
    const unsigned char *src = (const unsigned char *)(uintptr_t)g;
    int w = (int)b, h = (int)d;
    unsigned format = e, type = f;
    int is_bgr, has_a, pk, bpp_in, elem, dx, dy, W, H, row, col, align, rowstride;
    int u_rowlen, u_skippix, u_skiprows, u_swap, u_lsb, l;
    volatile unsigned char *vram;
    uint32_t pitch_b;
    float zx = 1.0f, zy = 1.0f;

    if (!p || !p->target_off || !p->target_pitch || !rp || !src || w <= 0 || h <= 0) {
        ctl_dump("table[0x0c]", c, a, b, d, e, f, g); return 0;
    }
    (void)u_lsb;
    switch (format) {
        case 0x1908u: is_bgr = 0; has_a = 1; break;   /* GL_RGBA */
        case 0x80E1u: is_bgr = 1; has_a = 1; break;   /* GL_BGRA */
        case 0x1907u: is_bgr = 0; has_a = 0; break;   /* GL_RGB  */
        case 0x80E0u: is_bgr = 1; has_a = 0; break;   /* GL_BGR  */
        default: ctl_dump("table[0x0c]", c, a, b, d, e, f, g); return 0;   /* unsupported format */
    }
    switch (type) {
        case 0x1401u: pk = 0; break;                                       /* GL_UNSIGNED_BYTE */
        case 0x8035u: pk = 1; if (!has_a) { ctl_dump("table[0x0c]",c,a,b,d,e,f,g); return 0; } break; /* INT_8_8_8_8 */
        case 0x8367u: pk = 2; if (!has_a) { ctl_dump("table[0x0c]",c,a,b,d,e,f,g); return 0; } break; /* INT_8_8_8_8_REV */
        default: ctl_dump("table[0x0c]", c, a, b, d, e, f, g); return 0;   /* unsupported type */
    }
    vram = (volatile unsigned char *)c->regs;
    if (!vram) return 0;
    W = (int)*(uint32_t *)((unsigned char *)c + 0x1f0);
    H = (int)*(uint32_t *)((unsigned char *)c + 0x1f4);
    if (W <= 0 || H <= 0) return 0;
    bpp_in = (pk || has_a) ? 4 : 3;       /* client bytes per pixel */
    elem   = pk ? 4 : 1;                  /* element size (packed word / component), for row alignment */
    dx = (int)(rp[0] + 0.5f);
    dy = (int)(rp[1] + 0.5f);
    pitch_b = p->target_pitch * 4u;
    mach64_unpack_state(c, &align, &u_rowlen, &u_skippix, &u_skiprows, &u_swap, &u_lsb);
    {   unsigned char *gcp = mach64_ctx_gc(c);   /* glPixelZoom: GL_ZOOM_X @ gc+0x63bc, GL_ZOOM_Y @ +0x63c0 */
        if (gcp) { float rzx = *(const float *)(gcp + 0x63bc), rzy = *(const float *)(gcp + 0x63c0);
                   if (rzx != 0.0f) zx = rzx; if (rzy != 0.0f) zy = rzy; }   /* 0 = unset/bad -> no zoom */
    }
    l = u_rowlen > 0 ? u_rowlen : w;      /* GL_UNPACK_ROW_LENGTH overrides the image width */
    rowstride = (elem >= align) ? (l * bpp_in)
                                : (align * ((l * bpp_in + align - 1) / align));
    src += u_skiprows * rowstride + u_skippix * bpp_in;   /* GL_UNPACK_SKIP_ROWS/PIXELS */
    gld_log("glDrawPixels: %dx%d fmt=0x%x type=0x%x -> (%d,%d) zoom=%.2f,%.2f [W=%d H=%d unpack a=%d rl=%d sp=%d sr=%d]\n",
            w, h, format, type, dx, dy, zx, zy, W, H, align, u_rowlen, u_skippix, u_skiprows);
    mach64_frame_start(c);                /* deferred glClear -> clear before this upload */
    mach64_flush_batch(c);                /* pending geometry before the upload */
    for (row = 0; row < h; row++) {
        const unsigned char *srow = src + row * rowstride;
        float wy0 = (float)dy + (float)row * zy, wy1 = (float)dy + (float)(row + 1) * zy;
        int ylo = iceil((wy0 < wy1 ? wy0 : wy1) - 0.5f);
        int yhi = iceil((wy0 < wy1 ? wy1 : wy0) - 0.5f);
        int wy;
        for (col = 0; col < w; col++) {
            const unsigned char *s = srow + col * bpp_in;
            float wx0 = (float)dx + (float)col * zx, wx1 = (float)dx + (float)(col + 1) * zx;
            int xlo = iceil((wx0 < wx1 ? wx0 : wx1) - 0.5f);
            int xhi = iceil((wx0 < wx1 ? wx1 : wx0) - 0.5f);
            unsigned char R, G, B, A;
            int wx;
            mach64_decode_pixel(s, pk, is_bgr, has_a, u_swap, &R, &G, &B, &A);
            for (wy = ylo; wy < yhi; wy++) {           /* fill this source pixel's zoomed dest rect */
                int fbrow = H - 1 - wy;                /* GL bottom-left origin -> top-down VRAM row */
                volatile unsigned char *orow;
                if (fbrow < 0 || fbrow >= H) continue;
                orow = vram + p->target_off + (uint32_t)fbrow * pitch_b;
                for (wx = xlo; wx < xhi; wx++) {
                    volatile unsigned char *o;
                    if (wx < 0 || wx >= W) continue;
                    o = orow + (uint32_t)wx * 4u;
                    o[0] = B; o[1] = G; o[2] = R; o[3] = A;   /* native LE ARGB8888 */
                }
            }
        }
    }
    return 0;
}
/* glCopyPixels(GL_COLOR).  GLEngine's glCopyPixels color path (impl GLEngine+0xcb5c) tail-calls this
 * renderer hook (dispatch table[0x10] = gc+0x78b8) AFTER viewport-transforming the raster position.
 * Reversed args (mach64_pxdisp + GLEngine disasm):
 *   a = float*{dst_x,dst_y,dst_z}  destination window coords (the current raster position)
 *   b = src_x, d = src_y           source lower-left, window coords (GL bottom-left origin)
 *   e = width, f = height          copy extent (pixels)
 *   g = copy type                  (this hook is only reached for the colour copy)
 * We do the framebuffer->framebuffer copy with the 2D blit engine, Y-flipping to top-left VRAM.
 * Read and draw buffers are both the current render target (target_off) -- for a DB context that's
 * the back buffer, matching GL's default GL_BACK read+draw.  First cut: no overlapping-rect handling
 * (blit runs L->R T->B) and hard clip if the rect leaves the surface. */
int mach64_ctl10(gld_context *c, uint32_t a, uint32_t b, uint32_t d,
                 uint32_t e, uint32_t f, uint32_t g)
{
    mach64_priv *p = c ? MACH64_PRIV(c) : 0;
    const float *rp = (const float *)(uintptr_t)a;
    int sx = (int)b, sy = (int)d, w = (int)e, h = (int)f;
    int dx, dy, svy, dvy, H, W;
    (void)g;
    if (!p || !p->target_off || !p->target_pitch || !rp || w <= 0 || h <= 0) {
        ctl_dump("table[0x10]", c, a, b, d, e, f, g);   /* unhandled shape -> log, no-op */
        return 0;
    }
    /* Drawable dims come from the context (ctx+0x1f0/0x1f4), NOT p->fb_h -- fb_h is only set for
     * double-buffered attaches, so it's 0 for a single-buffered context (same source readback and
     * mach64_clear_color use). */
    W = (int)*(uint32_t *)((unsigned char *)c + 0x1f0);
    H = (int)*(uint32_t *)((unsigned char *)c + 0x1f4);
    if (W <= 0 || H <= 0) { ctl_dump("table[0x10]", c, a, b, d, e, f, g); return 0; }
    dx = (int)(rp[0] + 0.5f);
    dy = (int)(rp[1] + 0.5f);
    svy = H - sy - h;                 /* GL bottom-left block -> VRAM top-left row */
    dvy = H - dy - h;
    if (sx < 0 || dx < 0 || svy < 0 || dvy < 0 ||
        sx + w > W || dx + w > W || svy + h > H || dvy + h > H)
        return 0;                     /* out of bounds -> skip (first-cut clipping) */
    gld_log("glCopyPixels: (%d,%d) %dx%d -> (%d,%d) [W=%d H=%d]\n", sx, sy, w, h, dx, dy, W, H);
    mach64_frame_start(c);            /* deferred glClear -> clear before this copy */
    mach64_flush_batch(c);            /* pending geometry before the copy */
    mach64_blit_surface(c, p->target_off, p->target_off,
                        (uint32_t)sx, (uint32_t)svy, (uint32_t)dx, (uint32_t)dvy,
                        (uint32_t)w, (uint32_t)h, p->target_pitch);
    return 0;
}
/* glBitmap.  GLEngine's glBitmap impl (GLEngine+0x2a04) computes the raster position then calls this
 * hook (dispatch table[0x14] = gc+0x78bc) with (raster_dst{x,y,z} float*, width, height, _, _, bitmap).
 * The bitmap is a 1-bpp MASK: MSB-first within each byte, rows bottom-to-top, each row padded to the
 * GL_UNPACK_ALIGNMENT.  SET bits are drawn in the current raster colour (GL_CURRENT_RASTER_COLOR --
 * read from the GLEngine gc, as readback reads GL_PACK_* state); CLEAR bits are skipped.  Honours
 * GL_UNPACK_ALIGNMENT/ROW_LENGTH/SKIP_ROWS/SKIP_PIXELS/LSB_FIRST (mach64_unpack_state).  Bitmap origin
 * folded into raster_dst by GLEngine; the xmove/ymove raster advance is GLEngine's, not ours. */
int mach64_ctl14(gld_context *c, uint32_t a, uint32_t b, uint32_t d,
                 uint32_t e, uint32_t f, uint32_t g)
{
    mach64_priv *p = c ? MACH64_PRIV(c) : 0;
    const float *rp = (const float *)(uintptr_t)a;
    const unsigned char *bm = (const unsigned char *)(uintptr_t)g;
    int w = (int)b, h = (int)d;
    int dx, dy, W, H, row, col, rowbytes;
    int u_align, u_rowlen, u_skippix, u_skiprows, u_swap, u_lsb, lbits;
    unsigned char R, G, B, A;
    volatile unsigned char *vram;
    uint32_t pitch_b;
    (void)e; (void)f;

    if (!p || !p->target_off || !p->target_pitch || !rp || !bm || w <= 0 || h <= 0) {
        ctl_dump("table[0x14]", c, a, b, d, e, f, g); return 0;
    }
    vram = (volatile unsigned char *)c->regs;
    if (!vram) return 0;
    W = (int)*(uint32_t *)((unsigned char *)c + 0x1f0);
    H = (int)*(uint32_t *)((unsigned char *)c + 0x1f4);
    if (W <= 0 || H <= 0) return 0;
    /* current raster colour (RGBA floats) from the GLEngine gc.  Raster block: pos x@0x2d0..w@0x2e0,
     * then GL_CURRENT_RASTER_COLOR at 0x2e4 (verified: glColor3f(1,1,0) reads back R@0x2e4=1 G@0x2e8=1
     * B@0x2ec=0).  Fallback to white if the gc pointer is unset. */
    if (mach64_ctx_gc(c)) {
        const float *rc = (const float *)(mach64_ctx_gc(c) + 0x2e4);
        int ri = (int)(rc[0]*255.0f+0.5f), gi = (int)(rc[1]*255.0f+0.5f);
        int bi = (int)(rc[2]*255.0f+0.5f), ai = (int)(rc[3]*255.0f+0.5f);
        R = (unsigned char)(ri<0?0:ri>255?255:ri); G = (unsigned char)(gi<0?0:gi>255?255:gi);
        B = (unsigned char)(bi<0?0:bi>255?255:bi); A = (unsigned char)(ai<0?0:ai>255?255:ai);
    } else { R = G = B = A = 0xFFu; }
    dx = (int)(rp[0] + 0.5f);
    dy = (int)(rp[1] + 0.5f);
    pitch_b = p->target_pitch * 4u;
    mach64_unpack_state(c, &u_align, &u_rowlen, &u_skippix, &u_skiprows, &u_swap, &u_lsb);
    (void)u_swap;                          /* SWAP_BYTES is a no-op for 1-bpp */
    lbits = u_rowlen > 0 ? u_rowlen : w;   /* GL_UNPACK_ROW_LENGTH (pixels/row) */
    rowbytes = (((lbits + 7) / 8 + u_align - 1) / u_align) * u_align;   /* row bytes padded to alignment */
    bm += u_skiprows * rowbytes;           /* GL_UNPACK_SKIP_ROWS */
    gld_log("glBitmap: %dx%d -> (%d,%d) colour=%d,%d,%d,%d [W=%d H=%d unpack a=%d rl=%d lsb=%d]\n",
            w, h, dx, dy, R, G, B, A, W, H, u_align, u_rowlen, u_lsb);
    mach64_frame_start(c);            /* deferred glClear -> clear before this bitmap */
    mach64_flush_batch(c);
    for (row = 0; row < h; row++) {
        int fbrow = H - 1 - (dy + row);   /* bitmap row 0 = bottom = raster y -> VRAM row */
        const unsigned char *brow = bm + row * rowbytes;
        if (fbrow < 0 || fbrow >= H) continue;
        for (col = 0; col < w; col++) {
            int gx = dx + col;
            volatile unsigned char *o;
            if (gx < 0 || gx >= W) continue;
            {   int bitidx = u_skippix + col;   /* GL_UNPACK_SKIP_PIXELS shifts the start bit */
                int bpos = bitidx & 7;
                unsigned char mask = u_lsb ? (unsigned char)(1u << bpos) : (unsigned char)(0x80u >> bpos);
                if (!(brow[bitidx >> 3] & mask)) continue;   /* clear bit -> skip (MSB/LSB-first) */
            }
            o = vram + p->target_off + (uint32_t)fbrow * pitch_b + (uint32_t)gx * 4u;
            o[0] = B; o[1] = G; o[2] = R; o[3] = A;   /* native LE ARGB8888 */
        }
    }
    return 0;
}

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
        { static int rclog = 0; if (rclog < 4) {
            gld_log("mach64_dispatch_render_ctl(0x60) surf_cn=%u dbuf=%u back_off=0x%x\n",
                    p?p->apple_surf_cn:0, p?p->dbuf:0, p?p->back_off:0); rclog++; } }
        /* PATH B: present through Apple's bound surface (surface_flush), the once-per-frame
         * hook.  Takes priority over the legacy VRAM back->front blit (which needs our-kext
         * buffers Path B doesn't allocate). */
        if (p && p->apple_surf_cn) {
            /* Render the frame's deferred glClear INTO the surface buffer with the engine (a
             * clear-only app dispatches no triangle, so the lazy clear would otherwise never
             * fire and we'd present the uninitialised buffer -> upper-left garbage). */
            mach64_frame_start(ctx);
            /* Flush THIS frame's pending triangles into the surface buffer BEFORE presenting.
             * Without this the batch only drains at the NEXT frame's clear (fill_rect's implicit
             * flush), so geometry lagged a frame and the clear/draw order was scrambled -> the
             * spinning triangle left a trail (never-cleared accumulation into a jagged circle),
             * and the mis-ordered per-frame 2D(clear)<->3D(tri) submits are the likely engine
             * wedge behind the second-run panic.  Now each frame is a clean
             * clear -> draw -> flush -> surface_flush. */
            /* Reset the engine to 2D state (Z off, scaler off) at the TAIL of the geometry batch, so
             * Apple's build_swap blit that follows does NOT inherit our Z_EN and depth-test its
             * buffer->scanout copy (rejecting pixels -> white triangles on screen though the render
             * buffer is correct).  Appended to p->batch so it stays ONE submit; marks state dirty so
             * next frame's first triangle re-emits the full 3D+Z state. */
            if (p->batch.buf && p->batch.n) {
                m64_reg(&p->batch, MACH64_Z_CNTL,        0);
                m64_reg(&p->batch, MACH64_SCALE_3D_CNTL, 0);
                p->dirty |= MACH64_DIRTY_STATE;
            }
            mach64_flush_batch(ctx);
            /* FENCE before present: the triangle submit KICKS the setup/scaler engine and returns;
             * surface_flush (build_swap) blits the surface buffer to scanout and can race ahead of
             * the still-rasterizing 3D engine, capturing the cleared-but-triangle-less buffer (black
             * screen).  finish (sel 8, drain+idle) waits for the engine to retire our geometry first.
             * (The old flush-at-next-clear timing hid this: the triangle had a whole frame to finish
             * before its present -- but then never cleared, hence the trail.) */
            { kern_return_t fkr = IOConnectMethodScalarIStructureI(ctx->connect,
                                      kATIRPro_finish, 0, 0, NULL);
              static int flog = 0; if (flog < 3) {
                  gld_log("  [pathb] finish-fence kr=0x%x\n", fkr); flog++; } }
            mach64_pathb_present(ctx);   /* surface_flush -> blit the rendered buffer to scanout */
            p->frame_new = 1;
            return;
        }
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
                mach64_frame_start(ctx);   /* apply a still-deferred clear BEFORE the blit, so a
                                            * clear-only or first frame shows the clear colour rather
                                            * than the uninitialised (per-context) back buffer. */
                mach64_flush_batch(ctx);
                mach64_blit_surface(ctx, p->back_off, p->front_off,
                                    0, 0, p->win_x, p->win_y, p->fb_w, p->fb_h, p->fb_pitch);
                if (p->z_off) mach64_clear_zbuffer(ctx);
                mach64_batch_clear_color(ctx);
                p->frame_new = 0;
            } else {
                /* FULLSCREEN present via a 2D BLIT (back -> front scanout), NOT a CRTC
                 * page-flip.  Writing CRTC_OFF_PITCH races Apple's framebuffer driver
                 * (no shared lock between the two) -> the intermittent fullscreen HARD-LOCK.
                 * A blit never touches the CRTC, so it is race-free -- and it is exactly how
                 * Apple's own surface present works (ATIR3Surface::build_swap = a clipped 2D
                 * blit executed by PIO, confirmed by RE; docs/re-notes/apple-glcontext-
                 * protocol.md).  Same pattern as the windowed branch above (always render into
                 * `back`, blit it to the live front at (0,0), NO buffer swap, NO CRTC touch);
                 * once per frame at slot 0x60, so no multi-blit accumulation.  Tears against
                 * the unsynced scan -- so we gate the blit on the vblank leading edge below,
                 * where the 2D engine outruns the beam and the present stays tear-free. */
                mach64_flush_batch(ctx);            /* geometry -> back, engine idle */
                mach64_wait_flip(ctx);              /* vsync: kick the blit at vblank start */
                mach64_blit_surface(ctx, p->back_off, p->front_off, 0, 0, 0, 0,
                                    p->fb_w, p->fb_h, p->fb_pitch);
                if (p->z_off) mach64_clear_zbuffer(ctx);
                mach64_batch_clear_color(ctx);
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

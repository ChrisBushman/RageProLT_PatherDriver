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
#include <string.h>
#include <stdlib.h>
#include <pthread.h>
#include <fcntl.h>
#include <sys/file.h>   /* flock (DMA lock shared with the texture upload) */
#include <mach/mach_time.h>                 /* idle repaint: time since the last present */
#include <CoreFoundation/CoreFoundation.h>  /* idle repaint: run-loop timer */

#define M64_BSTORE(p) ((p)->batch_sel == 0 ? (p)->batchbuf : (p)->batch_sel == 1 ? (p)->batchbuf2 : (p)->batch_sel == 2 ? (p)->batchbuf3 : (p)->batchbuf4)
void mach64_dma_fence(void);
int  mach64_eng_single(void);
int  mach64_eng_ready(gld_context *ctx);
int  mach64_eng_kick(gld_context *ctx, const uint32_t *w, size_t n);
void mach64_eng_fence(void);

/* ---- geometry: post-T&L float vertex -> hardware m64_vertex ----------
 * GLEngine gives window-space x,y (pixels, top-left origin), z, w, and a packed
 * colour.  The setup engine wants x,y as *4 fixed point (2 sub-pixel bits) and
 * W as float bits (1.0 for the untextured 2D case we support today). */
void mach64_pack_vertex(m64_vertex *out, float x, float y, float z,
                        float w, uint32_t argb)
{
    int xi = m64_f2i(x, 2);
    int yi = m64_f2i(y, 2);
    int zi = m64_f2i(z * 65535.0f + 0.5f, 0);   /* depth [0,1] -> 16-bit Z */
    float wf = (w != 0.0f) ? w : 1.0f;
    if (zi < 0) zi = 0; else if (zi > 0xffff) zi = 0xffff;
    out->x_y = (int32_t)(((xi & 0xffff) << 16) | (yi & 0xffff));
    out->z   = (int32_t)((uint32_t)zi << 15);   /* 17.15: 16-bit depth in [30:15] */
    __builtin_memcpy(&out->w, &wf, 4);
    out->s = out->t = 0;
    out->s2 = out->t2 = out->w2 = 0;
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
    mach64_dma_fence();   /* an async vertex-DMA job must finish before anything else drives the engine */
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
    /* The header lives in memory shared with the kernel, and `capacity` is read from it.  Never write more than the mapped region
     * can hold, whatever the header claims: a context whose ctx->fb is not Apple's context buffer (e.g. a hand-built test context,
     * or a stale header) must be refused, not scribbled over. */
    {   uint32_t maxdw = ctx->fb_size > ATIRPRO_CMDBUF_HDR_BYTES ? (ctx->fb_size - ATIRPRO_CMDBUF_HDR_BYTES) / 4u : 0u;
        if (cap > maxdw) cap = maxdw;
        if (!cap) return p->last_stamp;
    }
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
            /* Apple's drain does stwbrx(this[0x550] + (reg-0x400)) with NO bounds check: a register above the DRI window
             * lands past the mapped register page (kernel fault).  Our emitters never go there; refuse rather than trust. */
            if (reg > MACH64_DRI_REG_MAX) { cb->overflow = 1; break; }
            if (out + 2 > cap) { cb->overflow = 1; break; }   /* body full -> drop rest */
            body[out++] = reg - 0x400u;    /* Apple MMIO offset (base = BAR+0x400) */
            body[out++] = cb->buf[i++];    /* val (kext byte-reverses to the LE regs) */
        }
        if (cb->overflow) break;
    }
    if (cb->overflow) {
        /* The packed stream didn't fit the hardware body -> it is TRUNCATED mid-geometry.
         * Draining a truncated stream WEDGES the engine (the Flurry freeze).  Drop it instead:
         * lose this batch's geometry but keep the machine alive.  The draw_triangle batch sizing
         * (fb_size-0x20)/8 should keep us from ever getting here. */
        return p->last_stamp;
    }
    hdr->count  = out;                                 /* # body dwords for the kext to drain */
    hdr->flags |= ATIRPRO_CMDBUF_FLAG_SUBMIT;          /* bit0 = VALID/SUBMIT */
    /* SUBMIT = map type-1 (surface buffer): clientMemoryForType drains our command buffer through
     * the engine.  Map+unmap (the drain is the side effect of the map call). */
    {   kern_return_t mkr;
        mkr = IOConnectMapMemory(ctx->connect, ATIRPRO_MEM_COMMAND, mach_task_self(),
                                 &sub, &subsz, kIOMapAnywhere | kIOMapInhibitCache);
        if (!mkr && sub) IOConnectUnmapMemory(ctx->connect, ATIRPRO_MEM_COMMAND, mach_task_self(), sub);
    }
    p->last_stamp = hdr->stamp;                        /* kext-updated submit stamp */
    return p->last_stamp;
}

/* Rows a Z clear must cover: the 3D engine addresses Z with the SCREEN y (window origin included), so a windowed context needs
 * h + win_y rows cleared (the allocation is padded by the same amount in mach64_pathb_setup_target).  Clearing only h rows left rows
 * h..h+win_y-1 holding stale data and every depth-tested draw in the bottom win_y window rows failed. */
static uint32_t m64_zclear_rows(gld_context *ctx, uint32_t h)
{
    mach64_priv *p = MACH64_PRIV(ctx);
    if (p->win_mode) h += (uint32_t)(p->win_y > 0 ? p->win_y : 0);
    return h;
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
    uint32_t h    = m64_zclear_rows(ctx, *(const uint32_t *)((const char *)ctx + 0x1f4));  /* height (+ window y: see m64_zclear_rows) */
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
static uint32_t clampc(float f){ return m64_f2u8(f); }
void mach64_clear_color(gld_context *ctx)
{
    mach64_priv *p = MACH64_PRIV(ctx);
    uint32_t gli = mach64_gli(ctx);
    uint32_t w = *(const uint32_t *)((const char *)ctx + 0x1f0);
    uint32_t h = *(const uint32_t *)((const char *)ctx + 0x1f4);
    uint32_t col = 0;
    /* offset 0 is a VALID target (Apple's surface allocator hands out VRAM from 0): invalidity is render_skip, not target_off==0 */
    if (p->render_skip || !w || !h) return;
    if (gli) {
        float rf, gf, bf;
        uint32_t rb = mach64_gli_u32(ctx, GLI_CLEAR_COLOR + 0);
        uint32_t gb = mach64_gli_u32(ctx, GLI_CLEAR_COLOR + 4);
        uint32_t bb = mach64_gli_u32(ctx, GLI_CLEAR_COLOR + 8);
        uint32_t ab = mach64_gli_u32(ctx, GLI_CLEAR_COLOR + 12);
        float af;
        __builtin_memcpy(&rf, &rb, 4);
        __builtin_memcpy(&gf, &gb, 4);
        __builtin_memcpy(&bf, &bb, 4);
        __builtin_memcpy(&af, &ab, 4);
        /* the clear alpha goes into the ARGB8888 target's alpha byte too (destination alpha: DST_ALPHA blend factors, alpha readback) */
        col = (clampc(af) << 24) | (clampc(rf) << 16) | (clampc(gf) << 8) | clampc(bf);
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
    if (p->render_skip || !w || !h) return;   /* offset 0 is a valid target (see mach64_clear_color) */
    if (gli) {
        float rf, gf, bf;
        uint32_t rb = mach64_gli_u32(ctx, GLI_CLEAR_COLOR + 0);
        uint32_t gb = mach64_gli_u32(ctx, GLI_CLEAR_COLOR + 4);
        uint32_t bb = mach64_gli_u32(ctx, GLI_CLEAR_COLOR + 8);
        uint32_t ab = mach64_gli_u32(ctx, GLI_CLEAR_COLOR + 12);
        float af;
        __builtin_memcpy(&rf, &rb, 4);
        __builtin_memcpy(&gf, &gb, 4);
        __builtin_memcpy(&bf, &bb, 4);
        __builtin_memcpy(&af, &ab, 4);
        /* the clear alpha goes into the ARGB8888 target's alpha byte too (destination alpha: DST_ALPHA blend factors, alpha readback) */
        col = (clampc(af) << 24) | (clampc(rf) << 16) | (clampc(gf) << 8) | clampc(bf);
    }
    if (!p->batch.buf)
        m64_cb_init(&p->batch, M64_BSTORE(p), 32768);
    m64_reg(&p->batch, MACH64_Z_CNTL,           0);
    m64_reg(&p->batch, MACH64_SCALE_3D_CNTL,    0);
    m64_reg(&p->batch, MACH64_GUI_TRAJ_CNTL,    (1u<<0) | (1u<<1));
    m64_reg(&p->batch, MACH64_DP_PIX_WIDTH,     M64_PIXW(p));
    m64_reg(&p->batch, MACH64_DP_FRGD_CLR,      m64_pack_cpix(p, col));
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
    uint32_t h    = m64_zclear_rows(ctx, *(const uint32_t *)((const char *)ctx + 0x1f4));
    if (!zoff || !w32 || !h) return;
    if (!p->batch.buf)
        m64_cb_init(&p->batch, M64_BSTORE(p), 32768);
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
    if (p->render_skip || !w || !h) return;   /* offset 0 is a valid destination (Apple's surface allocator starts at 0) */
    if (!mach64_vram_span_ok(off, ((uint64_t)y + h) * pitch_px * M64_CBPP(p))) return;
    mach64_flush_batch(ctx);   /* order: pending triangles before the fill */
    m64_cb_init(&cb, p->scratch, sizeof(p->scratch) / sizeof(p->scratch[0]));
    m64_reg(&cb, MACH64_Z_CNTL,           0);
    m64_reg(&cb, MACH64_SCALE_3D_CNTL,    0);
    m64_reg(&cb, MACH64_GUI_TRAJ_CNTL,    (1u<<0) | (1u<<1));    /* L->R, T->B */
    m64_reg(&cb, MACH64_DP_PIX_WIDTH,     M64_PIXW(p));
    m64_reg(&cb, MACH64_DP_FRGD_CLR,      m64_pack_cpix(p, color));
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

/* PATH B present (HW-PROVEN 2026-09-06): the engine has ALREADY rendered into the
 * surface's VRAM buffer (surf[0x12c] == p->target_off) via mach64_submit.  Present = surface_flush
 * ONLY: flush_surface -> build_swap blits the buffer to the live scanout.  Do NOT write_lock /
 * CPU-fill the backing -- that sets the surface dirty bit, so clientMemoryForType/flush would copy
 * the backing OVER our engine render and clobber it (the prime cleared that bit; keep it clear). */
/* Visible part of the window (its rect minus every window in front of it, from CGS) -> the surface shape.  build_swap blits the backing only to
 * the shape's rectangles, so this is what keeps a window from painting over windows raised above it.  Returns 1 if the shape was changed (set_shape
 * re-allocates the backing: the caller must re-prime / re-read the render target, and must not flush the frame that was rendered into the old one).
 * `force` re-derives now; otherwise every 4th call.  Also maintains p->vis_hidden (completely covered -> present nothing). */
int mach64_vis_update(gld_context *ctx, int force)
{
    mach64_priv *p = MACH64_PRIV(ctx);
    short vr[32][4]; int n, i;
    uint32_t sig = 2166136261u;
    if (!p->apple_surf_cn || !p->win_mode || !p->cgs_win) return 0;
    if (!force && (p->vis_tick++ & 3u) != 0) return 0;
    n = mach64_cgs_visible_rects(p->cgs_win, p->cgs_surf, (int)p->win_x, (int)p->win_y, (int)p->fb_w, (int)p->fb_h, vr, 32);
    if (n < 0) return 0;
    p->vis_hidden = (n == 0) ? 1u : 0u;
    for (i = 0; i < n; i++) { int k; for (k = 0; k < 4; k++) sig = (sig ^ (uint16_t)vr[i][k]) * 16777619u; }
    sig ^= (uint32_t)n << 24;
    if (sig == p->vis_sig) return 0;
    p->vis_sig = sig;
    mach64_dma_fence();   /* a queued present may still be blitting through this surface: let it finish before the backing is re-allocated */
    mach64_apple_reshape((unsigned)p->apple_surf_cn, (const short (*)[4])vr, n, (int)p->win_x, (int)p->win_y, (int)p->fb_w, (int)p->fb_h);
    return 1;
}

int mach64_async_present_ok(gld_context *ctx);
int mach64_frame_end_async(gld_context *ctx);
int mach64_present_async(gld_context *ctx, unsigned surf);
static void mach64_repaint_arm(gld_context *ctx);
void mach64_pathb_present(gld_context *ctx)
{
    int async_ok = mach64_async_present_ok(ctx);
    mach64_priv *p = MACH64_PRIV(ctx);
    io_connect_t surf = (io_connect_t)p->apple_surf_cn;
    if (!async_ok) mach64_dma_fence();
    if (!surf) return;
    /* set_shape re-allocated the backing (the render target moved): re-prime and re-read the target, and do NOT flush this frame -- it was
     * rendered into the old, now freed, backing.  The next frame renders into the new one. */
    if (mach64_vis_update(ctx, 0)) { p->repaint_ok = 0; mach64_pathb_setup_target(ctx); return; }
    /* Completely covered by windows in front: present NOTHING (a stale shape made the window keep painting over the screen saver / a Terminal
     * window).  The check runs every 4th frame, so the window is presented again as soon as part of it is uncovered. */
    if (p->win_mode && p->cgs_win && p->vis_hidden) return;
    /* The buffer now holds a finished frame: let the idle timer re-present it until the next draw touches the buffer. */
    p->repaint_ok = (p->win_mode && p->dbuf) ? 1u : 0u;
    p->repaint_t  = mach_absolute_time();
    if (p->repaint_ok) mach64_repaint_arm(ctx);
    if (async_ok && mach64_present_async(ctx, (unsigned)surf) == 0) return;   /* queued behind the frame's batches: the worker does finish + surface_flush + finish */
    mach64_dma_fence();
    IOConnectMethodScalarIScalarO(surf, kATIRProSurf_surface_flush, 2, 0, 1u, 0u);
    /* the present's scaler blit runs asynchronously: without this fence the NEXT frame's colour clear races it and wipes the bottom rows before they are read */
    IOConnectMethodScalarIStructureI(ctx->connect, kATIRPro_finish, 0, 0, NULL);
}

/* ---- idle repaint (windowed Path B) ----
 * The present above blits the surface buffer straight onto the live screen; the WindowServer does not know those pixels are there, and
 * repaints the window from its own backing (white -- GL never draws into it) whenever it refreshes that window.  An app that redraws every
 * frame hides this; one that only redraws on events (Chess) shows its frame for a moment and then stays white.  While the buffer still holds
 * the last presented frame (repaint_ok: no draw, re-target or detach since), re-present it from a run-loop timer.  The timer lives on the run
 * loop of the thread that presents, so it fires only while that thread sits idle in its event loop -- never inside one of the app's GL calls
 * on the same context.  A thread with no run loop (a game's render thread) simply never fires it.  surface_flush is a plain buffer->screen
 * blit (build_swap); no write_lock, which would copy the stale system backing over the render (path-b-driver-spec.md 7/8). */
#define MACH64_REPAINT_NS 150000000ull   /* re-present an idle window at most every 150 ms */
static uint64_t mach64_ns_since(uint64_t t)
{
    static mach_timebase_info_data_t tb;
    if (!tb.denom) mach_timebase_info(&tb);
    return (mach_absolute_time() - t) * tb.numer / tb.denom;
}
static void mach64_repaint_fire(CFRunLoopTimerRef timer, void *info)
{
    gld_context *ctx = (gld_context *)info;
    mach64_priv *p = ctx ? MACH64_PRIV(ctx) : 0;
    (void)timer;
    if (!p || !p->repaint_ok || !p->apple_surf_cn || !p->win_mode || p->present_unsafe || p->render_skip || p->off_display) return;
    if (p->batch.buf && p->batch.n) return;                       /* a frame is being built: not idle */
    if (mach64_ns_since(p->repaint_t) < MACH64_REPAINT_NS) return; /* the app is presenting by itself */
    mach64_pathb_present(ctx);   /* same visibility rules as a real present; a reshape clears repaint_ok (the new buffer holds no frame) */
}
static void mach64_repaint_arm(gld_context *ctx)
{
    mach64_priv *p = MACH64_PRIV(ctx);
    CFRunLoopTimerContext tc = { 0, ctx, NULL, NULL, NULL };
    CFRunLoopTimerRef t;
    if (p->repaint_timer) return;
    t = CFRunLoopTimerCreate(kCFAllocatorDefault, CFAbsoluteTimeGetCurrent() + MACH64_REPAINT_NS / 1e9,
                             MACH64_REPAINT_NS / 1e9, 0, 0, mach64_repaint_fire, &tc);
    if (!t) return;
    CFRunLoopAddTimer(CFRunLoopGetCurrent(), t, kCFRunLoopCommonModes);   /* common modes: keep repainting during menu/drag tracking too */
    p->repaint_timer = t;
}
/* Stop and free the context's timer (context destroy). */
void mach64_repaint_stop(gld_context *ctx)
{
    mach64_priv *p = ctx && ctx->backend ? MACH64_PRIV(ctx) : 0;
    if (!p) return;
    p->repaint_ok = 0;
    if (p->repaint_timer) {
        CFRunLoopTimerInvalidate((CFRunLoopTimerRef)p->repaint_timer);
        CFRelease((CFRunLoopTimerRef)p->repaint_timer);
        p->repaint_timer = 0;
    }
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
    k = IOConnectMethodStructureIStructureO(ctx->connect, kATIRPro_new_texture,
                                            sizeof in, &osz, in, out);
    if (k != 0) return 0;
    if (!out[0] || !mach64_vram_span_ok(out[0], size) || (out[0] & 7u)) {   /* insane -> reject (OOM or wrong kext) */
        if (out[1]) { uint32_t f[2]={kATIRPro_TEXOP_FREE,out[1]}, o2[2]; IOByteCount z=sizeof o2;   /* free the record */
            IOConnectMethodStructureIStructureO(ctx->connect, kATIRPro_new_texture, sizeof f, &z, f, o2); }
        return 0;
    }
    if (record_out) *record_out = out[1];
    return out[0];
}

void mach64_vram_free(gld_context *ctx, uint32_t record)
{
    uint32_t in[2], out[2]; IOByteCount osz = sizeof out;
    if (!record || !ctx->connect) return;
    in[0] = kATIRPro_TEXOP_FREE; in[1] = record;
    IOConnectMethodStructureIStructureO(ctx->connect, kATIRPro_new_texture, sizeof in, &osz, in, out);
}

int m64_peek(io_connect_t conn, int selector, uint32_t kaddr, uint32_t *out)
{
    if (kaddr < 0x10000u || kaddr > M64_KADDR_MAX || (kaddr & 3u)) {
        if (out) *out = 0;
        return -1;
    }
    return IOConnectMethodScalarIScalarO(conn, selector, 1, 1, 0x80000000u | kaddr, out);
}

void mach64_pathb_setup_target(gld_context *ctx)
{
    mach64_priv *p = MACH64_PRIV(ctx);
    p->repaint_ok = 0;   /* re-primed / re-targeted buffer holds no presented frame */
    io_connect_t sc = (io_connect_t)p->apple_surf_cn;
    io_connect_t gc = ctx->connect;
    uint32_t surf = 0, desc = 0, rt = 0, d178 = 0;
    /* Read the surface's display association (before the prime present).  Per RE, a
     * surface with a VALID display index (surf[0x178] != 0xffff) has resetFullScreen repoint its present
     * DST (surf[0x12c]) to the LIVE scanout (ignoring the 0x20 blit bit), which races Apple's IONDRV
     * framebuffer -> panic.  surf[0x12c]==surf+0xcc means the DST is the private backing (safe). */
    IOConnectMethodScalarIScalarO(gc, kATIRPro_scale_surface, 1, 1, 0x10cu, &surf);
    if (surf > 0x10000u && surf <= M64_KADDR_MAX) {
        m64_peek(gc, kATIRPro_scale_surface, (surf + 0x178u), &d178);
        /* SAFETY BAIL: windowed (works) has surf[0x178]==0xffff (HW-confirmed).  A surface with a valid
         * display index has its present DST repointed to the LIVE scanout -> races IONDRV -> panic.  So
         * do NOT prime/present a display-associated surface: bail here (no prime), and render_ctl skips
         * the present too (p->present_unsafe).  Renders nothing rather than panicking. */
        if ((d178 & 0xffffu) != 0xffffu) {
            p->present_unsafe = 1;
            return;
        }
        p->present_unsafe = 0;
        /* SAFETY (screensaver preview): a SINGLE-buffered (dbuf=0) Path-B surface context -- e.g. the
         * System Preferences screensaver preview well -- has no back buffer, so ANY present blits an
         * unsynced/half-drawn buffer straight to the composited scanout: garbage over the live desktop
         * and, held too long, a present-vs-WindowServer panic.  This catches it whether or not the CGS
         * resolve pinned it to a window (a failed resolve would otherwise mis-fall-through to fullscreen
         * present).  Mark present_unsafe: STILL bind the render target below (geometry lands harmlessly
         * in the surface, never a stale/scanout offset), but skip the prime present here and the frame
         * present in render_ctl -- blank well, no garbage, no panic.  EVERY working case (windowed +
         * fullscreen, textured/flat) is double-buffered, so dbuf=1 paths are unaffected. */
        /* A single-buffered window other than a fullscreen-ish one (origin + >= 640 wide) is the small composited PREVIEW well:
         * surface_flush mis-positions it to (0,0) AND wedges the engine (HW-confirmed twice), so it stays blank-safe. */
        if (!p->dbuf && !(p->win_x == 0 && p->win_y == 0 && p->fb_w >= 640))
            p->present_unsafe = 1;
    }
    if (sc && !p->present_unsafe) {   /* skip prime for a blank-safe surface */
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
#define PEEK_OK(pp)  ((pp) > 0x10000u && (pp) <= M64_KADDR_MAX)
    IOConnectMethodScalarIScalarO(gc, kATIRPro_scale_surface, 1, 1, 0x10cu, &surf);
    if (PEEK_OK(surf)) m64_peek(gc, kATIRPro_scale_surface, (surf + 0x12cu), &desc);
    if (PEEK_OK(desc)) m64_peek(gc, kATIRPro_scale_surface, (desc + 0x28u), &rt);
#undef PEEK_OK
    /* SAFETY: if the bound surface's backing didn't resolve (rt=0) we have NO valid render target --
     * geometry would fall back to the scratch/base offset and can render into live VRAM (HW-confirmed
     * panic: Snowball Run's full-screen 1024x768 surface that didn't fit 8MB).  Mark render_skip so all
     * geometry/clears/present are skipped: fail-to-black, stable.  (A valid surface clears this.) */
    if (surf > 0x10000u && !rt) {
        p->render_skip = 1;
        p->present_unsafe = 1;
    } else if (rt) {
        p->render_skip = 0;
    }
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
        /* Bound the hardware scissor to the render surface.  GLEngine clips to its own viewport, which can extend past the bottom of the surface
         * (Sonic R: 44 rows = the window's screen y-offset); with the scissor wide open that overshoot was rasterized into the VRAM after the
         * colour surface and, for depth, after the Z buffer -- over the first texture of the texture heap (the 3D "R"'s) and into rows that are
         * never cleared (stale Z failing later frames' pixels).  SC_RIGHT/SC_BOTTOM are inclusive. */
        {   uint32_t sh = *(const uint32_t *)((const char *)ctx + 0x1f4);
            if (sh && tpitch) {
                MACH64_PRIV(ctx)->hw.sc_left_right = ((tpitch - 1u) << 16) | 0u;
                MACH64_PRIV(ctx)->hw.sc_top_bottom = ((sh - 1u) << 16) | 0u;
                MACH64_PRIV(ctx)->dirty |= MACH64_DIRTY_STATE; } }
        /* Depth buffer: place it in VRAM immediately AFTER our colour surface, overriding the
         * winpool guess (0x6bb800+).  [HW] the card is 8MB (ATY,MEMSIZE=0x800000), but the winpool
         * region collides with VRAM Apple actively uses (WindowServer backing / cursor) -> Z writes
         * there corrupt the display.  The tail of OUR just-allocated colour surface is the region
         * most likely free.  z = toff + (visible_h * pitch * 4); a 16-bit Z of the same pixel width.
         * Keep the drawable SMALL so colour(0x301000..) + Z stay below Apple's desktop surfaces:
         * a 320x240 window's colour(0x4b000)+Z(0x25800) end ~0x372000, well under 0x400000. */
        {   uint32_t vis_h = *(const uint32_t *)((const char *)ctx + 0x1f4);
            uint32_t color_bytes = tpitch * M64_CBPP(MACH64_PRIV(ctx)) * vis_h;
            uint32_t zsize   = tpitch * 2u * vis_h;               /* 16-bit Z, same pixel width */
            if (MACH64_PRIV(ctx)->win_mode) zsize += tpitch * 2u * (uint32_t)(MACH64_PRIV(ctx)->win_y > 0 ? MACH64_PRIV(ctx)->win_y : 0);   /* the present's blit addresses Z by SCREEN y (window origin included): cover rows up to win_y+h or the rows past the end read the texture heap as Z and fail the depth test */
            uint32_t texsize = 0x180000u;                         /* 1.5 MB texture heap        */
            uint32_t zoff = 0, theap = 0;
            /* PREFERRED: allocate Z + texture heap from Apple's OWN VRAM allocator (sel 10 op=ALLOC on
             * the v4 kext) -- collision-free at ANY surface size, so large/fullscreen textures+depth
             * work.  Falls back to the small-window guessed placement if the allocator isn't present
             * (unpatched kext) or is out of VRAM. */
            /* Allocator ONLY for WINDOWED (win_mode): there Apple's best-fit heap knows the
             * WindowServer's allocations and is collision-free (HW-proven: agltexbig).  At FULLSCREEN
             * (win_mode==0) the same best-fit can return an offset overlapping the LIVE SCANOUT, and
             * the texture upload into it corrupts the display -> KERNEL PANIC (HW-proven: flurryfs).
             * So fullscreen uses NO allocator: Z falls back (depth off if it won't fit) and the texheap
             * is placed manually in the tail past our own backing (below). */
            int use_alloc = MACH64_PRIV(ctx)->win_mode;   /* the guessed gap past the colour surface collides with a second window's surface/Z (measured: overlapping Atlantis windows corrupted each other) */
            if (p->z_record)   { mach64_vram_free(ctx, p->z_record);   p->z_record = 0; }   /* re-attach: no leak */
            if (p->tex_record) { mach64_vram_free(ctx, p->tex_record); p->tex_record = 0; }
            if (vis_h && use_alloc) {
                zoff  = mach64_vram_alloc(ctx, zsize,   &p->z_record);
                /* Largest texture heap the allocator will give: the card has 8 MB and a 640x480 window needs well under half; a bigger heap stops
                 * Sonic R's ~50 tpages from thrashing.  Try 4, 3, 2 MB, then the 1.5 MB default. */
                {   static const uint32_t try_kb[] = { 4096, 3072, 2048, 0 }; int ti = 0;
                    for (ti = 0; try_kb[ti]; ti++) {
                        if (p->tex_record) { mach64_vram_free(ctx, p->tex_record); p->tex_record = 0; }
                        theap = mach64_vram_alloc(ctx, try_kb[ti] * 1024u, &p->tex_record);
                        if (theap) { texsize = try_kb[ti] * 1024u; break; }
                    }
                    if (!theap) { if (p->tex_record) { mach64_vram_free(ctx, p->tex_record); p->tex_record = 0; } texsize = 0x180000u; theap = mach64_vram_alloc(ctx, texsize, &p->tex_record); }
                }
            }
            if (zoff) {
                mach64_set_zbuffer(ctx, zoff, tpitch);
            } else if (vis_h) {
                /* FALLBACK: gap past the colour surface (small windows only -- see the historical
                 * white-clobber note; writing Z inside the surface's tracked range clobbers present).
                 * BOUNDS-GUARD: at fullscreen size this gap lands PAST 8MB VRAM (e.g. 1024x768 ->
                 * 0xa78000) -- writing Z there is out-of-bounds and corrupts the machine.  Only use it
                 * if it fits; otherwise skip Z (depth off). */
                uint32_t zguess = toff + 2u * color_bytes;
                if (zguess + zsize <= 0x800000u)
                    mach64_set_zbuffer(ctx, zguess, tpitch);
            }
            if (theap) {
                mach64_set_texheap(theap, theap + texsize);       /* windowed: Apple-allocated -> safe */
            } else if (vis_h && MACH64_PRIV(ctx)->win_mode) {
                /* WINDOWED small-window fallback (no allocator): gap after Z, < 0x400000. */
                uint32_t g = p->z_off ? p->z_off + zsize : (toff + 2u * color_bytes);
                if (g < 0x3f0000u) mach64_set_texheap(g, 0x400000u);
                else mach64_set_texheap(0, 0);
            } else if (vis_h) {
                /* FULLSCREEN: at native size the VRAM is too cramped for a KNOWN-safe texheap
                 * -- the tail past our backing collides with Apple's framebuffer/cursor (HW: black->panic),
                 * and the allocator can hand back a scanout-colliding offset.  Mark the heap INVALID (0,0)
                 * so texture uploads SKIP -> fullscreen renders UNTEXTURED but STABLE (HW-proven flat). */
                mach64_set_texheap(0, 0);
            }
        }
    }
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
    IOConnectMethodScalarIScalarO(ctx->connect, kMach64Sel_wait_flip, 0, 0);
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
    if (!mach64_vram_span_ok(src_off, ((uint64_t)sy + h) * pitch_px * M64_CBPP(p)) || !mach64_vram_span_ok(dst_off, ((uint64_t)dy + h) * pitch_px * M64_CBPP(p)))
        return;
    mach64_flush_batch(ctx);   /* order: pending triangles before the blit */
    m64_cb_init(&cb, p->scratch, sizeof(p->scratch) / sizeof(p->scratch[0]));
    m64_reg(&cb, MACH64_Z_CNTL,           0);               /* Z off for the blit */
    m64_reg(&cb, MACH64_SCALE_3D_CNTL,    0);
    m64_reg(&cb, MACH64_GUI_TRAJ_CNTL,    (1u<<0) | (1u<<1));    /* L->R, T->B */
    m64_reg(&cb, MACH64_DP_PIX_WIDTH,     M64_PIXW(p));
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

/* Positioned 2D blit with SEPARATE src/dst pitches -- copy a w x h box from src_off (pitch src_pitch_px)
 * at (sx,sy) to dst_off (pitch dst_pitch_px) at (dx,dy).  This is the STOCK Rage Pro ICD's windowed
 * present: render off-screen, then blit to the live desktop framebuffer at the window's screen rect
 * (dst_off=0 = scanout base is legal).  No surface_flush, no CRTC touch, no QE -- a plain GUI-engine
 * blit, exactly what ATIRageProGLDriver does for a windowed context. */
void mach64_blit_2pitch(gld_context *ctx, uint32_t src_off, uint32_t src_pitch_px,
                        uint32_t dst_off, uint32_t dst_pitch_px,
                        uint32_t sx, uint32_t sy, uint32_t dx, uint32_t dy,
                        uint32_t w, uint32_t h)
{
    mach64_priv *p = MACH64_PRIV(ctx);
    m64_cmdbuf cb;
    if (!src_off || !w || !h || !src_pitch_px || !dst_pitch_px) return;   /* dst_off=0 (scanout) is OK */
    mach64_flush_batch(ctx);
    m64_cb_init(&cb, p->scratch, sizeof(p->scratch) / sizeof(p->scratch[0]));
    m64_reg(&cb, MACH64_Z_CNTL,           0);
    m64_reg(&cb, MACH64_SCALE_3D_CNTL,    0);
    m64_reg(&cb, MACH64_GUI_TRAJ_CNTL,    (1u<<0) | (1u<<1));
    m64_reg(&cb, MACH64_DP_PIX_WIDTH,     M64_PIXW(p));
    m64_reg(&cb, MACH64_DP_WRITE_MASK,    0xffffffffu);
    m64_reg(&cb, MACH64_DP_MIX,           MACH64_DP_MIX_DS);
    m64_reg(&cb, MACH64_DP_SRC,           MACH64_DP_SRC_BLIT);
    m64_reg(&cb, MACH64_SC_LEFT_RIGHT,    (0x1fffu<<16) | 0);
    m64_reg(&cb, MACH64_SC_TOP_BOTTOM,    (0x1fffu<<16) | 0);
    m64_reg(&cb, MACH64_CLR_CMP_CNTL,     0);
    m64_reg(&cb, MACH64_SRC_OFF_PITCH,    ((src_pitch_px / 8) << 22) | (src_off >> 3));
    m64_reg(&cb, MACH64_DST_OFF_PITCH,    ((dst_pitch_px / 8) << 22) | (dst_off >> 3));
    m64_reg(&cb, MACH64_SRC_WIDTH1,       w);
    m64_reg(&cb, MACH64_SRC_Y_X,          (sx << 16) | sy);
    m64_reg(&cb, MACH64_DST_Y_X,          (dx << 16) | dy);
    m64_reg(&cb, MACH64_DST_WIDTH_HEIGHT, (h << 16) | w);
    mach64_submit(ctx, &cb);
    p->dirty |= MACH64_DIRTY_STATE;
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
uint32_t mach64_batch_gen = 1;   /* bumped at every real submit; texture slots stamp the generation they were last bound in */
void mach64_flush_batch(gld_context *ctx)
{
    mach64_priv *p = MACH64_PRIV(ctx);
    mach64_dma_fence();   /* 'drain everything' must include a job in flight */
    if (!p->batch.buf || p->batch.n == 0) return;
    mach64_batch_gen++;
    mach64_submit(ctx, &p->batch);
    m64_cb_init(&p->batch, M64_BSTORE(p), 32768);
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
    p->repaint_ok = 0;   /* the buffer is about to change: no idle re-present until this frame is presented */
    if (p->render_skip) { p->frame_new = 0; return; }   /* invalid surface -> no clear into live VRAM */
    if (p->frame_new) {
        /* Batch the colour clear into p->batch (NOT a separate mach64_submit) so the clear and the
         * frame's triangles drain as ONE command stream -> ONE IOConnectMapMemory(type1) trigger ->
         * ONE backing->buffer copy opportunity, in the correct order (clear first, geometry on top).
         * With the clear as its OWN submit, each frame did multiple type-1 drains and the trailing
         * copy could clobber the just-rendered geometry with the (black) system backing -> the black
         * screen.  batch_clear_color marks state dirty so the following triangle re-emits full 3D
         * state after the 2D clear regs. */
        mach64_batch_clear_color(ctx);
        p->z_clear_pending = 1;   /* the Z clear is OWED for this frame -- see below */
        p->frame_new = 0;
    }
    /* Z clear: do it on the first draw of the frame that actually uses depth.  A frame's FIRST draws
     * are often depth-off (backgrounds, fade overlays); clearing Z only if Z_EN happened to be set on
     * the frame's very first draw consumed the clear without clearing, so depth from earlier frames
     * (e.g. a full-window quad at z=220 from a logo screen) survived and failed the later frames'
     * geometry (Sonic R: the bottom of the title and chunks of the in-game scene missing).  Draws
     * made with Z_EN off never touch the depth buffer, so deferring the clear loses nothing. */
    if (p->z_clear_pending && p->z_off && (p->hw.z_cntl & MACH64_Z_EN)) {
        mach64_batch_clear_zbuffer(ctx);   /* batched (NOT a separate submit): keep the frame
                                            * a single drain so the backing copy can't clobber */
        p->z_clear_pending = 0;
    }
}

/* ---- vertex-DMA overflow flush (docs/re-notes/vertex-dma-plan.md) ----
 * The patched ATIRagePro (patched9) answers GL selector 7 with a kernel bus-master submit handler: APPEND register-write packets (<= 4 KB per call, the IOKit
 * struct-input limit), then SUBMIT = one descriptor-chained DMA through the engine, synchronous (returns when the engine has drained).  Unlike the PIO drain
 * (map of type 1) it does not present, so it can run mid-frame.  Used only where the plugin used to DROP geometry: when the batch reaches one PIO command buffer. */
static int vdma_avail(gld_context *ctx)
{
    static int st = -1;
    if (mach64_eng_single() && mach64_eng_ready(ctx)) return 1;   /* single-thread engine: no kernel handler needed */
    if (st >= 0) return st;
    st = 0;
    {   uint32_t a = 0, b = 0, c = 0;
        /* sel 6 = get_surface_info on an unpatched kext (cold-safe miss -> 0xe00002c2) but kIOReturnUnsupported (0xe00002c7) on patched9, where sel 7 is the DMA handler */
        kern_return_t kr = IOConnectMethodScalarIScalarO(ctx->connect, 6, 1, 3, 0xdead0000u, &a, &b, &c);
        if (kr != 0xe00002c7u) return 0;
        { uint32_t in[2] = { 7, 0 }, out[4]; size_t osz = 16;
          kr = IOConnectMethodStructureIStructureO(ctx->connect, 7, 8, &osz, in, out);
          if (kr) return 0; } }
    st = 1; return 1;
}

/* Flush the batch (no terminator: the kernel appends its own) by DMA.  0 = done; -1 = nothing was drawn (caller drops, as before). */
/* ---- asynchronous vertex-DMA submit ----
 * The kernel submit blocks until the engine has drained (~36 of ~100 ms/frame in Sonic R) and spins the CPU meanwhile.  A worker thread performs vdma_flush while the game keeps
 * building the next batch in the second buffer.  Rules: (1) at most one job in flight; handing off a second one first waits for the first (the fence); (2) EVERY other engine user
 * -- PIO submit, batch flush, present, finish, real texture upload -- calls mach64_dma_fence() first, so ordering is exactly as before; (3) the kernel handler still holds
 * accel[0x544] for the whole submit, so WindowServer exclusion is unchanged. */
static int vdma_flush(gld_context *ctx, m64_cmdbuf *cb);
#define VDMA_RING 4   /* number of batch buffers: up to ring-1 jobs queued/running at once, the game builds in the remaining buffer */
static struct { pthread_mutex_t mu; pthread_cond_t cv_job, cv_done; m64_cmdbuf q[VDMA_RING]; gld_context *qctx[VDMA_RING]; unsigned qsurf[VDMA_RING]; /* 0 = batch job, else = present job on this surface connection */ int qhead, qcount, outstanding, started; } g_aw = { PTHREAD_MUTEX_INITIALIZER, PTHREAD_COND_INITIALIZER, PTHREAD_COND_INITIALIZER };
void mach64_dma_fence(void)
{
    mach64_eng_fence();                      /* single-thread engine: drain it and give the accelerator back (no-op otherwise) */
    if (!g_aw.started) return;
    pthread_mutex_lock(&g_aw.mu); while (g_aw.outstanding > 0) pthread_cond_wait(&g_aw.cv_done, &g_aw.mu); pthread_mutex_unlock(&g_aw.mu);
}
static void *vdma_worker(void *arg)
{
    (void)arg;
    for (;;) {
        m64_cmdbuf job; gld_context *jc; unsigned jsurf;
        pthread_mutex_lock(&g_aw.mu); while (g_aw.qcount == 0) pthread_cond_wait(&g_aw.cv_job, &g_aw.mu);
        job = g_aw.q[g_aw.qhead]; jc = g_aw.qctx[g_aw.qhead]; jsurf = g_aw.qsurf[g_aw.qhead]; g_aw.qhead = (g_aw.qhead + 1) % VDMA_RING; g_aw.qcount--; pthread_mutex_unlock(&g_aw.mu);
        if (jsurf) {   /* present job: retire the geometry, blit the surface to the scanout, retire the blit (the same three calls the synchronous present makes) */
            IOConnectMethodScalarIStructureI(jc->connect, kATIRPro_finish, 0, 0, NULL);
            IOConnectMethodScalarIScalarO((io_connect_t)jsurf, kATIRProSurf_surface_flush, 2, 0, 1u, 0u);
            IOConnectMethodScalarIStructureI(jc->connect, kATIRPro_finish, 0, 0, NULL);
        } else vdma_flush(jc, &job);
        pthread_mutex_lock(&g_aw.mu); g_aw.outstanding--; pthread_cond_broadcast(&g_aw.cv_done); pthread_mutex_unlock(&g_aw.mu);
    }
    return 0;
}
/* hand the batch to the worker; the caller must then build in the OTHER buffer.  returns 0 on success */
static int vdma_enqueue(gld_context *ctx, m64_cmdbuf *cb, unsigned surf)
{
    int slot;
    if (!surf && (cb->overflow || cb->n == 0 || cb->n > 32000)) return -1;
    if (mach64_eng_single() && mach64_eng_ready(ctx)) {     /* single-thread engine: no worker */
        if (!surf) return mach64_eng_kick(ctx, (const uint32_t *)cb->buf, cb->n);
        mach64_eng_fence();                                  /* present: drain the engine and release the accelerator, then the three Apple calls in this thread */
        IOConnectMethodScalarIStructureI(ctx->connect, kATIRPro_finish, 0, 0, NULL);
        IOConnectMethodScalarIScalarO((io_connect_t)surf, kATIRProSurf_surface_flush, 2, 0, 1u, 0u);
        IOConnectMethodScalarIStructureI(ctx->connect, kATIRPro_finish, 0, 0, NULL);
        return 0;
    }
    if (!g_aw.started) {
        pthread_t th;
        if (pthread_create(&th, 0, vdma_worker, 0) != 0) return -1;
        pthread_detach(th); g_aw.started = 1;
    }
    {   /* back-pressure: at most ring-1 jobs queued/running, so the buffer the game moves to next is always free */
        pthread_mutex_lock(&g_aw.mu);
        while (g_aw.outstanding >= VDMA_RING - 1) pthread_cond_wait(&g_aw.cv_done, &g_aw.mu);
        slot = (g_aw.qhead + g_aw.qcount) % VDMA_RING;
        if (cb) g_aw.q[slot] = *cb; g_aw.qctx[slot] = ctx; g_aw.qsurf[slot] = surf; g_aw.qcount++; g_aw.outstanding++; pthread_cond_signal(&g_aw.cv_job);
        pthread_mutex_unlock(&g_aw.mu); }
    return 0;
}
static int vdma_submit_async(gld_context *ctx, m64_cmdbuf *cb) { return vdma_enqueue(ctx, cb, 0); }

/* ---- asynchronous frame end -------------------------------------------------------------------------------
 * The frame's last batch and the present (finish, surface_flush, finish) are queued as jobs behind the frame's other batches on the DMA worker, so
 * the main thread returns to the game immediately instead of waiting ~7 ms for the engine and the blit.  Everything else that touches the engine or
 * the surface fences first (mach64_dma_fence), so ordering is unchanged. */
int mach64_async_present_ok(gld_context *ctx)
{
    return vdma_avail(ctx);
}
/* Queue the frame's last batch (called instead of mach64_flush_batch + finish).  Returns 0 on success, -1 if it could not be queued (caller falls back). */
int mach64_frame_end_async(gld_context *ctx)
{
    mach64_priv *p = MACH64_PRIV(ctx);
    if (!p->batch.buf || p->batch.n == 0) return 0;
    if (vdma_submit_async(ctx, &p->batch)) return -1;
    mach64_batch_gen++;
    p->batch_sel = (p->batch_sel + 1) % VDMA_RING;
    m64_cb_init(&p->batch, M64_BSTORE(p), 32768);
    p->dirty |= MACH64_DIRTY_STATE;
    return 0;
}
int mach64_present_async(gld_context *ctx, unsigned surf) { return vdma_enqueue(ctx, 0, surf); }

static int vdma_flush(gld_context *ctx, m64_cmdbuf *cb)
{
    static int lockfd = -2; static pthread_mutex_t mu = PTHREAD_MUTEX_INITIALIZER;
    uint32_t in[2 + 1000], out[4]; size_t i = 0, osz; kern_return_t kr; int rc = -1;
    if (cb->overflow || cb->n == 0 || cb->n > 32000) return -1;
    if (mach64_eng_single() && mach64_eng_ready(ctx)) { int r = mach64_eng_kick(ctx, (const uint32_t *)cb->buf, cb->n); if (!r) mach64_eng_fence(); return r; }   /* synchronous flush on the single-thread engine */
    pthread_mutex_lock(&mu);
    if (lockfd == -2) lockfd = open("/var/tmp/mach64_dma.lock", O_RDWR | O_CREAT, 0666);   /* same lock as the DMA texture upload: one bus-master user at a time */
    if (lockfd >= 0) flock(lockfd, LOCK_EX);
    in[0] = 0; in[1] = 0; osz = 0;
    kr = IOConnectMethodStructureIStructureO(ctx->connect, 7, 8, &osz, in, NULL);          /* reset */
    while (!kr && i < cb->n) {
        size_t k = cb->n - i; if (k > 1000) k = 1000;
        in[0] = 1; in[1] = 0; memcpy(in + 2, cb->buf + i, k * 4); osz = 0;
        kr = IOConnectMethodStructureIStructureO(ctx->connect, 7, 8 + k * 4, &osz, in, NULL);
        i += k;
    }
    if (!kr) { in[0] = 2; in[1] = 0; osz = 16; kr = IOConnectMethodStructureIStructureO(ctx->connect, 7, 8, &osz, in, out); if (!kr) rc = 0; }
    if (kr) { in[0] = 0; in[1] = 0; osz = 0; IOConnectMethodStructureIStructureO(ctx->connect, 7, 8, &osz, in, NULL); }
    if (lockfd >= 0) flock(lockfd, LOCK_UN);
    pthread_mutex_unlock(&mu);
    return rc;
}

int mach64_draw_triangle(gld_context *ctx, const m64_vertex *v0, const m64_vertex *v1, const m64_vertex *v2)
{
    mach64_priv *p = MACH64_PRIV(ctx);
    size_t limit, need;

    if (p->render_skip) return 0;   /* bound surface backing invalid -> skip geometry (no VRAM corruption) */
    mach64_frame_start(ctx);   /* deferred glClear -> colour/Z clear on the frame's first draw */

    if (!p->batch.buf)
        m64_cb_init(&p->batch, M64_BSTORE(p), 32768);

    /* Bound the batch to ONE hardware command buffer, and DROP triangles that don't fit this frame
     * rather than flushing mid-frame.  RE-PROVEN (2026-09-07): IOConnectMapMemory(type-1) -- our only
     * submit trigger -- is NOT a lightweight drain, it is the FULL end-of-frame PRESENT (build_swap +
     * submit_swap_buffer + CRTC flip).  It is safe exactly ONCE per frame (at render_ctl).  A 2nd
     * type-1 in the same frame hits the accel[0x74] "who-presented-last" latch: the real drain
     * (submit_context_buffer) is SKIPPED (that batch's geometry is dropped anyway) AND build_swap +
     * the CRTC flip run again mid-primitive -> HARD ENGINE WEDGE (unkillable, reboot-only -- what
     * killed Flurry and the flurrywin stress).  So mach64_draw_triangle must NEVER call
     * mach64_flush_batch (a mid-frame present); when the batch can't hold another triangle, drop it.
     * p->batch holds DRI packets {header, run...} which mach64_submit expands ~2x into {reg,val} body
     * pairs, and the hardware body is only (fb_size-0x20)/4 (=1016 @ 4KB) dwords, so bound the DRI
     * batch by (fb_size-0x20)/8 (half, for the 2x expansion) -> the frame's one submit never truncates.
     * CONSEQUENCE: a frame's geometry is capped at ONE command buffer.  On the stock 4KB buffer that
     * is only ~12-15 triangles; the v5 kext enlarges the command buffer to 64KB (map-size li->lis
     * patch) -> ~8000 pairs -> ~200 triangles/frame.  This limit auto-scales: fb_size is the live map
     * size, so a bigger v5 buffer lifts the ceiling with no driver change.  (Apple's mach64 GL is
     * PIO-only -- the vertex-DMA path in the kext is dead code -- so enlarging this buffer is THE fix
     * for volume, not a DMA rework.) */
    limit = (ctx->fb_size > 0x20u) ? (ctx->fb_size - 0x20u) / 8u : 256u;  /* guard underflow */
    if (limit > 32000) limit = 32000;  /* <= batchbuf[32768], with headroom */
    if (limit < 256) limit = 256;      /* fb not mapped yet / tiny buffer: a single tri fits */
    need = ((p->dirty & MACH64_DIRTY_STATE) ? 96u : 0u) + 64u;
    if (p->batch.n + need > limit - 8 && vdma_avail(ctx) && vdma_submit_async(ctx, &p->batch) == 0) {
        p->batch_sel = (p->batch_sel + 1) % VDMA_RING;   /* the job owns the old buffer until it has run; build in the next one (free: at most ring-1 jobs are outstanding) */
        m64_cb_init(&p->batch, M64_BSTORE(p), 32768);
        p->dirty |= MACH64_DIRTY_STATE;
        need = 96u + 64u;
    } else
    if (p->batch.n + need > limit - 8 && vdma_avail(ctx) && vdma_flush(ctx, &p->batch) == 0) {
        /* batch drawn by bus-master DMA (no present): start a fresh one and re-emit the render state */
        m64_cb_init(&p->batch, M64_BSTORE(p), 32768);
        p->dirty |= MACH64_DIRTY_STATE;
        need = 96u + 64u;
    }
    if (p->batch.n + need > limit - 8)
        return 0;   /* buffer full this frame -> drop; a mid-frame flush would present+flip -> wedge */

    if (p->dirty & MACH64_DIRTY_STATE) {
        m64_emit_state(&p->batch, &p->hw);
        p->dirty &= ~MACH64_DIRTY_STATE;
    }
    p->batch.multitex = p->hw.textured && p->hw.multitex;
    /* Normalise the perspective W so the triangle's largest is 1.0: S,T are raw and the engine computes interp(S*W)/interp(W), so a common scale leaves the
     * result unchanged, but a tiny W (Sonic R's far-plane 2D background: W = 1/3000) underflows the setup engine's fixed-point range -> black texture. */
    if (p->hw.textured) {
        float wa, wb, wc, mx, k;
        __builtin_memcpy(&wa, &v0->w, 4); __builtin_memcpy(&wb, &v1->w, 4); __builtin_memcpy(&wc, &v2->w, 4);
        mx = wa > wb ? wa : wb; if (wc > mx) mx = wc;
        if (mx > 0.0f && mx < 0.5f) {
            m64_vertex a = *v0, b = *v1, c = *v2;      /* copies only on the (rare) far-plane normalisation path */
            k = 1.0f / mx; wa *= k; wb *= k; wc *= k;
            __builtin_memcpy(&a.w, &wa, 4); __builtin_memcpy(&b.w, &wb, 4); __builtin_memcpy(&c.w, &wc, 4);
            a.w2 = a.w; b.w2 = b.w; c.w2 = c.w;
            p->batch.multitex = p->hw.textured && p->hw.multitex;
            if (!m64_emit_triangle(&p->batch, &a, &b, &c)) return 0;
            return 1;
        }
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
    /* The type-4 command buffer IS our vertex/command staging.  A real impl
     * returns a sub-allocation to fill; single-buffer synchronous today, so we
     * just hand back the base and rely on mach64_submit to serialize. */
    return ctx->fb;
}

int gldCompleteVertexBuffer(gld_context *ctx, void *vb)
{
    (void)vb;
    /* GLEngine has finished filling the primitive; flush whatever the dispatch
     * callbacks assembled.  With per-triangle submit this is already kicked, so
     * this is a no-op fence point today. */
    (void)ctx;
    return 0;
}

void gldFreeVertexBuffer(gld_context *ctx, void *vb) { (void)ctx; (void)vb; }

/* glFlush: ensure submitted work is kicked (already synchronous today). */
int gldFlush(gld_context *ctx)
{
    /* Path B windowed/double-buffered: glFlush must NOT drain.  Every drain is a type-1 submit that the kext follows with its backing copy, so an
     * app that glFlush()es between passes (Sonic R: EndFrame after the background clear, after the iris, after the logo) got 2-3 drains per frame and
     * the later passes were clobbered (title background black during the iris, bottom rows missing).  The once-per-frame present (render_ctl, slot 0x60)
     * drains everything in ONE submit; operations that must observe the triangles (readback, texture re-upload, fills) still flush for themselves. */
    mach64_priv *p = MACH64_PRIV(ctx);
    if (!p || !p->apple_surf_cn || !p->dbuf) mach64_flush_batch(ctx);
    /* Single-buffered FULLSCREEN present via the kext (surface_flush -> build_swap to the REAL
     * scanout the kext tracks).  gldFlush is the single-buffered present point (no swap hook
     * fires).  Fence geometry first so build_swap sees a complete frame.  FULLSCREEN only -- the
     * small preview well wedges. */
    if (p && !p->dbuf && p->apple_surf_cn && p->win_x == 0 && p->win_y == 0 && p->fb_w >= 640) {
        IOConnectMethodScalarIStructureI(ctx->connect, kATIRPro_finish, 0, 0, NULL);
        mach64_pathb_present(ctx);
    }
    return 0;
}

/* glFinish: block until the newest submission has retired. */
int gldFinish(gld_context *ctx)
{
    mach64_priv *p = MACH64_PRIV(ctx);
    mach64_dma_fence();
    mach64_flush_batch(ctx);   /* submit any pending triangles before fencing */
    IOConnectMethodScalarIStructureI(ctx->connect, kMach64Sel_wait_for_stamp,
                                     1, 0, (int)p->last_stamp, NULL);
    /* Double-buffered: single-buffer apps end a frame with glFinish (which lands
     * here), so present the back buffer to the front on each finish as well as on
     * the explicit swap (gldFlushBuffer).  Fence again so the blit completes before
     * the app draws the next frame over the back buffer. */
    if (p->dbuf) {
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

/* Control slots: the reference installs table[0x00,0x08,0x0c,0x10,0x14] as
 * its vertex/render engine (0x10=primitive-mode dispatcher, 0x08=vertex process).
 * All but 0x08 (readback, below) are no-ops here. */
int mach64_ctl00(gld_context *c,uint32_t a,uint32_t b,uint32_t d,uint32_t e,uint32_t f,uint32_t g){(void)c;(void)a;(void)b;(void)d;(void)e;(void)f;(void)g;return 0;}

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
    if (!p || !height) return 0;

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
        if (!mach64_vram_span_ok(zoff, (uint64_t)zpitch * 2u * height)) return 0;
        mach64_flush_batch(gc);
        {   const volatile unsigned char *vram = mach64_vram_cpu_begin();
            unsigned char *base = (unsigned char *)pixels;
            uint32_t zpitch_b = zpitch * 2u;
            int p_align, p_rowlen, p_skippix, p_skiprows, p_swap, l, row_stride, row, col;
            if (!vram) return 0;
            mach64_pack_state(gc, &p_align, &p_rowlen, &p_skippix, &p_skiprows, &p_swap);
            l = p_rowlen > 0 ? p_rowlen : w;
            row_stride = (elem >= p_align) ? (l * elem)
                                           : (p_align * ((l * elem + p_align - 1) / p_align));
            base += p_skiprows * row_stride + p_skippix * elem;
            for (row = 0; row < h; row++) {
                int fbrow = (int)height - 1 - (y + row);
                unsigned char *o;
                if (fbrow < 0) fbrow = 0;
                if (fbrow >= (int)height) fbrow = (int)height - 1;
                o = base + row * row_stride;
                for (col = 0; col < w; col++) {
                    int fx = x + col;                   /* clamp like the row: stay inside the checked span */
                    const volatile unsigned char *s;
                    if (fx < 0) fx = 0;
                    if (fx >= (int)zpitch) fx = (int)zpitch - 1;
                    s = vram + zoff + (uint32_t)fbrow * zpitch_b + (uint32_t)fx * 2u;
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
        return 0;
    }

    if (!toff || !tpitch) return 0;   /* color path needs the color render target */
    if (!mach64_vram_span_ok(toff, (uint64_t)tpitch * M64_CBPP(p) * height)) return 0;
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

        /* Read our render target from the CPU VRAM view (mach64_vram_cpu_begin).  ARGB8888 stored LE ->
         * bytes [B,G,R,A].  glReadPixels origin is bottom-left (flip Y vs drawable height H).
         * Honour GL_PACK_* state from GLEngine's gc (default-safe fallback): ALIGNMENT@0x63d8,
         * ROW_LENGTH@0x63c4, SKIP_PIXELS@0x63d0, SKIP_ROWS@0x63cc, SWAP_BYTES@0x63dc (byte). */
        {   const volatile unsigned char *vram = mach64_vram_cpu_begin();
            unsigned char *base = (unsigned char *)pixels;
            uint32_t pitch_b = tpitch * (p ? M64_CBPP(p) : 4u);
            int bpp_out = (pk || has_a) ? 4 : 3;
            int elem = pk ? 4 : 1;                                   /* element size (bytes) */
            int p_align, p_rowlen, p_skippix, p_skiprows, p_swap, l, row_stride, row, col;
            if (!vram) return 0;
            mach64_pack_state(gc, &p_align, &p_rowlen, &p_skippix, &p_skiprows, &p_swap);
            l = p_rowlen > 0 ? p_rowlen : w;
            row_stride = (elem >= p_align) ? (l * bpp_out)
                                           : (p_align * ((l * bpp_out + p_align - 1) / p_align));
            base += p_skiprows * row_stride + p_skippix * bpp_out;
            for (row = 0; row < h; row++) {
                int fbrow = (int)height - 1 - (y + row);
                unsigned char *o;
                if (fbrow < 0) fbrow = 0;
                if (fbrow >= (int)height) fbrow = (int)height - 1;
                o = base + row * row_stride;
                for (col = 0; col < w; col++) {
                    int fx = x + col;                   /* clamp like the row: stay inside the checked span */
                    const volatile unsigned char *s;
                    if (fx < 0) fx = 0;
                    if (fx >= (int)tpitch) fx = (int)tpitch - 1;
                    s = vram + toff + (uint32_t)fbrow * pitch_b + (uint32_t)fx * (p ? M64_CBPP(p) : 4u);
                    unsigned R, G, B, A = 0xFFu;                          /* X8 / RGB565 -> opaque */
                    if ((p ? M64_CBPP(p) : 4u) == 2u) {               /* RGB565, little-endian u16: expand with bit replication */
                        unsigned v16 = s[0] | ((unsigned)s[1] << 8), r5 = (v16 >> 11) & 0x1fu, g6 = (v16 >> 5) & 0x3fu, b5 = v16 & 0x1fu;
                        R = (r5 << 3) | (r5 >> 2); G = (g6 << 2) | (g6 >> 4); B = (b5 << 3) | (b5 >> 2);
                    } else { R = s[2]; G = s[1]; B = s[0]; }
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
 * write it to the render target via the CPU VRAM view (mach64_vram_cpu_begin), Y-flipping GL's bottom-left
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

    if (!p || p->render_skip || !p->target_pitch || !rp || !src || w <= 0 || h <= 0) return 0;
    switch (format) {
        case 0x1908u: is_bgr = 0; has_a = 1; break;   /* GL_RGBA */
        case 0x80E1u: is_bgr = 1; has_a = 1; break;   /* GL_BGRA */
        case 0x1907u: is_bgr = 0; has_a = 0; break;   /* GL_RGB  */
        case 0x80E0u: is_bgr = 1; has_a = 0; break;   /* GL_BGR  */
        default: return 0;   /* unsupported format */
    }
    switch (type) {
        case 0x1401u: pk = 0; break;                                       /* GL_UNSIGNED_BYTE */
        case 0x8035u: pk = 1; if (!has_a) return 0; break; /* INT_8_8_8_8 */
        case 0x8367u: pk = 2; if (!has_a) return 0; break; /* INT_8_8_8_8_REV */
        default: return 0;   /* unsupported type */
    }
    W = (int)*(uint32_t *)((unsigned char *)c + 0x1f0);
    H = (int)*(uint32_t *)((unsigned char *)c + 0x1f4);
    if (W <= 0 || H <= 0) return 0;
    if (W > (int)p->target_pitch) W = (int)p->target_pitch;   /* a row never runs past the pitch */
    if (!mach64_vram_span_ok(p->target_off, (uint64_t)p->target_pitch * M64_CBPP(p) * (uint32_t)H)) return 0;
    bpp_in = (pk || has_a) ? 4 : 3;       /* client bytes per pixel */
    elem   = pk ? 4 : 1;                  /* element size (packed word / component), for row alignment */
    dx = (int)(rp[0] + 0.5f);
    dy = (int)(rp[1] + 0.5f);
    pitch_b = p->target_pitch * M64_CBPP(p);
    mach64_unpack_state(c, &align, &u_rowlen, &u_skippix, &u_skiprows, &u_swap, &u_lsb);
    {   unsigned char *gcp = mach64_ctx_gc(c);   /* glPixelZoom: GL_ZOOM_X @ gc+0x63bc, GL_ZOOM_Y @ +0x63c0 */
        if (gcp) { float rzx = *(const float *)(gcp + 0x63bc), rzy = *(const float *)(gcp + 0x63c0);
                   if (rzx != 0.0f) zx = rzx; if (rzy != 0.0f) zy = rzy; }   /* 0 = unset/bad -> no zoom */
    }
    l = u_rowlen > 0 ? u_rowlen : w;      /* GL_UNPACK_ROW_LENGTH overrides the image width */
    rowstride = (elem >= align) ? (l * bpp_in)
                                : (align * ((l * bpp_in + align - 1) / align));
    src += u_skiprows * rowstride + u_skippix * bpp_in;   /* GL_UNPACK_SKIP_ROWS/PIXELS */
    mach64_frame_start(c);                /* deferred glClear -> clear before this upload */
    mach64_flush_batch(c);                /* pending geometry before the upload */
    vram = mach64_vram_cpu_begin();       /* engine idle: the CPU writes land after the queued geometry */
    if (!vram) return 0;
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
                    o = orow + (uint32_t)wx * M64_CBPP(p);
                    if (M64_CBPP(p) == 2u) { unsigned v16 = ((R >> 3) << 11) | ((G >> 2) << 5) | (B >> 3); o[0] = (unsigned char)v16; o[1] = (unsigned char)(v16 >> 8); }
                    else { o[0] = B; o[1] = G; o[2] = R; o[3] = A; }   /* native LE ARGB8888 */
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
    if (!p || p->render_skip || !p->target_pitch || !rp || w <= 0 || h <= 0) return 0;   /* unhandled shape -> no-op */
    /* Drawable dims come from the context (ctx+0x1f0/0x1f4), NOT p->fb_h -- fb_h is only set for
     * double-buffered attaches, so it's 0 for a single-buffered context (same source readback and
     * mach64_clear_color use). */
    W = (int)*(uint32_t *)((unsigned char *)c + 0x1f0);
    H = (int)*(uint32_t *)((unsigned char *)c + 0x1f4);
    if (W <= 0 || H <= 0) return 0;
    dx = (int)(rp[0] + 0.5f);
    dy = (int)(rp[1] + 0.5f);
    svy = H - sy - h;                 /* GL bottom-left block -> VRAM top-left row */
    dvy = H - dy - h;
    if (sx < 0 || dx < 0 || svy < 0 || dvy < 0 ||
        sx + w > W || dx + w > W || svy + h > H || dvy + h > H)
        return 0;                     /* out of bounds -> skip (first-cut clipping) */
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

    if (!p || p->render_skip || !p->target_pitch || !rp || !bm || w <= 0 || h <= 0) return 0;
    W = (int)*(uint32_t *)((unsigned char *)c + 0x1f0);
    H = (int)*(uint32_t *)((unsigned char *)c + 0x1f4);
    if (W <= 0 || H <= 0) return 0;
    if (W > (int)p->target_pitch) W = (int)p->target_pitch;   /* a row never runs past the pitch */
    if (!mach64_vram_span_ok(p->target_off, (uint64_t)p->target_pitch * M64_CBPP(p) * (uint32_t)H)) return 0;
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
    pitch_b = p->target_pitch * M64_CBPP(p);
    mach64_unpack_state(c, &u_align, &u_rowlen, &u_skippix, &u_skiprows, &u_swap, &u_lsb);
    (void)u_swap;                          /* SWAP_BYTES is a no-op for 1-bpp */
    lbits = u_rowlen > 0 ? u_rowlen : w;   /* GL_UNPACK_ROW_LENGTH (pixels/row) */
    rowbytes = (((lbits + 7) / 8 + u_align - 1) / u_align) * u_align;   /* row bytes padded to alignment */
    bm += u_skiprows * rowbytes;           /* GL_UNPACK_SKIP_ROWS */
    mach64_frame_start(c);            /* deferred glClear -> clear before this bitmap */
    mach64_flush_batch(c);
    vram = mach64_vram_cpu_begin();   /* engine idle: the CPU writes land after the queued geometry */
    if (!vram) return 0;
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
            o = vram + p->target_off + (uint32_t)fbrow * pitch_b + (uint32_t)gx * M64_CBPP(p);
            if (M64_CBPP(p) == 2u) { unsigned v16 = ((R >> 3) << 11) | ((G >> 2) << 5) | (B >> 3); o[0] = (unsigned char)v16; o[1] = (unsigned char)(v16 >> 8); }
            else { o[0] = B; o[1] = G; o[2] = R; o[3] = A; }   /* native LE ARGB8888 */
        }
    }
    return 0;
}

/* Find the gli context base from the driver ctx: ctx+0x10 points into the gli
 * (~+0x3610); scan back for the object whose [+0x7898] back-points to ctx. */
/* The located gli base is cached per context (the scan below walks up to 8192 words and ran several times per triangle: ~90% of a geometry frame's CPU).  A hit
 * is re-validated by the same back-pointer test as the scan; the entry is dropped when its context is destroyed (mach64_gli_forget), so a freed gli is never read. */
gld_context *g_gli_ctx; uint32_t g_gli_p10, g_gli_g;
void mach64_gli_forget(gld_context *ctx) { if (g_gli_ctx == ctx) { g_gli_ctx = 0; g_gli_g = 0; } }
static uint32_t mach64_gli(gld_context *ctx)
{
    uint32_t p10 = *(const uint32_t *)((const char *)ctx + 0x10), off;
    if (g_gli_ctx == ctx && g_gli_p10 == p10 && g_gli_g && *(const uint32_t *)(uintptr_t)(g_gli_g + 0x7898) == (uint32_t)(uintptr_t)ctx) return g_gli_g;
    for (off = 0; off <= 0x8000; off += 4) {
        uint32_t g = p10 - off;
        if (g < 0x10000 || g > M64_KADDR_MAX) break;
        if (*(const uint32_t *)(uintptr_t)(g + 0x7898) == (uint32_t)(uintptr_t)ctx)
            { g_gli_ctx = ctx; g_gli_p10 = p10; g_gli_g = g; return g; }
    }
    return 0;
}

/* Read a 32-bit gli field (0 if the gli can't be located). */
uint32_t mach64_gli_u32_slow(gld_context *ctx, uint32_t off)
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

/* Dispatch slot 0x4c (buffer-complete): the reference installs it; nothing to do here. */
void mach64_dispatch_complete(gld_context *ctx, const void *v, uint32_t n)
{
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
    mach64_priv *p = MACH64_PRIV(ctx);
    (void)a4; (void)a5; (void)a6; (void)a7; (void)a8;
    if (!p) return;
    /* PATH B: present through Apple's bound surface (surface_flush), the once-per-frame
     * hook.  Takes priority over the legacy VRAM back->front blit (which needs our-kext
     * buffers Path B doesn't allocate). */
    if (p->off_display) {   /* window on another display: nothing of ours to present, and never the legacy CRTC page-flip below */
        p->frame_new = 1;
        return;
    }
    if (p->apple_surf_cn) {
        /* A display-associated surface's present blits the LIVE scanout -> races IONDRV -> panic.
         * setup_target flagged it (present_unsafe); skip the whole present here (render nothing this
         * frame) rather than crash the machine.  The real fix is to keep the surface non-associated. */
        if (p->present_unsafe) {
            p->frame_new = 1;
            return;
        }
        /* Render the frame's deferred glClear INTO the surface buffer with the engine (a
         * clear-only app dispatches no triangle, so the lazy clear would otherwise never
         * fire and we'd present the uninitialised buffer -> upper-left garbage). */
        mach64_frame_start(ctx);
        /* Flush THIS frame's pending triangles into the surface buffer BEFORE presenting.
         * Without this the batch only drains at the NEXT frame's clear (fill_rect's implicit
         * flush), so geometry lagged a frame and the clear/draw order was scrambled.  Now each
         * frame is a clean clear -> draw -> flush -> surface_flush. */
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
        if (!(mach64_async_present_ok(ctx) && mach64_frame_end_async(ctx) == 0)) {
            /* (otherwise the batch is queued on the DMA worker; the worker's present job does its own finish fence) */
            mach64_flush_batch(ctx);
            /* FENCE before present: the triangle submit KICKS the setup/scaler engine and returns;
             * surface_flush (build_swap) blits the surface buffer to scanout and can race ahead of
             * the still-rasterizing 3D engine, capturing the cleared-but-triangle-less buffer (black
             * screen).  finish (sel 8, drain+idle) waits for the engine to retire our geometry first. */
            IOConnectMethodScalarIStructureI(ctx->connect, kATIRPro_finish, 0, 0, NULL);
        }
        mach64_pathb_present(ctx);   /* surface_flush -> blit the rendered buffer to scanout */
        p->frame_new = 1;
        return;
    }
    if (p->dbuf && p->back_off) {
        if (p->win_mode) {
            /* WINDOWED present: flush the w x h scene to the back
             * buffer, then BLIT it into the LIVE front at (win_x,win_y).  No page-flip --
             * the desktop stays visible and the WindowServer keeps compositing while our
             * bus-master blit + geometry DMA run.  We always render into `back`, never swap. */
            mach64_frame_start(ctx);   /* apply a still-deferred clear BEFORE the blit, so a
                                        * clear-only or first frame shows the clear colour rather
                                        * than the uninitialised (per-context) back buffer. */
            mach64_flush_batch(ctx);
            if (!p->win_no_blit)   /* win_no_blit: front geometry unknown / depth mismatch, see gld_entry.c */
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
    }
}

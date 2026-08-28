/*
 * gld_context.c - gldGetRendererInfo + gldCreateContext for the mach64 GL
 * plugin, structured to match ATIRage128GLDriver`gldCreateContext exactly
 * (see docs/re-notes/ABI.md and the turn-13 disassembly).
 *
 * This file needs the 10.3 IOKitLib + OpenGL private headers to compile; it
 * is the porting skeleton, not host-buildable.  The hardware-facing bodies
 * reuse the portable core in common/mach64_emit.c.
 */
#include <IOKit/IOKitLib.h>
#include <stdlib.h>
#include <string.h>
#include "gld.h"
#include "../common/mach64_abi.h"

/* Our renderer id (must be distinct from Apple's 0x2513 = Rage128). */
#define MACH64_RENDERER_ID  0x00002540

extern io_service_t mach64_match_service(uint32_t index); /* IOServiceGetMatchingService on the LT Pro */

/* ---- gldGetRendererInfo: open, query VRAM, advertise caps, close ----- */
int gldGetRendererInfo(gld_renderer_info *out, uint32_t renderer_mask)
{
    (void)renderer_mask;
    gld_log("gldGetRendererInfo(mask=0x%x)\n", renderer_mask);
    io_service_t svc = mach64_match_service(0);
    if (!svc) return 0x2719;                 /* no device                  */

    io_connect_t cn;
    if (IOServiceOpen(svc, mach_task_self(), MACH64_GL_CONNECT_TYPE, &cn)
        != KERN_SUCCESS)
        return 0x271f;

    /* get_config (selector 3): 0 in, 3 out -> {config0, ctx_token, config1} */
    mach64_get_config_out cfg;
    uint32_t outCnt = 3;
    IOConnectMethodScalarIScalarO(cn, kMach64Sel_get_config, 0, 3,
                                  &cfg.config0, &cfg.ctx_token, &cfg.config1);

    memset(out, 0, sizeof *out);
    out->renderer_id   = MACH64_RENDERER_ID;
    out->buffer_modes  = 0xd;                /* 0x0c: the reference sets this; we left it 0.
                                              * Buffer-mode mask GLEngine reads to know which
                                              * framebuffers exist -- a 0 here may be why
                                              * glReadPixels no-ops (no readable buffer). */
    out->caps_accel    = 0x8400;             /* accelerated + buffer modes  */
    out->caps_color    = 0x808000;
    out->caps_misc     = 0x1c01;
    out->max_something  = 0x80;
    out->video_memory  = cfg.config0;        /* from get_config             */
    out->texture_memory = cfg.config1;

    IOServiceClose(cn);
    return 0;
}

/* ---- gldCreateContext: the plugin<->kext handshake ------------------- */
int gldCreateContext(gld_context **outp, void *pixfmt, void *shared,
                     void *share_io, void *a7, void *a8, void *a9)
{
    (void)shared; (void)a9;
    if (!outp) return 0x271e;

    gld_context *ctx = calloc(1, 0x200);     /* 512-byte struct (verified)  */
    if (!ctx) return 0x2720;

    gld_log("gldCreateContext(pixfmt=%p)\n", pixfmt);
#ifdef GLD_APPLE_PROBE
    unsigned int apple_cn = mach64_apple_glctx_open();   /* Path B: open Apple's GL client, keep it */
#endif
    io_service_t svc = mach64_match_service(0);
    if (IOServiceOpen(svc, mach_task_self(), MACH64_GL_CONNECT_TYPE,
                      &ctx->connect) != KERN_SUCCESS) {
        free(ctx); return 0x271f;
    }

    if (share_io)                            /* share group                 */
        IOConnectAddClient(ctx->connect, *(io_connect_t *)share_io);

    /* get_config (selector 3): fill device_info[0..2]. */
    uint32_t oc = 3;
    IOConnectMethodScalarIScalarO(ctx->connect, kMach64Sel_get_config, 0, 3,
                                  &ctx->device_info[0], &ctx->device_info[1],
                                  &ctx->device_info[2]);

    /* Map the hardware regions we actually use.  NOTE: we deliberately do NOT
     * map type 0 (MACH64_MEM_REGISTERS = BAR2, the disabled aux aperture that
     * master-aborts on this board) -- unlike the reference driver, our register
     * access lives in the kext (rotateCommandBuffer), so the userspace plugin
     * never needs it.  Mapping it is pointless and risks wedging the bus. */
    IOByteCount sz; (void)sz;
    IOConnectMapMemory(ctx->connect, MACH64_MEM_SURFACE, mach_task_self(),
                       (vm_address_t *)&ctx->regs, (IOByteCount *)&ctx->regs_size, kIOMapAnywhere | kIOMapInhibitCache);
    IOConnectMapMemory(ctx->connect, MACH64_MEM_CONTROL, mach_task_self(),
                       (vm_address_t *)&ctx->surface, (IOByteCount *)&ctx->surface_size, kIOMapAnywhere | kIOMapInhibitCache);
    IOConnectMapMemory(ctx->connect, MACH64_MEM_CMDBUF, mach_task_self(),
                       (vm_address_t *)&ctx->fb, (IOByteCount *)&ctx->fb_size, kIOMapAnywhere | kIOMapInhibitCache);

    /* Allocate + initialize our backend (shadow registers, gouraud defaults).
     * Kept separate from Apple's 0x200 struct so we can grow it freely. */
    ctx->backend = calloc(1, sizeof(mach64_priv));
    if (!ctx->backend) { IOServiceClose(ctx->connect); free(ctx); return 0x2720; }
    mach64_init_state((mach64_priv *)ctx->backend);
#ifdef GLD_APPLE_PROBE
    ((mach64_priv *)ctx->backend)->apple_cn = apple_cn;   /* carry Apple's GL connection to attach */
#endif

    /* Consume the chosen pixel format (from gldChoosePixelFormat): record the
     * colour/buffer caps.  GLEngine reads ctx+0x2c to decide it can hardware-
     * rasterize this format -- WITHOUT it, it never dispatches geometry to our
     * render slots (confirmed: render slot never called).  Decode ported from
     * the reference gldCreateContext (ati128gld 0x48fc-0x4ab8).  Written at the
     * RAW 0x2c offset because our gld_context field layout is off and the map
     * calls above stored surface_size there, so this must come AFTER them.
     * (pixfmt is the raw 0x34-byte node, NOT our gld_pixel_format struct.) */
    if (pixfmt) {
        const uint32_t *n = (const uint32_t *)pixfmt;
        uint32_t f08 = n[2], f0c = n[3], f10 = n[4];
        uint32_t caps = 0;
        if (!(f08 & 2))        caps = 0x20;
        else if (f0c & 0xa)    caps = 0x8000;
        if (f08 & 0x800)       caps |= 0x2000;
        if (f0c & 8)           caps |= 0x400;
        else if (f08 & 2)      caps |= 0x800;
        if (f0c & 2)           caps |= 0x10;
        if (f10 & 0x03ffff00u) {
            if      (f10 & 0x00003fc0u) caps |= 3;
            else if (f10 & 0x0003f000u) caps |= 4;
            else if (f10 & 0x03f00000u) caps |= 0xb;
        } else if (f10 & 0x3f000000u) {
            if      (f10 & 0x03f00000u) caps |= 0xc;
            else if (f10 & 0x0c000000u) caps |= 0xd;
        }
        if (n[6] != 1) caps |= 0x40;                       /* node[0x18]      */
        if (n[7] != 1) caps |= 0x80;                       /* node[0x1c]      */
        { short d = (short)(n[8] & 0xffff);                /* halfword @0x22  */
          if (d == 1) caps |= 0x100; else if (d == 2) caps |= 0x200; }
        { short s = (short)((n[9] >> 16) & 0xffff);        /* halfword @0x24  */
          if (s > 0) caps |= 0x1000; }
        /* node[0x0c] bit 3 = double-buffered (the field this decode uses to emit
         * caps 0x400 vs single-buffer 0x800).  Setting bit 3 for a DB config also
         * hits the earlier `f0c & 0xa -> caps = 0x8000` branch; a single-buffered
         * context never has 0x8000, and a DB context WITH it gets zero geometry
         * dispatched (GLEngine routes it off the hardware path).  Strip 0x8000 for
         * DB so the caps match a hardware context plus the double-buffer bit 0x400. */
        if (ctx->backend && (f0c & 8)) {
            caps &= ~0x8000u;
            MACH64_PRIV(ctx)->dbuf = 1;
        }
        *(uint32_t *)((unsigned char *)ctx + 0x2c) = caps;
        gld_log("  ctx+0x2c fmt_caps = 0x%x (node [08]=0x%x [0c]=0x%x)%s\n",
                caps, f08, f0c, (f0c & 8) ? "  DBUF armed" : "");
    }

    /* Store GLEngine's gli context (6th arg, r8) at ctx+0x10 -- the render flush
     * reads the viewport/matrices from [ctx+0x10]+0x2db0.. (ref gldCreateContext
     * stw r22=r8, 0x10(ctx)).  Ours was NULL -> flush had no viewport. */
    *(void **)((unsigned char *)ctx + 0x10) = a8;
    /* a7 (r7) -> ctx+0x14: the GLEngine object the reference's type-0x36 back-buffer
     * attach flags for double buffering (sub_a524 sets bit1 of *(ctx+0x14)).  We
     * ignored it before; gldAttachDrawable needs it to set the DB dispatch gate. */
    *(void **)((unsigned char *)ctx + 0x14) = a7;
    gld_log("  ctx+0x10 <- a8=%p, ctx+0x14 <- a7=%p (a9=%p)\n", a8, a7, a9);

    *outp = ctx;
    return 0;
}

/* ---- gldDestroyContext: tear down the handshake --------------------- */
int gldDestroyContext(gld_context *ctx)
{
    if (!ctx) return 0;
#ifdef GLD_APPLE_PROBE
    if (ctx->backend && MACH64_PRIV(ctx)->apple_cn)
        IOServiceClose((io_connect_t)MACH64_PRIV(ctx)->apple_cn);
    if (ctx->backend && MACH64_PRIV(ctx)->apple_surf_cn)
        IOServiceClose((io_connect_t)MACH64_PRIV(ctx)->apple_surf_cn);
#endif
    if (ctx->backend && MACH64_PRIV(ctx)->win_buf_off)   /* release windowed buffer if not detached */
        mach64_winbuf_free(MACH64_PRIV(ctx)->win_buf_off, MACH64_PRIV(ctx)->win_buf_size);
    if (ctx->backend) { free(ctx->backend); ctx->backend = 0; }
    /* Release the userspace VM mappings gldCreateContext made before closing the
     * connection.  IOServiceClose tears down the kernel side but leaves these
     * vm_map entries in the process, so repeated context create/destroy would leak
     * address space.  Only the three regions actually mapped are unmapped (same
     * memtype + task as the map calls); the type-0 register aperture is never mapped. */
    if (ctx->connect) {
        if (ctx->regs)
            IOConnectUnmapMemory(ctx->connect, MACH64_MEM_SURFACE, mach_task_self(),
                                 (vm_address_t)ctx->regs);
        if (ctx->surface)
            IOConnectUnmapMemory(ctx->connect, MACH64_MEM_CONTROL, mach_task_self(),
                                 (vm_address_t)ctx->surface);
        if (ctx->fb)
            IOConnectUnmapMemory(ctx->connect, MACH64_MEM_CMDBUF, mach_task_self(),
                                 (vm_address_t)ctx->fb);
        IOServiceClose(ctx->connect);
    }
    free(ctx);
    return 0;
}

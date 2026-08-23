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
    (void)shared; (void)a7; (void)a8; (void)a9;
    if (!outp) return 0x271e;

    gld_context *ctx = calloc(1, 0x200);     /* 512-byte struct (verified)  */
    if (!ctx) return 0x2720;

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

    /* Map the four hardware regions (types & order from the disassembly). */
    IOByteCount sz;
    IOConnectMapMemory(ctx->connect, MACH64_MEM_REGISTERS, mach_task_self(),
                       (vm_address_t *)&ctx->cmd_regs, &sz, kIOMapAnywhere);
    IOConnectMapMemory(ctx->connect, MACH64_MEM_SURFACE, mach_task_self(),
                       (vm_address_t *)&ctx->regs, (IOByteCount *)&ctx->regs_size, kIOMapAnywhere);
    IOConnectMapMemory(ctx->connect, MACH64_MEM_CONTROL, mach_task_self(),
                       (vm_address_t *)&ctx->surface, (IOByteCount *)&ctx->surface_size, kIOMapAnywhere);
    IOConnectMapMemory(ctx->connect, MACH64_MEM_CMDBUF, mach_task_self(),
                       (vm_address_t *)&ctx->fb, (IOByteCount *)&ctx->fb_size, kIOMapAnywhere);

    /* Allocate + initialize our backend (shadow registers, gouraud defaults).
     * Kept separate from Apple's 0x200 struct so we can grow it freely. */
    ctx->backend = calloc(1, sizeof(mach64_priv));
    if (!ctx->backend) { IOServiceClose(ctx->connect); free(ctx); return 0x2720; }
    mach64_init_state((mach64_priv *)ctx->backend);

    /* Consume the chosen pixel format (from gldChoosePixelFormat): record the
     * colour/buffer caps and turn on the depth-test path if a depth buffer was
     * requested.  The render TARGET (surface offset/pitch) is bound later, when
     * the drawable is attached (mach64_set_render_target from get_surface_info);
     * the framework calls gldUpdateDispatch separately to install our render
     * slots. */
    {
        const gld_pixel_format *pf = (const gld_pixel_format *)pixfmt;
        if (pf) {
            ctx->fmt_caps  = pf->color_size;
            ctx->mode_word = pf->flags;
            if (pf->depth_size > 0)
                mach64_set_depth_test(ctx, 1, 0x0201 /* GL_LESS */);
        }
    }

    *outp = ctx;
    return 0;
}

/* ---- gldDestroyContext: tear down the handshake --------------------- */
int gldDestroyContext(gld_context *ctx)
{
    if (!ctx) return 0;
    if (ctx->backend) { free(ctx->backend); ctx->backend = 0; }
    /* TODO: IOConnectUnmapMemory the four regions before closing. */
    if (ctx->connect) IOServiceClose(ctx->connect);
    free(ctx);
    return 0;
}

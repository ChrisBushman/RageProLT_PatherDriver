/*
 * gld_context.c - gldGetRendererInfo + gldCreateContext/DestroyContext on the standalone kernel client.
 * (Apple's ATIRagePro handshake -- service match "ATIRagePro", type-1 client, get_config, mapped command regions -- is gone: one
 * rage_backend connection per context is the whole handshake.)  Layout of Apple's 0x200 context struct unchanged (gld.h).
 */
#include <stdlib.h>
#include <string.h>
#include "gld.h"

#define MACH64_RENDERER_ID  0x00002540

int gldGetRendererInfo(gld_renderer_info *out, uint32_t renderer_mask)
{
    rgb_t b; uint32_t g, f, vram = 0, fl;
    (void)renderer_mask;
    if (rgb_open(&b)) return 0x2719;                     /* no device / engine not live */
    { uint32_t e = 0; g = 0; f = 0; fl = 0;
      (void)IOConnectMethodScalarIScalarO(b.conn, 0 /* RAGE_GL_INFO */, 0, 4, &g, &f, &e, &fl); vram = e; }
    memset(out, 0, sizeof *out);
    out->renderer_id   = MACH64_RENDERER_ID;
    out->buffer_modes  = 0xd;
    out->caps_accel    = 0x8400;
    out->caps_color    = 0x808000;
    out->caps_misc     = 0x1c01;
    out->max_something = 0x80;
    out->video_memory  = vram;
    out->texture_memory = vram / 2u;
    rgb_close(&b);
    return 0;
}

int gldCreateContext(gld_context **outp, void *pixfmt, void *shared, void *share_io, void *a7, void *a8, void *a9)
{
    gld_context *ctx; mach64_priv *p;
    (void)shared; (void)share_io; (void)a9;
    if (!outp) return 0x271e;
    ctx = (gld_context *)calloc(1, 0x200);
    if (!ctx) return 0x2720;
    p = (mach64_priv *)calloc(1, sizeof *p);
    if (!p) { free(ctx); return 0x2720; }
    ctx->backend = p;
    if (rgb_open(&p->rb)) { free(p); free(ctx); return 0x2719; }
    mach64_init_state(p);
    m64_cb_init(&p->batch, p->batchbuf, GLD_BATCH_DWORDS);
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
    }

    /* Store GLEngine's gli context (6th arg, r8) at ctx+0x10 -- the render flush
     * reads the viewport/matrices from [ctx+0x10]+0x2db0.. (ref gldCreateContext
     * stw r22=r8, 0x10(ctx)).  Ours was NULL -> flush had no viewport. */
    *(void **)((unsigned char *)ctx + 0x10) = a8;
    /* a7 (r7) -> ctx+0x14: the GLEngine object the reference's type-0x36 back-buffer
     * attach flags for double buffering (sub_a524 sets bit1 of *(ctx+0x14)).  We
     * ignored it before; gldAttachDrawable needs it to set the DB dispatch gate. */
    *(void **)((unsigned char *)ctx + 0x14) = a7;

    *outp = ctx;
    return 0;
}


int gldDestroyContext(gld_context *ctx)
{
    mach64_priv *p;
    if (!ctx) return 0;
    p = MACH64_PRIV(ctx);
    mach64_gli_forget(ctx);
    if (p) {
        mach64_flush_batch(ctx);
        rgb_fence(&p->rb);
        rgb_release(&p->rb);
        rgb_close(&p->rb);
        free(p);
        ctx->backend = 0;
    }
    free(ctx);
    return 0;
}

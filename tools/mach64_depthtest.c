/*
 * mach64_depthtest.c - prove the hardware Z-buffer pipeline in isolation (no
 * GLEngine, off-screen readback).  Draws three big triangles over the SAME
 * center region at different depths, in an order that only comes out right if
 * depth testing works:
 *     A: z=0.75 red   (drawn 1st)   B: z=0.25 green (nearest, 2nd)
 *     C: z=0.90 blue  (drawn 3rd, should FAIL the LESS test vs B)
 * With Z_TEST_LESS and the Z buffer cleared to far (0xffff), the center must
 * read back GREEN (nearest wins, independent of draw order).  Without depth the
 * last-drawn blue would win -- so green == depth works.
 *
 * Build: links the gld sources (see tools/Makefile).  -framework IOKit.
 */
#include <IOKit/IOKitLib.h>
#include <libkern/OSByteOrder.h>
#include <mach/mach.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "../gldriver/gld.h"
#include "../common/mach64_abi.h"

#define PITCH_PIX   128
#define ROW_BYTES   (PITCH_PIX*4)
#define TARGET_OFF  0x600000u
#define ZBUF_OFF    0x620000u          /* 128x128x2 = 0x8000 bytes           */
#define GLD_LESS_F  0x0201             /* GL_LESS                            */

/* the gld sources log via gld_log(); stub it out for this standalone test */
void gld_log(const char *fmt, ...) { (void)fmt; }

static unsigned SURF;
static unsigned pix(int x,int y){ return OSReadLittleInt32((void*)SURF, TARGET_OFF + y*ROW_BYTES + x*4); }

/* one big triangle covering the center, all 3 verts at depth z, colour argb */
static void big_tri(gld_context *ctx, float z, unsigned argb)
{
    m64_vertex a,b,c;
    mach64_pack_vertex(&a,  14.f,  14.f, z, 1.f, argb);
    mach64_pack_vertex(&b, 114.f,  14.f, z, 1.f, argb);
    mach64_pack_vertex(&c,  64.f, 114.f, z, 1.f, argb);
    mach64_draw_triangle(ctx, &a, &b, &c);
}

int main(void)
{
    io_service_t svc; io_connect_t conn; kern_return_t kr;
    vm_address_t cmd=0, surf=0; vm_size_t cs=0, ss=0;
    gld_context *ctx; int x,y; unsigned cen;

    svc=IOServiceGetMatchingService(kIOMasterPortDefault,IOServiceMatching("Mach64Accelerator"));
    if(!svc){fprintf(stderr,"no service\n");return 1;}
    kr=IOServiceOpen(svc,mach_task_self(),MACH64_GL_CONNECT_TYPE,&conn); IOObjectRelease(svc);
    if(kr){fprintf(stderr,"open 0x%x\n",kr);return 1;}
    if(IOConnectMapMemory(conn,MACH64_MEM_CMDBUF,mach_task_self(),&cmd,&cs,kIOMapAnywhere|kIOMapInhibitCache)){fprintf(stderr,"map cmdbuf\n");return 1;}
    if(IOConnectMapMemory(conn,MACH64_MEM_SURFACE,mach_task_self(),&surf,&ss,kIOMapAnywhere|kIOMapInhibitCache)){fprintf(stderr,"map surface\n");return 1;}
    SURF=surf;

    ctx=calloc(1,0x200);
    ctx->connect=conn; ctx->fb=(void*)cmd;
    ctx->backend=calloc(1,sizeof(mach64_priv));
    mach64_init_state((mach64_priv*)ctx->backend);
    mach64_set_render_target(ctx, TARGET_OFF, PITCH_PIX);
    printf("surface map size = 0x%lx  (Z buffer at 0x%x, %s)\n",
           (unsigned long)ss, ZBUF_OFF,
           (ZBUF_OFF+0x8000<=ss) ? "IN map" : "OUT of map!");

    /* ---- CONTROL: depth OFF, z=0 (proven-safe) -> last-drawn (blue) wins ---- */
    for(y=0;y<128;y++) for(x=0;x<128;x++) OSWriteLittleInt32((void*)SURF, TARGET_OFF + y*ROW_BYTES + x*4, 0);
    big_tri(ctx, 0.f, 0x00ff0000u);
    big_tri(ctx, 0.f, 0x0000ff00u);
    big_tri(ctx, 0.f, 0x000000ffu);
    gldFinish(ctx);
    cen = pix(64,50) & 0xffffff;
    printf("[depth OFF z=0] center = 0x%06x  stamp=%u  (blue=ok; stamp==0 => DMA never ran)\n",
           cen, (unsigned)((mach64_priv*)ctx->backend)->last_stamp);
    /* dump a wider area in case rendering landed elsewhere */
    { int yy; printf("  scan corners: (10,10)=%06x (64,64)=%06x (120,120)=%06x\n",
        pix(10,10)&0xffffff, pix(64,64)&0xffffff, pix(120,120)&0xffffff); (void)yy; }

    /* ---- CONTROL2: depth OFF, z=0.75 -> does a large packed Z break rendering? ---- */
    for(y=0;y<128;y++) for(x=0;x<128;x++) OSWriteLittleInt32((void*)SURF, TARGET_OFF + y*ROW_BYTES + x*4, 0);
    big_tri(ctx, 0.75f, 0x000000ffu);
    gldFinish(ctx);
    cen = pix(64,50) & 0xffffff;
    printf("[depth OFF z=0.75] center = 0x%06x  (BLUE=z-value ok, black=z-format breaks setup)\n", cen);

    /* ---- DEPTH ON ---- */
    mach64_set_zbuffer(ctx, ZBUF_OFF, PITCH_PIX);
    mach64_set_depth_test(ctx, 1, GLD_LESS_F);
    printf("z_off_pitch=0x%08x z_cntl=0x%08x\n",
           ((mach64_priv*)ctx->backend)->hw.z_off_pitch,
           ((mach64_priv*)ctx->backend)->hw.z_cntl);
    for(y=0;y<128;y++) for(x=0;x<128;x++) OSWriteLittleInt32((void*)SURF, TARGET_OFF + y*ROW_BYTES + x*4, 0);
    for(x=0;x<128*128/2;x++) OSWriteLittleInt32((void*)SURF, ZBUF_OFF + x*4, 0xffffffff);
    printf("  Z[after clear] 0x%04x 0x%04x (expect ffff)\n",
           OSReadLittleInt16((void*)SURF, ZBUF_OFF), OSReadLittleInt16((void*)SURF, ZBUF_OFF+2));
    big_tri(ctx, 0.75f, 0x00ff0000u);   /* red   far    (1st) */
    big_tri(ctx, 0.25f, 0x0000ff00u);   /* green nearest(2nd) */
    big_tri(ctx, 0.90f, 0x000000ffu);   /* blue  farther(3rd, should be occluded) */
    gldFinish(ctx);

    cen = pix(64,50) & 0x00ffffff;
    printf("[depth ON ] center = 0x%06x  -> %s\n", cen,
           cen==0x00ff00 ? "GREEN (nearest) -- DEPTH WORKS" :
           cen==0x0000ff ? "BLUE (last-drawn) -- depth NOT applied" :
           cen==0xff0000 ? "RED (first) -- unexpected" : "black/other");
    printf("  Z[after draw] @center = 0x%04x (expect ~green z 0.25*ffff=0x3fff)\n",
           OSReadLittleInt16((void*)SURF, ZBUF_OFF + 50*PITCH_PIX*2 + 64*2));
    return 0;
}

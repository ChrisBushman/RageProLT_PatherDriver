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
    mach64_set_zbuffer(ctx, ZBUF_OFF, PITCH_PIX);
    mach64_set_depth_test(ctx, 1, GLD_LESS_F);

    /* clear colour to black and the Z buffer to far (0xffff) */
    for(y=0;y<128;y++) for(x=0;x<128;x++) OSWriteLittleInt32((void*)SURF, TARGET_OFF + y*ROW_BYTES + x*4, 0);
    for(x=0;x<128*128/2;x++) OSWriteLittleInt32((void*)SURF, ZBUF_OFF + x*4, 0xffffffff);

    printf("z_off_pitch=0x%08x z_cntl=0x%08x\n",
           ((mach64_priv*)ctx->backend)->hw.z_off_pitch,
           ((mach64_priv*)ctx->backend)->hw.z_cntl);

    big_tri(ctx, 0.75f, 0x00ff0000u);   /* red   far    (1st) */
    big_tri(ctx, 0.25f, 0x0000ff00u);   /* green nearest(2nd) */
    big_tri(ctx, 0.90f, 0x000000ffu);   /* blue  farther(3rd, should be occluded) */
    gldFinish(ctx);

    cen = pix(64,50) & 0x00ffffff;
    printf("center(64,50) = 0x%06x  -> %s\n", cen,
           cen==0x00ff00 ? "GREEN (nearest) -- DEPTH WORKS" :
           cen==0x0000ff ? "BLUE (last-drawn) -- depth NOT applied" :
           cen==0xff0000 ? "RED (first) -- unexpected" : "other");
    /* sample a few points across the triangle */
    for(y=20;y<=110;y+=30){ printf("  row %3d:",y);
        for(x=20;x<=110;x+=15) printf(" %06x", pix(x,y)&0xffffff);
        printf("\n"); }
    return 0;
}

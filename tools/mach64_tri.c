/*
 * mach64_tri.c - first 3D operation: a solid gouraud triangle rasterized into
 * OFF-SCREEN VRAM via the setup engine, verified by reading VRAM back.
 *
 * Implements the recipe in docs/re-notes/3d-triangle.md (derived from DRI):
 *   state: SCALE_3D_CNTL=0x100c1, SETUP_CNTL=0x40, Z_CNTL=0, ARGB8888
 *   per vertex: VERTEX_n_Z=(z<<15), VERTEX_n_ARGB=color,
 *               VERTEX_n_X_Y=((x*4)<<16)|((y*4)&0xffff)   [X high, Y low]
 *   TRIGGER: ONE_OVER_AREA_UC(0x300) = float bits of 1.0/area, written last.
 *
 * Safety (same as mach64_fill.c): idle-wait, save/restore engine context,
 * FIFO-gated writes with timeouts, off-screen target, readback verify.
 * Build with -framework IOKit.
 */
#include <IOKit/IOKitLib.h>
#include <libkern/OSByteOrder.h>
#include <mach/mach.h>
#include <string.h>
#include <stdio.h>
#include "../common/mach64_abi.h"

#define REG_WINDOW    0x7FF800u

/* register offsets */
#define SETUP_CNTL      0x0304
#define ONE_OVER_AREA_UC 0x0300
#define DST_OFF_PITCH   0x0500
#define Z_CNTL          0x054c
#define SCALE_3D_CNTL   0x05fc
#define SC_LEFT_RIGHT   0x06a8
#define SC_TOP_BOTTOM   0x06b4
#define DP_WRITE_MASK   0x06c8
#define DP_PIX_WIDTH    0x06d0
#define DP_MIX          0x06d4
#define DP_SRC          0x06d8
#define CLR_CMP_CNTL    0x0708
#define FIFO_STAT       0x0710
#define GUI_STAT        0x0738
/* vertex n=0,1,2 -> slot base 0x240 + n*0x20; +0x10 Z, +0x14 ARGB, +0x18 X_Y */
#define VTX_Z(n)        (0x0240 + (n)*0x20 + 0x10)
#define VTX_ARGB(n)     (0x0240 + (n)*0x20 + 0x14)
#define VTX_XY(n)       (0x0240 + (n)*0x20 + 0x18)

#define FIFO_SLOT_MASK  0x0000ffff
#define GUI_ACTIVE      0x00000001

/* target: off-screen VRAM, 128-pixel pitch (row stride 512 bytes) */
#define TARGET_OFF   0x600000u
#define PITCH_PIX    128
#define ROW_BYTES    (PITCH_PIX * 4)
#define TRI_COLOR    0xAA33CC55u

static unsigned REGB, FB;
static unsigned rd(unsigned o)          { return OSReadLittleInt32((void*)REGB, o); }
static void     wr(unsigned o, unsigned v){ OSWriteLittleInt32((void*)REGB, o, v); }

static int wait_for_fifo(int e)
{
    int i; for (i=0;i<1000000;i++)
        if ((rd(FIFO_STAT)&FIFO_SLOT_MASK) <= (unsigned)(0x8000>>e)) return 0;
    fprintf(stderr,"wait_for_fifo(%d) TIMEOUT (0x%08x)\n",e,rd(FIFO_STAT)); return -1;
}
static int wait_for_idle(void)
{
    int i; if (wait_for_fifo(16)<0) return -1;
    for (i=0;i<1000000;i++) if (!(rd(GUI_STAT)&GUI_ACTIVE)) return 0;
    fprintf(stderr,"wait_for_idle TIMEOUT (0x%08x)\n",rd(GUI_STAT)); return -1;
}
static void ewr(unsigned o, unsigned v){ wait_for_fifo(1); wr(o,v); }

static const unsigned ctx_regs[] = {
    SCALE_3D_CNTL, SETUP_CNTL, Z_CNTL, DP_PIX_WIDTH, DST_OFF_PITCH,
    SC_LEFT_RIGHT, SC_TOP_BOTTOM, DP_WRITE_MASK, DP_MIX, DP_SRC, CLR_CMP_CNTL,
};
#define NCTX (sizeof(ctx_regs)/sizeof(ctx_regs[0]))

/* the triangle (screen coords, relative to the render-target base) */
static const int TX[3] = { 32, 96, 48 };
static const int TY[3] = { 32, 40, 110 };

static unsigned pix(int x, int y){ return OSReadLittleInt32((void*)FB, TARGET_OFF + y*ROW_BYTES + x*4); }

int main(void)
{
    io_service_t svc; io_connect_t conn; kern_return_t kr;
    vm_address_t addr=0; vm_size_t size=0;
    unsigned saved[NCTX]; int i, x, y;
    int xx[3], yy[3];
    double area; float ooa; unsigned oobits;

    svc = IOServiceGetMatchingService(kIOMasterPortDefault,
                                      IOServiceMatching("Mach64Accelerator"));
    if (!svc){ fprintf(stderr,"no Mach64Accelerator service\n"); return 1; }
    kr = IOServiceOpen(svc, mach_task_self(), MACH64_GL_CONNECT_TYPE, &conn);
    IOObjectRelease(svc);
    if (kr!=KERN_SUCCESS){ fprintf(stderr,"IOServiceOpen: 0x%x\n",kr); return 1; }
    kr = IOConnectMapMemory(conn, MACH64_MEM_SURFACE, mach_task_self(),
                            &addr,&size, kIOMapAnywhere|kIOMapInhibitCache);
    if (kr!=KERN_SUCCESS){ fprintf(stderr,"map: 0x%x\n",kr); IOServiceClose(conn); return 1; }
    FB=addr; REGB=addr+REG_WINDOW;

    printf("engine idle wait...\n"); fflush(stdout);
    if (wait_for_idle()<0){ fprintf(stderr,"not idle; aborting\n"); goto out; }
    for (i=0;i<(int)NCTX;i++) saved[i]=rd(ctx_regs[i]);

    /* zero the bounding box so readback proves the ENGINE drew */
    for (y=0;y<120;y++) for (x=0;x<128;x++)
        OSWriteLittleInt32((void*)FB, TARGET_OFF + y*ROW_BYTES + x*4, 0);

    /* reciprocal area (see recipe): xx=x*4, yy=y*4 */
    for (i=0;i<3;i++){ xx[i]=TX[i]*4; yy[i]=TY[i]*4; }
    area = (1.0/16.0) * ((double)(xx[0]-xx[2])*(yy[1]-yy[2])
                       - (double)(yy[0]-yy[2])*(xx[1]-xx[2]));
    printf("triangle (%d,%d)(%d,%d)(%d,%d) area=%.1f\n",
           TX[0],TY[0],TX[1],TY[1],TX[2],TY[2],area);
    if (area==0){ fprintf(stderr,"degenerate\n"); goto restore; }
    ooa = (float)(1.0/area);
    memcpy(&oobits,&ooa,4);

    printf("programming 3D triangle...\n"); fflush(stdout);
    /* 3D state */
    ewr(SCALE_3D_CNTL, 0x000100c1);
    ewr(SETUP_CNTL,    0x00000040);
    ewr(Z_CNTL,        0);
    ewr(DP_PIX_WIDTH,  0x60060666);          /* ARGB8888 */
    ewr(DST_OFF_PITCH, ((PITCH_PIX/8)<<22) | (TARGET_OFF>>3));
    ewr(SC_LEFT_RIGHT, (0x1fff<<16)|0);
    ewr(SC_TOP_BOTTOM, (0x1fff<<16)|0);
    ewr(DP_WRITE_MASK, 0xffffffff);
    ewr(CLR_CMP_CNTL,  0);
    ewr(DP_MIX,        (3<<0)|(7<<16));       /* BKGD_MIX_D | FRGD_MIX_S */
    ewr(DP_SRC,        (5<<0)|(5<<8));        /* BKGD_SRC_3D | FRGD_SRC_3D */

    /* three vertices: Z, ARGB, X_Y */
    for (i=0;i<3;i++){
        ewr(VTX_Z(i),    0);
        ewr(VTX_ARGB(i), TRI_COLOR);
        ewr(VTX_XY(i),   ((xx[i])<<16) | (yy[i] & 0xffff));  /* X high, Y low */
    }
    /* TRIGGER */
    ewr(ONE_OVER_AREA_UC, oobits);

    if (wait_for_idle()<0) fprintf(stderr,"warning: engine didn't go idle\n");

restore:
    for (i=0;i<(int)NCTX;i++){ wait_for_fifo(1); wr(ctx_regs[i], saved[i]); }
    wait_for_idle();

    /* verify: sample clearly-interior pixels + a couple outside */
    printf("\nreadback:\n");
    {
        struct { int x,y,inside; } samp[] = {
            {58,60,1},{50,55,1},{60,72,1},{55,45,1},  /* interior */
            {10,10,0},{110,110,0},                    /* outside  */
        };
        int nin=0, hit=0;
        for (i=0;i<6;i++){
            unsigned v=pix(samp[i].x,samp[i].y);
            printf("  (%3d,%3d) %-7s = 0x%08x  %s\n", samp[i].x,samp[i].y,
                   samp[i].inside?"inside":"outside", v,
                   v==TRI_COLOR ? "FILLED" : "");
            if (samp[i].inside){ nin++; if(v==TRI_COLOR) hit++; }
        }
        printf("\n%s\n", (hit==nin && nin>0)
               ? "3D TRIANGLE CONFIRMED: the setup engine rasterized our triangle!"
               : "triangle not (fully) present -- see notes (ooa sign / DP_SRC / SETUP_CNTL)");
    }
out:
    IOConnectUnmapMemory(conn, MACH64_MEM_SURFACE, mach_task_self(), addr);
    IOServiceClose(conn);
    return 0;
}

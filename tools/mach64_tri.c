/*
 * mach64_tri.c - 3D solid triangle into off-screen VRAM, readback-verified.
 *
 * Progress: the setup engine rasterizes the correct SHAPE, but with DP_SRC=3D
 * (gouraud) the color comes out 0xffffffff -- the per-vertex ARGB isn't
 * reaching the interpolator via individual MMIO writes.  For a FLAT triangle we
 * don't need gouraud: take coverage from the setup engine but color from
 * DP_FRGD_CLR (as the 2D fill does).  This run tests color-source variants:
 *   A: DP_SRC=FRGD_CLR (0x101) + DP_FRGD_CLR=color      [flat color, likely]
 *   B: DP_SRC=3D but write ARGB AFTER X_Y (latch order)
 *   C: DP_SRC=FRGD_CLR + scale_3d_cntl=0 (coverage only)
 *
 * See docs/re-notes/3d-triangle.md.  Build with -framework IOKit.
 */
#include <IOKit/IOKitLib.h>
#include <libkern/OSByteOrder.h>
#include <mach/mach.h>
#include <string.h>
#include <stdio.h>
#include "../common/mach64_abi.h"

#define REG_WINDOW    0x7FF800u
#define SETUP_CNTL      0x0304
#define ONE_OVER_AREA_UC 0x0300
#define DST_OFF_PITCH   0x0500
#define Z_CNTL          0x054c
#define SCALE_3D_CNTL   0x05fc
#define DP_FRGD_CLR     0x06c4
#define SC_LEFT_RIGHT   0x06a8
#define SC_TOP_BOTTOM   0x06b4
#define DP_WRITE_MASK   0x06c8
#define DP_PIX_WIDTH    0x06d0
#define DP_MIX          0x06d4
#define DP_SRC          0x06d8
#define CLR_CMP_CNTL    0x0708
#define FIFO_STAT       0x0710
#define GUI_STAT        0x0738
#define VTX_Z(n)        (0x0240 + (n)*0x20 + 0x10)
#define VTX_ARGB(n)     (0x0240 + (n)*0x20 + 0x14)
#define VTX_XY(n)       (0x0240 + (n)*0x20 + 0x18)
#define FIFO_SLOT_MASK  0x0000ffff
#define GUI_ACTIVE      0x00000001
#define PITCH_PIX    128
#define ROW_BYTES    (PITCH_PIX * 4)
#define TRI_COLOR    0xAA33CC55u

static unsigned REGB, FB;
static unsigned rd(unsigned o){ return OSReadLittleInt32((void*)REGB, o); }
static void     wr(unsigned o, unsigned v){ OSWriteLittleInt32((void*)REGB, o, v); }
static int wfifo(int e){ int i; for(i=0;i<1000000;i++) if((rd(FIFO_STAT)&FIFO_SLOT_MASK)<=(unsigned)(0x8000>>e))return 0; return -1; }
static int widle(void){ int i; if(wfifo(16)<0)return -1; for(i=0;i<1000000;i++) if(!(rd(GUI_STAT)&GUI_ACTIVE))return 0; return -1; }
static void ewr(unsigned o, unsigned v){ wfifo(1); wr(o,v); }

static const unsigned ctx_regs[] = {
    SCALE_3D_CNTL,SETUP_CNTL,Z_CNTL,DP_PIX_WIDTH,DST_OFF_PITCH,DP_FRGD_CLR,
    SC_LEFT_RIGHT,SC_TOP_BOTTOM,DP_WRITE_MASK,DP_MIX,DP_SRC,CLR_CMP_CNTL };
#define NCTX (sizeof(ctx_regs)/sizeof(ctx_regs[0]))
static const int TX[3]={32,96,48}, TY[3]={32,40,110};

/* dpsrc: color source; frgd: DP_FRGD_CLR; argb_last: write ARGB after X_Y */
static void draw_tri(unsigned target, unsigned scale3d, unsigned dpsrc,
                     unsigned frgd, int argb_last)
{
    int i,x,y,xx[3],yy[3]; double area; float ooa; unsigned oob;
    for (y=0;y<120;y++) for (x=0;x<128;x++)
        OSWriteLittleInt32((void*)FB, target + y*ROW_BYTES + x*4, 0);
    for (i=0;i<3;i++){ xx[i]=TX[i]*4; yy[i]=TY[i]*4; }
    area=(1.0/16.0)*((double)(xx[0]-xx[2])*(yy[1]-yy[2])-(double)(yy[0]-yy[2])*(xx[1]-xx[2]));
    ooa=(float)(1.0/area); memcpy(&oob,&ooa,4);
    widle();
    ewr(SCALE_3D_CNTL, scale3d);
    ewr(SETUP_CNTL,    0x00000040);
    ewr(Z_CNTL,        0);
    ewr(DP_PIX_WIDTH,  0x60060666);
    ewr(DST_OFF_PITCH, ((PITCH_PIX/8)<<22)|(target>>3));
    ewr(DP_FRGD_CLR,   frgd);
    ewr(SC_LEFT_RIGHT, 0x1fff0000);
    ewr(SC_TOP_BOTTOM, 0x3fff0000);
    ewr(DP_WRITE_MASK, 0xffffffff);
    ewr(CLR_CMP_CNTL,  0);
    ewr(DP_MIX,        0x00070007);
    ewr(DP_SRC,        dpsrc);
    for (i=0;i<3;i++){
        ewr(VTX_Z(i),0);
        if (argb_last){ ewr(VTX_XY(i),(xx[i]<<16)|(yy[i]&0xffff)); ewr(VTX_ARGB(i),TRI_COLOR); }
        else          { ewr(VTX_ARGB(i),TRI_COLOR); ewr(VTX_XY(i),(xx[i]<<16)|(yy[i]&0xffff)); }
    }
    ewr(ONE_OVER_AREA_UC, oob);
    widle();
}
static unsigned centroid(unsigned target){ return OSReadLittleInt32((void*)FB, target + 60*ROW_BYTES + 58*4); }

int main(void)
{
    io_service_t svc; io_connect_t conn; kern_return_t kr;
    vm_address_t addr=0; vm_size_t size=0; unsigned saved[NCTX]; int i;
    struct { const char *name; unsigned off, scale, dpsrc, frgd; int argb_last; } V[] = {
        { "A frgd-clr color ", 0x600000, 0x000100c1, 0x00000101, TRI_COLOR, 0 },
        { "B 3D argb-last    ", 0x640000, 0x000100c1, 0x00000505, 0,         1 },
        { "C frgd + scale0   ", 0x680000, 0x00000000, 0x00000101, TRI_COLOR, 0 },
    };
    svc=IOServiceGetMatchingService(kIOMasterPortDefault,IOServiceMatching("Mach64Accelerator"));
    if(!svc){fprintf(stderr,"no service\n");return 1;}
    kr=IOServiceOpen(svc,mach_task_self(),MACH64_GL_CONNECT_TYPE,&conn); IOObjectRelease(svc);
    if(kr){fprintf(stderr,"open 0x%x\n",kr);return 1;}
    kr=IOConnectMapMemory(conn,MACH64_MEM_SURFACE,mach_task_self(),&addr,&size,kIOMapAnywhere|kIOMapInhibitCache);
    if(kr){fprintf(stderr,"map 0x%x\n",kr);IOServiceClose(conn);return 1;}
    FB=addr; REGB=addr+REG_WINDOW;
    if(widle()<0){fprintf(stderr,"not idle\n");goto out;}
    for(i=0;i<(int)NCTX;i++) saved[i]=rd(ctx_regs[i]);

    printf("target color 0x%08x; testing color-source variants...\n\n", TRI_COLOR);
    for(i=0;i<3;i++){
        draw_tri(V[i].off, V[i].scale, V[i].dpsrc, V[i].frgd, V[i].argb_last);
        unsigned c=centroid(V[i].off);
        printf("  %s dpsrc=0x%04x scale=0x%08x -> centroid=0x%08x  %s\n",
               V[i].name, V[i].dpsrc, V[i].scale, c,
               c==TRI_COLOR?"*** CORRECT ***":(c?"(other)":"(empty)"));
    }
    for(i=0;i<(int)NCTX;i++){ wfifo(1); wr(ctx_regs[i],saved[i]); }
    widle();
out:
    IOConnectUnmapMemory(conn,MACH64_MEM_SURFACE,mach_task_self(),addr);
    IOServiceClose(conn); return 0;
}

/*
 * mach64_tri.c - 3D solid triangle into off-screen VRAM, readback-verified.
 *
 * v1 proved the SETUP ENGINE rasterizes correctly (perfect triangle shape) but
 * the color came out 0xffffffff instead of the vertex ARGB.  To nail the color
 * path in one hardware run, this draws the SAME triangle three ways to three
 * off-screen targets and reads each back:
 *   A: DRI-exact state (dp_mix=0x70007, scale_3d_cntl=0xb4100cb)
 *   B: minimal scale_3d_cntl=0x100c1 + dp_mix fix
 *   C: minimal without SCALE_PIX_EXPAND_DYNAMIC_RANGE (0x100c0)
 * Whichever reads back 0xAA33CC55 is the right state.
 *
 * Recipe + safety per docs/re-notes/3d-triangle.md and mach64_fill.c.
 * Build with -framework IOKit.
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
static int wait_for_fifo(int e){ int i; for(i=0;i<1000000;i++) if((rd(FIFO_STAT)&FIFO_SLOT_MASK)<=(unsigned)(0x8000>>e))return 0; return -1; }
static int wait_for_idle(void){ int i; if(wait_for_fifo(16)<0)return -1; for(i=0;i<1000000;i++) if(!(rd(GUI_STAT)&GUI_ACTIVE))return 0; return -1; }
static void ewr(unsigned o, unsigned v){ wait_for_fifo(1); wr(o,v); }

static const unsigned ctx_regs[] = {
    SCALE_3D_CNTL,SETUP_CNTL,Z_CNTL,DP_PIX_WIDTH,DST_OFF_PITCH,
    SC_LEFT_RIGHT,SC_TOP_BOTTOM,DP_WRITE_MASK,DP_MIX,DP_SRC,CLR_CMP_CNTL };
#define NCTX (sizeof(ctx_regs)/sizeof(ctx_regs[0]))

static const int TX[3]={32,96,48}, TY[3]={32,40,110};

/* draw the triangle to target off-screen offset with the given state */
static void draw_tri(unsigned target, unsigned scale3d, unsigned dpmix)
{
    int i,x,y,xx[3],yy[3]; double area; float ooa; unsigned oob;
    for (y=0;y<120;y++) for (x=0;x<128;x++)
        OSWriteLittleInt32((void*)FB, target + y*ROW_BYTES + x*4, 0);
    for (i=0;i<3;i++){ xx[i]=TX[i]*4; yy[i]=TY[i]*4; }
    area=(1.0/16.0)*((double)(xx[0]-xx[2])*(yy[1]-yy[2])-(double)(yy[0]-yy[2])*(xx[1]-xx[2]));
    ooa=(float)(1.0/area); memcpy(&oob,&ooa,4);
    wait_for_idle();
    ewr(SCALE_3D_CNTL, scale3d);
    ewr(SETUP_CNTL,    0x00000040);
    ewr(Z_CNTL,        0);
    ewr(DP_PIX_WIDTH,  0x60060666);
    ewr(DST_OFF_PITCH, ((PITCH_PIX/8)<<22)|(target>>3));
    ewr(SC_LEFT_RIGHT, 0x1fff0000);
    ewr(SC_TOP_BOTTOM, 0x3fff0000);
    ewr(DP_WRITE_MASK, 0xffffffff);
    ewr(CLR_CMP_CNTL,  0);
    ewr(DP_MIX,        dpmix);
    ewr(DP_SRC,        (5<<0)|(5<<8)|(0<<16));   /* BKGD_SRC_3D|FRGD_SRC_3D|MONO_ONE */
    for (i=0;i<3;i++){
        ewr(VTX_Z(i),0); ewr(VTX_ARGB(i),TRI_COLOR);
        ewr(VTX_XY(i), (xx[i]<<16)|(yy[i]&0xffff));
    }
    ewr(ONE_OVER_AREA_UC, oob);
    wait_for_idle();
}
static unsigned centroid(unsigned target){ int x=58,y=60; return OSReadLittleInt32((void*)FB, target + y*ROW_BYTES + x*4); }

int main(void)
{
    io_service_t svc; io_connect_t conn; kern_return_t kr;
    vm_address_t addr=0; vm_size_t size=0; unsigned saved[NCTX]; int i;
    struct { const char *name; unsigned off, scale, mix; } V[] = {
        { "A DRI-exact      ", 0x600000, 0x0b4100cb, 0x00070007 },
        { "B minimal+mixfix ", 0x640000, 0x000100c1, 0x00070007 },
        { "C no-pix-expand  ", 0x680000, 0x000100c0, 0x00070007 },
    };
    svc=IOServiceGetMatchingService(kIOMasterPortDefault,IOServiceMatching("Mach64Accelerator"));
    if(!svc){fprintf(stderr,"no service\n");return 1;}
    kr=IOServiceOpen(svc,mach_task_self(),MACH64_GL_CONNECT_TYPE,&conn); IOObjectRelease(svc);
    if(kr){fprintf(stderr,"open 0x%x\n",kr);return 1;}
    kr=IOConnectMapMemory(conn,MACH64_MEM_SURFACE,mach_task_self(),&addr,&size,kIOMapAnywhere|kIOMapInhibitCache);
    if(kr){fprintf(stderr,"map 0x%x\n",kr);IOServiceClose(conn);return 1;}
    FB=addr; REGB=addr+REG_WINDOW;

    if(wait_for_idle()<0){fprintf(stderr,"not idle\n");goto out;}
    for(i=0;i<(int)NCTX;i++) saved[i]=rd(ctx_regs[i]);

    printf("target color 0x%08x; drawing 3 variants...\n\n", TRI_COLOR);
    for(i=0;i<3;i++){
        draw_tri(V[i].off, V[i].scale, V[i].mix);
        unsigned c=centroid(V[i].off);
        printf("  %s scale=0x%08x mix=0x%08x -> centroid=0x%08x  %s\n",
               V[i].name, V[i].scale, V[i].mix, c,
               c==TRI_COLOR?"*** CORRECT ***":(c?"(wrong color)":"(empty)"));
    }
    for(i=0;i<(int)NCTX;i++){ wait_for_fifo(1); wr(ctx_regs[i],saved[i]); }
    wait_for_idle();
out:
    IOConnectUnmapMemory(conn,MACH64_MEM_SURFACE,mach_task_self(),addr);
    IOServiceClose(conn); return 0;
}

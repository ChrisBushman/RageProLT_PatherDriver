/*
 * mach64_gouraud.c - test per-vertex (gouraud) color by submitting vertices as
 * auto-incrementing packets through the bus-master data port (BM_ADDR), the way
 * the DRI does -- instead of individual register writes (which don't latch the
 * vertex ARGB).  This is "pseudo-DMA": MMIO writes to BM_ADDR, no DMA ring yet.
 *
 * 3D state is set directly (known-good); only the vertices + trigger go through
 * BM_ADDR.  Three distinct vertex colors (R,G,B) -> if gouraud works we see a
 * smooth gradient (each corner ~its color, centroid blended).  DP_SRC=3D.
 *
 * Command stream format (from DRI): each packet = (count<<16)|ADRINDEX(start),
 * then count+1 values, written successively to BM_ADDR.
 *
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
#define DP_FRGD_CLR     0x06c4
#define SC_LEFT_RIGHT   0x06a8
#define SC_TOP_BOTTOM   0x06b4
#define DP_WRITE_MASK   0x06c8
#define DP_PIX_WIDTH    0x06d0
#define DP_MIX          0x06d4
#define DP_SRC          0x06d8
#define BM_ADDR         0x0648
#define CLR_CMP_CNTL    0x0708
#define FIFO_STAT       0x0710
#define GUI_STAT        0x0738
#define VTX_Z(n)        (0x0240 + (n)*0x20 + 0x10)
#define FIFO_SLOT_MASK  0x0000ffff
#define GUI_ACTIVE      0x00000001
#define PITCH_PIX    128
#define ROW_BYTES    (PITCH_PIX*4)
#define ADRINDEX(r)  ((unsigned)((r) >= 0x400 ? (((r)-0x400)>>2) : (((r)>>2)|0x100)))

static unsigned REGB, FB;
static unsigned rd(unsigned o){ return OSReadLittleInt32((void*)REGB, o); }
static void     wr(unsigned o, unsigned v){ OSWriteLittleInt32((void*)REGB, o, v); }
static int wfifo(int e){ int i; for(i=0;i<1000000;i++) if((rd(FIFO_STAT)&FIFO_SLOT_MASK)<=(unsigned)(0x8000>>e))return 0; return -1; }
static int widle(void){ int i; if(wfifo(16)<0)return -1; for(i=0;i<1000000;i++) if(!(rd(GUI_STAT)&GUI_ACTIVE))return 0; return -1; }
static void ewr(unsigned o, unsigned v){ wfifo(1); wr(o,v); }
static void bm(unsigned dword){ wfifo(1); wr(BM_ADDR, dword); }   /* feed BM data port */

static const unsigned ctx_regs[] = {
    SCALE_3D_CNTL,SETUP_CNTL,Z_CNTL,DP_PIX_WIDTH,DST_OFF_PITCH,DP_FRGD_CLR,
    SC_LEFT_RIGHT,SC_TOP_BOTTOM,DP_WRITE_MASK,DP_MIX,DP_SRC,CLR_CMP_CNTL };
#define NCTX (sizeof(ctx_regs)/sizeof(ctx_regs[0]))
static const int TX[3]={20,110,50}, TY[3]={16,44,116};
static const unsigned VC[3]={0xFFFF0000u,0xFF00FF00u,0xFF0000FFu};  /* R,G,B */
static unsigned pix(unsigned t,int x,int y){ return OSReadLittleInt32((void*)FB, t + y*ROW_BYTES + x*4); }

int main(void)
{
    io_service_t svc; io_connect_t conn; kern_return_t kr;
    vm_address_t addr=0; vm_size_t size=0; unsigned saved[NCTX];
    int i,x,y,xx[3],yy[3]; double area; float ooa; unsigned oob; unsigned T=0x600000;

    svc=IOServiceGetMatchingService(kIOMasterPortDefault,IOServiceMatching("Mach64Accelerator"));
    if(!svc){fprintf(stderr,"no service\n");return 1;}
    kr=IOServiceOpen(svc,mach_task_self(),MACH64_GL_CONNECT_TYPE,&conn); IOObjectRelease(svc);
    if(kr){fprintf(stderr,"open 0x%x\n",kr);return 1;}
    kr=IOConnectMapMemory(conn,MACH64_MEM_SURFACE,mach_task_self(),&addr,&size,kIOMapAnywhere|kIOMapInhibitCache);
    if(kr){fprintf(stderr,"map 0x%x\n",kr);IOServiceClose(conn);return 1;}
    FB=addr; REGB=addr+REG_WINDOW;
    if(widle()<0){fprintf(stderr,"not idle\n");goto out;}
    for(i=0;i<(int)NCTX;i++) saved[i]=rd(ctx_regs[i]);
    for(y=0;y<128;y++) for(x=0;x<128;x++) OSWriteLittleInt32((void*)FB, T + y*ROW_BYTES + x*4, 0);
    for(i=0;i<3;i++){ xx[i]=TX[i]*4; yy[i]=TY[i]*4; }
    area=(1.0/16.0)*((double)(xx[0]-xx[2])*(yy[1]-yy[2])-(double)(yy[0]-yy[2])*(xx[1]-xx[2]));
    ooa=(float)(1.0/area); memcpy(&oob,&ooa,4);

    printf("gouraud triangle via BM_ADDR: v0=RED v1=GREEN v2=BLUE\n\n");
    /* state directly (DP_SRC=3D for gouraud) */
    ewr(SCALE_3D_CNTL,0x000100c1); ewr(SETUP_CNTL,0x40); ewr(Z_CNTL,0);
    ewr(DP_PIX_WIDTH,0x60060666); ewr(DST_OFF_PITCH, ((PITCH_PIX/8)<<22)|(T>>3));
    ewr(SC_LEFT_RIGHT,0x1fff0000); ewr(SC_TOP_BOTTOM,0x3fff0000);
    ewr(DP_WRITE_MASK,0xffffffff); ewr(CLR_CMP_CNTL,0);
    ewr(DP_MIX,0x00070007); ewr(DP_SRC,(5<<0)|(5<<8));   /* 3D source */

    /* vertices via BM_ADDR packets: (2<<16)|ADRINDEX(VTX_Z), Z, ARGB, X_Y */
    for(i=0;i<3;i++){
        bm( (2<<16) | ADRINDEX(VTX_Z(i)) );
        bm( 0 );                                   /* Z */
        bm( VC[i] );                               /* ARGB (per-vertex color) */
        bm( (xx[i]<<16)|(yy[i]&0xffff) );          /* X_Y */
    }
    /* trigger via BM_ADDR */
    bm( ADRINDEX(ONE_OVER_AREA_UC) );  /* count 0 */
    bm( oob );

    if(widle()<0) fprintf(stderr,"warning: not idle\n");
    for(i=0;i<(int)NCTX;i++){ wfifo(1); wr(ctx_regs[i],saved[i]); }
    widle();

    /* sample near each vertex + centroid */
    printf("readback (per-vertex color proves gouraud):\n");
    printf("  near v0 (RED)   (%d,%d) = 0x%08x\n", TX[0]+6,TY[0]+14, pix(T,TX[0]+6,TY[0]+14));
    printf("  near v1 (GREEN) (%d,%d) = 0x%08x\n", TX[1]-14,TY[1]+6, pix(T,TX[1]-14,TY[1]+6));
    printf("  near v2 (BLUE)  (%d,%d) = 0x%08x\n", TX[2]+2,TY[2]-14, pix(T,TX[2]+2,TY[2]-14));
    printf("  centroid        (58,58) = 0x%08x\n", pix(T,58,58));
    {   int filled=0; for(y=0;y<124;y++) for(x=0;x<124;x++) if(pix(T,x,y)) filled++;
        printf("\n%d filled pixels; ", filled);
        unsigned a=pix(T,TX[0]+6,TY[0]+14), b=pix(T,TX[1]-14,TY[1]+6), c=pix(T,TX[2]+2,TY[2]-14);
        if(filled>200 && (a!=b || b!=c) && a!=0xffffffff)
            printf("GOURAUD WORKS: per-vertex colors interpolated via BM_ADDR!\n");
        else if(filled>200 && a==0xffffffff)
            printf("shape ok but still white -- BM_ADDR path didn't latch color either\n");
        else printf("triangle not present\n");
    }
out:
    IOConnectUnmapMemory(conn,MACH64_MEM_SURFACE,mach_task_self(),addr);
    IOServiceClose(conn); return 0;
}

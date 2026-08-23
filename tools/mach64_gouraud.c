/*
 * mach64_gouraud.c - per-vertex (gouraud) 3D triangle via the REAL bus-master
 * DMA ring (proven in mach64_dmatest).  The setup engine interpolates the three
 * vertex colors across the triangle; the pixel color comes from the 3D scaler
 * (DP_SRC = BKGD/FRGD_SRC_3D), not the flat DP_FRGD_CLR.
 *
 * Why DMA: individual MMIO writes to VERTEX_n_ARGB don't latch into the
 * interpolator (flat triangle works, gouraud reads back white).  The DRI submits
 * the vertices as a register-write STREAM through the bus-master GUI path
 * ("Mach64's vertex data is actually register writes", drm_mach64_state.c) --
 * which we now know reaches the GUI drawing-engine block.
 *
 * Flow: set static 3D state via MMIO -> build a DMA command buffer with the 3
 * vertex packets + the ONE_OVER_AREA_UC trigger + a SRC_CNTL=0 terminator ->
 * point a ring descriptor at it -> quiesce/save ATIRagePro's DMA regs ->
 * activate bus-master + kick -> restore -> read back the off-screen gradient.
 *
 * Requires 'go' to actually activate DMA (same safety model as mach64_dmatest).
 * Build with -framework IOKit.
 */
#include <IOKit/IOKitLib.h>
#include <libkern/OSByteOrder.h>
#include <mach/mach.h>
#include <string.h>
#include <stdio.h>
#include "../common/mach64_abi.h"

#define RAM_LIMIT     0x20000000u
#define REG_WINDOW    0x7FF800u
#define APERTURE_OFFSET 0x7ff800u

/* 3D state registers (verified working in mach64_tri.c) */
#define SETUP_CNTL       0x0304
#define ONE_OVER_AREA_UC 0x0300
#define DST_OFF_PITCH    0x0500
#define Z_CNTL           0x054c
#define SCALE_3D_CNTL    0x05fc
#define DP_FRGD_CLR      0x06c4
#define SC_LEFT_RIGHT    0x06a8
#define SC_TOP_BOTTOM    0x06b4
#define DP_WRITE_MASK    0x06c8
#define DP_PIX_WIDTH     0x06d0
#define DP_MIX           0x06d4
#define DP_SRC           0x06d8
#define CLR_CMP_CNTL     0x0708
#define FIFO_STAT        0x0710
#define GUI_STAT         0x0738
#define VTX_Z(n)         (0x0240 + (n)*0x20 + 0x10)   /* VERTEX_1/2/3_Z */

/* bus-master DMA registers */
#define BUS_CNTL         0x04a0
#define SRC_CNTL         0x05b4
#define DST_HEIGHT_WIDTH 0x0518
#define BM_ADDR          0x0648
#define BM_GUI_TABLE_CMD 0x064c
#define BM_GUI_TABLE     0x01b8

#define BUS_MASTER_DIS   (1u<<6)
#define BUS_EXT_REG_EN   (1u<<27)
#define SRC_BM_ENABLE    (1u<<8)
#define SRC_BM_SYNC      (1u<<9)
#define SRC_BM_OP_SYSTEM_TO_REG (3u<<10)
#define DMA_EOL          (1u<<31)
#define DMA_HOLD_OFFSET  (1u<<30)

#define FIFO_SLOT_MASK   0x0000ffffu
#define GUI_ACTIVE       0x00000001u
#define PITCH_PIX        128
#define ROW_BYTES        (PITCH_PIX*4)
#define TARGET_OFF       0x600000u
#define ADRINDEX(r)  ((unsigned)((r) >= 0x400 ? (((r)-0x400)>>2) : (((r)>>2)|0x100)))

static void mb(void){ __asm__ volatile("sync; eieio" ::: "memory"); }
static unsigned REGB, FB, RING, CMD;
static unsigned rd(unsigned o){ return OSReadLittleInt32((void*)REGB, o); }
static void     wr(unsigned o, unsigned v){ OSWriteLittleInt32((void*)REGB, o, v); }
static void     rw(unsigned o, unsigned v){ OSWriteLittleInt32((void*)RING, o, v); }
static void     cw(unsigned o, unsigned v){ OSWriteLittleInt32((void*)CMD, o, v); }
static int wfifo(int e){ int i; for(i=0;i<1000000;i++) if((rd(FIFO_STAT)&FIFO_SLOT_MASK)<=(unsigned)(0x8000>>e))return 0; return -1; }
static int widle(void){ int i; if(wfifo(16)<0)return -1; for(i=0;i<2000000;i++) if(!(rd(GUI_STAT)&GUI_ACTIVE))return 0; return -1; }
static void ewr(unsigned o, unsigned v){ wfifo(1); wr(o,v); }

static unsigned mapmem(io_connect_t c, unsigned type)
{
    vm_address_t a=0; vm_size_t s=0;
    if (IOConnectMapMemory(c,type,mach_task_self(),&a,&s,kIOMapAnywhere|kIOMapInhibitCache)!=KERN_SUCCESS)
        return 0;
    return a;
}

static const unsigned ctx_regs[] = {
    SCALE_3D_CNTL,SETUP_CNTL,Z_CNTL,DP_PIX_WIDTH,DST_OFF_PITCH,DP_FRGD_CLR,
    SC_LEFT_RIGHT,SC_TOP_BOTTOM,DP_WRITE_MASK,DP_MIX,DP_SRC,CLR_CMP_CNTL };
#define NCTX (sizeof(ctx_regs)/sizeof(ctx_regs[0]))
static const int TX[3]={20,110,50}, TY[3]={16,44,116};
static const unsigned VC[3]={0xFFFF0000u,0xFF00FF00u,0xFF0000FFu};  /* R,G,B */
static unsigned pix(int x,int y){ return OSReadLittleInt32((void*)FB, TARGET_OFF + y*ROW_BYTES + x*4); }

int main(int argc, char **argv)
{
    io_service_t svc; io_connect_t conn; kern_return_t kr;
    vm_address_t fb=0; vm_size_t fbs=0; unsigned saved[NCTX];
    unsigned cmdbus=0, ringbus=0, cfg=0, off, count;
    unsigned save_bus, save_src, save_tbl;
    int i,x,y,xx[3],yy[3],go,valid; double area; float ooa; unsigned oob;

    svc=IOServiceGetMatchingService(kIOMasterPortDefault,IOServiceMatching("Mach64Accelerator"));
    if(!svc){fprintf(stderr,"no service\n");return 1;}
    kr=IOServiceOpen(svc,mach_task_self(),MACH64_GL_CONNECT_TYPE,&conn); IOObjectRelease(svc);
    if(kr){fprintf(stderr,"open 0x%x\n",kr);return 1;}
    if(IOConnectMapMemory(conn,MACH64_MEM_SURFACE,mach_task_self(),&fb,&fbs,kIOMapAnywhere|kIOMapInhibitCache)){fprintf(stderr,"map fb\n");return 1;}
    FB=fb; REGB=fb+REG_WINDOW;
    CMD=mapmem(conn,MACH64_MEM_CMDBUF);
    RING=mapmem(conn,MACH64_MEM_RING);
    if(!CMD||!RING){fprintf(stderr,"map cmd/ring failed\n");goto out;}

    IOConnectMethodScalarIScalarO(conn, kMach64Sel_get_config, 0, 3, &cmdbus, &ringbus, &cfg);
    printf("cmdbuf bus=0x%08x  ring bus=0x%08x\n", cmdbus, ringbus);
    valid = cmdbus>=0x1000 && cmdbus<RAM_LIMIT && ringbus>=0x1000 && ringbus<RAM_LIMIT;
    if(!valid){ printf("*** INVALID bus addr -- refusing to activate DMA ***\n"); goto out; }
    go = (argc>1 && strcmp(argv[1],"go")==0);
    if(!go){ printf("DRY RUN ok (addresses valid). Re-run with 'go' to draw.\n"); goto out; }

    if(widle()<0){fprintf(stderr,"not idle\n");goto out;}
    for(i=0;i<(int)NCTX;i++) saved[i]=rd(ctx_regs[i]);
    for(y=0;y<128;y++) for(x=0;x<128;x++) OSWriteLittleInt32((void*)FB, TARGET_OFF + y*ROW_BYTES + x*4, 0);
    for(i=0;i<3;i++){ xx[i]=TX[i]*4; yy[i]=TY[i]*4; }
    area=(1.0/16.0)*((double)(xx[0]-xx[2])*(yy[1]-yy[2])-(double)(yy[0]-yy[2])*(xx[1]-xx[2]));
    ooa=(float)(1.0/area); memcpy(&oob,&ooa,4);
    printf("gouraud triangle via DMA ring: v0=RED v1=GREEN v2=BLUE\n\n");

    /* static 3D state via MMIO -- same as the working flat triangle, except
     * DP_SRC selects the interpolating 3D scaler (gouraud) not DP_FRGD_CLR */
    ewr(SCALE_3D_CNTL, 0x000100c1);
    ewr(SETUP_CNTL,    0x00000040);      /* FLAT_SHADE bits clear -> smooth */
    ewr(Z_CNTL,        0);
    ewr(DP_PIX_WIDTH,  0x60060666);      /* ARGB8888 */
    ewr(DST_OFF_PITCH, ((PITCH_PIX/8)<<22)|(TARGET_OFF>>3));
    ewr(SC_LEFT_RIGHT, 0x1fff0000);
    ewr(SC_TOP_BOTTOM, 0x3fff0000);
    ewr(DP_WRITE_MASK, 0xffffffff);
    ewr(CLR_CMP_CNTL,  0);
    ewr(DP_MIX,        0x00070007);
    ewr(DP_SRC,        (5u<<0)|(5u<<8)); /* BKGD/FRGD_SRC_3D = 0x0505 (gouraud) */
    widle();

    /* DMA command buffer: 3 vertex packets + trigger + terminator.
     * Each vertex packet = header (3 consecutive regs) then Z, ARGB, X_Y:
     *   header = ADRINDEX(VERTEX_n_Z) | ((3-1)<<16) */
    off = 0;
    for(i=0;i<3;i++){
        cw(off, ADRINDEX(VTX_Z(i)) | (2u<<16));      off+=4;  /* Z,ARGB,X_Y */
        cw(off, 0);                                  off+=4;  /* VERTEX_n_Z   */
        cw(off, VC[i]);                              off+=4;  /* VERTEX_n_ARGB*/
        cw(off, (xx[i]<<16)|(yy[i]&0xffff));         off+=4;  /* VERTEX_n_X_Y */
    }
    cw(off, ADRINDEX(ONE_OVER_AREA_UC) | (0u<<16));  off+=4;  /* trigger */
    cw(off, oob);                                    off+=4;
    cw(off, ADRINDEX(SRC_CNTL) | (0u<<16));          off+=4;  /* terminator */
    cw(off, 0);                                      off+=4;
    count = off;   /* byte count of the command buffer */

    /* descriptor -> DMA the command buffer to the BM_ADDR data port */
    rw(0,  APERTURE_OFFSET + BM_ADDR);
    rw(4,  cmdbus);
    rw(8,  count | DMA_HOLD_OFFSET | DMA_EOL);
    rw(12, 0);

    /* quiesce + save ATIRagePro's DMA state, activate, kick */
    save_bus=rd(BUS_CNTL); save_src=rd(SRC_CNTL); save_tbl=rd(BM_GUI_TABLE);
    mb();
    if(widle()<0) fprintf(stderr,"warn: not idle before DMA\n");
    wr(BUS_CNTL, (save_bus & ~BUS_MASTER_DIS) | BUS_EXT_REG_EN);
    wr(BM_GUI_TABLE_CMD, ringbus);
    wr(SRC_CNTL, SRC_BM_ENABLE | SRC_BM_SYNC | SRC_BM_OP_SYSTEM_TO_REG);
    mb();
    wr(DST_HEIGHT_WIDTH, 0);              /* kick */
    if(widle()<0) fprintf(stderr,"warn: DMA didn't go idle (possible stall)\n");
    printf("after kick: BM_GUI_TABLE=0x%08x (ring base 0x%08x) GUI_STAT=0x%08x\n\n",
           rd(BM_GUI_TABLE), ringbus, rd(GUI_STAT));

    /* restore ATIRagePro's DMA state + our 3D context */
    wr(SRC_CNTL, save_src);
    wr(BM_GUI_TABLE_CMD, save_tbl & 0xfffffff0);
    wr(BUS_CNTL, save_bus);
    for(i=0;i<(int)NCTX;i++){ wfifo(1); wr(ctx_regs[i],saved[i]); }
    widle();

    /* ASCII readback: dominant channel per covered pixel (R/G/B).  A gradient
     * (>=2 distinct dominant channels) proves per-vertex color interpolation. */
    {
        int nfill=0, nR=0,nG=0,nB=0;
        printf("off-screen readback (R/G/B = dominant channel, '.' = empty):\n");
        for(y=0;y<124;y+=4){
            printf("  ");
            for(x=0;x<124;x+=2){
                unsigned v=pix(x,y);
                if(!v){ putchar('.'); continue; }
                unsigned r=(v>>16)&0xff, g=(v>>8)&0xff, b=v&0xff;
                nfill++;
                if(r>=g && r>=b){ putchar('R'); nR++; }
                else if(g>=r && g>=b){ putchar('G'); nG++; }
                else { putchar('B'); nB++; }
            }
            putchar('\n');
        }
        printf("\nsamples: near-v0=0x%08x near-v1=0x%08x near-v2=0x%08x centroid=0x%08x\n",
               pix(TX[0]+6,TY[0]+14), pix(TX[1]-14,TY[1]+6), pix(TX[2]+2,TY[2]-14), pix(58,58));
        printf("covered=%d  dominant R=%d G=%d B=%d\n", nfill, nR,nG,nB);
        {
            int classes = (nR>0)+(nG>0)+(nB>0);
            printf("\n%s\n", (nfill>50 && classes>=2)
                ? "GOURAUD CONFIRMED: setup engine interpolated per-vertex colors via DMA!"
                : (nfill>50 ? "shape drawn but single color -- gouraud didn't interpolate"
                            : "triangle not present"));
        }
    }
out:
    IOServiceClose(conn); return 0;
}

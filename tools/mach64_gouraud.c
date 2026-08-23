/*
 * mach64_gouraud.c - per-vertex (gouraud) 3D triangle via the bus-master DMA
 * ring.  The setup engine interpolates the three vertex colors (RED/GREEN/BLUE
 * corners) across the triangle; the pixel color comes from the 3D scaler
 * (DP_SRC = BKGD/FRGD_SRC_3D), not the flat DP_FRGD_CLR.
 *
 * Vertices are submitted as a DMA register-write stream (proven in
 * mach64_dmatest); individual MMIO writes to VERTEX_n_ARGB don't latch.
 *
 * The state was isolated by a sweep (git history): the flat triangle's
 * SETUP_CNTL=0x40 (LOG_MAX_INC_ADJ) broke color-gradient setup -> use 0; and
 * the vertex must carry W=1.0 (the scaler's color interp is perspective-correct,
 * so an unset W saturates every pixel to white).  Either fix alone works; we use
 * both.  Requires 'go'.  Build with -framework IOKit.
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

#define SETUP_CNTL       0x0304
#define ONE_OVER_AREA_UC 0x0300
#define DST_OFF_PITCH    0x0500
#define Z_CNTL           0x054c
#define SCALE_3D_CNTL    0x05fc
#define SC_LEFT_RIGHT    0x06a8
#define SC_TOP_BOTTOM    0x06b4
#define DP_WRITE_MASK    0x06c8
#define DP_PIX_WIDTH     0x06d0
#define DP_MIX           0x06d4
#define DP_SRC           0x06d8
#define CLR_CMP_CNTL     0x0708
#define FIFO_STAT        0x0710
#define GUI_STAT         0x0738
#define VTX_BASE(n)      (0x0240 + (n)*0x20)    /* S,T,W,SPEC,Z,ARGB,X_Y block */
#define VTX_Z(n)         (VTX_BASE(n) + 0x10)

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
#define ONE_F            0x3f800000u             /* 1.0f */
#define ADRINDEX(r)  ((unsigned)((r) >= 0x400 ? (((r)-0x400)>>2) : (((r)>>2)|0x100)))

static void mb(void){ __asm__ volatile("sync; eieio" ::: "memory"); }
static unsigned REGB, FB, RING, CMD, CMDBUS, RINGBUS;
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

static const int TX[3]={20,110,50}, TY[3]={16,44,116};
static const unsigned VC[3]={0xFFFF0000u,0xFF00FF00u,0xFF0000FFu};  /* R,G,B */
static unsigned pixT(unsigned t,int x,int y){ return OSReadLittleInt32((void*)FB, t + y*ROW_BYTES + x*4); }

/* draw the RGB triangle into off-screen buffer `targ` with the given state,
 * vertices submitted via the DMA ring.  full=1 writes the whole vertex block
 * (incl W=1.0); full=0 writes only {Z,ARGB,X_Y}. */
static void draw(unsigned targ, unsigned setup, unsigned scale, unsigned dpsrc, int full)
{
    int i,x,y,xx[3],yy[3]; double area; float ooa; unsigned oob, off, save_bus,save_src,save_tbl;

    for(y=0;y<128;y++) for(x=0;x<128;x++) OSWriteLittleInt32((void*)FB, targ + y*ROW_BYTES + x*4, 0);
    for(i=0;i<3;i++){ xx[i]=TX[i]*4; yy[i]=TY[i]*4; }
    area=(1.0/16.0)*((double)(xx[0]-xx[2])*(yy[1]-yy[2])-(double)(yy[0]-yy[2])*(xx[1]-xx[2]));
    ooa=(float)(1.0/area); memcpy(&oob,&ooa,4);

    widle();
    ewr(SCALE_3D_CNTL, scale);
    ewr(SETUP_CNTL,    setup);
    ewr(Z_CNTL,        0);
    ewr(DP_PIX_WIDTH,  0x60060666);
    ewr(DST_OFF_PITCH, ((PITCH_PIX/8)<<22)|(targ>>3));
    ewr(SC_LEFT_RIGHT, 0x1fff0000);
    ewr(SC_TOP_BOTTOM, 0x3fff0000);
    ewr(DP_WRITE_MASK, 0xffffffff);
    ewr(CLR_CMP_CNTL,  0);
    ewr(DP_MIX,        0x00070007);
    ewr(DP_SRC,        dpsrc);
    widle();

    /* command buffer: 3 vertex packets + trigger + terminator */
    off = 0;
    for(i=0;i<3;i++){
        if(full){
            cw(off, ADRINDEX(VTX_BASE(i)) | (6u<<16)); off+=4;  /* S,T,W,SPEC,Z,ARGB,X_Y */
            cw(off, 0);                                off+=4;  /* S    */
            cw(off, 0);                                off+=4;  /* T    */
            cw(off, ONE_F);                            off+=4;  /* W=1.0 */
            cw(off, 0);                                off+=4;  /* SPEC_ARGB */
            cw(off, 0);                                off+=4;  /* Z    */
            cw(off, VC[i]);                            off+=4;  /* ARGB */
            cw(off, (xx[i]<<16)|(yy[i]&0xffff));       off+=4;  /* X_Y  */
        } else {
            cw(off, ADRINDEX(VTX_Z(i)) | (2u<<16));    off+=4;  /* Z,ARGB,X_Y */
            cw(off, 0);                                off+=4;
            cw(off, VC[i]);                            off+=4;
            cw(off, (xx[i]<<16)|(yy[i]&0xffff));       off+=4;
        }
    }
    cw(off, ADRINDEX(ONE_OVER_AREA_UC) | (0u<<16));    off+=4;
    cw(off, oob);                                      off+=4;
    cw(off, ADRINDEX(SRC_CNTL) | (0u<<16));            off+=4;
    cw(off, 0);                                        off+=4;

    rw(0, APERTURE_OFFSET + BM_ADDR);
    rw(4, CMDBUS);
    rw(8, off | DMA_HOLD_OFFSET | DMA_EOL);
    rw(12, 0);

    save_bus=rd(BUS_CNTL); save_src=rd(SRC_CNTL); save_tbl=rd(BM_GUI_TABLE);
    mb(); widle();
    wr(BUS_CNTL, (save_bus & ~BUS_MASTER_DIS) | BUS_EXT_REG_EN);
    wr(BM_GUI_TABLE_CMD, RINGBUS);
    wr(SRC_CNTL, SRC_BM_ENABLE | SRC_BM_SYNC | SRC_BM_OP_SYSTEM_TO_REG);
    mb();
    wr(DST_HEIGHT_WIDTH, 0);           /* kick */
    widle();
    wr(SRC_CNTL, save_src);
    wr(BM_GUI_TABLE_CMD, save_tbl & 0xfffffff0);
    wr(BUS_CNTL, save_bus);
    widle();
}

/* Winning gouraud state (isolated by the state sweep):
 *   SETUP_CNTL   = 0        <- the flat-triangle's 0x40 (LOG_MAX_INC_ADJ) broke
 *                             color-gradient setup; 0 is mesa's value
 *   SCALE_3D_CNTL= 0x100c1  <- 3D shade pipe (FCN_SHADE), minimal/clean
 *   DP_SRC       = 0x0505   <- BKGD/FRGD_SRC_3D: pixel color from the scaler
 *   full vertex incl W=1.0  <- perspective divide needs W (independently fixes it)
 */
#define G_SETUP  0x00000000u
#define G_SCALE  0x000100c1u
#define G_DPSRC  0x00000505u
#define G_TARGET 0x600000u

int main(int argc, char **argv)
{
    io_service_t svc; io_connect_t conn; kern_return_t kr;
    vm_address_t fb=0; vm_size_t fbs=0; unsigned cfg=0; int go,valid,x,y;

    svc=IOServiceGetMatchingService(kIOMasterPortDefault,IOServiceMatching("Mach64Accelerator"));
    if(!svc){fprintf(stderr,"no service\n");return 1;}
    kr=IOServiceOpen(svc,mach_task_self(),MACH64_GL_CONNECT_TYPE,&conn); IOObjectRelease(svc);
    if(kr){fprintf(stderr,"open 0x%x\n",kr);return 1;}
    if(IOConnectMapMemory(conn,MACH64_MEM_SURFACE,mach_task_self(),&fb,&fbs,kIOMapAnywhere|kIOMapInhibitCache)){fprintf(stderr,"map fb\n");return 1;}
    FB=fb; REGB=fb+REG_WINDOW;
    CMD=mapmem(conn,MACH64_MEM_CMDBUF);
    RING=mapmem(conn,MACH64_MEM_RING);
    if(!CMD||!RING){fprintf(stderr,"map cmd/ring failed\n");goto out;}
    IOConnectMethodScalarIScalarO(conn, kMach64Sel_get_config, 0, 3, &CMDBUS, &RINGBUS, &cfg);
    printf("cmdbuf bus=0x%08x  ring bus=0x%08x\n", CMDBUS, RINGBUS);
    valid = CMDBUS>=0x1000 && CMDBUS<RAM_LIMIT && RINGBUS>=0x1000 && RINGBUS<RAM_LIMIT;
    if(!valid){ printf("*** INVALID bus addr -- refusing DMA ***\n"); goto out; }
    go = (argc>1 && strcmp(argv[1],"go")==0);
    if(!go){ printf("DRY RUN ok. Re-run with 'go' to draw.\n"); goto out; }

    if(widle()<0){fprintf(stderr,"not idle\n");goto out;}
    printf("gouraud triangle via DMA ring: v0=RED v1=GREEN v2=BLUE\n\n");
    draw(G_TARGET, G_SETUP, G_SCALE, G_DPSRC, 1);

    /* ASCII readback: dominant channel per covered pixel.  A red corner (v0),
     * green corner (v1), blue corner (v2) with a blended interior = gouraud. */
    {
        int nfill=0,nR=0,nG=0,nB=0,classes;
        printf("off-screen readback (R/G/B = dominant channel, '.' = empty):\n");
        for(y=0;y<124;y+=4){
            printf("  ");
            for(x=0;x<124;x+=2){
                unsigned v=pixT(G_TARGET,x,y);
                if(!v){ putchar('.'); continue; }
                unsigned r=(v>>16)&0xff, g=(v>>8)&0xff, b=v&0xff;
                nfill++;
                if(r>=g && r>=b){ putchar('R'); nR++; }
                else if(g>=r && g>=b){ putchar('G'); nG++; }
                else { putchar('B'); nB++; }
            }
            putchar('\n');
        }
        printf("\nsamples: near-v0=%08x near-v1=%08x near-v2=%08x centroid=%08x\n",
               pixT(G_TARGET,26,30), pixT(G_TARGET,96,50), pixT(G_TARGET,52,102), pixT(G_TARGET,58,58));
        printf("covered=%d  dominant R=%d G=%d B=%d\n", nfill, nR,nG,nB);
        classes=(nR>0)+(nG>0)+(nB>0);
        printf("\n%s\n", (nfill>50 && classes>=2)
            ? "GOURAUD CONFIRMED: setup engine interpolated per-vertex colors via DMA!"
            : (nfill>50 ? "shape drawn but single color -- gouraud didn't interpolate"
                        : "triangle not present"));
    }
out:
    IOServiceClose(conn); return 0;
}

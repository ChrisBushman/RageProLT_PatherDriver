/*
 * mach64_bmupload.c - bus-mastered host->VRAM upload throughput prototype (gap 6).
 *
 * Streams a w x h ARGB8888 image into VRAM through the GUI bus-master: the engine is set up for a host-data blit by MMIO (engine idle),
 * and the pixel data is fed to HOST_DATA0..15 by bus-master DMA descriptors (register-write stream, DMAREG header + 16 data dwords per
 * packet).  Uses the Path A kext's physically contiguous, uncached command buffer (128 KB) and ring, and the dmatest-proven recipe
 * (see docs/re-notes/bus-master-ring.md).  Reports MB/s and verifies the VRAM contents.  QUIESCE as in mach64_dmatest (no lock shared
 * with ATIRagePro): run headless.   usage: mach64_bmupload [go]   (default = dry run: addresses only)
 */
#include <IOKit/IOKitLib.h>
#include <libkern/OSByteOrder.h>
#include <mach/mach.h>
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <sys/time.h>
#include "../common/mach64_abi.h"

#define RAM_LIMIT 0x20000000u
#define REG_WINDOW 0x7FF800u
#define BUS_CNTL 0x04a0
#define SRC_CNTL 0x05b4
#define DST_HEIGHT_WIDTH 0x0518
#define BM_ADDR 0x0648
#define BM_GUI_TABLE_CMD 0x064c
#define BM_GUI_TABLE 0x01b8
#define FIFO_STAT 0x0710
#define GUI_STAT 0x0738
#define APERTURE_OFFSET 0x7ff800u
#define BUS_MASTER_DIS (1u<<6)
#define BUS_EXT_REG_EN (1u<<27)
#define SRC_BM_ENABLE (1u<<8)
#define SRC_BM_SYNC (1u<<9)
#define SRC_BM_OP_SYSTEM_TO_REG (3u<<10)
#define DMA_EOL (1u<<31)
#define DMA_HOLD_OFFSET (1u<<30)
#define ADRINDEX(r) ((unsigned)((r)>=0x400 ? (((r)-0x400)>>2) : (((r)>>2)|0x100)))
#define HOST_DATA0 0x0600
#define DSTBASE 0x300000u            /* free VRAM region used as the destination */
#define PITCH_PX 256u
#define W 256u
#define H 112u                       /* 256*112*4 = 112 KB of pixels; stream incl. headers + setup block fits 128 KB */

static void mb(void){ __asm__ volatile("sync; eieio" ::: "memory"); }
static unsigned REGB, RING, CMD, FB;
static unsigned rd(unsigned o){ return OSReadLittleInt32((void*)REGB, o); }
static void wr(unsigned o, unsigned v){ OSWriteLittleInt32((void*)REGB, o, v); }
static void rw(unsigned o, unsigned v){ OSWriteLittleInt32((void*)RING, o, v); }
static void cw(unsigned o, unsigned v){ OSWriteLittleInt32((void*)CMD, o, v); }
static int wfifo(int e){ int i; for(i=0;i<1000000;i++) if((rd(FIFO_STAT)&0xffffu)<=(unsigned)(0x8000>>e)) return 0; return -1; }
static int widle(void){ int i; if(wfifo(16)<0) return -1; for(i=0;i<4000000;i++) if(!(rd(GUI_STAT)&1u)) return 0; return -1; }
static double now_s(void){ struct timeval tv; gettimeofday(&tv,0); return tv.tv_sec+tv.tv_usec/1e6; }
static unsigned mapmem(io_connect_t c, unsigned type){ vm_address_t a=0; vm_size_t s=0; if(IOConnectMapMemory(c,type,mach_task_self(),&a,&s,kIOMapAnywhere|kIOMapInhibitCache)!=KERN_SUCCESS) return 0; return a; }

static void dma_arm_kick(unsigned save_bus, unsigned ringbus)
{
    wr(SRC_CNTL, 0); widle();
    wr(0x04a0 /*BUS_CNTL*/, (save_bus & ~BUS_MASTER_DIS) | BUS_EXT_REG_EN);
    wr(0x06c8, 0xffffffffu);                   /* DP_WRITE_MASK */
    wr(0x06d0, 0x60060666u);                   /* DP_PIX_WIDTH  ARGB8888 */
    wr(0x06d4, (3u<<0) | (7u<<16));                /* DP_MIX  BKGD_D | FRGD_S */
    wr(0x06d8, 0x00000200u);                   /* DP_SRC  FRGD_SRC_HOST */
    wr(0x0730, 3u);                        /* GUI_TRAJ_CNTL L->R T->B */
    wr(0x06a8, 0x1fff0000u); wr(0x06b4, 0x3fff0000u); wr(0x0708, 0);   /* scissor, CLR_CMP_CNTL */
    wr(0x05fc, 0);                         /* SCALE_3D_CNTL = 0 (2D engine only) */
    wr(0x0500, ((PITCH_PX/8u)<<22) | (DSTBASE>>3));        /* DST_OFF_PITCH */
    wr(BM_GUI_TABLE_CMD, ringbus);
    wr(SRC_CNTL, SRC_BM_ENABLE | SRC_BM_SYNC | SRC_BM_OP_SYSTEM_TO_REG);
    mb();
    wr(DST_HEIGHT_WIDTH, 0);                   /* kick the descriptor walk */
}

int main(int argc, char **argv)
{
    io_service_t svc; io_connect_t conn; vm_address_t fb=0; vm_size_t fbs=0;
    unsigned cmdbus=0, ringbus=0, cfg=0, save_bus, save_src, save_tbl, i, n, nd, off, packets, bad=0, iters=50;
    int go = (argc>1 && !strcmp(argv[1],"go"));
    double t0, dt;
    svc=IOServiceGetMatchingService(kIOMasterPortDefault,IOServiceMatching("Mach64Accelerator"));
    if(!svc){fprintf(stderr,"no service\n");return 1;}
    if(IOServiceOpen(svc,mach_task_self(),MACH64_GL_CONNECT_TYPE,&conn)){fprintf(stderr,"open\n");return 1;}
    IOObjectRelease(svc);
    if(IOConnectMapMemory(conn,MACH64_MEM_SURFACE,mach_task_self(),&fb,&fbs,kIOMapAnywhere|kIOMapInhibitCache)){fprintf(stderr,"map fb\n");return 1;}
    FB=fb; REGB=fb+REG_WINDOW; CMD=mapmem(conn,MACH64_MEM_CMDBUF); RING=mapmem(conn,MACH64_MEM_RING);
    if(!CMD||!RING){fprintf(stderr,"map cmd/ring failed\n");return 1;}
    IOConnectMethodScalarIScalarO(conn, kMach64Sel_get_config, 0, 3, &cmdbus, &ringbus, &cfg);
    printf("cmdbuf bus=0x%08x ring bus=0x%08x\n", cmdbus, ringbus);
    if(!(cmdbus>=0x1000 && cmdbus<RAM_LIMIT && ringbus>=0x1000 && ringbus<RAM_LIMIT)){ printf("INVALID bus addresses -- refusing\n"); return 1; }
    if(!go){ printf("dry run ok; re-run with 'go'\n"); return 0; }

    /* command stream: packets of {DMAREG(HOST_DATA0)|(15<<16), 16 dwords}; 60 packets (1020 dwords = 4080 B) per 4 KB block, one descriptor per block */
    packets = (W*H)/16u; nd = (packets + 59u)/60u + 1u;   /* +1: block 0 = the setup packet */
    printf("image %ux%u = %u pixels, %u packets, %u descriptors (%u bytes of stream)\n", W, H, W*H, packets, nd, nd*4096u);
    if(nd*4096u > 0x20000u){ fprintf(stderr,"stream does not fit the 128KB command buffer\n"); return 1; }
    cw(0, ADRINDEX(0x06e8) | (1u<<16)); cw(4, 0); cw(8, (H<<16) | W);   /* DST_X_Y = 0,0 then DST_WIDTH_HEIGHT: the write that arms the blit */
    for(n=0,off=0,i=0;n<packets;n++){
        unsigned k, blk=1u+n/60u, pos=n%60u, o=blk*4096u+pos*17u*4u;
        cw(o, ADRINDEX(HOST_DATA0) | (15u<<16));
        for(k=0;k<16u;k++) cw(o+4+k*4, 0xff000000u | ((n*16u+k)&0xffffffu));   /* pixel value = 0xff000000 | pixel index */
        (void)off; (void)i;
    }
    for(i=0;i<nd;i++){
        unsigned cnt = (i==0) ? 12u : ((i==nd-1u) ? (packets-(i-1u)*60u)*17u*4u : 4080u);
        rw(i*16u+0, APERTURE_OFFSET + BM_ADDR); rw(i*16u+4, cmdbus + i*4096u);
        rw(i*16u+8, cnt | DMA_HOLD_OFFSET | ((i==nd-1u)?DMA_EOL:0u)); rw(i*16u+12, 0);
    }
    mb();

    save_bus=rd(BUS_CNTL); save_src=rd(SRC_CNTL); save_tbl=rd(BM_GUI_TABLE);
    for(n=0;n<iters;n++){
        if(n==1) t0=now_s();     /* iteration 0 = warm-up + verification */
        if(n==0) { for(i=0;i<W*H;i++) OSWriteLittleInt32((void*)FB, DSTBASE+i*4, 0); }
        dma_arm_kick(save_bus, ringbus);
        if(widle()<0){ fprintf(stderr,"stall on iteration %u (GUI_STAT=0x%08x)\n", n, rd(GUI_STAT)); break; }
        if(n==0){
            for(i=0;i<W*H;i++) if(OSReadLittleInt32((void*)FB, DSTBASE+i*4)!=(0xff000000u|i)) { if(bad<4) printf("  mismatch px %u: got 0x%08x want 0x%08x\n", i, OSReadLittleInt32((void*)FB, DSTBASE+i*4), 0xff000000u|i); bad++; }
            printf("verification: %u of %u pixels wrong\n", bad, W*H);
        }
    }
    dt=now_s()-t0;
    if(n==iters){
        /* ---- CPU cost (step 1 of the DMA-upload evaluation): what the CPU pays per 112 KB image ---- */
        unsigned reps=50, *src=(unsigned*)malloc(W*H*4), *dst=(unsigned*)malloc(W*H*4+64); double tt; volatile unsigned long spin; unsigned long base_rate, it, spins; double te;
        for(i=0;i<W*H;i++) src[i]=0xff000000u|i;
        tt=now_s(); for(n=0;n<reps;n++) for(i=0;i<W*H;i++) OSWriteLittleInt32((void*)FB, DSTBASE+i*4, src[i]);
        printf("CPU stores straight to VRAM (today's cost, no kext): %.2f ms per image\n", (now_s()-tt)*1000/reps);
        tt=now_s(); for(n=0;n<reps;n++) { unsigned pk, k; for(pk=0;pk<W*H/16u;pk++){ unsigned o=1u*4096u+(pk/60u)*4096u+(pk%60u)*68u; cw(o, ADRINDEX(HOST_DATA0)|(15u<<16)); for(k=0;k<16u;k++) cw(o+4+k*4, src[pk*16u+k]); } }
        printf("CPU fill of the UNCACHED staging buffer (packet format): %.2f ms per image\n", (now_s()-tt)*1000/reps);
        tt=now_s(); for(n=0;n<reps;n++) { memcpy(dst, src, W*H*4); { char *q; for(q=(char*)dst; q<(char*)dst+W*H*4; q+=32) __asm__ volatile("dcbf 0,%0" :: "r"(q) : "memory"); __asm__ volatile("sync" ::: "memory"); } }
        printf("CPU memcpy + dcbf of a CACHED staging buffer (lower bound for a cached design): %.2f ms per image\n", (now_s()-tt)*1000/reps);
        /* free CPU while a DMA runs: calibrate a spin loop, then spin (polling GUI_STAT rarely) from kick to idle */
        te=now_s(); spin=0; while(now_s()-te<0.2) { unsigned q; for(q=0;q<1000;q++) spin++; } base_rate=(unsigned long)(spin/(now_s()-te));
        { double dmat=0, freef=0; unsigned runs=20, r;
          for(r=0;r<runs;r++){ unsigned long iters2=0; dma_arm_kick(save_bus, ringbus); te=now_s();
              do { unsigned q; for(q=0;q<200;q++) iters2++; } while(rd(GUI_STAT)&1u);
              { double d=now_s()-te; dmat+=d; freef+=(double)iters2/((double)base_rate*d); } }
          printf("DMA of one image: %.2f ms; CPU free during it (spin work done / spin work possible, polling every ~200 spins): %.0f%%\n", dmat*1000/runs, 100.0*freef/runs); }
        (void)it; (void)spins; (void)spin;
        n=iters;
    }
    wr(SRC_CNTL, save_src); wr(BM_GUI_TABLE_CMD, save_tbl & 0xfffffff0); wr(BUS_CNTL, save_bus);
    if(n==iters) printf("bus-master upload: %u x %u KB in %.3f s = %.1f MB/s\n", iters-1, W*H*4/1024, dt, (iters-1)*(W*H*4/1048576.0)/dt);
    IOServiceClose(conn); return 0;
}

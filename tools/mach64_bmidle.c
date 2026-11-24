/*
 * mach64_bmidle.c - does GUI_STAT.GUI_ACTIVE stay set for the whole GUI bus-master stream? (vertex-DMA plan, question 1).
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
#include <sys/select.h>
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


/* Re-uses mach64_bmupload's stream byte for byte (setup packet + 1792 HOST_DATA0 packets, verified there), but measures how GUI_STAT behaves from the kick:
 * samples GUI_STAT in a tight loop right after the kick, records the first value, every GUI_ACTIVE transition, and -- the point -- checks the LAST pixel of the
 * image (which the stream writes last) the instant GUI_ACTIVE is first seen low.  If GUI_ACTIVE can read low before the stream has finished, the last pixel is still 0. */
int main(int argc, char **argv)
{
    io_service_t svc; io_connect_t conn; vm_address_t fb=0; vm_size_t fbs=0;
    unsigned cmdbus=0, ringbus=0, cfg=0, save_bus, save_src, save_tbl, i, n, nd, packets, runs = 60, early = 0, firstlow_after_active = 0, zero_first = 0, never = 0;
    int go = (argc>1 && !strcmp(argv[1],"go")); unsigned maxsamples = 0, minsamples = 0xffffffffu;
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
    packets = (W*H)/16u; nd = (packets + 59u)/60u + 1u;
    if(nd*4096u > 0x20000u){ fprintf(stderr,"stream does not fit\n"); return 1; }
    cw(0, ADRINDEX(0x06e8) | (1u<<16)); cw(4, 0); cw(8, (H<<16) | W);
    for(n=0;n<packets;n++){
        unsigned k, blk=1u+n/60u, pos=n%60u, o=blk*4096u+pos*17u*4u;
        cw(o, ADRINDEX(HOST_DATA0) | (15u<<16));
        for(k=0;k<16u;k++) cw(o+4+k*4, 0xff000000u | ((n*16u+k)&0xffffffu));
    }
    for(i=0;i<nd;i++){
        unsigned cnt = (i==0) ? 12u : ((i==nd-1u) ? (packets-(i-1u)*60u)*17u*4u : 4080u);
        rw(i*16u+0, APERTURE_OFFSET + BM_ADDR); rw(i*16u+4, cmdbus + i*4096u);
        rw(i*16u+8, cnt | DMA_HOLD_OFFSET | ((i==nd-1u)?DMA_EOL:0u)); rw(i*16u+12, 0);
    }
    mb();
    save_bus=rd(BUS_CNTL); save_src=rd(SRC_CNTL); save_tbl=rd(BM_GUI_TABLE);


    if(argc>2 && argv[2][0]=='D'){   /* D: how long does GUI_ACTIVE read 0 after the kick before it first reads 1?  (idle reads before the first active read, timed) */
        unsigned R = (argc>3) ? (unsigned)atoi(argv[3]) : 200u, hist[6] = {0,0,0,0,0,0}, maxz = 0, never_active = 0; double maxus = 0, t0;
        for(n=0;n<R;n++){
            unsigned z = 0, v = 0, t; struct timeval w; 
            if(n%10==0){ w.tv_sec=0; w.tv_usec=300000; select(0,0,0,0,&w); }
            dma_arm_kick(save_bus, ringbus); t0 = now_s();
            for(t=0;t<100000u;t++){ v = rd(GUI_STAT)&1u; if(v) break; z++; }
            if(!v) never_active++;
            { double us = (now_s()-t0)*1e6; if(us>maxus) maxus=us; }
            if(z>maxz) maxz=z; hist[z==0?0: z==1?1: z<=3?2: z<=10?3: z<=100?4:5]++;
            { unsigned j; for(j=0;j<3000000u;j++){ if(rd(BM_GUI_TABLE)==ringbus+nd*16u && !(rd(GUI_STAT)&1u)) break; } }   /* finish before the next run */
            w.tv_sec=0; w.tv_usec=2000; select(0,0,0,0,&w);
        }
        printf("MODE D: %u runs: idle reads before first active read: 0:%u  1:%u  2-3:%u  4-10:%u  11-100:%u  >100:%u (max %u reads, %.1f us); never went active: %u\n", R, hist[0], hist[1], hist[2], hist[3], hist[4], hist[5], maxz, maxus, never_active);
        wr(SRC_CNTL, save_src); wr(BM_GUI_TABLE_CMD, save_tbl & 0xfffffff0); wr(BUS_CNTL, save_bus);
        return 0;
    }
    if(argc>2){   /* experiments: argv[2] = A (poll GUI_STAT until first idle, then wait 50 ms), B (kick, wait 50 ms, no register reads), C (kick, wait for BM_GUI_TABLE to reach the table end AND GUI idle); full verification every run */
        char mode = argv[2][0]; unsigned bad_runs = 0, first_idle_immediate = 0, tot_bad_px = 0, R = (argc>3) ? (unsigned)atoi(argv[3]) : 100u;
        for(n=0;n<R;n++){
            unsigned bad = 0, samples = 0, firstv = 9, endtbl = ringbus + nd*16u; struct timeval w;
            for(i=0;i<W*H;i++) OSWriteLittleInt32((void*)FB, DSTBASE+i*4, 0);
            if(n%10==0){ w.tv_sec=0; w.tv_usec=300000; select(0,0,0,0,&w); }
            dma_arm_kick(save_bus, ringbus);
            if(mode=='A'){ while(samples<3000000u){ unsigned v = rd(GUI_STAT)&1u; if(!samples) firstv=v; samples++; if(!v) break; } if(firstv==0) first_idle_immediate++; w.tv_sec=0; w.tv_usec=50000; select(0,0,0,0,&w); }
            else if(mode=='B'){ w.tv_sec=0; w.tv_usec=50000; select(0,0,0,0,&w); }
            else { unsigned t, tb = 0, gs = 0; for(t=0;t<200000u;t++){ tb = rd(BM_GUI_TABLE); gs = rd(GUI_STAT); if(tb==endtbl && !(gs&1u)) break; } if(n<3) printf("  mode C run %u: %u polls, BM_GUI_TABLE=0x%08x (want 0x%08x) GUI_STAT=0x%08x\n", n, t, tb, endtbl, gs); w.tv_sec=0; w.tv_usec=2000; select(0,0,0,0,&w); }
            for(i=0;i<W*H;i++) if(OSReadLittleInt32((void*)FB, DSTBASE+i*4)!=(0xff000000u|i)) bad++;
            if(bad){ bad_runs++; tot_bad_px += bad; if(bad_runs<=6) printf("  run %u mode %c: %u of %u pixels wrong (first sample %u)\n", n, mode, bad, W*H, firstv); }
        }
        printf("MODE %c: %u runs, %u runs with wrong pixels (%u pixels total), first sample already idle in %u runs\n", mode, R, bad_runs, tot_bad_px, first_idle_immediate);
        wr(SRC_CNTL, save_src); wr(BM_GUI_TABLE_CMD, save_tbl & 0xfffffff0); wr(BUS_CNTL, save_bus);
        return 0;
    }
    for(n=0;n<runs;n++){
        unsigned s, samples = 0, active_seen = 0, trans = 0, prev = 2, firstval = 0, lastpx;
        unsigned lastidx = W*H-1u, midpx_at_low = 0, lastpx_at_low = 0, low_seen = 0, tbl_low = 0, bmst_low = 0, tbl_late, bmst_late, sampled_good = 0, sampled_tot = 0; struct timeval tvd;
        for(i=0;i<W*H;i+=64) OSWriteLittleInt32((void*)FB, DSTBASE+i*4, 0);                    /* clear sampled pixels */
        OSWriteLittleInt32((void*)FB, DSTBASE+lastidx*4, 0);
        if(n%10==0){ struct timeval cs; cs.tv_sec=0; cs.tv_usec=300000; select(0,0,0,0,&cs); }   /* every 10th run: engine cold (idle 300 ms) before the kick */
        dma_arm_kick(save_bus, ringbus);
        while(samples < 3000000u){
            s = rd(GUI_STAT) & 1u; if(samples==0) firstval = s; samples++;
            if(s != prev){ trans++; prev = s; }
            if(s) active_seen = 1;
            if(!s && !low_seen){
                low_seen = 1; lastpx_at_low = OSReadLittleInt32((void*)FB, DSTBASE+lastidx*4); tbl_low = rd(BM_GUI_TABLE); bmst_low = rd(0x018c);
                if(active_seen) firstlow_after_active = 1;
                break;
            }
        }
        lastpx = OSReadLittleInt32((void*)FB, DSTBASE+lastidx*4); (void)midpx_at_low;
        { unsigned j, st = 0; for(j=0;j<2000000u;j++) { st = rd(GUI_STAT) & 1u; if(!st) break; } (void)st; }   /* make sure it really finishes */
        tvd.tv_sec = 0; tvd.tv_usec = 50000; select(0, 0, 0, 0, &tvd);                                       /* 50 ms: anything pending must have finished */
        lastpx = OSReadLittleInt32((void*)FB, DSTBASE+lastidx*4); tbl_late = rd(BM_GUI_TABLE); bmst_late = rd(0x018c);
        { unsigned good = 0, tot = 0, q; for(q=0;q<W*H;q+=64){ tot++; if(OSReadLittleInt32((void*)FB, DSTBASE+q*4)==(0xff000000u|q)) good++; } sampled_good = good; sampled_tot = tot; }
        if(lastpx != (0xff000000u|lastidx)) never++;
        if(firstval==0) zero_first++;
        if(lastpx_at_low != (0xff000000u|lastidx)) early++;
        if(samples>maxsamples) maxsamples=samples; if(samples<minsamples) minsamples=samples;
        if(n<5 || lastpx_at_low != (0xff000000u|lastidx)) printf("  run %2u: first GUI_ACTIVE sample=%u, %u samples to first idle, %u transitions, last pixel at first idle 0x%08x, after 50 ms 0x%08x | BM_GUI_TABLE at idle 0x%08x late 0x%08x | BM_STATUS at idle 0x%08x late 0x%08x | image pixels present after 50 ms %u/%u %s\n", n, firstval, samples, trans, lastpx_at_low, lastpx, tbl_low, tbl_late, bmst_low, bmst_late, sampled_good, sampled_tot, (lastpx_at_low != (0xff000000u|lastidx)) ? "<-- IDLE BEFORE STREAM DONE" : "");
    }
    printf("RESULT over %u runs: first sample after the kick already idle in %u runs; GUI_ACTIVE read low BEFORE the last pixel landed in %u runs; stream never landed even after 50 ms in %u runs; samples to first idle min %u max %u\n", runs, zero_first, early, never, minsamples, maxsamples);
    wr(SRC_CNTL, save_src); wr(BM_GUI_TABLE_CMD, save_tbl & 0xfffffff0); wr(BUS_CNTL, save_bus);
    (void)firstlow_after_active;
    return 0;
}

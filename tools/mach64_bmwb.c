/*
 * mach64_bmwb.c - two checks on the SYSTEM bus master (RRG 6-11, BM_SYSTEM_TABLE SYSTEM_TRIGGER):
 *   A) which transfers latch CRTC_INT_CNTL bit 25 (BUSMASTER_EOL_INT)?  (enables are never set; status only; acked by a write-1-to-clear of bit 25)
 *   B) is a frame buffer -> system memory transfer (SYSTEM_TRIGGER 1) ordered behind the draw engine?  Fill a large region through the register FIFO
 *      (the last pixel is written last), trigger a readback of that pixel right away, and see whether system memory gets the NEW colour while the engine was still busy.
 * Data goes through the Path A kext's physically contiguous staging buffer/ring.  usage: mach64_bmwb [go]  (default dry run)
 */
#include <IOKit/IOKitLib.h>
#include <libkern/OSByteOrder.h>
#include <mach/mach.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/time.h>
#include "../common/mach64_abi.h"

#define RAM_LIMIT 0x20000000u
#define REG_WINDOW 0x7FF800u
#define BUS_CNTL 0x04a0
#define BM_SYSTEM_TABLE 0x01bc
#define BM_COMMAND 0x0188
#define CRTC_INT_CNTL 0x0018
#define FIFO_STAT 0x0710
#define GUI_STAT 0x0738
#define DMA_EOL (1u<<31)
#define FILLB 0x380000u
#define FW 256u
#define FH 512u
#define TARGET (FILLB + (FH-1u)*FW*4u + (FW-1u)*4u)

static void mb(void){ __asm__ volatile("sync; eieio" ::: "memory"); }
static unsigned PW;   /* byte offset in the staging buffer of the word polled for a readback */
static unsigned REGB, RING, CMD, FB, cmdbus, ringbus;
static unsigned rd(unsigned o){ return OSReadLittleInt32((void*)REGB, o); }
static void wr(unsigned o, unsigned v){ OSWriteLittleInt32((void*)REGB, o, v); }
static double now_s(void){ struct timeval tv; gettimeofday(&tv,0); return tv.tv_sec+tv.tv_usec/1e6; }
static unsigned mapmem(io_connect_t c, unsigned type){ vm_address_t a=0; vm_size_t s=0; if(IOConnectMapMemory(c,type,mach_task_self(),&a,&s,kIOMapAnywhere|kIOMapInhibitCache)!=KERN_SUCCESS) return 0; return a; }
static int wfifo(int e){ int i; for(i=0;i<1000000;i++) if((rd(FIFO_STAT)&0xffffu)<=(unsigned)(0x8000>>e)) return 0; return -1; }
static int widle(void){ int i; if(wfifo(16)<0) return -1; for(i=0;i<200000;i++) if(!(rd(GUI_STAT)&1u)) return 0; return -1; }
static void fill(unsigned off, unsigned color, unsigned w, unsigned h)
{
    wfifo(16);
    wr(0x054c, 0); wr(0x05fc, 0); wr(0x0730, 3u); wr(0x06d0, 0x60060666u); wr(0x06c4, color); wr(0x06c8, 0xffffffffu);
    wr(0x06d4, (3u<<0)|(7u<<16)); wr(0x06d8, (1u<<0)|(1u<<8)); wr(0x06a8, 0x1fff0000u); wr(0x06b4, 0x3fff0000u); wr(0x0708, 0);
    wr(0x0500, ((256u/8u)<<22) | (off>>3)); wr(0x06e8, 0); wr(0x06ec, (h<<16)|w);
}
/* one descriptor: fb offset, system phys, count|EOL */
static unsigned xfer(unsigned trig, unsigned fboff, unsigned sysaddr, unsigned count, unsigned *t_us)
{
    unsigned i, end = ringbus + 16u; double t0 = now_s(); unsigned widx = PW;   /* readback: done when the sentinel in the first needed word changes */
    OSWriteLittleInt32((void*)RING, 0, fboff); OSWriteLittleInt32((void*)RING, 4, sysaddr);
    OSWriteLittleInt32((void*)RING, 8, count | DMA_EOL); OSWriteLittleInt32((void*)RING, 12, 0);
    mb(); wr(BM_SYSTEM_TABLE, ringbus | trig);
    for(i=0;i<200000u;i++){ if(trig==1u){ if(OSReadLittleInt32((void*)CMD, widx)!=0xdeadbeefu) break; } else if(rd(BM_SYSTEM_TABLE)==end && rd(BM_COMMAND)==0x80000000u) break; }
    *t_us = (unsigned)((now_s()-t0)*1e6); return i<200000u;
}
int main(int argc, char **argv)
{
    io_service_t svc; io_connect_t conn; vm_address_t fb=0; vm_size_t fbs=0; unsigned cfg=0, bus, t_us, i, r, go=(argc>1&&!strcmp(argv[1],"go"));
    setvbuf(stdout, 0, _IONBF, 0);
    svc=IOServiceGetMatchingService(kIOMasterPortDefault,IOServiceMatching("Mach64Accelerator"));
    if(!svc){fprintf(stderr,"no service\n");return 1;}
    if(IOServiceOpen(svc,mach_task_self(),MACH64_GL_CONNECT_TYPE,&conn)){fprintf(stderr,"open\n");return 1;}
    IOObjectRelease(svc);
    if(IOConnectMapMemory(conn,MACH64_MEM_SURFACE,mach_task_self(),&fb,&fbs,kIOMapAnywhere|kIOMapInhibitCache)){fprintf(stderr,"map fb\n");return 1;}
    FB=fb; REGB=fb+REG_WINDOW; CMD=mapmem(conn,MACH64_MEM_CMDBUF); RING=mapmem(conn,MACH64_MEM_RING);
    if(!CMD||!RING){fprintf(stderr,"map cmd/ring failed\n");return 1;}
    IOConnectMethodScalarIScalarO(conn, kMach64Sel_get_config, 0, 3, &cmdbus, &ringbus, &cfg);
    printf("cmdbuf bus=0x%08x ring bus=0x%08x fb size 0x%lx\n", cmdbus, ringbus, (unsigned long)fbs);
    if(!(cmdbus>=0x1000 && cmdbus<RAM_LIMIT && ringbus>=0x1000 && ringbus<RAM_LIMIT && !(ringbus&0xf) && !(cmdbus&0xfff))){ printf("INVALID bus addresses -- refusing\n"); return 1; }
    bus=rd(BUS_CNTL); printf("BUS_CNTL=0x%08x INT_CNTL=0x%08x\n", bus, rd(CRTC_INT_CNTL));
    if((bus&(1u<<6)) || !(bus&(1u<<27))){ printf("bus mastering/ext regs not enabled -- refusing\n"); return 1; }
    if(!go){ printf("dry run ok; re-run with 'go'\n"); return 0; }
    printf("idle? %d (GUI_STAT 0x%08x FIFO_STAT 0x%08x)\n", widle(), rd(GUI_STAT), rd(FIFO_STAT));

    /* ---- A) EOL status latching ---- */
    for(i=0;i<1024;i++) OSWriteLittleInt32((void*)CMD, i*4, 0xff000000u|i);
    wr(CRTC_INT_CNTL, 0x02000000u); printf("A0 ack bit25: INT_CNTL=0x%08x\n", rd(CRTC_INT_CNTL));
    xfer(0, 0x3f0000u, cmdbus, 4096u, &t_us);  printf("A1 after system->frame 4 KB (%u us): INT_CNTL=0x%08x  bit25=%u\n", t_us, rd(CRTC_INT_CNTL), (rd(CRTC_INT_CNTL)>>25)&1);
    wr(CRTC_INT_CNTL, 0x02000000u); printf("A2 ack: INT_CNTL=0x%08x\n", rd(CRTC_INT_CNTL));
    for(i=0;i<1024;i++) OSWriteLittleInt32((void*)CMD, 4096u+i*4, 0xdeadbeefu);
    PW = 4096u; xfer(1, 0x3f0000u, cmdbus+4096u, 4096u, &t_us); printf("A3 regs after: BM_SYSTEM_TABLE=0x%08x BM_COMMAND=0x%08x (table end would be 0x%08x)\n", rd(BM_SYSTEM_TABLE), rd(BM_COMMAND), ringbus+16u); printf("A3 after frame->system 4 KB (%u us): INT_CNTL=0x%08x bit25=%u; data check: sys[0]=0x%08x (expect 0xff000000) sys[1023]=0x%08x (expect 0xff0003ff)\n", t_us, rd(CRTC_INT_CNTL), (rd(CRTC_INT_CNTL)>>25)&1, OSReadLittleInt32((void*)CMD,4096u), OSReadLittleInt32((void*)CMD,4096u+4092u));

    /* ---- B) ordering of frame->system readback vs the draw engine ---- */
    { unsigned newc=0, oldc=0, other=0, busy_new=0, busy_old=0, busy_total=0, fails=0; double sumfill=0;
      for(r=0;r<200;r++){
        unsigned c = 0xff200000u + r*0x101u, old = 0xff000000u, got, busy, tgt = TARGET & ~15u, idx = TARGET & 15u;
        widle();
        OSWriteLittleInt32((void*)FB, TARGET, old);                    /* CPU puts the OLD value in the pixel */
        OSWriteLittleInt32((void*)CMD, 8192u + idx, 0xdeadbeefu);
        mb();
        { double tf = now_s(); fill(FILLB, c, FW, FH);                 /* big fill; the last pixel is written last */
          PW = 8192u + idx; if(!xfer(1, tgt, cmdbus+8192u, 16u, &t_us)){ fails++; continue; }
          busy = rd(GUI_STAT)&1u; sumfill += (now_s()-tf); }
        got = OSReadLittleInt32((void*)CMD, 8192u + idx);
        busy_total += busy;
        if(got==c){ newc++; busy_new += busy; } else if(got==old){ oldc++; busy_old += busy; } else other++;
        widle();
      }
      printf("B frame->system readback of the LAST pixel of a %ux%u engine fill, issued right after the fill was queued:\n  NEW colour %u (engine still busy at completion: %u), OLD colour %u (busy: %u), other %u, failed %u, engine busy at completion in %u of 200\n", FW, FH, newc, busy_new, oldc, busy_old, other, fails, busy_total);
      printf("  mean fill+readback issue-to-done %.2f ms\n", sumfill*1000/200); }
    printf("final INT_CNTL=0x%08x\n", rd(CRTC_INT_CNTL)); wr(CRTC_INT_CNTL, 0x02000000u); printf("after final ack INT_CNTL=0x%08x\n", rd(CRTC_INT_CNTL));
    IOServiceClose(conn); return 0;
}

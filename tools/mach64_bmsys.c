/*
 * mach64_bmsys.c - SYSTEM bus-master transfer (Programmer's Guide 8.10.3): system memory -> frame buffer without the draw engine.
 *
 * Descriptor table entries {frame buffer offset, system physical address, byte count (<= 4096) | HOLD(30) | EOL(31), 0}; a write of
 * (table physical address | SYSTEM_TRIGGER 0) to BM_SYSTEM_TABLE (MM 1_6F = 0x1bc) starts the copy.  Unlike the GUI path (mach64_bmupload) the
 * data does not go through the register FIFO, so it needs no packet headers and cannot interleave with another client's register writes.
 * Uses the Path A kext's physically contiguous uncached command buffer (data) and ring (descriptor table).  Completion is detected by a sentinel
 * in the last dword (the status register encodings are logged for decoding).  usage: mach64_bmsys [go]   (default: dry run)
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
#define BM_SYSTEM_TABLE 0x01bc      /* MM 1_6F: the 1_xx prefix is the low register block (BM_GUI_TABLE 1_6E is 0x1b8) */
#define BM_COMMAND 0x0188           /* MM 1_62 (read: current descriptor command) */
#define BM_STATUS 0x018c            /* MM 1_63 */
#define CRTC_INT_CNTL 0x0018        /* MM 0_06 */
#define DMA_EOL (1u<<31)
#define DMA_HOLD (1u<<30)
#define BUS_MASTER_DIS (1u<<6)
#define BUS_EXT_REG_EN (1u<<27)
#define DSTBASE 0x300000u
#define NBYTES (112u*1024u)

static void mb(void){ __asm__ volatile("sync; eieio" ::: "memory"); }
static unsigned REGB, RING, CMD, FB;
static unsigned rd(unsigned o){ return OSReadLittleInt32((void*)REGB, o); }
static void wr(unsigned o, unsigned v){ OSWriteLittleInt32((void*)REGB, o, v); }
static double now_s(void){ struct timeval tv; gettimeofday(&tv,0); return tv.tv_sec+tv.tv_usec/1e6; }
static unsigned mapmem(io_connect_t c, unsigned type){ vm_address_t a=0; vm_size_t s=0; if(IOConnectMapMemory(c,type,mach_task_self(),&a,&s,kIOMapAnywhere|kIOMapInhibitCache)!=KERN_SUCCESS) return 0; return a; }
static unsigned fbrd(unsigned o){ return OSReadLittleInt32((void*)FB, o); }
static unsigned cmdbus, ringbus;

#define FIFO_STAT 0x0710
#define GUI_STAT 0x0738
static int wfifo(int e){ int i; for(i=0;i<1000000;i++) if((rd(FIFO_STAT)&0xffffu)<=(unsigned)(0x8000>>e)) return 0; return -1; }
static int widle(void){ int i; if(wfifo(16)<0) return -1; for(i=0;i<4000000;i++) if(!(rd(GUI_STAT)&1u)) return 0; return -1; }
static void fill(unsigned off, unsigned color, unsigned w, unsigned h)   /* same register sequence as the plugin's fill (2D engine through the register FIFO) */
{
    wr(0x054c, 0); wr(0x05fc, 0); wr(0x0730, 3u); wr(0x06d0, 0x60060666u); wr(0x06c4, color); wr(0x06c8, 0xffffffffu);
    wr(0x06d4, (3u<<0)|(7u<<16)); wr(0x06d8, (1u<<0)|(1u<<8)); wr(0x06a8, 0x1fff0000u); wr(0x06b4, 0x3fff0000u); wr(0x0708, 0);
    wr(0x0500, ((256u/8u)<<22) | (off>>3)); wr(0x06e8, 0); wr(0x06ec, (h<<16)|w);
}


static void build(unsigned nd)
{
    unsigned i;
    for(i=0;i<nd;i++){
        unsigned cnt = (i==nd-1u) ? NBYTES-(nd-1u)*4096u : 4096u;
        OSWriteLittleInt32((void*)RING, i*16u+0, DSTBASE + i*4096u);   /* frame buffer offset */
        OSWriteLittleInt32((void*)RING, i*16u+4, cmdbus + i*4096u);    /* system physical address */
        OSWriteLittleInt32((void*)RING, i*16u+8, cnt | ((i==nd-1u)?DMA_EOL:0u));   /* no HOLD: the frame buffer offset increments */
        OSWriteLittleInt32((void*)RING, i*16u+12, 0);
    }
    mb();
}

int main(int argc, char **argv)
{
    io_service_t svc; io_connect_t conn; vm_address_t fb=0; vm_size_t fbs=0;
    unsigned cfg=0, save_bus, nd=NBYTES/4096u, i, n, reps=50, bad=0; int go=(argc>1&&!strcmp(argv[1],"go"));
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
    if(!(cmdbus>=0x1000 && cmdbus<RAM_LIMIT && ringbus>=0x1000 && ringbus<RAM_LIMIT && !(ringbus&0xf))){ printf("INVALID bus addresses -- refusing\n"); return 1; }
    printf("before: BUS_CNTL=0x%08x BM_SYSTEM_TABLE=0x%08x BM_COMMAND=0x%08x BM_STATUS=0x%08x CRTC_INT_CNTL=0x%08x\n", rd(BUS_CNTL), rd(BM_SYSTEM_TABLE), rd(BM_COMMAND), rd(BM_STATUS), rd(CRTC_INT_CNTL));
    if(!go){ printf("dry run ok; re-run with 'go'\n"); return 0; }

    for(i=0;i<NBYTES/4;i++) OSWriteLittleInt32((void*)CMD, i*4, 0xff000000u | i);    /* pixel i = 0xff000000|i */
    build(nd);
    save_bus=rd(BUS_CNTL);
    for(n=0;n<reps;n++){
        unsigned last=DSTBASE+NBYTES-4, spins=0;
        if(n==1) t0=now_s();
        for(i=0;i<NBYTES/4;i+=1024) OSWriteLittleInt32((void*)FB, DSTBASE+i*4, 0);   /* clear a dword per page-ish so stale data can't pass */
        OSWriteLittleInt32((void*)FB, last, 0);
        wr(BUS_CNTL, (save_bus & ~BUS_MASTER_DIS) | BUS_EXT_REG_EN);
        mb();
        wr(BM_SYSTEM_TABLE, ringbus | 0u);                          /* SYSTEM_TRIGGER 0: system memory -> frame buffer, starts now */
        if(n==0) printf("just after trigger: BM_SYSTEM_TABLE=0x%08x BM_COMMAND=0x%08x BM_STATUS=0x%08x CRTC_INT_CNTL=0x%08x\n", rd(BM_SYSTEM_TABLE), rd(BM_COMMAND), rd(BM_STATUS), rd(CRTC_INT_CNTL));
        while(fbrd(last)!=(0xff000000u|(NBYTES/4-1)) && ++spins<2000000u) ;
        if(spins>=2000000u){ printf("TIMEOUT on iteration %u: BM_SYSTEM_TABLE=0x%08x BM_COMMAND=0x%08x BM_STATUS=0x%08x CRTC_INT_CNTL=0x%08x\n", n, rd(BM_SYSTEM_TABLE), rd(BM_COMMAND), rd(BM_STATUS), rd(CRTC_INT_CNTL)); break; }
        if(n==0){
            printf("after completion: BM_SYSTEM_TABLE=0x%08x BM_COMMAND=0x%08x BM_STATUS=0x%08x CRTC_INT_CNTL=0x%08x\n", rd(BM_SYSTEM_TABLE), rd(BM_COMMAND), rd(BM_STATUS), rd(CRTC_INT_CNTL));
            for(i=0;i<NBYTES/4;i++) if(fbrd(DSTBASE+i*4)!=(0xff000000u|i)){ if(bad<4) printf("  mismatch dword %u: got 0x%08x\n", i, fbrd(DSTBASE+i*4)); bad++; }
            printf("verification: %u of %u dwords wrong\n", bad, NBYTES/4);
        }
    }
    dt=now_s()-t0;

    /* ---- (a) concurrency: 2D fills through the register FIFO while a system transfer is in flight ---- */
    if(n==reps){
        unsigned r, ok=0, fails=0, last=DSTBASE+NBYTES-4, spins;
        for(r=0;r<20;r++){
            unsigned FILLB=0x380000u, c=0xff102030u+r;
            OSWriteLittleInt32((void*)FB, last, 0); OSWriteLittleInt32((void*)FB, FILLB+10u*256u*4u+10u*4u, 0);
            wr(BUS_CNTL,(save_bus & ~BUS_MASTER_DIS)|BUS_EXT_REG_EN); mb();
            wr(BM_SYSTEM_TABLE, ringbus|0u);
            fill(FILLB, c, 64, 64);                                         /* engine busy with a fill while the DMA streams */
            widle();
            spins=0; while(fbrd(last)!=(0xff000000u|(NBYTES/4-1)) && ++spins<2000000u) ;
            { unsigned bad2=0, q; for(q=0;q<NBYTES/4;q+=7) if(fbrd(DSTBASE+q*4)!=(0xff000000u|q)) bad2++;
              if(spins<2000000u && !bad2 && fbrd(FILLB+10u*256u*4u+10u*4u)==c) ok++; else fails++; }
        }
        printf("concurrency (fill through the FIFO during a transfer): %u of 20 rounds had both the image and the fill intact, %u failed\n", ok, fails);
    }
    /* ---- (b) CPU free while a transfer runs ---- */
    if(n==reps){
        double te=now_s(), dmat=0, freef=0; unsigned long spin=0, base_rate, iters2; unsigned r, last=DSTBASE+NBYTES-4;
        while(now_s()-te<0.2){ unsigned q; for(q=0;q<1000;q++) spin++; } base_rate=(unsigned long)(spin/(now_s()-te));
        for(r=0;r<20;r++){
            OSWriteLittleInt32((void*)FB, last, 0); wr(BUS_CNTL,(save_bus & ~BUS_MASTER_DIS)|BUS_EXT_REG_EN); mb();
            iters2=0; wr(BM_SYSTEM_TABLE, ringbus|0u); te=now_s();
            do { unsigned q; for(q=0;q<5000;q++) iters2++; } while(rd(BM_COMMAND)!=0x80000000u || rd(BM_SYSTEM_TABLE)!=ringbus+nd*16u);
            { double d=now_s()-te; dmat+=d; freef+=(double)iters2/((double)base_rate*d); }
        }
        printf("transfer of one 112 KB image: %.2f ms; CPU free while it runs (polling BM_COMMAND/BM_SYSTEM_TABLE every ~5000 spins): %.0f%%\\n", dmat*1000/20, 100.0*freef/20);
    }
    wr(BUS_CNTL, save_bus);
    if(n==reps) printf("system bus-master upload: %u x %u KB in %.3f s = %.1f MB/s (includes the page-clear writes and sentinel polling)\n", reps-1, NBYTES/1024, dt, (reps-1)*(NBYTES/1048576.0)/dt);
    IOServiceClose(conn); (void)cfg; return 0;
}

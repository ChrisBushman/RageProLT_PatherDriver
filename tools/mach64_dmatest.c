/*
 * mach64_dmatest.c - minimal bus-master DMA test: DMA one register write.
 *
 * The safe first ring milestone (the DRI's bm_dma_test equivalent): build a
 * command buffer that writes SCRATCH_REG1, put a descriptor for it in the ring,
 * activate bus-mastering, kick, and verify SCRATCH_REG1 changed.  Proves the
 * DMA memory / bus addresses / descriptor / kick all work before any vertices.
 *
 * The kext allocates the ring + command buffer physically-contiguous and maps
 * them UNCACHED to us (no PPC cache flush needed); get_config returns their bus
 * addresses.  We orchestrate the ring + kick via the register window.
 *
 * QUIESCE: done with the engine idle; we save/restore ATIRagePro's BUS_CNTL/
 * SRC_CNTL/BM_GUI_TABLE around our DMA.  A collision with ATIRagePro's async
 * DMA is possible (no lock) -> display glitch/reboot; acceptable headless.
 *
 * Build with -framework IOKit.
 */
#include <IOKit/IOKitLib.h>
#include <libkern/OSByteOrder.h>
#include <mach/mach.h>
#include <stdio.h>
#include <string.h>
#include "../common/mach64_abi.h"

#define RAM_LIMIT  0x20000000u   /* 512MB; bus addr must be valid RAM */

#define REG_WINDOW    0x7FF800u
#define SCRATCH_REG1  0x0484
#define PAT_REG0      0x0680   /* GUI drawing-engine pattern regs: the DRI's */
#define PAT_REG1      0x0684   /* bm_dma_test target (reachable via GUI-master) */
#define BUS_CNTL      0x04a0
#define SRC_CNTL      0x05b4
#define DST_HEIGHT_WIDTH 0x0518
#define BM_ADDR       0x0648
#define BM_GUI_TABLE_CMD 0x064c
#define BM_GUI_TABLE  0x01b8
#define FIFO_STAT     0x0710
#define GUI_STAT      0x0738
#define APERTURE_OFFSET 0x7ff800u
#define BUS_MASTER_DIS  (1u<<6)
#define BUS_EXT_REG_EN  (1u<<27)
#define SRC_BM_ENABLE   (1u<<8)
#define SRC_BM_SYNC     (1u<<9)
#define SRC_BM_OP_SYSTEM_TO_REG (3u<<10)
#define DMA_EOL         (1u<<31)
#define DMA_HOLD_OFFSET (1u<<30)
#define GUI_ACTIVE      1u
#define FIFO_SLOT_MASK  0xffffu
#define ADRINDEX(r)  ((unsigned)((r)>=0x400 ? (((r)-0x400)>>2) : (((r)>>2)|0x100)))
#define TESTVAL      0xCAFEF00Du

static void mb(void){ __asm__ volatile("sync; eieio" ::: "memory"); }
static unsigned REGB, RING, CMD;
static unsigned rd(unsigned o){ return OSReadLittleInt32((void*)REGB, o); }
static void     wr(unsigned o, unsigned v){ OSWriteLittleInt32((void*)REGB, o, v); }
static void     rw(unsigned o, unsigned v){ OSWriteLittleInt32((void*)RING, o, v); }
static void     cw(unsigned o, unsigned v){ OSWriteLittleInt32((void*)CMD, o, v); }
static int wfifo(int e){ int i; for(i=0;i<1000000;i++) if((rd(FIFO_STAT)&FIFO_SLOT_MASK)<=(unsigned)(0x8000>>e))return 0; return -1; }
static int widle(void){ int i; if(wfifo(16)<0)return -1; for(i=0;i<2000000;i++) if(!(rd(GUI_STAT)&GUI_ACTIVE))return 0; return -1; }

static unsigned mapmem(io_connect_t c, unsigned type)
{
    vm_address_t a=0; vm_size_t s=0;
    if (IOConnectMapMemory(c,type,mach_task_self(),&a,&s,kIOMapAnywhere|kIOMapInhibitCache)!=KERN_SUCCESS)
        return 0;
    return a;
}

int main(int argc, char **argv)
{
    io_service_t svc; io_connect_t conn; kern_return_t kr;
    vm_address_t fb=0; vm_size_t fbs=0;
    unsigned cmdbus=0, ringbus=0, cfg=0;
    unsigned save_bus, save_src, save_tbl, before, after;
    int valid, go;

    svc=IOServiceGetMatchingService(kIOMasterPortDefault,IOServiceMatching("Mach64Accelerator"));
    if(!svc){fprintf(stderr,"no service\n");return 1;}
    kr=IOServiceOpen(svc,mach_task_self(),MACH64_GL_CONNECT_TYPE,&conn); IOObjectRelease(svc);
    if(kr){fprintf(stderr,"open 0x%x\n",kr);return 1;}
    if(IOConnectMapMemory(conn,MACH64_MEM_SURFACE,mach_task_self(),&fb,&fbs,kIOMapAnywhere|kIOMapInhibitCache)){fprintf(stderr,"map fb\n");return 1;}
    REGB=fb+REG_WINDOW;
    CMD =mapmem(conn,MACH64_MEM_CMDBUF);
    RING=mapmem(conn,MACH64_MEM_RING);
    if(!CMD||!RING){fprintf(stderr,"map cmd/ring failed\n");return 1;}

    /* get_config -> bus addresses (this build) */
    IOConnectMethodScalarIScalarO(conn, kMach64Sel_get_config, 0, 3, &cmdbus, &ringbus, &cfg);
    printf("cmdbuf bus=0x%08x  ring bus=0x%08x  (RAM limit 0x%08x)\n", cmdbus, ringbus, RAM_LIMIT);

    /* HARD SAFETY GUARD: never activate DMA with an out-of-range bus address
     * (that panicked the kernel last time -- addr was ~0xd0320000 > RAM). */
    valid = cmdbus>=0x1000 && cmdbus<RAM_LIMIT && ringbus>=0x1000 && ringbus<RAM_LIMIT;
    printf("bus addresses: %s\n", valid ? "VALID (in RAM)" :
           "*** INVALID -- refusing to activate DMA ***");
    if(!valid) goto out;

    go = (argc>1 && strcmp(argv[1],"go")==0);
    if(!go){ printf("\nDRY RUN ok. Re-run with 'go' to actually activate DMA.\n"); goto out; }

    /* command buffer: DMA-write PAT_REG0/PAT_REG1, exactly like the DRI's
     * bm_dma_test.  Target the PATTERN registers (GUI drawing-engine block) --
     * NOT scratch: the bus-master GUI path decodes GUI registers, which is
     * almost certainly why SCRATCH_REG1 never changed even though the engine
     * walked the ring.  Header = DMAREG(reg) | ((n_regs-1)<<16). */
    cw(0,  ADRINDEX(PAT_REG0) | (1u<<16));  /* write 2 consecutive regs */
    cw(4,  0x22222222u);                     /* -> PAT_REG0 */
    cw(8,  0xAAAAAAAAu);                     /* -> PAT_REG1 */
    cw(12, ADRINDEX(SRC_CNTL) | (0u<<16));   /* terminator: SRC_CNTL = 0 */
    cw(16, 0);

    /* descriptor: DMA the 20-byte command buffer to the BM_ADDR data port */
    rw(0,  APERTURE_OFFSET + BM_ADDR);
    rw(4,  cmdbus);
    rw(8,  20u | DMA_HOLD_OFFSET | DMA_EOL);
    rw(12, 0);

    /* init test regs via MMIO (confirms plain register writes work first) */
    wr(SRC_CNTL, 0);
    wr(PAT_REG0, 0x11111111u);
    wr(PAT_REG1, 0x11111111u);
    widle();
    before = rd(PAT_REG0);
    printf("PAT before: PAT_REG0=0x%08x PAT_REG1=0x%08x (want 0x11111111 both)\n",
           before, rd(PAT_REG1));

    save_bus=rd(BUS_CNTL); save_src=rd(SRC_CNTL); save_tbl=rd(BM_GUI_TABLE);
    printf("saved BUS_CNTL=0x%08x SRC_CNTL=0x%08x BM_GUI_TABLE=0x%08x\n", save_bus,save_src,save_tbl);

    mb();                              /* flush cmd/ring stores to RAM */
    if(widle()<0) fprintf(stderr,"warn: not idle before DMA\n");
    wr(BUS_CNTL, (save_bus & ~BUS_MASTER_DIS) | BUS_EXT_REG_EN);
    wr(BM_GUI_TABLE_CMD, ringbus /* | CIRCULAR_BUF_SIZE_16KB(0) */);
    wr(SRC_CNTL, SRC_BM_ENABLE | SRC_BM_SYNC | SRC_BM_OP_SYSTEM_TO_REG);
    mb();
    wr(DST_HEIGHT_WIDTH, 0);           /* kick the transfer */
    if(widle()<0) fprintf(stderr,"warn: DMA didn't go idle (possible stall)\n");

    /* diagnostics: did the engine walk the ring? (BM_GUI_TABLE should advance
     * from ring base by one descriptor = 0x10 bytes if it processed ours) */
    printf("after kick: BM_GUI_TABLE=0x%08x (ring base 0x%08x) GUI_STAT=0x%08x\n",
           rd(BM_GUI_TABLE), ringbus, rd(GUI_STAT));

    after = rd(PAT_REG0);
    unsigned after1 = rd(PAT_REG1);

    /* restore ATIRagePro's DMA state + pattern regs */
    wr(SRC_CNTL, save_src);
    wr(BM_GUI_TABLE_CMD, save_tbl & 0xfffffff0);
    wr(BUS_CNTL, save_bus);
    wr(PAT_REG0, 0x11111111u);
    wr(PAT_REG1, 0x11111111u);

    printf("PAT after:  PAT_REG0=0x%08x (want 0x22222222) PAT_REG1=0x%08x (want 0xaaaaaaaa)\n",
           after, after1);
    printf("\n%s\n", (after==0x22222222u && after1==0xAAAAAAAAu)
        ? "DMA RING WORKS: engine bus-master-executed our command buffer!"
        : "DMA did not take -- check bus address / coherency / activation");
out:
    IOServiceClose(conn); return 0;
}

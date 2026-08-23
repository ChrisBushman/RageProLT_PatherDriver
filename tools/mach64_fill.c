/*
 * mach64_fill.c - first ENGINE operation: a solid-color rectangle fill into
 * OFF-SCREEN VRAM, verified by reading the VRAM back (not by the screen).
 *
 * This is the first side-effecting engine use, and it shares the 2D engine
 * with ATIRagePro.  Safety measures:
 *   - target is 6MB into VRAM (past any visible framebuffer) -> nothing visible
 *   - save ALL engine context registers we touch, restore them after -> the
 *     engine is left exactly as ATIRagePro had it
 *   - proper FIFO/idle waits with timeouts (no engine deadlock)
 *   - verify by reading the filled VRAM back
 *
 * Registers are in the LE framebuffer window (BAR0 + 0x7FF800); the fill
 * sequence mirrors the DRI mach64_dma_dispatch_clear path.
 * Build with -framework IOKit.
 */
#include <IOKit/IOKitLib.h>
#include <libkern/OSByteOrder.h>
#include <mach/mach.h>
#include <stdio.h>
#include "../common/mach64_abi.h"

#define REG_WINDOW   0x7FF800u

/* register offsets (from the DRI mach64 driver) */
#define Z_CNTL          0x054c
#define SCALE_3D_CNTL   0x05fc
#define DST_OFF_PITCH   0x0500
#define SC_LEFT_RIGHT   0x06a8
#define SC_TOP_BOTTOM   0x06b4
#define DP_BKGD_CLR     0x06c0
#define DP_FRGD_CLR     0x06c4
#define DP_WRITE_MASK   0x06c8
#define DP_PIX_WIDTH    0x06d0
#define DP_MIX          0x06d4
#define DP_SRC          0x06d8
#define DST_X_Y         0x06e8
#define DST_WIDTH_HEIGHT 0x06ec
#define CLR_CMP_CNTL    0x0708
#define FIFO_STAT       0x0710
#define GUI_TRAJ_CNTL   0x0730
#define GUI_STAT        0x0738

#define FIFO_SLOT_MASK  0x0000ffff
#define GUI_ACTIVE      0x00000001
#define DATATYPE_ARGB8888 6

/* fill parameters */
#define FILL_OFFSET  0x600000u          /* 6MB into VRAM: off-screen        */
#define FILL_PITCH   256                /* bytes per row                    */
#define FILL_W       8
#define FILL_H       8
#define FILL_COLOR   0xAA55CC33u

static unsigned REGB;   /* register-window base (userspace addr)            */

static unsigned rd(unsigned off)          { return OSReadLittleInt32((void*)REGB, off); }
static void     wr(unsigned off, unsigned v){ OSWriteLittleInt32((void*)REGB, off, v); }

static int wait_for_fifo(int entries)
{
    int i;
    for (i = 0; i < 1000000; i++)
        if ((rd(FIFO_STAT) & FIFO_SLOT_MASK) <= (unsigned)(0x8000 >> entries))
            return 0;
    fprintf(stderr, "wait_for_fifo(%d) TIMEOUT (FIFO_STAT=0x%08x)\n",
            entries, rd(FIFO_STAT));
    return -1;
}
static int wait_for_idle(void)
{
    int i;
    if (wait_for_fifo(16) < 0) return -1;
    for (i = 0; i < 1000000; i++)
        if (!(rd(GUI_STAT) & GUI_ACTIVE)) return 0;
    fprintf(stderr, "wait_for_idle TIMEOUT (GUI_STAT=0x%08x)\n", rd(GUI_STAT));
    return -1;
}
/* one FIFO-gated register write */
static void ewr(unsigned off, unsigned v) { wait_for_fifo(1); wr(off, v); }

/* context registers we modify (save/restore for coexistence) */
static const unsigned ctx_regs[] = {
    Z_CNTL, SCALE_3D_CNTL, SC_LEFT_RIGHT, SC_TOP_BOTTOM, CLR_CMP_CNTL,
    GUI_TRAJ_CNTL, DP_PIX_WIDTH, DP_FRGD_CLR, DP_WRITE_MASK, DP_MIX, DP_SRC,
    DST_OFF_PITCH,
};
#define NCTX (sizeof(ctx_regs)/sizeof(ctx_regs[0]))

int main(void)
{
    io_service_t  svc;
    io_connect_t  conn;
    kern_return_t kr;
    vm_address_t  addr = 0;
    vm_size_t     size = 0;
    unsigned      fb, saved[NCTX], i, pixw, ok = 1;

    svc = IOServiceGetMatchingService(kIOMasterPortDefault,
                                      IOServiceMatching("Mach64Accelerator"));
    if (!svc) { fprintf(stderr, "no Mach64Accelerator service\n"); return 1; }
    kr = IOServiceOpen(svc, mach_task_self(), MACH64_GL_CONNECT_TYPE, &conn);
    IOObjectRelease(svc);
    if (kr != KERN_SUCCESS) { fprintf(stderr, "IOServiceOpen: 0x%x\n", kr); return 1; }
    kr = IOConnectMapMemory(conn, MACH64_MEM_SURFACE, mach_task_self(),
                            &addr, &size, kIOMapAnywhere | kIOMapInhibitCache);
    if (kr != KERN_SUCCESS) { fprintf(stderr, "map: 0x%x\n", kr); IOServiceClose(conn); return 1; }
    fb   = addr;
    REGB = addr + REG_WINDOW;

    printf("engine idle wait...\n"); fflush(stdout);
    if (wait_for_idle() < 0) { fprintf(stderr, "engine not idle; aborting\n"); goto out; }

    /* save context */
    for (i = 0; i < NCTX; i++) saved[i] = rd(ctx_regs[i]);

    /* zero the target first so the readback proves the ENGINE wrote it */
    for (i = 0; i < FILL_W; i++)
        OSWriteLittleInt32((void*)fb, FILL_OFFSET + i*4, 0);

    pixw = (DATATYPE_ARGB8888<<0)|(DATATYPE_ARGB8888<<4)|(DATATYPE_ARGB8888<<8)
         | (DATATYPE_ARGB8888<<16)|(DATATYPE_ARGB8888<<28);

    printf("programming solid fill: color=0x%08x  %dx%d at VRAM+0x%x\n",
           FILL_COLOR, FILL_W, FILL_H, FILL_OFFSET); fflush(stdout);

    ewr(Z_CNTL, 0);
    ewr(SCALE_3D_CNTL, 0);
    ewr(SC_LEFT_RIGHT, (0x1fff << 16) | 0);
    ewr(SC_TOP_BOTTOM, (0x1fff << 16) | 0);
    ewr(CLR_CMP_CNTL, 0);
    ewr(GUI_TRAJ_CNTL, (1<<0) | (1<<1));          /* L->R, T->B */
    ewr(DP_PIX_WIDTH, pixw);
    ewr(DP_FRGD_CLR, FILL_COLOR);
    ewr(DP_WRITE_MASK, 0xffffffff);
    ewr(DP_MIX, (3<<0) | (7<<16));                /* BKGD_MIX_D | FRGD_MIX_S */
    ewr(DP_SRC, (1<<0) | (1<<8) | (0<<16));        /* both srcs = FRGD_CLR, mono=one */
    ewr(DST_OFF_PITCH, ((FILL_PITCH/8) << 22) | (FILL_OFFSET >> 3));
    ewr(DST_X_Y, 0);
    ewr(DST_WIDTH_HEIGHT, (FILL_H << 16) | FILL_W); /* <-- triggers the fill */

    if (wait_for_idle() < 0) fprintf(stderr, "warning: engine didn't go idle\n");

    /* restore context so ATIRagePro sees the engine untouched */
    for (i = 0; i < NCTX; i++) { wait_for_fifo(1); wr(ctx_regs[i], saved[i]); }
    wait_for_idle();

    /* verify: read the filled VRAM back */
    printf("\nreadback:\n");
    for (i = 0; i < FILL_W; i++) {
        unsigned v = OSReadLittleInt32((void*)fb, FILL_OFFSET + i*4);
        printf("  pixel[%u] = 0x%08x  %s\n", i, v, v==FILL_COLOR?"OK":"--");
        if (v != FILL_COLOR) ok = 0;
    }
    {   unsigned outside = OSReadLittleInt32((void*)fb, FILL_OFFSET + 0x80);
        printf("  outside  = 0x%08x  (should NOT be fill color)\n", outside);
    }
    printf("\n%s\n", ok ? "ENGINE FILL CONFIRMED: the 2D engine executed our command!"
                        : "*** fill did not appear in VRAM ***");
out:
    IOConnectUnmapMemory(conn, MACH64_MEM_SURFACE, mach_task_self(), addr);
    IOServiceClose(conn);
    return ok ? 0 : 1;
}

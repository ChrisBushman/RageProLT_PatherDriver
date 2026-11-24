/*
 * gld_engine.c - single-thread engine: kick a batch, keep building, poll for completion (no worker thread).
 *
 * Why: the DMA worker thread (gld_render.c vdma_*) overlaps the engine with the game on a 2-CPU machine, but on a single CPU the worker's wait competes with the main thread
 * for the one CPU, and the scheduler, not the driver, decides the order.  Here the main thread does everything, in an order the driver controls:
 *     build batch N+1 while the engine drains batch N;  poll before N+1 is kicked;  fence (drain + release) before anything else touches the accelerator.
 * The engine is driven entirely from userland through the Path A kext's buffers and the BAR register window (the same recipe as the kernel handler's dma_start/dma_done):
 *   - the stream (DRI register packets + a SRC_CNTL=0 terminator) goes into one of two 128 KB uncached staging buffers, little-endian, with a descriptor table in a 16 KB-aligned
 *     block of the ring; descriptors are {BM_ADDR aperture, stream bus address, count|HOLD|EOL, 0}
 *   - kick = BUS_CNTL/BM_GUI_TABLE_CMD/SRC_CNTL programming + a write of DST_HEIGHT_W; done = BM_GUI_TABLE == table end and the engine idle; then the registers are restored
 *   - Apple's accelerator mutex (accel[0x544]; the lock every GL/2D drain takes) is taken with Path A's accel_hold when the first batch is kicked and kept until the engine
 *     is fenced, so WindowServer's 2D cannot interleave register writes with the walk.  The consequence: NO Apple call may be made while it is held (it would self-deadlock),
 *     so gld.h routes every IOConnect... and IOService... call through mach64_eng_guard(), which fences first.  A 2 s watchdog in the kext releases a hold whose owner died.
 * Enabled by MACH64_SINGLE=1 (or automatically on a 1-CPU machine; MACH64_SINGLE=0 forces the worker design).
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/types.h>
#include <sys/sysctl.h>
#include <mach/mach_time.h>
#include <libkern/OSByteOrder.h>
#include <IOKit/IOKitLib.h>
#include "gld.h"
#include "../common/mach64_abi.h"

#undef IOConnectMethodScalarIScalarO     /* this file talks to Path A (never fenced) and Apple's peek, so it uses the real calls */
#undef IOConnectMethodScalarIStructureI
#undef IOConnectMethodStructureIStructureO
#undef IOConnectMethodScalarIStructureO
#undef IOConnectMapMemory
#undef IOConnectUnmapMemory
#undef IOServiceOpen
#undef IOServiceClose

extern int  mach64_pathA(io_connect_t *conn, volatile uint8_t **regs, volatile uint8_t **cmd, volatile uint8_t **ring, uint32_t *cmdbus, uint32_t *ringbus, size_t *cmdsz, size_t *ringsz);
extern void mach64_eng_lock(void);
extern void mach64_eng_unlock(void);

#define R_BUS_CNTL      0x04a0u
#define R_DST_HEIGHT_W  0x0518u
#define R_SRC_CNTL      0x05b4u
#define R_BM_GUI_TABLE  0x01b8u
#define R_BM_TABLE_CMD  0x064cu
#define R_GUI_STAT      0x0738u
#define R_FIFO_STAT     0x0710u
#define APERTURE_BM_ADDR 0x007ffe48u
#define CHUNK           4092u
#define ENG_BUF         0x20000u          /* one staging buffer; buffer b lives at cmd + ENG_BUF * (1 + b) (buffer 0 of the Path A kext is the texture-upload staging) */
#define ENG_MAXWORDS    ((ENG_BUF - 16u) / 4u)
#define HOLD_REHOLD_NS  1000000000ull      /* re-take the hold if it is older than this (the kext's watchdog fires at 2 s) */

static struct {
    int state;                            /* 0 untried, 1 ready, -1 unavailable */
    int held, inflight, cur;
    io_connect_t pa;                      /* Path A connection */
    volatile uint8_t *regs, *cmd, *ring;
    uint32_t cmdbus, ringbus, mutex, tbl[2];   /* tbl[b] = offset of buffer b's 16 KB-aligned descriptor table inside the ring */
    uint32_t end, sv_bus, sv_src, sv_tbl;
    uint64_t t_hold;
} g;

static inline unsigned rd(uint32_t r) { return OSReadLittleInt32((void *)g.regs, r); }
static inline void wr(uint32_t r, uint32_t v) { OSWriteLittleInt32((void *)g.regs, r, v); __asm__ volatile("eieio" ::: "memory"); }
static uint64_t ticks_ns(uint64_t t) { static mach_timebase_info_data_t tb; if (!tb.denom) mach_timebase_info(&tb); return t * tb.numer / tb.denom; }

int mach64_eng_single(void)
{
    static int on = -1;
    if (on < 0) {
        const char *e = getenv("MACH64_SINGLE");   /* user override: 1 = single-thread engine, 0 = worker-thread engine */
        if (e) on = atoi(e) != 0;
        else { int n = 2; size_t l = sizeof n; int mib[2] = { CTL_HW, HW_NCPU }; if (sysctl(mib, 2, &n, &l, 0, 0)) n = 2; on = (n == 1); }
    }
    return on;
}

/* true when the engine can be driven from userland (Path A new enough, mutex address known) */
int mach64_eng_ready(gld_context *ctx)
{
    uint32_t cmdbus, ringbus, a = 0, m = 0; size_t cs, rs;
    if (g.state) return g.state > 0;
    g.state = -1;
    if (!mach64_eng_single()) return 0;
    if (!mach64_pathA(&g.pa, &g.regs, &g.cmd, &g.ring, &cmdbus, &ringbus, &cs, &rs)) return 0;
    if (cs < 3u * ENG_BUF || rs < 0x10000u) return 0;
    if (IOConnectMethodScalarIScalarO(ctx->connect, 14, 1, 1, 0xb0u, &a) || a < 0x10000u || a > M64_KADDR_MAX || m64_peek(ctx->connect, 14, a + 0x544u, &m) || m < 0x10000u || m > M64_KADDR_MAX || (m & 3u))
        return 0;
    g.cmdbus = cmdbus; g.ringbus = ringbus; g.mutex = m;
    /* two descriptor tables, each a 16 KB block that is PHYSICALLY 16 KB aligned (the ring's own alignment is not honoured), above the texture-upload tables [0, 0x2400) */
    { uint32_t o = (((ringbus + 0x2400u) + 0x3fffu) & ~0x3fffu) - ringbus; g.tbl[0] = o; g.tbl[1] = o + 0x4000u; if (g.tbl[1] + 0x4000u > rs) return 0; }
    { uint32_t bus = rd(R_BUS_CNTL); if ((bus & (1u << 6)) || !(bus & (1u << 27))) { GLD_TRACE("eng_ready: bus mastering off (BUS_CNTL=0x%08x): engine off\n", bus); return 0; } }
    g.state = 1;
    GLD_TRACE("eng_ready: single-thread engine on\n");
    return 1;
}

static int idle_now(void) { return !(rd(R_FIFO_STAT) & 0xffffu) && !(rd(R_GUI_STAT) & 1u) && !(rd(R_GUI_STAT) & 1u); }
static int done_now(void) { return rd(R_BM_GUI_TABLE) == g.end && idle_now(); }

static void restore_regs(void)
{
    wr(R_SRC_CNTL, g.sv_src); wr(R_BM_TABLE_CMD, g.sv_tbl & 0xfffffff0u); wr(R_BUS_CNTL, g.sv_bus);
    g.inflight = 0;
}

/* wait until the batch in flight has drained (spin), then restore the registers.  The hold stays. */
static int wait_done(void)
{
    unsigned long n = 0; uint64_t t0 = mach_absolute_time(), lim = 0;
    while (!done_now()) {
        if ((++n & 0x3ffu) == 0) { lim = ticks_ns(mach_absolute_time() - t0); if (lim > 3000000000ull) break; }
    }
    if (lim > 3000000000ull) GLD_TRACE("eng: wait_done TIMEOUT table=0x%x end=0x%x GUI_STAT=0x%08x FIFO=0x%08x\n", rd(R_BM_GUI_TABLE), g.end, rd(R_GUI_STAT), rd(R_FIFO_STAT));
    restore_regs();
    return lim > 3000000000ull ? -1 : 0;
}

static void drop_hold(void)
{
    if (!g.held) return;
    IOConnectMethodScalarIScalarO(g.pa, kMach64Sel_accel_release, 0, 0);
    g.held = 0;
    mach64_eng_unlock();
}

/* drain the engine and give the accelerator back: after this any Apple call is safe */
void mach64_eng_fence(void)
{
    if (!g.held && !g.inflight) return;
    if (g.inflight) wait_done();
    drop_hold();
}

/* called (through the gld.h macros) before every call that goes to Apple's kext or the IOKit registry */
void mach64_eng_guard(io_connect_t c) { if (g.held && c != g.pa) mach64_eng_fence(); }
int  mach64_eng_held(void) { return g.held; }

/* Write the stream (+ terminator) into staging buffer b and its descriptor table; *endp = the table end the walk must reach. */
static int stage(int b, const uint32_t *w, size_t n, uint32_t *endp)
{
    volatile uint8_t *data = g.cmd + ENG_BUF * (1u + (unsigned)b), *tab = g.ring + g.tbl[b];
    uint32_t datapa = g.cmdbus + ENG_BUF * (1u + (unsigned)b), total, nd, i; size_t k;
    if (n == 0 || n > ENG_MAXWORDS) return -1;
    for (k = 0; k < n; k++) OSWriteLittleInt32((void *)data, (unsigned)(4u * k), w[k]);
    OSWriteLittleInt32((void *)data, (unsigned)(4u * n), 0x6du);                  /* terminator: SRC_CNTL (ADRINDEX 0x6d) = 0, as the DRI and the kernel handler do */
    OSWriteLittleInt32((void *)data, (unsigned)(4u * n + 4u), 0u);
    total = (uint32_t)(4u * n + 8u); nd = (total + CHUNK - 1u) / CHUNK;
    for (i = 0; i < nd; i++) {
        uint32_t off = i * CHUNK, c = total - off; if (c > CHUNK) c = CHUNK;
        OSWriteLittleInt32((void *)tab, 16u * i + 0u, APERTURE_BM_ADDR);
        OSWriteLittleInt32((void *)tab, 16u * i + 4u, datapa + off);
        OSWriteLittleInt32((void *)tab, 16u * i + 8u, c | 0x40000000u | ((off + c >= total) ? 0x80000000u : 0u));   /* HOLD_OFFSET always, EOL on the last */
        OSWriteLittleInt32((void *)tab, 16u * i + 12u, 0u);
    }
    __asm__ volatile("sync" ::: "memory");
    *endp = g.ringbus + g.tbl[b] + nd * 16u;
    return 0;
}

/* Hand a batch to the engine and return without waiting.  The words are copied, so the caller may reuse its buffer at once.  0 on success. */
int mach64_eng_kick(gld_context *ctx, const uint32_t *w, size_t n)
{
    int b; uint32_t newend, t;
    if (!mach64_eng_ready(ctx)) return -1;
    b = g.cur;
    if (stage(b, w, n, &newend)) return -1;                          /* buffer b is free: the batch in flight (if any) is on its twin */
    if (g.held && ticks_ns(mach_absolute_time() - g.t_hold) > HOLD_REHOLD_NS) mach64_eng_fence();   /* old enough that the kext watchdog may have taken it: drain and re-take it below */
    if (g.inflight) wait_done();                                      /* the previous batch (g.end is still ITS table end); the hold stays */
    if (!g.held) {
        kern_return_t kr;
        mach64_eng_lock();
        kr = IOConnectMethodScalarIScalarO(g.pa, kMach64Sel_accel_hold, 1, 0, g.mutex);
        if (kr) { GLD_TRACE("eng: accel_hold refused 0x%x\n", kr); mach64_eng_unlock(); return -1; }
        g.held = 1; g.t_hold = mach_absolute_time();
    }
    if (!idle_now()) { int i; for (i = 0; i < 4000000 && !idle_now(); i++) ; if (i >= 4000000) GLD_TRACE("eng: engine never idle before kick (GUI_STAT=0x%08x)\n", rd(R_GUI_STAT)); }   /* engine must be idle before the registers are reprogrammed (same as the kernel handler) */
    g.sv_bus = rd(R_BUS_CNTL); g.sv_src = rd(R_SRC_CNTL); g.sv_tbl = rd(R_BM_GUI_TABLE);
    g.end = newend;
    wr(R_BUS_CNTL, (g.sv_bus & ~0x40u) | 0x08000000u);               /* bus mastering on, extended registers on */
    wr(R_BM_TABLE_CMD, g.ringbus + g.tbl[b]);                         /* 16 KB circular size = 0 */
    wr(R_SRC_CNTL, (1u << 8) | (1u << 9) | (3u << 10));              /* BM_ENABLE | BM_SYNC | SYSTEM_TO_REG */
    __asm__ volatile("sync" ::: "memory");
    wr(R_DST_HEIGHT_W, 0);                                            /* kick the descriptor walk */
    for (t = 0; t < 8u; t++) (void)rd(R_GUI_STAT);                    /* ride out the ~1 us before GUI_ACTIVE rises */
    g.inflight = 1; g.cur ^= 1;
    return 0;
}

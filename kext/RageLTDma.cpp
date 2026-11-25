/* kext/RageLTDma.cpp - RAGE_FEAT_DMA: bus-master submission of validated batches (design 3.5).  The kernel owns the command buffer and the ring;
 * clients never see a bus address.  The stream is built here from the already-validated pairs plus the kernel prologue, as GUI register-write
 * packets (ADRINDEX header + value), chained over 4092-byte descriptors (the byte count is 12 bits wide, HW-proven), terminated by SRC_CNTL=0.
 * Completion = BM_GUI_TABLE reached the table end AND the engine idle for several reads (the kick can read idle for ~1 us).  Submit returns without
 * waiting; the next engine operation (any path) first drains via idleWait().  One buffer: a second submit waits for the first (serial model).
 * Anything that fails to allocate or time out falls back to PIO / resets the engine.  No floating point. */
#include "RageLTDevice.h"
#include <IOKit/IOLib.h>
#include <IOKit/IOBufferMemoryDescriptor.h>
#include <ppc/proc_reg.h>
#include "rage_guard.h"
#include "RageLTEngine.h"

#define LOG(...) IOLog("RageLT: " __VA_ARGS__)

#define DMA_CMD_BYTES   0x12000u         /* 8192 pairs * 8 + prologue + terminator, rounded */
#define DMA_RING_BYTES  0x8000u          /* 16 KB-aligned table picked inside (physical alignment of the request is not honoured, measured earlier) */
#define DMA_CHUNK       4092u

#if RAGE_FEAT_DMA
/* write back + invalidate every cache line of [p, p+len), then order it before the register writes that start the engine */
static void dmaFlush(volatile uint8_t *p, uint32_t len)
{
    uint32_t a = (uint32_t)p & ~31u, e = (uint32_t)p + len;
    for (; a < e; a += 32) __asm__ volatile("dcbf 0,%0" :: "r"(a) : "memory");
    __asm__ volatile("sync; eieio" ::: "memory");
}

bool RageLTDevice::dmaInit(void)
{
    IOOptionBits o = kIOMemoryPhysicallyContiguous | kIODirectionOutIn;
    IOByteCount seg; IOPhysicalAddress pc, pr;
    fDmaOk = false; fDmaUse = false; fDmaPending = false;
    fDmaCmd = IOBufferMemoryDescriptor::withOptions(o, DMA_CMD_BYTES, PAGE_SIZE);
    fDmaRing = IOBufferMemoryDescriptor::withOptions(o, DMA_RING_BYTES, 0x4000);
    if (!fDmaCmd || !fDmaRing) { LOG("DMA buffers not allocated: PIO only\n"); dmaFree(); return false; }
    if (fDmaCmd->prepare(kIODirectionOutIn) != kIOReturnSuccess || fDmaRing->prepare(kIODirectionOutIn) != kIOReturnSuccess) { dmaFree(); return false; }
    pc = fDmaCmd->getPhysicalSegment(0, &seg);
    if (seg < DMA_CMD_BYTES) { LOG("command buffer not contiguous\n"); dmaFree(); return false; }
    pr = fDmaRing->getPhysicalSegment(0, &seg);
    if (seg < DMA_RING_BYTES) { LOG("ring not contiguous\n"); dmaFree(); return false; }
    if (!pc || !pr || pc >= 0x80000000u || pr >= 0x80000000u) { LOG("implausible bus addresses 0x%x 0x%x\n", (unsigned)pc, (unsigned)pr); dmaFree(); return false; }
    /* The kernel's own (cached) addresses are used and flushed with dcbf before each kick: an uncached second mapping does not help, because the
     * allocation's zero-fill can still sit dirty in the cache and be what the (coherent) bus master reads (measured: ring never walked). */
    fDmaCmdKva = (volatile uint8_t *)fDmaCmd->getBytesNoCopy();
    fDmaRingKva0 = (volatile uint8_t *)fDmaRing->getBytesNoCopy();
    if (!fDmaCmdKva || !fDmaRingKva0) { dmaFree(); return false; }
    fDmaCmdBus = (uint32_t)pc;
    fDmaRingBus = ((uint32_t)pr + 0x3fffu) & ~0x3fffu;
    fDmaRingKva = fDmaRingKva0 + (fDmaRingBus - (uint32_t)pr);
    fDmaUse = true;                                                     /* default on; RAGE_GL_CONFIG key 1 selects PIO */
    fPCI->setBusMasterEnable(true);                                    /* PCI command bit 2: without it the engine never fetches the ring (measured) */
    fDmaOk = true;
    logEvent(RAGE_EV_MARK, 0, 0xd0010000u, fDmaCmdBus, fDmaRingBus, 0, 0);
    return true;
}

void RageLTDevice::dmaFree(void)
{
    fDmaOk = false;
    if (fDmaCmd) { fDmaCmd->complete(kIODirectionOutIn); fDmaCmd->release(); fDmaCmd = 0; }
    if (fDmaRing) { fDmaRing->complete(kIODirectionOutIn); fDmaRing->release(); fDmaRing = 0; }
}

/* Waits (bounded) for the pending stream.  Caller holds fLock.  On timeout the engine is reset and the work abandoned. */
IOReturn RageLTDevice::dmaWait(void)
{
    uint32_t spins, idle = 0, end;
    if (!fDmaPending) return kIOReturnSuccess;
    end = fDmaRingBus + 16u * fDmaNDesc;
    for (spins = 0; spins < RAGE_WAIT_SPINS; spins++) {
        uint32_t tbl = eng_rd(fRegs, MACH64_BM_GUI_TABLE) & ~0xfu;
        if (tbl == end && !(eng_rd(fRegs, MACH64_GUI_STAT) & MACH64_GUI_ACTIVE)) { if (++idle >= 8) break; } else idle = 0;
    }
    fDmaPending = false;
    if (idle < 8) {
        logEvent(RAGE_EV_ERROR, 0, 0xd0020000u, eng_rd(fRegs, MACH64_BM_GUI_TABLE), eng_rd(fRegs, MACH64_GUI_STAT), eng_rd(fRegs, MACH64_FIFO_STAT), fDmaNDesc);
        logEvent(RAGE_EV_ERROR, 0, 0xd0021000u, eng_rd(fRegs, MACH64_BUS_CNTL), eng_rd(fRegs, MACH64_SRC_CNTL), eng_rd(fRegs, MACH64_BM_GUI_TABLE_CMD), fPCI->configRead32(kIOPCIConfigCommand));
        logEvent(RAGE_EV_ERROR, 0, 0xd0022000u, OSReadLittleInt32((void *)fDmaRingKva, 0), OSReadLittleInt32((void *)fDmaRingKva, 4), OSReadLittleInt32((void *)fDmaRingKva, 8), OSReadLittleInt32((void *)fDmaCmdKva, 0));
        eng_reset(fRegs, 1);
        eng_wr(fRegs, MACH64_BUS_CNTL, fDmaSaveBus);
        fFenceCompleted = fFenceSubmitted;
        return kIOReturnTimeout;
    }
    eng_wr(fRegs, MACH64_BUS_CNTL, fDmaSaveBus);                       /* bus mastering off again between batches */
    fFenceCompleted = fFenceSubmitted;
    return kIOReturnSuccess;
}

/* pairs already validated by the caller; c = the owner's context (copied under fLock) */
IOReturn RageLTDevice::dmaSubmit(const struct rage_ctx *c, const uint32_t *pairs, uint32_t npairs)
{
    volatile uint32_t *w = (volatile uint32_t *)fDmaCmdKva; uint32_t n = 0, i, bytes, off, nd = 0;
    IOReturn rc = dmaWait();                                           /* single buffer: the previous stream must be finished */
    if (rc) return rc;
    if (eng_wait_idle(fRegs)) return kIOReturnTimeout;
#define PUT(reg, val) do { OSWriteLittleInt32((void *)w, 4u * n, MACH64_ADRINDEX(reg)); OSWriteLittleInt32((void *)w, 4u * n + 4u, (val)); n += 2; } while (0)
    { uint32_t pp[64], np = prologuePairs(c, pp), k; for (k = 0; k < np; k++) PUT(pp[2 * k], pp[2 * k + 1]); }
    /* Consecutive registers (reg, reg+4, ...) leave as ONE packet: the burst form is what latches per-vertex colour into the interpolator (a vertex
     * written as individual register packets gives flat colour, docs/re-notes/bus-master-ring.md), so a client's {S,T,W,SPEC,Z,ARGB,X_Y} run must stay a run. */
    for (i = 0; i < npairs; ) {
        uint32_t reg = pairs[2 * i], run = 1, k;
        while (i + run < npairs && run < 32u && pairs[2 * (i + run)] == reg + 4u * run && (reg < 0x400u) == (reg + 4u * run < 0x400u)) run++;
        OSWriteLittleInt32((void *)w, 4u * n, MACH64_ADRINDEX(reg) | ((run - 1u) << 16)); n++;
        for (k = 0; k < run; k++) {
            uint32_t val = pairs[2 * (i + k) + 1];
            if (pairs[2 * (i + k)] == MACH64_Z_CNTL && !c->z_set) val = 0;
            OSWriteLittleInt32((void *)w, 4u * n, val); n++;
        }
        i += run;
    }
    PUT(MACH64_SRC_CNTL, 0);                                           /* terminator: leaves SRC_CNTL as the PIO path expects */
#undef PUT
    bytes = 4u * n;
    for (off = 0; off < bytes; off += DMA_CHUNK, nd++) {
        uint32_t len = bytes - off > DMA_CHUNK ? DMA_CHUNK : bytes - off;
        volatile uint8_t *d = fDmaRingKva + 16u * nd;
        OSWriteLittleInt32((void *)d, 0, MACH64_APERTURE_OFFSET + MACH64_BM_ADDR);
        OSWriteLittleInt32((void *)d, 4, fDmaCmdBus + off);
        OSWriteLittleInt32((void *)d, 8, len | MACH64_DMA_HOLD_OFFSET | ((off + len >= bytes) ? MACH64_DMA_EOL : 0));
        OSWriteLittleInt32((void *)d, 12, 0);
    }
    fDmaNDesc = nd;
    dmaFlush(fDmaCmdKva, bytes);
    dmaFlush(fDmaRingKva, 16u * nd);
    fDmaSaveBus = eng_rd(fRegs, MACH64_BUS_CNTL);
    eng_wr(fRegs, MACH64_BUS_CNTL, (fDmaSaveBus & ~MACH64_BUS_MASTER_DIS) | MACH64_BUS_EXT_REG_EN);
    eng_wq(fRegs, MACH64_BM_GUI_TABLE_CMD, fDmaRingBus | MACH64_CIRCULAR_BUF_SIZE_16KB);
    eng_wq(fRegs, MACH64_SRC_CNTL, MACH64_SRC_BM_ENABLE | MACH64_SRC_BM_SYNC | MACH64_SRC_BM_OP_SYSTEM_TO_REG);
    fDmaPending = true;
    eng_wq(fRegs, MACH64_DST_HEIGHT_WIDTH, 0);                         /* kick */
    logEvent(RAGE_EV_MARK, 0, 0xd0030000u, bytes, nd, npairs, 0);
    return kIOReturnSuccess;
}

#if RAGE_DEBUG_ENGTEST
/* DEBUG: the minimal proven stream (bm_dma_test): DMA two pattern registers.  variant bit0: skip eng_reset-style prep; returns 1 on success. */
uint32_t RageLTDevice::dmaMinimalTest(uint32_t variant)
{
    volatile uint32_t *w = (volatile uint32_t *)fDmaCmdKva; uint32_t sb, r0, r1, t0, t1, spins;
    (void)variant;
    if (!fDmaOk || (!fEngineLive && !variant)) return 0xffff0000u;
    IOLockLock(fLock); (void)dmaWait();
    OSWriteLittleInt32((void *)w, 0, MACH64_ADRINDEX(0x680) | (1u << 16)); OSWriteLittleInt32((void *)w, 4, 0x22222222u); OSWriteLittleInt32((void *)w, 8, 0xaaaaaaaau);
    OSWriteLittleInt32((void *)w, 12, MACH64_ADRINDEX(MACH64_SRC_CNTL)); OSWriteLittleInt32((void *)w, 16, 0);
    OSWriteLittleInt32((void *)fDmaRingKva, 0, MACH64_APERTURE_OFFSET + MACH64_BM_ADDR); OSWriteLittleInt32((void *)fDmaRingKva, 4, fDmaCmdBus);
    OSWriteLittleInt32((void *)fDmaRingKva, 8, 20u | MACH64_DMA_HOLD_OFFSET | MACH64_DMA_EOL); OSWriteLittleInt32((void *)fDmaRingKva, 12, 0);
    dmaFlush(fDmaCmdKva, 32); dmaFlush(fDmaRingKva, 16);
    eng_wq(fRegs, MACH64_SRC_CNTL, 0); eng_wq(fRegs, 0x680, 0x11111111u); eng_wq(fRegs, 0x684, 0x11111111u);
    (void)eng_wait_idle(fRegs);
    sb = eng_rd(fRegs, MACH64_BUS_CNTL); t0 = eng_rd(fRegs, MACH64_BM_GUI_TABLE);
    eng_wr(fRegs, MACH64_BUS_CNTL, (sb & ~MACH64_BUS_MASTER_DIS) | MACH64_BUS_EXT_REG_EN);
    eng_wq(fRegs, MACH64_BM_GUI_TABLE_CMD, fDmaRingBus);
    eng_wq(fRegs, MACH64_SRC_CNTL, MACH64_SRC_BM_ENABLE | MACH64_SRC_BM_SYNC | MACH64_SRC_BM_OP_SYSTEM_TO_REG);
    eng_wq(fRegs, MACH64_DST_HEIGHT_WIDTH, 0);
    for (spins = 0; spins < 200000; spins++) if (eng_rd(fRegs, MACH64_BM_GUI_TABLE) != t0) break;
    t1 = eng_rd(fRegs, MACH64_BM_GUI_TABLE);
    (void)eng_wait_idle(fRegs);
    r0 = eng_rd(fRegs, 0x680); r1 = eng_rd(fRegs, 0x684);
    logEvent(RAGE_EV_MARK, 0, 0xd0040000u, t0, t1, r0, r1);
    eng_wq(fRegs, MACH64_SRC_CNTL, 0); eng_wr(fRegs, MACH64_BUS_CNTL, sb);
    IOLockUnlock(fLock);
    return (r0 == 0x22222222u && r1 == 0xaaaaaaaau ? 1u : 0u) | (t1 != t0 ? 2u : 0u);
}
#endif
#else
bool RageLTDevice::dmaInit(void) { fDmaOk = false; fDmaUse = false; fDmaPending = false; return false; }
void RageLTDevice::dmaFree(void) {}
IOReturn RageLTDevice::dmaWait(void) { return kIOReturnSuccess; }
IOReturn RageLTDevice::dmaSubmit(const struct rage_ctx *, const uint32_t *, uint32_t) { return kIOReturnUnsupported; }
#endif

/* Everything that touches the engine or the aperture from the CPU first calls this (caller holds fLock). */
IOReturn RageLTDevice::idleWait(void)
{
    IOReturn rc = dmaWait();
    if (rc) return rc;
    return eng_wait_idle(fRegs) ? kIOReturnTimeout : kIOReturnSuccess;
}

void RageLTDevice::dmaConfig(uint32_t key, uint32_t value)
{
    IOLockLock(fLock);
    if (key == 1) { (void)dmaWait(); fDmaUse = fDmaOk && value; }
    IOLockUnlock(fLock);
}

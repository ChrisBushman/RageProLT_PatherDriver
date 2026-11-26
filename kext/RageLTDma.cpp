/* kext/RageLTDma.cpp - RAGE_FEAT_DMA: bus-master submission of validated batches (design 3.5).  The kernel owns the command buffer and the ring;
 * clients never see a bus address.  The stream is built here from the already-validated pairs plus the kernel prologue, as GUI register-write
 * packets (ADRINDEX header + value), chained over 4092-byte descriptors (the byte count is 12 bits wide, HW-proven), terminated by SRC_CNTL=0.
 * Completion = BM_GUI_TABLE reached the table end AND the engine idle for several reads (the kick can read idle for ~1 us).  Submit returns without
 * waiting; the next engine operation (any path) first drains via idleWait().  Two buffers (the established single-thread engine of the SonicR plugin, gld_engine.c):
 * submit N+1 BUILDS its stream in the free buffer while the engine still draws N, and only then waits for N and kicks; no thread, no interrupt.
 * Anything that fails to allocate or time out falls back to PIO / resets the engine.  No floating point. */
#include "RageLTDevice.h"
#include <IOKit/IOLib.h>
#include <IOKit/IOBufferMemoryDescriptor.h>
#include <ppc/proc_reg.h>
#include "rage_guard.h"
#include "RageLTEngine.h"

#define LOG(...) IOLog("RageLT: " __VA_ARGS__)

#define DMA_CMD_BYTES   0x12000u         /* 8192 pairs * 8 + prologue + terminator, rounded */
#define DMA_RING_BYTES  0xc000u          /* two 16 KB-aligned tables picked inside (physical alignment of the request is not honoured, measured earlier: up to 16 KB lost to alignment) */
#define DMA_TABLE_STRIDE 0x4000u         /* table b = ring base + b * stride */
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
    fDmaOk = false; fDmaUse = false; fDmaPending = false; fGuiPending = false;
    fXferOk = false; fXferUse = false; fXferPending = false; fXferStage = 0; fXferTab = 0; fStageLock = 0;
    fDmaCmd = IOBufferMemoryDescriptor::withOptions(o, DMA_CMD_BYTES, PAGE_SIZE);
    fDmaCmd2 = IOBufferMemoryDescriptor::withOptions(o, DMA_CMD_BYTES, PAGE_SIZE);        /* optional: without it submits stay strictly serial */
    fDmaRing = IOBufferMemoryDescriptor::withOptions(o, DMA_RING_BYTES, 0x4000);
    if (!fDmaCmd || !fDmaRing) { LOG("DMA buffers not allocated: PIO only\n"); dmaFree(); return false; }
    if (fDmaCmd2 && fDmaCmd2->prepare(kIODirectionOutIn) != kIOReturnSuccess) { fDmaCmd2->release(); fDmaCmd2 = 0; }
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
    if (fDmaCmd2) {
        IOPhysicalAddress p2 = fDmaCmd2->getPhysicalSegment(0, &seg);
        fDmaCmdKva2 = (volatile uint8_t *)fDmaCmd2->getBytesNoCopy();
        if (seg < DMA_CMD_BYTES || !p2 || p2 >= 0x80000000u || !fDmaCmdKva2) { fDmaCmd2->complete(kIODirectionOutIn); fDmaCmd2->release(); fDmaCmd2 = 0; }
        else fDmaCmdBus2 = (uint32_t)p2;
    }
    fDmaCmdBus = (uint32_t)pc;
    fDmaRingBus = ((uint32_t)pr + 0x3fffu) & ~0x3fffu;
    fDmaRingKva = fDmaRingKva0 + (fDmaRingBus - (uint32_t)pr);
    fDmaUse = true;                                                     /* default on; RAGE_GL_CONFIG key 1 selects PIO */
    fPCI->setBusMasterEnable(true);                                    /* PCI command bit 2: without it the engine never fetches the ring (measured) */
    fDmaOk = true;
    xferInit();
    logEvent(RAGE_EV_MARK, 0, 0xd0010000u, fDmaCmdBus, fDmaRingBus, 0, 0);
    return true;
}

void RageLTDevice::dmaFree(void)
{
    fDmaOk = false; fXferOk = false;
    if (fXferStage) { fXferStage->complete(kIODirectionOutIn); fXferStage->release(); fXferStage = 0; }
    if (fXferTab) { fXferTab->complete(kIODirectionOutIn); fXferTab->release(); fXferTab = 0; }
    if (fStageLock) { IOLockFree(fStageLock); fStageLock = 0; }
    if (fDmaCmd) { fDmaCmd->complete(kIODirectionOutIn); fDmaCmd->release(); fDmaCmd = 0; }
    if (fDmaCmd2) { fDmaCmd2->complete(kIODirectionOutIn); fDmaCmd2->release(); fDmaCmd2 = 0; }
    if (fDmaRing) { fDmaRing->complete(kIODirectionOutIn); fDmaRing->release(); fDmaRing = 0; }
}

extern uint32_t pf_err;
/* ---- texture/pixel upload by system transfer (docs/re-notes/texture-dma-upload-standalone.md) ---- */
#define XFER_MAX_BYTES  0x100000u        /* preferred staging size = the largest single upload (1 MB); halved down to 64 KB if the contiguous allocation fails */
#define XFER_DESC       4096u

void RageLTDevice::xferInit(void)
{
    IOOptionBits o = kIOMemoryPhysicallyContiguous | kIODirectionOutIn;
    IOByteCount seg; IOPhysicalAddress ps, pt; uint32_t sz;
    fXferOk = false; fXferUse = false; fXferPending = false;
    fStageLock = IOLockAlloc();
    if (!fStageLock) return;
    for (sz = XFER_MAX_BYTES; sz >= 0x10000u && !fXferStage; sz >>= 1) {
        fXferStage = IOBufferMemoryDescriptor::withOptions(o, sz, PAGE_SIZE);
        if (fXferStage && fXferStage->prepare(kIODirectionOutIn) != kIOReturnSuccess) { fXferStage->release(); fXferStage = 0; }
        if (fXferStage) { ps = fXferStage->getPhysicalSegment(0, &seg); if (seg < sz || !ps || ps >= 0x80000000u || ((uint32_t)ps & 0xfffu)) { fXferStage->complete(kIODirectionOutIn); fXferStage->release(); fXferStage = 0; } else fXferStageBytes = sz; }
    }
    fXferTab = IOBufferMemoryDescriptor::withOptions(o, PAGE_SIZE, PAGE_SIZE);
    if (fXferTab && fXferTab->prepare(kIODirectionOutIn) != kIOReturnSuccess) { fXferTab->release(); fXferTab = 0; }
    if (!fXferStage || !fXferTab) { LOG("upload staging not allocated: CPU uploads only\n"); return; }
    ps = fXferStage->getPhysicalSegment(0, &seg); pt = fXferTab->getPhysicalSegment(0, &seg);
    fXferStageKva = (volatile uint8_t *)fXferStage->getBytesNoCopy(); fXferTabKva = (volatile uint8_t *)fXferTab->getBytesNoCopy();
    if (!pt || pt >= 0x80000000u || seg < PAGE_SIZE || ((uint32_t)pt & 0xfu) || !fXferStageKva || !fXferTabKva) { LOG("upload table not usable\n"); return; }
    fXferStageBus = (uint32_t)ps; fXferTabBus = (uint32_t)pt;
    fXferOk = true; fXferUse = true;
    logEvent(RAGE_EV_MARK, 0, 0xd0070000u, fXferStageBus, fXferTabBus, fXferStageBytes, 0);
}

IOReturn RageLTDevice::xferReserve(uint8_t **stage, uint32_t *maxBytes)
{
    IOReturn rc = kIOReturnSuccess;
    if (!fXferOk || !fXferUse || !fEngineLive) return kIOReturnUnsupported;
    IOLockLock(fStageLock);
    IOLockLock(fLock);
    if (fXferPending) rc = xferWait();                                  /* the stage is rewritten only after the previous transfer has finished */
    IOLockUnlock(fLock);
    if (rc || !fXferUse) { IOLockUnlock(fStageLock); return rc ? rc : kIOReturnUnsupported; }
    *stage = (uint8_t *)fXferStageKva; *maxBytes = fXferStageBytes;
    return kIOReturnSuccess;
}

void RageLTDevice::xferAbort(void) { IOLockUnlock(fStageLock); }

IOReturn RageLTDevice::xferKick(uint32_t owner, uint32_t off, uint32_t len, uint32_t flags)
{
    uint32_t nd, i; IOReturn rc;
    if ((off | len) & 3u || !len || len > fXferStageBytes || (flags & ~3u)) { IOLockUnlock(fStageLock); return kIOReturnBadArgument; }
    if (flags) rage_xfer_inplace((uint8_t *)fXferStageKva, len, flags);   /* cached stores; the stage is ours alone (stage lock) and no transfer is in flight */
    nd = (len + XFER_DESC - 1u) / XFER_DESC;
    IOLockLock(fLock);
    if (!rage_vram_owns(&fVram, off, len, owner)) { IOLockUnlock(fLock); IOLockUnlock(fStageLock); return kIOReturnBadArgument; }
    rc = idleWait();                                                    /* engine idle: the destination may be in use by a draw; also finishes any GUI stream */
    if (rc) { fXferUse = false; IOLockUnlock(fLock); IOLockUnlock(fStageLock); return rc; }
    for (i = 0; i < nd; i++) {
        uint32_t cnt = (i == nd - 1u) ? len - i * XFER_DESC : XFER_DESC; volatile uint8_t *d = fXferTabKva + 16u * i;
        OSWriteLittleInt32((void *)d, 0, off + i * XFER_DESC);          /* frame buffer offset (explicit per descriptor) */
        OSWriteLittleInt32((void *)d, 4, fXferStageBus + i * XFER_DESC);
        OSWriteLittleInt32((void *)d, 8, cnt | ((i == nd - 1u) ? MACH64_DMA_EOL : 0u));
        OSWriteLittleInt32((void *)d, 12, 0);
    }
    dmaFlush(fXferStageKva, len); dmaFlush(fXferTabKva, 16u * nd);
    fXferDst = off; fXferLen = len; fXferEnd = fXferTabBus + 16u * nd;
    fXferSaveBus = eng_rd(fRegs, MACH64_BUS_CNTL);
    eng_wr(fRegs, MACH64_BUS_CNTL, (fXferSaveBus & ~MACH64_BUS_MASTER_DIS) | MACH64_BUS_EXT_REG_EN);
    fXferPending = true; fDmaPending = true;
    eng_wr(fRegs, MACH64_BM_SYSTEM_TABLE, fXferTabBus);                 /* SYSTEM_TRIGGER 0: memory -> frame buffer, starts now */
    logEvent(RAGE_EV_MARK, owner, 0xd0071000u, off, len, nd, flags);
    IOLockUnlock(fLock);
    IOLockUnlock(fStageLock);
    return kIOReturnSuccess;
}

/* caller holds fLock.  Done = descriptors consumed (BM_COMMAND EOL, table pointer at the end) AND the last word reads back from VRAM as staged. */
IOReturn RageLTDevice::xferWait(void)
{
    volatile uint8_t *vram = (volatile uint8_t *)fMap->getVirtualAddress(); uint32_t spins, last, want; bool done = false;
    if (!fXferPending) return kIOReturnSuccess;
    last = fXferDst + fXferLen - 4u;
    want = OSReadLittleInt32((void *)fXferStageKva, fXferLen - 4u);
    for (spins = 0; spins < RAGE_WAIT_SPINS; spins++) {
        if (eng_rd(fRegs, MACH64_BM_COMMAND) == 0x80000000u && (eng_rd(fRegs, MACH64_BM_SYSTEM_TABLE) & ~0xfu) == fXferEnd) { done = true; break; }
    }
    if (done) { done = false; for (spins = 0; spins < RAGE_WAIT_SPINS; spins++) if (OSReadLittleInt32((void *)vram, last) == want) { done = true; break; } }
    fXferPending = false; fDmaPending = fGuiPending;
    if (!done) {
        pf_err++; fXferUse = false;                                    /* never trust it again this session: the CPU path from now on */
        logEvent(RAGE_EV_ERROR, 0, 0xd0072000u, eng_rd(fRegs, MACH64_BM_SYSTEM_TABLE), eng_rd(fRegs, MACH64_BM_COMMAND), fXferDst, fXferLen);
        logEvent(RAGE_EV_ERROR, 0, 0xd0073000u, eng_rd(fRegs, MACH64_BUS_CNTL), OSReadLittleInt32((void *)vram, last), want, fPCI->configRead32(kIOPCIConfigCommand));
        eng_reset(fRegs, 1);
        eng_wr(fRegs, MACH64_BUS_CNTL, fXferSaveBus);
        fFenceCompleted = fFenceSubmitted;
        return kIOReturnTimeout;
    }
    eng_wr(fRegs, MACH64_BUS_CNTL, fXferSaveBus);                       /* bus mastering exactly as found */
    return kIOReturnSuccess;
}

/* PROFILE (always compiled, a few mftb reads per submit): ticks spent building the stream, waiting for the previous stream, waiting for engine idle before the kick, and in idleWait() from
 * every other path (clear/fill/present/readback...: dma part and engine-idle part).  Logged as marks 0xd0060000 / 0xd0061000 every 256 submits (raw mftb ticks). */
uint32_t pf_user, pf_valid, pf_tex, pf_err;   /* pf_err: cumulative engine timeouts/resets (dmaWait, idle before kick, waitFence) */
uint32_t pf_dummy_;                       /* GL client: userIn of the pairs, rage_batch_ok, tex-record handling (ticks) */
static uint32_t pf_starved, pf_have, pf_build, pf_prev, pf_idle, pf_oth_dma, pf_oth_idle, pf_n, pf_oth_n, pf_total;   /* cumulative since load, in units of 4 mftb ticks (96 ns): the event log drops records, so the last surviving mark holds the totals */

/* Waits (bounded) for the pending stream.  Caller holds fLock.  On timeout the engine is reset and the work abandoned. */
IOReturn RageLTDevice::dmaWait(void)
{
    uint32_t spins, idle = 0, end;
    if (!fDmaPending) return kIOReturnSuccess;
    if (fXferPending) { IOReturn xr = xferWait(); if (xr) { fDmaPending = fGuiPending; return xr; } }   /* a texture/pixel upload in flight is always finished first */
    if (!fGuiPending) { fDmaPending = false; return kIOReturnSuccess; }
    end = fDmaEnd;                                                     /* table end of the stream in flight (set at its kick) */
    for (spins = 0; spins < RAGE_WAIT_SPINS; spins++) {
        uint32_t tbl = eng_rd(fRegs, MACH64_BM_GUI_TABLE) & ~0xfu;
        if (tbl == end && !(eng_rd(fRegs, MACH64_GUI_STAT) & MACH64_GUI_ACTIVE)) { if (++idle >= 8) break; } else idle = 0;
    }
    fDmaPending = false; fGuiPending = false;
    if (idle < 8) {
        pf_err++;
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

/* pairs already validated by the caller; c = the owner's context (copied under fLock).
 * Order (the established double-buffer engine): build into the buffer the engine is NOT reading -> wait for the stream in flight -> engine idle -> kick. */
IOReturn RageLTDevice::dmaSubmit(const struct rage_ctx *c, const uint32_t *pairs, uint32_t npairs)
{
    uint32_t n = 0, i, bytes, off, nd = 0, b = fDmaCmd2 ? fDmaCur : 0u, cmdBus, ringBus, t_a = tbl32(), t_b, t_c; IOReturn rc;
    volatile uint32_t *w = (volatile uint32_t *)(b ? fDmaCmdKva2 : fDmaCmdKva); volatile uint8_t *tab = fDmaRingKva + b * DMA_TABLE_STRIDE;
    cmdBus = b ? fDmaCmdBus2 : fDmaCmdBus; ringBus = fDmaRingBus + b * DMA_TABLE_STRIDE;
    if (!fDmaCmd2) { rc = dmaWait(); if (rc) return rc; }              /* single buffer: the previous stream must be finished before its memory is rewritten */
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
        volatile uint8_t *d = tab + 16u * nd;
        OSWriteLittleInt32((void *)d, 0, MACH64_APERTURE_OFFSET + MACH64_BM_ADDR);
        OSWriteLittleInt32((void *)d, 4, cmdBus + off);
        OSWriteLittleInt32((void *)d, 8, len | MACH64_DMA_HOLD_OFFSET | ((off + len >= bytes) ? MACH64_DMA_EOL : 0));
        OSWriteLittleInt32((void *)d, 12, 0);
    }
    dmaFlush((volatile uint8_t *)w, bytes);
    dmaFlush(tab, 16u * nd);
    t_b = tbl32();
    if (fGuiPending) { pf_have++; if ((eng_rd(fRegs, MACH64_BM_GUI_TABLE) & ~0xfu) == fDmaEnd && !(eng_rd(fRegs, MACH64_GUI_STAT) & MACH64_GUI_ACTIVE)) pf_starved++; }   /* PROFILE: was the previous stream already finished (chip starved by the CPU)? */
    rc = dmaWait();                                                    /* the stream on the other buffer (if any) must be finished before the engine is reprogrammed */
    if (rc) return rc;
    t_c = tbl32();
    if (eng_wait_idle(fRegs)) { pf_err++; return kIOReturnTimeout; }
    pf_build += (t_b - t_a) >> 2; pf_prev += (t_c - t_b) >> 2; pf_idle += (tbl32() - t_c) >> 2; pf_total++;
    if (++pf_n >= 256u) { logEvent(RAGE_EV_MARK, 0, 0xd0060000u, pf_build, pf_prev, pf_idle, pf_total); logEvent(RAGE_EV_MARK, 0, 0xd0061000u, pf_oth_dma, pf_oth_idle, pf_oth_n, 0); logEvent(RAGE_EV_MARK, 0, 0xd0062000u, pf_user, pf_valid, pf_tex, 0); { uint32_t vi, used = 0; for (vi = 0; vi < fVram.n; vi++) used += fVram.e[vi].len; logEvent(RAGE_EV_MARK, 0, 0xd0063000u, pf_starved, pf_have, pf_err, used); }  pf_n = 0; }
    fDmaNDesc = nd; fDmaEnd = ringBus + 16u * nd;
    fDmaSaveBus = eng_rd(fRegs, MACH64_BUS_CNTL);
    eng_wr(fRegs, MACH64_BUS_CNTL, (fDmaSaveBus & ~MACH64_BUS_MASTER_DIS) | MACH64_BUS_EXT_REG_EN);
    eng_wq(fRegs, MACH64_BM_GUI_TABLE_CMD, ringBus | MACH64_CIRCULAR_BUF_SIZE_16KB);
    eng_wq(fRegs, MACH64_SRC_CNTL, MACH64_SRC_BM_ENABLE | MACH64_SRC_BM_SYNC | MACH64_SRC_BM_OP_SYSTEM_TO_REG);
    fDmaPending = true; fGuiPending = true;
    eng_wq(fRegs, MACH64_DST_HEIGHT_WIDTH, 0);                         /* kick */
    if (fDmaCmd2) fDmaCur ^= 1u;                                       /* the next stream is built in the twin while this one draws */
    logEvent(RAGE_EV_MARK, 0, 0xd0030000u, bytes, nd, npairs, b);
    return kIOReturnSuccess;
}

#if RAGE_DEBUG_ENGTEST
/* DEBUG: the minimal proven stream (bm_dma_test): DMA two pattern registers.  variant bit0: skip eng_reset-style prep; returns 1 on success. */
uint32_t RageLTDevice::dmaMinimalTest(uint32_t variant)
{
    volatile uint32_t *w = (volatile uint32_t *)fDmaCmdKva; uint32_t sb, r0, r1, t0, t1, spins, ci0 = 0, ciK = 0, ciD = 0, ciE = 0;
    /* variant bit1: EOL interrupt probe, stage A (docs/re-notes/dma-async-queue.md): polling only, NO IRQ registered.  Does BUSMASTER_EOL_INT (CRTC_INT_CNTL bit 25) set when this
     * GUI-master list ends?  The register reads status at the bit positions where writes acknowledge, so a write keeps ONLY the enable bits (1,3,7,9,12,14,16,18,20,22,24,26,29)
     * plus the one acknowledge bit we mean (25). */
    const uint32_t ci_en = (1u<<1)|(1u<<3)|(1u<<7)|(1u<<9)|(1u<<12)|(1u<<14)|(1u<<16)|(1u<<18)|(1u<<20)|(1u<<22)|(1u<<24)|(1u<<26)|(1u<<29);
    if (!fDmaOk || (!fEngineLive && !(variant & 1u))) return 0xffff0000u;
    IOLockLock(fLock); (void)dmaWait();
    OSWriteLittleInt32((void *)w, 0, MACH64_ADRINDEX(0x680) | (1u << 16)); OSWriteLittleInt32((void *)w, 4, 0x22222222u); OSWriteLittleInt32((void *)w, 8, 0xaaaaaaaau);
    OSWriteLittleInt32((void *)w, 12, MACH64_ADRINDEX(MACH64_SRC_CNTL)); OSWriteLittleInt32((void *)w, 16, 0);
    OSWriteLittleInt32((void *)fDmaRingKva, 0, MACH64_APERTURE_OFFSET + MACH64_BM_ADDR); OSWriteLittleInt32((void *)fDmaRingKva, 4, fDmaCmdBus);
    OSWriteLittleInt32((void *)fDmaRingKva, 8, 20u | MACH64_DMA_HOLD_OFFSET | MACH64_DMA_EOL); OSWriteLittleInt32((void *)fDmaRingKva, 12, 0);
    dmaFlush(fDmaCmdKva, 32); dmaFlush(fDmaRingKva, 16);
    eng_wq(fRegs, MACH64_SRC_CNTL, 0); eng_wq(fRegs, 0x680, 0x11111111u); eng_wq(fRegs, 0x684, 0x11111111u);
    (void)eng_wait_idle(fRegs);
    sb = eng_rd(fRegs, MACH64_BUS_CNTL); t0 = eng_rd(fRegs, MACH64_BM_GUI_TABLE);
    if (variant & 2u) { ci0 = eng_rd(fRegs, MACH64_CRTC_INT_CNTL); eng_wr(fRegs, MACH64_CRTC_INT_CNTL, (ci0 & ci_en) | (1u << 25) | (1u << 24)); ciE = eng_rd(fRegs, MACH64_CRTC_INT_CNTL); }   /* ack any stale EOL, enable it */
    eng_wr(fRegs, MACH64_BUS_CNTL, (sb & ~MACH64_BUS_MASTER_DIS) | MACH64_BUS_EXT_REG_EN);
    eng_wq(fRegs, MACH64_BM_GUI_TABLE_CMD, fDmaRingBus);
    eng_wq(fRegs, MACH64_SRC_CNTL, MACH64_SRC_BM_ENABLE | MACH64_SRC_BM_SYNC | MACH64_SRC_BM_OP_SYSTEM_TO_REG);
    eng_wq(fRegs, MACH64_DST_HEIGHT_WIDTH, 0);
    for (spins = 0; spins < 200000; spins++) if (eng_rd(fRegs, MACH64_BM_GUI_TABLE) != t0) break;
    t1 = eng_rd(fRegs, MACH64_BM_GUI_TABLE);
    if (variant & 2u) ciK = eng_rd(fRegs, MACH64_CRTC_INT_CNTL);
    (void)eng_wait_idle(fRegs);
    if (variant & 2u) { ciD = eng_rd(fRegs, MACH64_CRTC_INT_CNTL); eng_wr(fRegs, MACH64_CRTC_INT_CNTL, (ci0 & ci_en & ~(1u << 24)) | (1u << 25)); }   /* disable EOL, ack it: back to the state we found */
    r0 = eng_rd(fRegs, 0x680); r1 = eng_rd(fRegs, 0x684);
    logEvent(RAGE_EV_MARK, 0, 0xd0040000u, t0, t1, r0, r1);
    if (variant & 2u) logEvent(RAGE_EV_MARK, 0, 0xd0050000u, ci0, ciE, ciK, ciD);      /* CRTC_INT_CNTL: found, after enable+ack, right after the walk moved, after idle */
    eng_wq(fRegs, MACH64_SRC_CNTL, 0); eng_wr(fRegs, MACH64_BUS_CNTL, sb);
    IOLockUnlock(fLock);
    return (r0 == 0x22222222u && r1 == 0xaaaaaaaau ? 1u : 0u) | (t1 != t0 ? 2u : 0u) | ((variant & 2u) && (ciD & (1u << 25)) ? 4u : 0u);
}
#endif
#else
bool RageLTDevice::dmaInit(void) { fDmaOk = false; fDmaUse = false; fDmaPending = false; fGuiPending = false; fXferOk = false; fXferUse = false; fXferPending = false; return false; }
void RageLTDevice::dmaFree(void) {}
IOReturn RageLTDevice::dmaWait(void) { return kIOReturnSuccess; }
IOReturn RageLTDevice::xferReserve(uint8_t **, uint32_t *) { return kIOReturnUnsupported; }
void RageLTDevice::xferAbort(void) {}
IOReturn RageLTDevice::xferKick(uint32_t, uint32_t, uint32_t, uint32_t) { return kIOReturnUnsupported; }
IOReturn RageLTDevice::xferWait(void) { return kIOReturnSuccess; }
void RageLTDevice::xferInit(void) {}
IOReturn RageLTDevice::dmaSubmit(const struct rage_ctx *, const uint32_t *, uint32_t) { return kIOReturnUnsupported; }
#endif

/* Everything that touches the engine or the aperture from the CPU first calls this (caller holds fLock). */
IOReturn RageLTDevice::idleWait(void)
{
    uint32_t t0 = tbl32(), t1; IOReturn rc = dmaWait();
    t1 = tbl32(); pf_oth_dma += (t1 - t0) >> 2; pf_oth_n++;
    if (rc) return rc;
    rc = eng_wait_idle(fRegs) ? kIOReturnTimeout : kIOReturnSuccess;
    pf_oth_idle += (tbl32() - t1) >> 2;
    return rc;
}

void RageLTDevice::dmaConfig(uint32_t key, uint32_t value)
{
    IOLockLock(fLock);
    if (key == 1) { (void)dmaWait(); fDmaUse = fDmaOk && value; }
    if (key == 2) { (void)dmaWait(); fXferUse = fXferOk && value; }       /* texture/pixel upload by system transfer on/off */
    IOLockUnlock(fLock);
}

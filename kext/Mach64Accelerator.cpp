/*
 * Mach64Accelerator.cpp - Rage LT Pro IOService provider (compile skeleton).
 * Builds against the Mac OS X 10.3 Kernel framework.
 *
 * NOTE on coexistence: Apple's ATIRagePro matches this same IOPCIDevice with
 * a positive probe score.  To actually attach, this driver's IOProbeScore
 * must exceed it (or Apple's driver must be prevented from matching).  That
 * is a load-time concern; this file only needs to COMPILE for now.
 */
#include "Mach64Accelerator.h"
#include "Mach64GLUserClient.h"
#include <IOKit/IOLib.h>
#include <libkern/OSByteOrder.h>
#include "../common/mach64_regs.h"

#define super IOService
OSDefineMetaClassAndStructors(Mach64Accelerator, IOService)

/* PPC store barrier: flush our uncached ring/command writes to RAM and order
 * them before the MMIO kick.  Mirrors the mb() in the proven userspace tools. */
static inline void mach64_sync(void) { __asm__ volatile("sync; eieio" ::: "memory"); }

bool Mach64Accelerator::start(IOService *provider)
{
    if (!super::start(provider)) return false;

    fPCI = OSDynamicCast(IOPCIDevice, provider);
    if (!fPCI) return false;

    fPCI->setMemoryEnable(true);
    fPCI->setBusMasterEnable(true);

    /* Diagnostic: log every BAR's phys addr + size so we can identify the
     * 4KB MMIO register block vs the 8MB framebuffer empirically. */
    for (int bar = 0; bar < 6; bar++) {
        IODeviceMemory *m =
            fPCI->getDeviceMemoryWithRegister(kIOPCIConfigBaseAddress0 + bar * 4);
        if (m)
            IOLog("Mach64Accelerator: BAR%d phys=0x%08lx len=0x%lx\n", bar,
                  (unsigned long)m->getPhysicalAddress(),
                  (unsigned long)m->getLength());
        else
            IOLog("Mach64Accelerator: BAR%d (none)\n", bar);
    }

    /* BAR layout (TODO verify on hardware via the device's assigned-addresses):
     *   BAR0 = linear framebuffer aperture, BAR2 = MMIO register aperture. */
    fFB   = fPCI->getDeviceMemoryWithRegister(kIOPCIConfigBaseAddress0);
    fRegs = fPCI->getDeviceMemoryWithRegister(kIOPCIConfigBaseAddress2);
    if (!fRegs || !fFB) {
        IOLog("Mach64Accelerator: missing register/FB aperture\n");
        return false;
    }
    fRegs->retain();
    fFB->retain();

    /* Shared control page (type 2). */
    fControl = IOBufferMemoryDescriptor::withOptions(
        kIOMemoryKernelUserShared | kIODirectionInOut, 0x1000, PAGE_SIZE);

    /* DMA command buffer (type 4) + descriptor ring (type 5): the engine
     * bus-master-READS these, so they must be physically contiguous and we
     * need their bus addresses.  Mapped uncached to userspace (see
     * clientMemoryForType) so no PPC cache flush is needed. */
    IOOptionBits dmaOpts = kIOMemoryPhysicallyContiguous |
                           kIOMemoryKernelUserShared | kIODirectionOutIn;
    fCmdBuf = IOBufferMemoryDescriptor::withOptions(dmaOpts, 0x10000, PAGE_SIZE);
    /* ring MUST be 16KB-aligned: BM_GUI_TABLE|CIRCULAR_BUF_SIZE_16KB treats it
     * as a 16KB circular buffer and masks the base to 16KB. */
    fRing   = IOBufferMemoryDescriptor::withOptions(dmaOpts, 0x4000, 0x4000);
    if (!fControl || !fCmdBuf || !fRing) return false;

    /* prepare() wires the pages so getPhysicalSegment returns REAL physical
     * addresses (without it we got ~0xd0320000, above the 512MB RAM -> panic). */
    fCmdBuf->prepare(kIODirectionOutIn);
    fRing->prepare(kIODirectionOutIn);
    IOByteCount seg;
    fCmdBufBus = fCmdBuf->getPhysicalSegment(0, &seg);
    fRingBus   = fRing->getPhysicalSegment(0, &seg);
    IOLog("Mach64Accelerator: cmdbuf bus=0x%08lx ring bus=0x%08lx (physmem=0x%lx)\n",
          (unsigned long)fCmdBufBus, (unsigned long)fRingBus, (unsigned long)0x20000000);

    /* Kernel-side DMA submission maps (see the proven mach64_gouraud path):
     *  - registers live in the last 2KB of BAR0 (NOT BAR2, which master-aborts);
     *    map inhibit-cache so kernel MMIO on PPC doesn't machine-check.
     *  - map the ring uncached too, so descriptors we write reach RAM before the
     *    kick without a separate dcache flush. */
    fRegMap  = fFB->map(kIOMapInhibitCache);
    fRingMap = fRing->map(kIOMapInhibitCache);
    if (!fRegMap || !fRingMap) {
        IOLog("Mach64Accelerator: failed to map regs/ring into the kernel\n");
        return false;
    }
    fRegBase = (volatile void *)(fRegMap->getVirtualAddress() + MACH64_REG_WINDOW);
    fRingKVA = (volatile void *)(fRingMap->getVirtualAddress());
    fLock    = IOLockAlloc();
    if (!fLock) return false;
    fCompletedStamp = 0;
    fFlipPending    = 0;
    fFlipScanline   = 0;
    fCmdBufSize     = 0x10000;   /* must match the fCmdBuf allocation above */

    fStamp   = 0;
    /* Real values come from reading CONFIG_CHIP_ID / memory config; the plugin
     * only inspects bit 1 of config0 (see gldCreateContext). */
    fConfig0 = 0;
    fConfig1 = 0x00800000;   /* 8 MB VRAM (verified on this Lombard)         */

    /* Enable the engine's AUTO-fast-fill and AUTO-block-write (HW_DEBUG bits 12/13 are the
     * *_DIS bits, 0 = enabled -- RRG 0_1F).  With them on, solid fills (every frame's
     * full-screen colour + Z clears) run at memory bandwidth via SGRAM fast-fill instead of
     * pixel-by-pixel, which is the dominant per-frame cost for animation.  We own the engine
     * exclusively (GA plugin removed), so a global HW_DEBUG tweak is safe. */
    {
        UInt32 hd = regRead(MACH64_HW_DEBUG);
        UInt32 nw = hd & ~(MACH64_HW_DEBUG_AUTO_FF_DIS | MACH64_HW_DEBUG_AUTO_BLKWRT_DIS);
        IOLog("Mach64Accelerator: HW_DEBUG 0x%08lx -> 0x%08lx (enable auto fast-fill/block-write)\n",
              (unsigned long)hd, (unsigned long)nw);
        if (nw != hd) regWrite(MACH64_HW_DEBUG, nw);
    }

    IOLog("Mach64Accelerator: started on %s\n", fPCI->getName());
    registerService();
    return true;
}

void Mach64Accelerator::stop(IOService *provider)
{
    super::stop(provider);
}

void Mach64Accelerator::free(void)
{
    /* release the maps before the descriptors they cover */
    if (fRegMap)  { fRegMap->release();  fRegMap = 0; fRegBase = 0; }
    if (fRingMap) { fRingMap->release(); fRingMap = 0; fRingKVA = 0; }
    if (fLock)    { IOLockFree(fLock);   fLock = 0; }
    if (fRegs)    { fRegs->release();    fRegs = 0; }
    if (fFB)      { fFB->release();      fFB = 0; }
    if (fControl) { fControl->release(); fControl = 0; }
    if (fCmdBuf)  { fCmdBuf->complete(); fCmdBuf->release(); fCmdBuf = 0; }
    if (fRing)    { fRing->complete();   fRing->release();   fRing = 0; }
    super::free();
}

/* ---- register / ring MMIO (little-endian window, see mach64_regs.h) ---- */
UInt32 Mach64Accelerator::regRead(UInt32 off) const
{ return OSReadLittleInt32((void *)fRegBase, off); }

UInt32 Mach64Accelerator::crtcOffPitch(void)
{ return regRead(MACH64_CRTC_OFF_PITCH); }   // live display scanout base+pitch

UInt32 Mach64Accelerator::crtcVTotalDisp(void)
{ return regRead(MACH64_CRTC_V_TOTAL_DISP); }   // V_DISP[26:16] = displayed lines - 1

void Mach64Accelerator::regWrite(UInt32 off, UInt32 v) const
{ OSWriteLittleInt32((void *)fRegBase, off, v); }

void Mach64Accelerator::ringWrite(UInt32 off, UInt32 v) const
{ OSWriteLittleInt32((void *)fRingKVA, off, v); }

/* wait until >= `entries` FIFO slots are free (same gating as the tools) */
bool Mach64Accelerator::waitFifo(int entries) const
{
    for (int i = 0; i < 100000; i++) {
        if ((regRead(MACH64_FIFO_STAT) & MACH64_FIFO_SLOT_MASK)
                <= (UInt32)(0x8000 >> entries))
            return true;
        IODelay(1);
    }
    return false;
}

bool Mach64Accelerator::waitIdle(void) const
{
    if (!waitFifo(16)) return false;
    for (int i = 0; i < 100000; i++) {
        if (!(regRead(MACH64_GUI_STAT) & MACH64_GUI_ACTIVE)) return true;
        IODelay(1);
    }
    return false;
}

/* The proven bus-master submit: point a ring descriptor at the client's filled
 * command buffer, quiesce + save ATIRagePro's DMA regs, activate + kick, wait
 * for idle, restore.  `byteCount` = bytes of register-write stream (which must
 * already end in a SRC_CNTL=0 terminator, appended by the GL plugin). */
IOReturn Mach64Accelerator::submitCommandBuffer(UInt32 byteCount)
{
    ringWrite(0,  MACH64_APERTURE_OFFSET + MACH64_BM_ADDR);
    ringWrite(4,  (UInt32)fCmdBufBus);
    ringWrite(8,  byteCount | MACH64_DMA_HOLD_OFFSET | MACH64_DMA_EOL);
    ringWrite(12, 0);

    /* No HW lock is shared with ATIRagePro, so an async collision is possible
     * between save and restore -> display glitch/reboot; acceptable headless
     * (see bus-master-ring.md).  A real coexistence story needs the DRI-style
     * hardware lock or full device takeover. */
    UInt32 saveBus = regRead(MACH64_BUS_CNTL);
    UInt32 saveSrc = regRead(MACH64_SRC_CNTL);
    UInt32 saveTbl = regRead(MACH64_BM_GUI_TABLE);

    mach64_sync();                       /* push descriptor to RAM before kick */
    if (!waitIdle())
        IOLog("Mach64Accelerator: engine not idle before DMA\n");

    regWrite(MACH64_BUS_CNTL,
             (saveBus & ~MACH64_BUS_MASTER_DIS) | MACH64_BUS_EXT_REG_EN);
    regWrite(MACH64_BM_GUI_TABLE_CMD,
             (UInt32)fRingBus | MACH64_CIRCULAR_BUF_SIZE_16KB);
    regWrite(MACH64_SRC_CNTL,
             MACH64_SRC_BM_ENABLE | MACH64_SRC_BM_SYNC | MACH64_SRC_BM_OP_SYSTEM_TO_REG);
    mach64_sync();
    regWrite(MACH64_DST_HEIGHT_WIDTH, 0);   /* kick the transfer */
    bool ok = waitIdle();

    /* restore ATIRagePro's DMA state */
    regWrite(MACH64_SRC_CNTL, saveSrc);
    regWrite(MACH64_BM_GUI_TABLE_CMD, saveTbl & 0xfffffff0);
    regWrite(MACH64_BUS_CNTL, saveBus);

    return ok ? kIOReturnSuccess : kIOReturnTimeout;
}

/* ---- regions ---- */
IOMemoryDescriptor *Mach64Accelerator::allocSurface(Mach64GLUserClient *c)
{
    (void)c;
    /* TODO: carve a color/depth surface out of VRAM.  Placeholder: whole FB. */
    return fFB;
}

IOMemoryDescriptor *Mach64Accelerator::commandBuffer(Mach64GLUserClient *c)
{
    (void)c;
    return fCmdBuf;
}

/* ---- config / context ---- */
UInt32 Mach64Accelerator::allocContextBuffer(Mach64GLUserClient *c)
{
    (void)c;
    return ++fStamp;   /* opaque handle; real impl allocates per-engine state */
}

/* ---- submission / fences (the proven DMA ring) ---- */

/* Submit the register-write stream the client wrote into the type-4 command
 * buffer (bytes [0, size), ending in a SRC_CNTL=0 terminator) and return a
 * stamp the client can wait_for_stamp() on.  new_vertex_buffer() calls this to
 * "kick the previous buffer"; in the single-buffer synchronous model the client
 * simply refills from offset 0 for the next batch. */
UInt32 Mach64Accelerator::rotateCommandBuffer(Mach64GLUserClient *c, UInt32 size)
{
    (void)c;
    if (size == 0 || size > fCmdBufSize || (size & 3))
        return fCompletedStamp;      /* nothing valid to submit */

    IOLockLock(fLock);
    UInt32 stamp = ++fStamp;
    IOReturn r = submitCommandBuffer(size);
    /* Synchronous today: the quiesce model owns the engine only between save and
     * restore, so the DMA has already retired here.  TODO for CPU/GPU overlap:
     * split fCmdBuf into two halves, alternate per stamp, and retire stamps by
     * polling BM_GUI_TABLE instead of blocking in submitCommandBuffer(). */
    if (r == kIOReturnSuccess)
        fCompletedStamp = stamp;
    else
        IOLog("Mach64Accelerator: DMA submit failed (0x%x)\n", r);
    IOLockUnlock(fLock);
    return stamp;
}

IOReturn Mach64Accelerator::freeCommandBuffer(Mach64GLUserClient *c, UInt32 id)
{
    (void)c; (void)id;
    return kIOReturnSuccess;   /* single buffer: nothing to reclaim yet */
}

/* Fence on a submission stamp.  Signed compare tolerates 32-bit wraparound.
 * Structured as a real wait so it stays correct once submission goes async;
 * with synchronous submits the stamp is already retired on entry. */
IOReturn Mach64Accelerator::waitForStamp(UInt32 stamp)
{
    for (int i = 0; i < 100000; i++) {
        if ((SInt32)(fCompletedStamp - stamp) >= 0)
            return kIOReturnSuccess;
        IODelay(10);
    }
    return kIOReturnTimeout;
}

IOReturn Mach64Accelerator::waitForIdle(void)
{ return waitIdle() ? kIOReturnSuccess : kIOReturnTimeout; }

UInt32 Mach64Accelerator::engineStatus(void)
{ return regRead(MACH64_GUI_STAT); }

IOReturn Mach64Accelerator::setSurface(Mach64GLUserClient *c,
                                       UInt32 a, UInt32 b, UInt32 cc, UInt32 d)
{
    (void)c; (void)b; (void)cc; (void)d;
    /* PAGE FLIP (selector 0, repurposed from the set_surface stub).  `a` is the new
     * CRTC_OFF_PITCH value ((pitch/8<<22)|(offset>>3), same encoding as DST_OFF_PITCH).
     *
     * Enable the CRTC-offset HARDWARE double-buffer: HW_DEBUG.DBL_BUFFER_EN (bit10) set +
     * BLOCK_DBL_BUF (bit15) clear (RRG 0_1F).  With it, a write to CRTC_OFF_PITCH is
     * LATCHED AT VBLANK by the hardware -- atomic, tear-free -- instead of landing
     * mid-scan.  Idempotent read-modify-write, touches only those two bits. */
    {
        UInt32 hd = regRead(MACH64_HW_DEBUG);
        UInt32 nw = (hd | MACH64_HW_DEBUG_DBL_BUFFER_EN) & ~MACH64_HW_DEBUG_BLOCK_DBL_BUF;
        if (nw != hd) regWrite(MACH64_HW_DEBUG, nw);
    }
    regWrite(MACH64_CRTC_OFF_PITCH, a);   /* loads at the next vblank (HW double-buffered) */
    /* DEFERRED WAIT: do NOT block for the vblank load here.  Record the flip as pending
     * and the current scanline; the caller (driver) does its next-frame work (GLEngine
     * geometry dispatch, batch build) and then calls waitFlip() right before it reuses
     * the just-freed buffer -- so the vblank wait overlaps that work instead of being
     * pure dead time in the flip. */
    fFlipScanline = ((UInt32)regRead(MACH64_CRTC_VLINE_CRNT_VLINE) >> 16) & 0x7ff;
    fFlipPending  = 1;
    return kIOReturnSuccess;
}

/* Block until a pending page flip has retired (loaded at vblank), detected by the
 * scanline counter wrapping (max -> 0) since the flip write.  No-op if none pending.
 * Called by the driver just before it reuses the previously-shown buffer. */
IOReturn Mach64Accelerator::waitFlip(void)
{
    if (!fFlipPending) return kIOReturnSuccess;
    {
        UInt32 prev = fFlipScanline;
        for (int i = 0; i < 200000; i++) {
            UInt32 cur = ((UInt32)regRead(MACH64_CRTC_VLINE_CRNT_VLINE) >> 16) & 0x7ff;
            if (cur < prev) break;   /* wrapped -> vblank passed, flip loaded */
            prev = cur;
            IODelay(5);
        }
    }
    fFlipPending = 0;
    return kIOReturnSuccess;
}

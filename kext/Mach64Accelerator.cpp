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
static bool     fbPublishedTramp(void *target, void *ref, IOService *svc);
static IOReturn fbEventTramp(OSObject *self, void *ref, IOFramebuffer *fb, IOIndex event, void *info);

bool Mach64Accelerator::start(IOService *provider)
{
    if (!super::start(provider)) return false;

    fPCI = OSDynamicCast(IOPCIDevice, provider);
    if (!fPCI) return false;

    fPCI->setMemoryEnable(true);
    fPCI->setBusMasterEnable(true);

    /* BAR layout (HW-verified on the Rage LT Pro 1002:4c49 in the MDD G4, Nov 2026, after BIOS init):
     *   BAR0 = linear framebuffer aperture (8 MB VRAM, register block at +0x7FF800),
     *   BAR1 = 256-byte I/O register window (needs setIOEnable), BAR2 = 4 KB MMIO register aperture. */
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
    /* 128KB (was 64KB): with descriptor chaining a whole batch is submitted in one kick, so a
     * bigger buffer = fewer kicks/frame.  Physically contiguous (dmaOpts) so chained
     * descriptors can address the whole span. */
    /* 384 KB (was 128 KB): [0, 128K) texture-upload staging, [128K, 256K) and [256K, 384K) the two engine batch buffers of the single-thread kick/poll mode */
    fCmdBuf = IOBufferMemoryDescriptor::withOptions(dmaOpts, 0x60000, PAGE_SIZE);
    /* ring MUST be 16KB-aligned: BM_GUI_TABLE|CIRCULAR_BUF_SIZE_16KB treats it
     * as a 16KB circular buffer and masks the base to 16KB. */
    /* 64 KB: [0, 0x2400) the texture-upload tables; the two engine batch tables are 16 KB-PHYSICALLY-aligned 16 KB blocks picked by the driver above that (the descriptor-ring
     * alignment request is NOT honoured physically: measured ring bus 0x00a29000, 0x0cf63000), so there has to be room for the worst-case alignment slack. */
    fRing   = IOBufferMemoryDescriptor::withOptions(dmaOpts, 0x10000, 0x4000);
    if (!fControl || !fCmdBuf || !fRing) return false;

    /* prepare() wires the pages so getPhysicalSegment returns REAL physical
     * addresses (without it we got ~0xd0320000, above the 512MB RAM -> panic). */
    fCmdBuf->prepare(kIODirectionOutIn);
    fRing->prepare(kIODirectionOutIn);
    IOByteCount seg;
    fCmdBufBus = fCmdBuf->getPhysicalSegment(0, &seg);
    fRingBus   = fRing->getPhysicalSegment(0, &seg);

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
    fOrigCrtc       = 0;   /* captured at the first page flip, restored in stop() */
    fCmdBufSize     = 0x20000;   /* the legacy in-kernel submit path (rotateCommandBuffer) still treats the FIRST 128 KB as its buffer */

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
        if (nw != hd) regWrite(MACH64_HW_DEBUG, nw);
    }

    /* Display mode changes: Apple's ATIRagePro resets its VRAM heap, stops the engine and redirects every user mapping of the VRAM
     * aperture between "will" and "did" (docs/re-notes/display-mode-change.md).  Our BAR0 mapping is a separate descriptor, so do the
     * same for it, and publish a generation the GL plugin polls to drop its cached VRAM offsets.  addNotification also calls back for
     * framebuffers that are already published. */
    fNumFB = 0; fModeDepth = 0; fModeGen = 0; fRedirected = false;
    fFBPublish = addNotification(gIOPublishNotification, serviceMatching("IOFramebuffer"), &fbPublishedTramp, this, 0);
    if (!fFBPublish) IOLog("Mach64Accelerator: no IOFramebuffer publish notification (mode changes will not be tracked)\n");

    registerService();
    return true;
}

static bool fbPublishedTramp(void *target, void *ref, IOService *svc)
{
    (void)ref;
    return ((Mach64Accelerator *)target)->fbPublished(svc);
}
static IOReturn fbEventTramp(OSObject *self, void *ref, IOFramebuffer *fb, IOIndex event, void *info)
{
    (void)ref;
    return ((Mach64Accelerator *)self)->fbEvent(fb, event, info);
}

bool Mach64Accelerator::fbPublished(IOService *svc)
{
    IOFramebuffer *fb = OSDynamicCast(IOFramebuffer, svc);
    IOService *s;
    if (!fb || !fLock) return true;
    for (s = fb; s && s != fPCI; s = s->getProvider()) ;
    if (!s) return true;                              /* another card's framebuffer */
    IOLockLock(fLock);
    {   int i; bool have = false;
        for (i = 0; i < fNumFB; i++) if (fFBs[i] == fb) have = true;
        if (!have && fNumFB < kMaxFB) {
            IONotifier *n = fb->addFramebufferNotification(&fbEventTramp, this, 0);
            if (n) { fFBs[fNumFB] = fb; fFBNotify[fNumFB] = n; fNumFB++; }
            IOLog("Mach64Accelerator: tracking mode changes of %s (%s)\n", fb->getName(), n ? "ok" : "FAILED");
        }
    }
    IOLockUnlock(fLock);
    return true;
}

/* Same event mapping as IOATIRProAccelerator::display_change_handler: mode will/did change, and sleep/wake without info, bracket a VRAM
 * re-layout.  Counted across heads so the first "will" starts it and the last "did" ends it. */
IOReturn Mach64Accelerator::fbEvent(IOFramebuffer *fb, IOIndex event, void *info)
{
    (void)fb;
    bool will = (event == kIOFBNotifyDisplayModeWillChange) || (event == kIOFBNotifyWillSleep && !info);
    bool did  = (event == kIOFBNotifyDisplayModeDidChange)  || (event == kIOFBNotifyDidWake && !info);
    if (will) {
        if (OSIncrementAtomic((SInt32 *)&fModeDepth) == 0) {
            OSIncrementAtomic((SInt32 *)&fModeGen);       /* odd: changing -- accel_hold is refused, the plugin renders nothing */
            mach64_sync();
            /* Block user access to the aperture (VRAM + registers) until "did": a user touch now waits in the fault handler instead of
             * hitting a chip Apple is stopping and re-initialising.  Our own kernel mapping (kernel_task) stays usable. */
            if (fFB && fFB->redirect(kernel_task, true) == kIOReturnSuccess) fRedirected = true;
            IOLog("Mach64Accelerator: display mode will change (gen %lu)\n", (unsigned long)fModeGen);
        }
    } else if (did) {
        if (fModeDepth > 0 && OSDecrementAtomic((SInt32 *)&fModeDepth) == 1) {
            if (fRedirected) { fFB->redirect(kernel_task, false); fRedirected = false; }
            mach64_sync();
            OSIncrementAtomic((SInt32 *)&fModeGen);       /* even: new layout -- the plugin re-validates every cached VRAM offset */
            IOLog("Mach64Accelerator: display mode did change (gen %lu)\n", (unsigned long)fModeGen);
        }
    }
    return kIOReturnSuccess;
}

void Mach64Accelerator::stop(IOService *provider)
{
    /* Put the desktop's scanout register back before we go away, so `kextunload` returns the
     * display to the WindowServer's framebuffer instead of leaving the CRTC on our render
     * buffer (the frozen-frame-on-exit bug).  fRegBase is still mapped here (free() releases
     * it later). */
    if (fOrigCrtc && fRegBase) {
        regWrite(MACH64_CRTC_OFF_PITCH, fOrigCrtc);
        fOrigCrtc = 0;
    }
    if (fFBPublish) { fFBPublish->remove(); fFBPublish = 0; }
    {   int i; for (i = 0; i < fNumFB; i++) if (fFBNotify[i]) { fFBNotify[i]->remove(); fFBNotify[i] = 0; } fNumFB = 0; }
    if (fRedirected && fFB) { fFB->redirect(kernel_task, false); fRedirected = false; }
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
    /* CHAIN the command stream across as many ring descriptors as it takes.  The bus-master
     * byte-count field is 12-bit, so ONE descriptor tops out at 4095 bytes -- a whole frame
     * is far larger.  The old model did one descriptor (and one full quiesce/kick/wait-idle
     * cycle) per <=960-dword batch => ~80 kicks/frame => ~3fps, almost all submit overhead.
     * Every descriptor uses HOLD_OFFSET, i.e. all data is streamed to the single BM_ADDR
     * command port, so the GUI parser sees ONE continuous dword stream regardless of where
     * the DMA chunk boundaries fall -- we can split at a fixed 4092-byte stride and give only
     * the LAST descriptor EOL.  The engine walks descriptors from the ring base until EOL,
     * so a 128KB buffer becomes ~32 descriptors kicked+waited ONCE.  (fCmdBuf is physically
     * contiguous, so fCmdBufBus+off is a valid bus address across the whole buffer; the ring
     * is 16KB = 1024 descriptors, far more than fCmdBufSize/4092 could ever need.) */
    {
        const UInt32 CHUNK = 4092;          /* <=4095 and 4-aligned (0x1000 would trunc to 0) */
        UInt32 off = 0, ring = 0;
        do {
            UInt32 n = byteCount - off;
            if (n > CHUNK) n = CHUNK;
            ringWrite(ring + 0,  MACH64_APERTURE_OFFSET + MACH64_BM_ADDR);
            ringWrite(ring + 4,  (UInt32)fCmdBufBus + off);
            ringWrite(ring + 8,  n | MACH64_DMA_HOLD_OFFSET |
                                 ((off + n >= byteCount) ? MACH64_DMA_EOL : 0));
            ringWrite(ring + 12, 0);
            ring += 16;
            off  += n;
        } while (off < byteCount);
    }

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
    /* Type-1 region for the legacy path = the VRAM aperture (the plugin allocates surfaces itself: Path B uses Apple's allocator, the
     * legacy path its own heap).  Per-client carving is not needed by either. */
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
     * restore, so the DMA has already retired here.  Possible optimisation (CPU/GPU overlap, not needed for correctness):
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
    /* Save the desktop's scanout register the FIRST time we hijack it, so stop() (kextunload)
     * can put it back.  Without this, the CRTC is left pointing at our render buffer after the
     * app exits and NOTHING recovers the desktop -- not even `killall WindowServer`, because
     * Apple's framebuffer driver caches its own CRTC_OFF_PITCH value and won't rewrite the
     * register it believes is already correct.  Only a direct HW write (here / in stop) fixes
     * it.  Read BEFORE this first flip, so it's the genuine pre-hijack desktop value. */
    if (!fOrigCrtc) fOrigCrtc = regRead(MACH64_CRTC_OFF_PITCH);
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

/* Wait for the LEADING EDGE of vertical blank.  The fullscreen present is now a back->front
 * BLIT (the CRTC page-flip is gone); kicking that blit right after this call runs it during
 * retrace, and since the 2D engine outruns the scan beam a top-to-bottom blit stays ahead of
 * the beam -> tear-free.  Reads the active display height from CRTC_V_TOTAL_DISP (V_DISP[26:16]
 * = last active line) and waits for the scanline to cross from active display into blanking.
 * Two bounded spins (each < one frame at 2us/iter) so it can never hang (display off / VLINE
 * stuck just falls through).  (Formerly the page-flip vblank wait.) */
IOReturn Mach64Accelerator::waitFlip(void)
{
    UInt32 active = (((UInt32)regRead(MACH64_CRTC_V_TOTAL_DISP) >> 16) & 0x7ff) + 1;
    int i;
    /* make sure we're in active display first, so we catch the NEXT entry into vblank
     * (not a stale mid-vblank sample) */
    for (i = 0; i < 20000; i++) {
        if ((((UInt32)regRead(MACH64_CRTC_VLINE_CRNT_VLINE) >> 16) & 0x7ff) < active) break;
        IODelay(2);
    }
    /* then wait for the scanline to cross into vertical blank */
    for (i = 0; i < 20000; i++) {
        if ((((UInt32)regRead(MACH64_CRTC_VLINE_CRNT_VLINE) >> 16) & 0x7ff) >= active) break;
        IODelay(2);
    }
    (void)fFlipPending; (void)fFlipScanline;
    return kIOReturnSuccess;
}

/* kext/RageLTDevice.cpp - Phase 1 skeleton.
 * Attaches to the Rage LT Pro, maps BAR0 uncached, builds the VRAM bookkeeping.  It reads and writes NO chip register in start():
 * a PPC read of an un-initialised chip blocks the CPU forever (docs/memory-map.md 6), so the chip is only touched once a later phase
 * establishes that the boot init (RageStubFB on the G4, Apple's ATY,264LTPro on the Lombard) has run.  No floating point anywhere. */
#include "RageLTDevice.h"
#include "RageLTSurfaceClient.h"
#include <IOKit/IOLib.h>
#include <libkern/OSByteOrder.h>
#include <ppc/proc_reg.h>      /* eieio */
#include <kern/clock.h>
#include "RageLTLogClient.h"
#include "RageLTGLClient.h"

#define super IOAccelerator
OSDefineMetaClassAndStructors(RageLTDevice, IOAccelerator)

#define LOG(...) IOLog("RageLT: " __VA_ARGS__)

bool RageLTDevice::start(IOService *provider)
{
    if (!super::start(provider)) return false;
    setName("ATIRagePro");                       /* the framebuffer's IOAccelTypes path names this node (the stock accelerator has the same name); before modeStart publishes the path */
    fPCI = OSDynamicCast(IOPCIDevice, provider);
    if (!fPCI) return false;

    fPCI->setMemoryEnable(true);                 /* config-space command bits only; no chip access */
    fFB = fPCI->getDeviceMemoryWithRegister(kIOPCIConfigBaseAddress0);
    if (!fFB) { LOG("no BAR0\n"); return false; }
    fFB->retain();
    fMap = fFB->map(kIOMapInhibitCache);          /* registers must never be cached on PPC */
    if (!fMap) { LOG("BAR0 map failed\n"); return false; }
    fRegs = (volatile uint8_t *)(fMap->getVirtualAddress() + MACH64_REG_WINDOW);

    fLock = IOLockAlloc();
    if (!fLock) return false;
#if RAGE_FEAT_PANIC_LOG
    fLog = (struct rage_log *)IOMalloc(sizeof(struct rage_log));
    fLogLock = IOSimpleLockAlloc();
    if (!fLog || !fLogLock) { LOG("no memory for the event ring\n"); return false; }
    rage_log_init(fLog);
#endif
    rage_vram_init(&fVram);
    fScratch = 0x770000u;          /* first word of the reserved top block (never engine-written): rewritten with its own value to flush the engine cache */
    fNextOwner = 1;
    { uint32_t k; for (k = 0; k < RAGE_MAX_SURF; k++) fSurf[k].used = 0; }
    fEngineLive = false;
    fRefused = 0;
    fDispReserved = false;
    { uint32_t k; for (k = 0; k < RAGE_MAX_CTX; k++) fCtx[k].owner = 0; }
    fFenceSubmitted = fFenceCompleted = 0;
    fQuadU = fQuadV = fQuadX = 0;
    fDmaCmd = fDmaCmd2 = fDmaRing = 0; fDmaCur = 0; fDmaOk = fDmaUse = fDmaPending = false;
    (void)dmaInit();
    modeStart();

    logEvent(RAGE_EV_START, 0, RAGE_FEATURE_BITS, 0, 0, 0, 0);
    setProperty("RageLTFeatures", (unsigned long long)RAGE_FEATURE_BITS, 32);
    LOG("attached (skeleton, dormant): BAR0 %lu MB, engine off, no register access\n", (unsigned long)(fMap->getLength() >> 20));
    setProperty("IOAccelRevision", (unsigned long long)2, 32);
    registerService();
    return true;
}

void RageLTDevice::stop(IOService *provider)
{
    modeStop();
    if (fEngineLive) { IOLockLock(fLock); (void)dmaWait(); IOLockUnlock(fLock); }
    fEngineLive = false;
    logEvent(RAGE_EV_STOP, 0, 0, 0, 0, 0, 0);
    super::stop(provider);
}

void RageLTDevice::free(void)
{
    dmaFree();
    if (fMap) { fMap->release(); fMap = 0; }
    if (fFB)  { fFB->release();  fFB = 0; }
    if (fLock) { IOLockFree(fLock); fLock = 0; }
#if RAGE_FEAT_PANIC_LOG
    if (fLogLock) { IOSimpleLockFree(fLogLock); fLogLock = 0; }
    if (fLog) { IOFree(fLog, sizeof(struct rage_log)); fLog = 0; }
#endif
    super::free();
}

uint32_t RageLTDevice::newOwner(void)
{
    uint32_t o;
    IOLockLock(fLock);
    o = fNextOwner++;
    if (fNextOwner == 0) fNextOwner = 1;          /* 0 is the reserved owner */
    IOLockUnlock(fLock);
    logEvent(RAGE_EV_OWNER_NEW, o, 0, 0, 0, 0, 0);
    return o;
}

void RageLTDevice::releaseOwner(uint32_t owner)
{
    uint32_t n;
    IOLockLock(fLock);
    if (fEngineLive && fDmaPending) (void)dmaWait();            /* the engine may still be drawing into memory that is about to be reused */
    { struct rage_ctx *c = ctxFor(owner, false); if (c) c->owner = 0; }
    n = rage_vram_free_owner(&fVram, owner);
    IOLockUnlock(fLock);
    logEvent(RAGE_EV_OWNER_FREE, owner, n, 0, 0, 0, 0);
}

IOReturn RageLTDevice::vramAlloc(uint32_t owner, uint32_t length, uint32_t align, uint32_t *offset)
{
    int rc;
    IOLockLock(fLock);
    rc = rage_vram_alloc(&fVram, length, align, owner, offset);
    IOLockUnlock(fLock);
    logEvent(RAGE_EV_VRAM_ALLOC, owner, length, align, rc ? 0xffffffffu : *offset, rc ? 1u : 0u, 0);
    return rc ? kIOReturnNoSpace : kIOReturnSuccess;
}

IOReturn RageLTDevice::vramFree(uint32_t owner, uint32_t offset)
{
    int rc;
    IOLockLock(fLock);
    if (fEngineLive && fDmaPending) (void)dmaWait();
    rc = rage_vram_free(&fVram, offset, owner);
    IOLockUnlock(fLock);
    logEvent(RAGE_EV_VRAM_FREE, owner, offset, rc ? 1u : 0u, 0, 0, 0);
    return rc ? kIOReturnBadArgument : kIOReturnSuccess;
}

/* ---- RAGE_FEAT_PANIC_LOG ---- */
#if RAGE_FEAT_PANIC_LOG
void RageLTDevice::logEvent(uint32_t id, uint32_t owner, uint32_t a0, uint32_t a1, uint32_t a2, uint32_t a3, uint32_t a4)
{
    uint32_t sec = 0, usec = 0; IOInterruptState s;
    if (!fLog || !fLogLock) return;
    clock_get_system_microtime(&sec, &usec);
    s = IOSimpleLockLockDisableInterrupt(fLogLock);
    rage_log_put(fLog, sec * 1000u + usec / 1000u, id, owner, a0, a1, a2, a3, a4);
    IOSimpleLockUnlockEnableInterrupt(fLogLock, s);
}

IOReturn RageLTDevice::logInfo(uint32_t *head, uint32_t *capacity, uint32_t *refused, uint32_t *features)
{
    IOInterruptState s;
    if (!fLog) return kIOReturnNotReady;
    s = IOSimpleLockLockDisableInterrupt(fLogLock);
    *head = fLog->head;
    IOSimpleLockUnlockEnableInterrupt(fLogLock, s);
    *capacity = RAGE_LOG_ENTRIES; *refused = fRefused; *features = RAGE_FEATURE_BITS;
    return kIOReturnSuccess;
}

IOReturn RageLTDevice::logRead(uint32_t fromSeq, struct rage_log_read_out *out)
{
    IOInterruptState s; uint32_t i;
    if (!fLog) return kIOReturnNotReady;
    for (i = 0; i < sizeof *out / 4; i++) ((uint32_t *)out)[i] = 0;
    s = IOSimpleLockLockDisableInterrupt(fLogLock);                 /* copies at most 31 x 32 bytes: short */
    out->count = rage_log_read(fLog, fromSeq, out->ev, RAGE_LOG_READ_EVENTS, &out->lost);
    out->head = fLog->head;
    IOSimpleLockUnlockEnableInterrupt(fLogLock, s);
    out->capacity = RAGE_LOG_ENTRIES;
    return kIOReturnSuccess;
}

#else
void RageLTDevice::logEvent(uint32_t, uint32_t, uint32_t, uint32_t, uint32_t, uint32_t, uint32_t) {}
IOReturn RageLTDevice::logInfo(uint32_t *, uint32_t *, uint32_t *, uint32_t *) { return kIOReturnUnsupported; }
IOReturn RageLTDevice::logRead(uint32_t, struct rage_log_read_out *) { return kIOReturnUnsupported; }
#endif

IOReturn RageLTDevice::newUserClient(task_t owningTask, void *securityID, UInt32 type, IOUserClient **handler)
{
    IOUserClient *c;
    if (type == RAGE_CLIENT_LOG && RAGE_FEAT_PANIC_LOG) c = new RageLTLogClient;
    else if (type == RAGE_CLIENT_GL) c = new RageLTGLClient;
    else if (type == kIOAccelSurfaceClientType) c = new RageLTSurfaceClient;
    else return kIOReturnUnsupported;
    if (!c) return kIOReturnNoMemory;
    if (!c->initWithTask(owningTask, securityID, type) || !c->attach(this) || !c->start(this)) { c->release(); return kIOReturnError; }
    *handler = c;
    return kIOReturnSuccess;
}

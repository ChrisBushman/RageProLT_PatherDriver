/* kext/RageLTDevice.cpp - Phase 1 skeleton.
 * Attaches to the Rage LT Pro, maps BAR0 uncached, builds the VRAM bookkeeping.  It reads and writes NO chip register in start():
 * a PPC read of an un-initialised chip blocks the CPU forever (docs/memory-map.md 6), so the chip is only touched once a later phase
 * establishes that the boot init (RageStubFB on the G4, Apple's ATY,264LTPro on the Lombard) has run.  No floating point anywhere. */
#include "RageLTDevice.h"
#include <IOKit/IOLib.h>
#include <libkern/OSByteOrder.h>
#include <ppc/proc_reg.h>      /* eieio */

#define super IOService
OSDefineMetaClassAndStructors(RageLTDevice, IOService)

#define LOG(...) IOLog("RageLT: " __VA_ARGS__)

bool RageLTDevice::start(IOService *provider)
{
    if (!super::start(provider)) return false;
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
    rage_vram_init(&fVram);
    fNextOwner = 1;
    fEngineLive = false;
    fRefused = 0;

    LOG("attached (skeleton, dormant): BAR0 %lu MB, engine off, no register access\n", (unsigned long)(fMap->getLength() >> 20));
    registerService();
    return true;
}

void RageLTDevice::stop(IOService *provider)
{
    fEngineLive = false;
    super::stop(provider);
}

void RageLTDevice::free(void)
{
    if (fMap) { fMap->release(); fMap = 0; }
    if (fFB)  { fFB->release();  fFB = 0; }
    if (fLock) { IOLockFree(fLock); fLock = 0; }
    super::free();
}

uint32_t RageLTDevice::newOwner(void)
{
    uint32_t o;
    IOLockLock(fLock);
    o = fNextOwner++;
    if (fNextOwner == 0) fNextOwner = 1;          /* 0 is the reserved owner */
    IOLockUnlock(fLock);
    return o;
}

void RageLTDevice::releaseOwner(uint32_t owner)
{
    IOLockLock(fLock);
    (void)rage_vram_free_owner(&fVram, owner);
    IOLockUnlock(fLock);
}

IOReturn RageLTDevice::vramAlloc(uint32_t owner, uint32_t length, uint32_t align, uint32_t *offset)
{
    int rc;
    IOLockLock(fLock);
    rc = rage_vram_alloc(&fVram, length, align, owner, offset);
    IOLockUnlock(fLock);
    return rc ? kIOReturnNoSpace : kIOReturnSuccess;
}

IOReturn RageLTDevice::vramFree(uint32_t owner, uint32_t offset)
{
    int rc;
    IOLockLock(fLock);
    rc = rage_vram_free(&fVram, offset, owner);
    IOLockUnlock(fLock);
    return rc ? kIOReturnBadArgument : kIOReturnSuccess;
}

IOReturn RageLTDevice::submitBatch(const uint32_t *pairs, uint32_t npairs)
{
    uint32_t i;
    if (!rage_batch_ok(pairs, npairs)) { fRefused++; return kIOReturnBadArgument; }   /* whole batch dropped */
    if (!fEngineLive) return kIOReturnNotReady;                                        /* dormant until a later phase enables it */
    IOLockLock(fLock);
    for (i = 0; i < npairs; i++) {
        OSWriteLittleInt32((void *)fRegs, pairs[2 * i], pairs[2 * i + 1]);
        eieio();
    }
    IOLockUnlock(fLock);
    return kIOReturnSuccess;
}

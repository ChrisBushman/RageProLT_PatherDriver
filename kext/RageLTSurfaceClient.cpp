#include "RageLTSurfaceClient.h"
#include "RageLTDevice.h"
#include <IOKit/IOLib.h>
#include <string.h>

#define super IOUserClient
OSDefineMetaClassAndStructors(RageLTSurfaceClient, IOUserClient)

#define LOG(...) IOLog("RageLT: " __VA_ARGS__)

/* ---- device side: the surface table and the framebuffer properties WindowServer / CGL follow to find us ---- */
int RageLTDevice::surfAcquire(void)
{
    int k, r = -1;
    IOLockLock(fLock);
    for (k = 0; k < (int)RAGE_MAX_SURF; k++) if (!fSurf[k].used) { memset(&fSurf[k], 0, sizeof fSurf[k]); fSurf[k].used = 1; r = k; break; }
    IOLockUnlock(fLock);
    return r;
}
void RageLTDevice::surfRelease(int slot)
{
    if (slot < 0 || slot >= (int)RAGE_MAX_SURF) return;
    IOLockLock(fLock); fSurf[slot].used = 0; IOLockUnlock(fLock);
}
void RageLTDevice::surfSetIDMode(int slot, uint32_t id, uint32_t mode)
{
    if (slot < 0 || slot >= (int)RAGE_MAX_SURF) return;
    IOLockLock(fLock); fSurf[slot].id = id; fSurf[slot].mode = mode; fSurf[slot].gen++; IOLockUnlock(fLock);
}
IOReturn RageLTDevice::surfSetShape(int slot, const IOAccelDeviceRegion *rgn, uint32_t size)
{
    uint32_t n, i;
    if (slot < 0 || slot >= (int)RAGE_MAX_SURF || !rgn || size < sizeof(IOAccelDeviceRegion)) return kIOReturnBadArgument;
    n = rgn->num_rects;
    if (n > (size - sizeof(IOAccelDeviceRegion)) / sizeof(IOAccelBounds)) return kIOReturnBadArgument;   /* the claimed count must fit the bytes we were given */
    IOLockLock(fLock);
    fSurf[slot].bounds = rgn->bounds;
    fSurf[slot].nrects = n > RAGE_SURF_RECTS ? RAGE_SURF_RECTS : n;     /* beyond our table: keep the first ones (the present clips to bounds anyway) */
    for (i = 0; i < fSurf[slot].nrects; i++) fSurf[slot].rects[i] = rgn->rect[i];
    fSurf[slot].gen++;
    IOLockUnlock(fLock);
    return kIOReturnSuccess;
}
/* What the stock accelerator's foundFramebuffer does (disassembled): set IOAccelTypes (our registry path), IOAccelIndex and IOAccelRevision on the framebuffer. */
void RageLTDevice::publishFramebuffer(IOFramebuffer *fb, uint32_t index)
{
    char path[256]; int len = sizeof path;
    if (!getPath(path, &len, gIOServicePlane)) { LOG("cannot form the registry path of the accelerator\n"); return; }
    fb->setProperty("IOAccelTypes", path);
    fb->setProperty("IOAccelIndex", (unsigned long long)index, 32);
    fb->setProperty("IOAccelRevision", (unsigned long long)2, 32);
    LOG("framebuffer %s: IOAccelTypes %s index %u\n", fb->getName(), path, (unsigned)index);
}

/* ---- the client ---- */
bool RageLTSurfaceClient::initWithTask(task_t owningTask, void *securityID, UInt32 type)
{
    if (type != kIOAccelSurfaceClientType) return false;
    if (!super::initWithTask(owningTask, securityID, type)) return false;
    fDev = 0; fSlot = -1;
    return true;
}

bool RageLTSurfaceClient::start(IOService *provider)
{
    if (!super::start(provider)) return false;
    fDev = OSDynamicCast(RageLTDevice, provider);
    if (!fDev) return false;
    fSlot = fDev->surfAcquire();
    if (fSlot < 0) { fDev = 0; return false; }
#define M(i, fn, kind, nin, nout) do { IOExternalMethod e = { (IOService *)0, (IOMethod)&RageLTSurfaceClient::fn, kind, nin, nout }; fMethods[i] = e; } while (0)
    const UInt32 V = 0xffffffffu;      /* kIOUCVariableStructureSize (user-space header only) */
    M(kIOAccelSurfaceReadLockOptions,    mLockOpt,        kIOUCScalarIStructO, 1, V);
    M(kIOAccelSurfaceReadUnlockOptions,  mUnlockOpt,      kIOUCScalarIScalarO, 1, 0);
    M(kIOAccelSurfaceGetState,           mGetState,       kIOUCScalarIScalarO, 0, 1);
    M(kIOAccelSurfaceWriteLockOptions,   mLockOpt,        kIOUCScalarIStructO, 1, V);
    M(kIOAccelSurfaceWriteUnlockOptions, mUnlockOpt,      kIOUCScalarIScalarO, 1, 0);
    M(kIOAccelSurfaceRead,               mRead,           kIOUCStructIStructO, V, 0);
    M(kIOAccelSurfaceSetShapeBacking,    mSetShapeBacking,kIOUCScalarIStructI, 4, V);
    M(kIOAccelSurfaceSetIDMode,          mSetIDMode,      kIOUCScalarIScalarO, 2, 0);
    M(kIOAccelSurfaceSetScale,           mSetScale,       kIOUCScalarIStructI, 1, V);
    M(kIOAccelSurfaceSetShape,           mSetShape,       kIOUCScalarIStructI, 2, V);
    M(kIOAccelSurfaceFlush,              mFlush,          kIOUCScalarIScalarO, 2, 0);
    M(kIOAccelSurfaceQueryLock,          mQueryLock,      kIOUCScalarIScalarO, 0, 0);
    M(kIOAccelSurfaceReadLock,           mLock,           kIOUCScalarIStructO, 0, V);
    M(kIOAccelSurfaceReadUnlock,         mUnlock,         kIOUCScalarIScalarO, 0, 0);
    M(kIOAccelSurfaceWriteLock,          mLock,           kIOUCScalarIStructO, 0, V);
    M(kIOAccelSurfaceWriteUnlock,        mUnlock,         kIOUCScalarIScalarO, 0, 0);
#undef M
    return true;
}

IOReturn RageLTSurfaceClient::clientClose(void)
{
    if (fDev) { fDev->surfRelease(fSlot); fDev = 0; fSlot = -1; }
    return kIOReturnSuccess;
}
IOReturn RageLTSurfaceClient::clientDied(void) { return clientClose(); }

IOExternalMethod *RageLTSurfaceClient::getTargetAndMethodForIndex(IOService **target, UInt32 index)
{
    if (!fDev || index >= kIOAccelNumSurfaceMethods) return 0;
    *target = this;
    return &fMethods[index];
}

IOReturn RageLTSurfaceClient::mLockOpt(UInt32 options, void *out, IOByteCount *outSize) { (void)options; (void)out; (void)outSize; return kIOReturnUnsupported; }   /* no CPU backing store */
IOReturn RageLTSurfaceClient::mUnlockOpt(UInt32 options) { (void)options; return kIOReturnSuccess; }
IOReturn RageLTSurfaceClient::mGetState(UInt32 *state) { *state = kIOAccelSurfaceStateIdleBit; return kIOReturnSuccess; }     /* nothing of ours is pending on a surface */
IOReturn RageLTSurfaceClient::mRead(void *in, void *out, IOByteCount inSize, IOByteCount *outSize) { (void)in; (void)out; (void)inSize; (void)outSize; return kIOReturnUnsupported; }
IOReturn RageLTSurfaceClient::mSetShapeBacking(UInt32 bits, UInt32 fb, UInt32 backing, UInt32 rowBytes, void *region, UInt32 regionSize)
{ (void)bits; (void)fb; (void)backing; (void)rowBytes; return fDev ? fDev->surfSetShape(fSlot, (const IOAccelDeviceRegion *)region, regionSize) : kIOReturnNotReady; }
IOReturn RageLTSurfaceClient::mSetIDMode(UInt32 id, UInt32 mode) { if (!fDev) return kIOReturnNotReady; fDev->surfSetIDMode(fSlot, id, mode); return kIOReturnSuccess; }
IOReturn RageLTSurfaceClient::mSetScale(UInt32 options, void *scaling, UInt32 size) { (void)options; (void)scaling; (void)size; return kIOReturnSuccess; }
IOReturn RageLTSurfaceClient::mSetShape(UInt32 bits, UInt32 fb, void *region, UInt32 regionSize)
{ (void)bits; (void)fb; return fDev ? fDev->surfSetShape(fSlot, (const IOAccelDeviceRegion *)region, regionSize) : kIOReturnNotReady; }
IOReturn RageLTSurfaceClient::mFlush(UInt32 doSwap, UInt32 unused) { (void)doSwap; (void)unused; return kIOReturnSuccess; }          /* our bundle presents through the GL client */
IOReturn RageLTSurfaceClient::mQueryLock(void) { return kIOReturnSuccess; }
IOReturn RageLTSurfaceClient::mLock(void *out, IOByteCount *outSize) { (void)out; (void)outSize; return kIOReturnUnsupported; }
IOReturn RageLTSurfaceClient::mUnlock(void) { return kIOReturnSuccess; }

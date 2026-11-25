/* kext/RageLTSurfaceClient.h - the stock-compatible SURFACE user client (client type 0 = kIOAccelSurfaceClientType, selector set
 * eIOAccelSurfaceMethods from IOKit/graphics/IOAccelSurfaceConnect.h, counts as in the stock ATIRagePro kext's methodDescs).
 * WindowServer opens one per GL surface and tells us its id, mode and visible region.  We keep that record (so the GL path can clip its present to
 * the visible region); there is no CPU backing store, so the lock selectors return kIOReturnUnsupported. */
#ifndef RAGELTSURFACECLIENT_H
#define RAGELTSURFACECLIENT_H

#include <IOKit/IOUserClient.h>
#include <IOKit/graphics/IOAccelSurfaceConnect.h>

class RageLTDevice;

class RageLTSurfaceClient : public IOUserClient
{
    OSDeclareDefaultStructors(RageLTSurfaceClient)

    RageLTDevice    *fDev;
    int              fSlot;                      /* index in the device's surface table, -1 = none */
    IOExternalMethod fMethods[kIOAccelNumSurfaceMethods];

public:
    virtual bool initWithTask(task_t owningTask, void *securityID, UInt32 type);
    virtual bool start(IOService *provider);
    virtual IOReturn clientClose(void);
    virtual IOReturn clientDied(void);
    virtual IOExternalMethod *getTargetAndMethodForIndex(IOService **target, UInt32 index);

    IOReturn mLockOpt(UInt32 options, void *out, IOByteCount *outSize);
    IOReturn mUnlockOpt(UInt32 options);
    IOReturn mGetState(UInt32 *state);
    IOReturn mRead(void *in, void *out, IOByteCount inSize, IOByteCount *outSize);
    IOReturn mSetShapeBacking(UInt32 bits, UInt32 fb, UInt32 backing, UInt32 rowBytes, void *region, UInt32 regionSize);
    IOReturn mSetIDMode(UInt32 id, UInt32 mode);
    IOReturn mSetScale(UInt32 options, void *scaling, UInt32 size);
    IOReturn mSetShape(UInt32 bits, UInt32 fb, void *region, UInt32 regionSize);
    IOReturn mFlush(UInt32 doSwap, UInt32 unused);
    IOReturn mQueryLock(void);
    IOReturn mLock(void *out, IOByteCount *outSize);
    IOReturn mUnlock(void);
};

#endif

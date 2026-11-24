#include "RageLTLogClient.h"
#include "RageLTDevice.h"
#include <IOKit/IOLib.h>
#include <kern/debug.h>

#define super IOUserClient
OSDefineMetaClassAndStructors(RageLTLogClient, IOUserClient)

bool RageLTLogClient::initWithTask(task_t owningTask, void *securityID, UInt32 type)
{
    if (type != RAGE_CLIENT_LOG) return false;
    if (!super::initWithTask(owningTask, securityID, type)) return false;
    fTask = owningTask; fDev = 0; fOwner = 0;
    return true;
}

bool RageLTLogClient::start(IOService *provider)
{
    if (!super::start(provider)) return false;
    fDev = OSDynamicCast(RageLTDevice, provider);
    if (!fDev) return false;
    fOwner = fDev->newOwner();
    {
        IOExternalMethod m[RAGE_LOG_SEL_COUNT] = {
            { (IOService *)0, (IOMethod)&RageLTLogClient::selInfo, kIOUCScalarIScalarO, 0, 4 },
            { (IOService *)0, (IOMethod)&RageLTLogClient::selRead, kIOUCScalarIStructO, 1, RAGE_LOG_READ_BYTES },
            { (IOService *)0, (IOMethod)&RageLTLogClient::selMark, kIOUCScalarIScalarO, 2, 0 },
        };
        for (int i = 0; i < RAGE_LOG_SEL_COUNT; i++) fMethods[i] = m[i];
#if RAGE_DEBUG_ENGTEST
        { IOExternalMethod e = { (IOService *)0, (IOMethod)&RageLTLogClient::selEngTest, kIOUCScalarIScalarO, 1, 4 }; fMethods[RAGE_LOG_SEL_ENGTEST] = e; }
#endif
#if RAGE_DEBUG_PANIC
        { IOExternalMethod p = { (IOService *)0, (IOMethod)&RageLTLogClient::selPanic, kIOUCScalarIScalarO, 1, 0 }; fMethods[RAGE_LOG_SEL_PANIC] = p; }
#endif
    }
    fDev->logEvent(RAGE_EV_CLIENT_OPEN, fOwner, RAGE_CLIENT_LOG, 0, 0, 0, 0);
    return true;
}

IOExternalMethod *RageLTLogClient::getTargetAndMethodForIndex(IOService **target, UInt32 index)
{
    if (!fDev) return 0;
    if (index >= RAGE_LOG_SEL_COUNT && !(RAGE_DEBUG_PANIC && index == RAGE_LOG_SEL_PANIC) && !(RAGE_DEBUG_ENGTEST && index == RAGE_LOG_SEL_ENGTEST)) return 0;
    *target = this;
    return &fMethods[index];
}

IOReturn RageLTLogClient::clientClose(void)
{
    if (fDev) { fDev->logEvent(RAGE_EV_CLIENT_CLOSE, fOwner, RAGE_CLIENT_LOG, 0, 0, 0, 0); fDev->releaseOwner(fOwner); fDev = 0; }
    if (!isInactive()) terminate();
    return kIOReturnSuccess;
}

IOReturn RageLTLogClient::clientDied(void) { return clientClose(); }

IOReturn RageLTLogClient::selInfo(uint32_t *head, uint32_t *capacity, uint32_t *refused, uint32_t *features)
{
    if (!fDev) return kIOReturnNotAttached;
    return fDev->logInfo(head, capacity, refused, features);
}

IOReturn RageLTLogClient::selRead(uint32_t fromSeq, void *out, IOByteCount *outSize)
{
    if (!fDev) return kIOReturnNotAttached;
    if (!out || !outSize || *outSize < RAGE_LOG_READ_BYTES) return kIOReturnBadArgument;
    *outSize = RAGE_LOG_READ_BYTES;
    return fDev->logRead(fromSeq, (struct rage_log_read_out *)out);
}

IOReturn RageLTLogClient::selMark(uint32_t a0, uint32_t a1)
{
    if (!fDev) return kIOReturnNotAttached;
    fDev->logEvent(RAGE_EV_MARK, fOwner, a0, a1, 0, 0, 0);
    return kIOReturnSuccess;
}

#if RAGE_DEBUG_PANIC
IOReturn RageLTLogClient::selPanic(uint32_t token)
{
    if (!fDev) return kIOReturnNotAttached;
    fDev->logEvent(RAGE_EV_ERROR, fOwner, 0xdead0001u, token, 0, 0, 0);          /* the last event the ring will ever hold */
    IOLog("RageLT: DEBUG forced panic, token 0x%lx\n", (unsigned long)token);
    panic("RageLT: forced panic test (token 0x%x)", token);
    return kIOReturnSuccess;
}
#else
IOReturn RageLTLogClient::selPanic(uint32_t) { return kIOReturnUnsupported; }
#endif

#if RAGE_DEBUG_ENGTEST
IOReturn RageLTLogClient::selEngTest(uint32_t mask, uint32_t *passmask, uint32_t *outside, uint32_t *inside, uint32_t *status)
{
    if (!fDev) return kIOReturnNotAttached;
    return fDev->engineSelfTest(mask, passmask, outside, inside, status);
}
#else
IOReturn RageLTLogClient::selEngTest(uint32_t, uint32_t *, uint32_t *, uint32_t *, uint32_t *) { return kIOReturnUnsupported; }
#endif

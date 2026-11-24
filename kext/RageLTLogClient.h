/* kext/RageLTLogClient.h - diagnostics user client (RAGE_CLIENT_LOG): read the event ring, add marks.  No holds, no timers, no locks
 * kept across calls: clientClose() must stay trivial (the old client's hold watchdog once left killed processes stuck in 'E'). */
#ifndef RAGELTLOGCLIENT_H
#define RAGELTLOGCLIENT_H

#include <IOKit/IOUserClient.h>
#include "rage_abi.h"

class RageLTDevice;

class RageLTLogClient : public IOUserClient
{
    OSDeclareDefaultStructors(RageLTLogClient)

    RageLTDevice    *fDev;
    task_t           fTask;
    uint32_t         fOwner;
    IOExternalMethod fMethods[RAGE_LOG_SEL_COUNT + 2];

public:
    virtual bool initWithTask(task_t owningTask, void *securityID, UInt32 type);
    virtual bool start(IOService *provider);
    virtual IOReturn clientClose(void);
    virtual IOReturn clientDied(void);
    virtual IOExternalMethod *getTargetAndMethodForIndex(IOService **target, UInt32 index);

    IOReturn selInfo(uint32_t *head, uint32_t *capacity, uint32_t *refused, uint32_t *features);
    IOReturn selRead(uint32_t fromSeq, void *out, IOByteCount *outSize);
    IOReturn selMark(uint32_t a0, uint32_t a1);
    IOReturn selPanic(uint32_t token);                 /* RAGE_DEBUG_PANIC only */
    IOReturn selEngTest(uint32_t mask, uint32_t *passmask, uint32_t *outside, uint32_t *inside, uint32_t *status);   /* RAGE_DEBUG_ENGTEST only */
};

#endif

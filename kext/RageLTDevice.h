/* kext/RageLTDevice.h - the chip: PCI attach, BAR0 map, VRAM bookkeeping, validated command submission (design 3.1). */
#ifndef RAGELTDEVICE_H
#define RAGELTDEVICE_H

#include <IOKit/IOService.h>
#include <IOKit/IOLocks.h>
#include <IOKit/pci/IOPCIDevice.h>
#include "features.h"
#include "rage_vram.h"
#include "rage_abi.h"

class RageLTDevice : public IOService
{
    OSDeclareDefaultStructors(RageLTDevice)

    IOPCIDevice         *fPCI;
    IODeviceMemory      *fFB;          /* BAR0: 8 MB VRAM + the register file in its top 2 KB */
    IOMemoryMap         *fMap;
    volatile uint8_t      *fRegs;        /* BAR0 + 0x7FF800: real register byte offsets 0..0x7FC */
    IOLock              *fLock;
    struct rage_vram     fVram;        /* the only VRAM allocator (design 3.1) */
    uint32_t               fNextOwner;
    bool                 fEngineLive;  /* false until a later phase turns the engine on; the validated write path is dormant until then */
    uint32_t               fRefused;     /* batches refused by the guard (diagnostic) */
    struct rage_log     *fLog;         /* event ring (RAGE_FEAT_PANIC_LOG), allocated at start */
    IOSimpleLock        *fLogLock;     /* short spin lock: logging must be safe from any context, including an interrupt filter */

public:
    virtual bool start(IOService *provider);
    virtual void stop(IOService *provider);
    virtual void free(void);

    /* A client must obtain an owner id; closing the client calls releaseOwner() so nothing leaks. */
    uint32_t       newOwner(void);
    void         releaseOwner(uint32_t owner);
    IOReturn     vramAlloc(uint32_t owner, uint32_t length, uint32_t align, uint32_t *offset);
    IOReturn     vramFree(uint32_t owner, uint32_t offset);

    /* pairs[2*i] = register byte offset, pairs[2*i+1] = value.  Validated as a whole by rage_batch_ok(); refused batches change nothing. */
    IOReturn     submitBatch(const uint32_t *pairs, uint32_t npairs);

    /* RAGE_FEAT_PANIC_LOG: all of these are no-ops (return success / do nothing) when the feature is compiled out */
    void         logEvent(uint32_t id, uint32_t owner, uint32_t a0, uint32_t a1, uint32_t a2, uint32_t a3, uint32_t a4);
    IOReturn     logInfo(uint32_t *head, uint32_t *capacity, uint32_t *refused, uint32_t *features);
    IOReturn     logRead(uint32_t fromSeq, struct rage_log_read_out *out);
    virtual IOReturn newUserClient(task_t owningTask, void *securityID, UInt32 type, IOUserClient **handler);

    uint32_t       refusedCount(void) const { return fRefused; }
    bool         engineLive(void) const { return fEngineLive; }
};

#endif

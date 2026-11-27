/* kext/RageLT2DClient.h - the stock-compatible 2D CONTEXT user client (client type 2, class ATIR32DContext in stock).  Selector table and semantics:
 * docs/re-notes/ga-2d-client-decode.md section 3; plan: docs/re-notes/b1b-2d-client-plan.md.  Map type 0 is the command buffer AND the doorbell: each map submits the
 * pending buffer (validated, copied first, executed as abstract ops by the kernel, rage_2d_guard.h) and returns a fresh one.  Selectors 16-18 (stock's unrestricted
 * register window) are NOT provided. */
#ifndef RAGELT2DCLIENT_H
#define RAGELT2DCLIENT_H

#include <IOKit/IOUserClient.h>
#include <IOKit/IOBufferMemoryDescriptor.h>
#include "rage_2d_guard.h"

class RageLTDevice;

#define RAGE2D_NSEL      16u
#define RAGE2D_RING      4u          /* command-buffer descriptors rotated through (repeated maps may or may not be coalesced by IOKit: not established, so a fixed ring) */
#define RAGE2D_BUFBYTES  4096u
#define RAGE2D_MAXOPS    128u

class RageLT2DClient : public IOUserClient
{
    OSDeclareDefaultStructors(RageLT2DClient)

    RageLTDevice                 *fDev;
    IOExternalMethod              fMethods[RAGE2D_NSEL];
    IOBufferMemoryDescriptor     *fRing[RAGE2D_RING];
    IOBufferMemoryDescriptor     *fCtxBuf;
    uint32_t                      fCur;                /* ring index of the buffer last handed to the client */
    bool                          fHanded;             /* a buffer is out and may hold commands */
    IOLock                       *fLock;               /* serialises the doorbell (stock takes the accelerator mutex) */
    struct rage2d_shadow          fShadow;
    uint32_t                      fWords[RAGE2D_BUFBYTES / 4];   /* the kernel's private copy of a command buffer */
    struct rage2d_op             *fOps;
    uint32_t                      fSubmits, fRefusedBufs, fExecuted;

    void     initBuffer(IOBufferMemoryDescriptor *b);
    IOReturn process(IOBufferMemoryDescriptor *b);

public:
    virtual bool initWithTask(task_t owningTask, void *securityID, UInt32 type);
    virtual bool start(IOService *provider);
    virtual void free(void);
    virtual IOReturn clientClose(void);
    virtual IOReturn clientDied(void);
    virtual IOExternalMethod *getTargetAndMethodForIndex(IOService **target, UInt32 index);
    virtual IOReturn clientMemoryForType(UInt32 type, UInt32 *options, IOMemoryDescriptor **memory);

    IOReturn mSetSurface(UInt32 id, UInt32 mode, void *out, IOByteCount *outSize);
    IOReturn mGetConfig(UInt32 *a, UInt32 *b);
    IOReturn mSwap(UInt32 ignored, UInt32 *out);
    IOReturn mLockMemory(UInt32 ignored, UInt32 *o0, UInt32 *o1);
    IOReturn mUnlockMemory(UInt32 arg, UInt32 *out);
    IOReturn mFinish(UInt32 mode, void *s, UInt32 sz);
    IOReturn mUnsupported3(UInt32 a, UInt32 b, UInt32 c);
    IOReturn mUnsupportedSS(void *in, void *out, IOByteCount inSize, IOByteCount *outSize);
    IOReturn mUnsupportedIS(UInt32 a, void *s, UInt32 sz);
    IOReturn mUnsupported22(UInt32 a, UInt32 b, UInt32 *o0, UInt32 *o1);
    IOReturn mUnsupported31(UInt32 a, UInt32 b, UInt32 c, UInt32 *o);
};

#endif

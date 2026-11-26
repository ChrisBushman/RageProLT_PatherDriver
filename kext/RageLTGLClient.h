/* kext/RageLTGLClient.h - the new GL bundle's user client (RAGE_CLIENT_GL).  Selector contract: common/rage_abi.h.  No holds, no timers: close must stay trivial. */
#ifndef RAGELTGLCLIENT_H
#define RAGELTGLCLIENT_H

#include <IOKit/IOUserClient.h>
#include "rage_abi.h"

class RageLTDevice;

class RageLTGLClient : public IOUserClient
{
    OSDeclareDefaultStructors(RageLTGLClient)

    RageLTDevice    *fDev;
    task_t           fTask;
    uint32_t         fOwner;
    IOExternalMethod fMethods[RAGE_GL_SEL_COUNT];

    IOReturn         userIn(uint32_t addr, uint8_t *dst, uint32_t len);       /* copy from the calling task */
    IOReturn         userOut(uint32_t addr, const uint8_t *src, uint32_t len);

public:
    virtual bool initWithTask(task_t owningTask, void *securityID, UInt32 type);
    virtual bool start(IOService *provider);
    virtual IOReturn clientClose(void);
    virtual IOReturn clientDied(void);
    virtual IOExternalMethod *getTargetAndMethodForIndex(IOService **target, UInt32 index);

    IOReturn selInfo(uint32_t *gen, uint32_t *features, uint32_t *usableEnd, uint32_t *flags);
    IOReturn selAlloc(uint32_t length, uint32_t align, uint32_t *offset);
    IOReturn selFree(uint32_t offset);
    IOReturn selSetTarget(uint32_t off, uint32_t pitch, uint32_t bpp, uint32_t w, uint32_t h);
    IOReturn selSetScissor(uint32_t l, uint32_t r, uint32_t t, uint32_t b);
    IOReturn selSetZ(uint32_t off, uint32_t pitch);
    IOReturn selFill(uint32_t x, uint32_t y, uint32_t w, uint32_t h, uint32_t color);
    IOReturn selSubmit(uint32_t addr, uint32_t npairs);
    IOReturn selSubmitEx(uint32_t addr, uint32_t npairs, uint32_t texAddr, uint32_t ntex, uint32_t tfmt, uint32_t tfmt2);
    IOReturn selFence(uint32_t seq, uint32_t *completed);
    IOReturn selPresent(uint32_t sx, uint32_t sy, uint32_t w, uint32_t h, uint32_t dx, uint32_t dy);
    IOReturn selVramWrite(uint32_t off, uint32_t addr, uint32_t len, uint32_t flags);
    IOReturn selVramRead(uint32_t off, uint32_t addr, uint32_t len, uint32_t flags);
    IOReturn selDisplay(uint32_t *gen, uint32_t *w, uint32_t *h, uint32_t *pitch, uint32_t *bpp, uint32_t *base);
    IOReturn selClearZ(uint32_t value);
    IOReturn selSetTexture(uint32_t index, uint32_t off, uint32_t fmt);
    IOReturn selConfig(uint32_t key, uint32_t value);
    IOReturn selPresentScaled(uint32_t s, uint32_t sz, uint32_t d, uint32_t dsz, uint32_t filter);
    IOReturn selScanoutRead(uint32_t byteOff, uint32_t addr, uint32_t len);
};

#endif

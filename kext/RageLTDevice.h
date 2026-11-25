/* kext/RageLTDevice.h - the chip: PCI attach, BAR0 map, VRAM bookkeeping, validated command submission (design 3.1). */
#ifndef RAGELTDEVICE_H
#define RAGELTDEVICE_H

#include <IOKit/IOService.h>
#include <IOKit/IOLocks.h>
#include <IOKit/pci/IOPCIDevice.h>
#include <IOKit/graphics/IOFramebuffer.h>
#include "features.h"
#include "rage_vram.h"
#include "rage_abi.h"

/* Per-client drawing context (design 3.1 RageLTEngine).  The KERNEL owns all of it: a client can only ask for values that are checked against what it
 * owns, and the scissor is always clamped to the target, so the hardware clip (proven, design 11.11-11.12) contains every primitive. */
struct rage_ctx {
    uint32_t owner;                    /* 0 = slot free */
    uint32_t tgt_off, tgt_pitch, tgt_bpp, tgt_w, tgt_h;   /* tgt_bpp 0 = no target set */
    uint32_t sc_l, sc_r, sc_t, sc_b;
    uint32_t z_off, z_pitch, z_set;
    uint32_t tex_off[12], tex_set, tex_fmt, tex2_fmt;   /* TEX_0..10_OFF, SECONDARY_TEX_OFF (index 11); tex_set = bitmask of bound ones; texel formats (DP_PIX_WIDTH) */
};
#define RAGE_MAX_CTX 16u
#define RAGE_MAX_FB  4u

struct rage_display { uint32_t base, pitch_px, w, h, bpp, depth; };   /* live scanout as read from the CRTC (kernel-owned copy) */

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
    bool                 fDispReserved; /* scanout + cursor block reserved in the allocator (done once, permanent) */
    struct rage_ctx      fCtx[RAGE_MAX_CTX];
    struct rage_display  fDisp;
    uint32_t               fModeGen, fModeDepth;               /* odd gen = a mode change is in progress; all targets invalid */
    IONotifier          *fFBPublish;
    IOFramebuffer       *fFBs[RAGE_MAX_FB];
    IONotifier          *fFBNotify[RAGE_MAX_FB];
    uint32_t               fNumFB;
    uint32_t               fFenceSubmitted, fFenceCompleted;   /* PIO phase: completes before submit returns; DMA keeps the same counters */
    /* RAGE_FEAT_DMA (RageLTDma.cpp) */
    class IOBufferMemoryDescriptor *fDmaCmd, *fDmaRing;
    IOMemoryMap         *fDmaCmdMap, *fDmaRingMap;
    volatile uint8_t      *fDmaCmdKva, *fDmaRingKva, *fDmaRingKva0;
    uint32_t               fDmaCmdBus, fDmaRingBus, fDmaNDesc, fDmaSaveBus;
    bool                 fDmaOk, fDmaUse, fDmaPending;
    struct rage_log     *fLog;         /* event ring (RAGE_FEAT_PANIC_LOG), allocated at start */
    IOSimpleLock        *fLogLock;     /* short spin lock: logging must be safe from any context, including an interrupt filter */

public:
    virtual bool start(IOService *provider);
    virtual void stop(IOService *provider);
    virtual void free(void);

    /* A client must obtain an owner id; closing the client calls releaseOwner() so nothing leaks. */
    uint32_t       newOwner(void);
    struct rage_ctx *ctxFor(uint32_t owner, bool create);   /* caller holds fLock */
    void         releaseOwner(uint32_t owner);
    IOReturn     vramAlloc(uint32_t owner, uint32_t length, uint32_t align, uint32_t *offset);
    IOReturn     vramFree(uint32_t owner, uint32_t offset);

    /* ---- engine (RageLTEngine.cpp).  All checked against what `owner` owns; refused calls change nothing. ---- */
    IOReturn     engineEnable(void);                 /* reads the chip: only once the boot init is known to have run (BUS_EXT_REG_EN, CRTC on); resets + initialises the engine */
    IOReturn     reserveDisplay(void);               /* reserves the scanout buffer (from the live CRTC) and the cursor/framebuffer block at the top */
    IOReturn     setRenderTarget(uint32_t owner, uint32_t off, uint32_t pitch_px, uint32_t bpp, uint32_t w, uint32_t h);
    IOReturn     setScissor(uint32_t owner, int32_t l, int32_t r, int32_t t, int32_t b);
    IOReturn     setZBuffer(uint32_t owner, uint32_t off, uint32_t pitch_px);
    IOReturn     setTexture(uint32_t owner, uint32_t index, uint32_t off, uint32_t fmt);   /* index 0..10 = TEX_n_OFF, 11 = SECONDARY_TEX_OFF; off 0xffffffff unbinds */
    static uint32_t prologuePairs(const struct rage_ctx *c, uint32_t *out);                  /* kernel-owned state as {reg,val} pairs (max 32 pairs) */
    IOReturn     clearZ(uint32_t owner, uint32_t value);
    IOReturn     fillRect(uint32_t owner, int32_t x, int32_t y, int32_t w, int32_t h, uint32_t color565);
    IOReturn     present(uint32_t owner, uint32_t sx, uint32_t sy, uint32_t w, uint32_t h, uint32_t dx, uint32_t dy);
    IOReturn     vramCopyIn(uint32_t owner, uint32_t off, const uint8_t *src, uint32_t len, uint32_t flags);     /* src = kernel buffer */
    IOReturn     vramCopyOut(uint32_t owner, uint32_t off, uint8_t *dst, uint32_t len, uint32_t flags);
    IOReturn     scanoutCopyOut(uint32_t byteOff, uint8_t *dst, uint32_t len);                                    /* debug */
    IOReturn     displayInfo(struct rage_display *out, uint32_t *gen);
    uint32_t       modeGen(void) const { return fModeGen; }
    /* mode changes (RageLTMode.cpp) */
    void         modeWillChange(void);
    void         modeDidChange(void);
    bool         fbPublished(IOService *svc);
    IOReturn     fbEvent(IOFramebuffer *fb, IOIndex event, void *info);
    void         modeStop(void);
    void         modeStart(void);
    IOReturn     modeRegsRead(void);                 /* reads the CRTC into fDisp */
    uint32_t       fenceSubmitted(void) const { return fFenceSubmitted; }
    uint32_t       fenceCompleted(void) const { return fFenceCompleted; }
    IOReturn     fenceWait(uint32_t seq);            /* takes fLock; for clients */
    IOReturn     waitFence(uint32_t seq);            /* bounded; kIOReturnTimeout on a stuck engine (which is reset) */

    bool         dmaInit(void);
    void         dmaFree(void);
    IOReturn     dmaWait(void);                      /* caller holds fLock */
    IOReturn     dmaSubmit(const struct rage_ctx *c, const uint32_t *pairs, uint32_t npairs);
    IOReturn     idleWait(void);                     /* drain DMA, then engine idle: before any CPU/PIO use of the engine or the aperture */
    uint32_t       dmaMinimalTest(uint32_t variant);   /* DEBUG ONLY */
    void         dmaConfig(uint32_t key, uint32_t value);
    bool         dmaActive(void) const { return fDmaOk && fDmaUse; }
    /* pairs[2*i] = register byte offset, pairs[2*i+1] = value.  Validated as a whole by rage_batch_ok(); refused batches change nothing. */
    IOReturn     submitBatch(uint32_t owner, const uint32_t *pairs, uint32_t npairs);

    /* RAGE_FEAT_PANIC_LOG: all of these are no-ops (return success / do nothing) when the feature is compiled out */
    void         logEvent(uint32_t id, uint32_t owner, uint32_t a0, uint32_t a1, uint32_t a2, uint32_t a3, uint32_t a4);
    IOReturn     logInfo(uint32_t *head, uint32_t *capacity, uint32_t *refused, uint32_t *features);
    IOReturn     logRead(uint32_t fromSeq, struct rage_log_read_out *out);
    virtual IOReturn newUserClient(task_t owningTask, void *securityID, UInt32 type, IOUserClient **handler);

    /* DEBUG ONLY (RAGE_DEBUG_ENGTEST): see RageLTSelfTest.cpp */
    IOReturn     engineSelfTest(uint32_t mask, uint32_t *passmask, uint32_t *outside, uint32_t *inside, uint32_t *status);

    uint32_t       refusedCount(void) const { return fRefused; }
    bool         engineLive(void) const { return fEngineLive; }
};

#endif

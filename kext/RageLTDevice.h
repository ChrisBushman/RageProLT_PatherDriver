/* kext/RageLTDevice.h - the chip: PCI attach, BAR0 map, VRAM bookkeeping, validated command submission (design 3.1). */
#ifndef RAGELTDEVICE_H
#define RAGELTDEVICE_H

#include <IOKit/IOService.h>
#include <IOKit/IOLocks.h>
#include <IOKit/pci/IOPCIDevice.h>
#include <IOKit/graphics/IOFramebuffer.h>
#include <IOKit/graphics/IOAccelerator.h>
#include <IOKit/graphics/IOAccelSurfaceConnect.h>
#include "features.h"
#include "rage_vram.h"
#include "rage_abi.h"

/* Per-client drawing context (design 3.1 RageLTEngine).  The KERNEL owns all of it: a client can only ask for values that are checked against what it
 * owns, and the scissor is always clamped to the target, so the hardware clip (proven, design 11.11-11.12) contains every primitive. */
struct rage_ctx {
    uint32_t owner;                    /* 0 = slot free */
    uint32_t tgt_off, tgt_pitch, tgt_bpp, tgt_w, tgt_h;   /* tgt_bpp 0 = no target set */
    uint32_t tgt_555;                  /* 16 bpp target is ARGB1555 (the scanout is 15 bit) rather than RGB565; fixed when the target is set */
    uint32_t sc_l, sc_r, sc_t, sc_b;
    uint32_t z_off, z_pitch, z_set;
    uint32_t tex_off[12], tex_set, tex_fmt, tex2_fmt;   /* TEX_0..10_OFF, SECONDARY_TEX_OFF (index 11); tex_set = bitmask of bound ones; texel formats (DP_PIX_WIDTH) */
};
#define RAGE_MAX_CTX 16u
#define RAGE_MAX_FB  4u

/* One WindowServer surface (RageLTSurfaceClient, client type 0): id and mode from set_id_mode, bounds and visible rectangles from set_shape. */
#define RAGE_MAX_SURF  32u
#define RAGE_SURF_RECTS 24u
struct rage_surface {
    uint32_t used, id, mode, gen;          /* gen increments on every shape change */
    IOAccelBounds bounds;                  /* unclipped surface rectangle in device coordinates */
    uint32_t nrects;                       /* 0 = the bounds are the visible rectangle */
    IOAccelBounds rects[RAGE_SURF_RECTS];
};

struct rage_display { uint32_t base, pitch_px, w, h, bpp, depth; };   /* live scanout as read from the CRTC (kernel-owned copy) */

/* The stock ATIRageProGA plugin's Start does IOObjectConformsTo(accelerator, "ATIRagePro") and fails with 0x2c7 otherwise (ga-2d-client-decode.md); that tests the CLASS chain,
 * not the registry name, so the chain contains a class literally named ATIRagePro (it cannot coexist with the stock kext's metaclass of that name: the installer sets stock aside). */
class ATIRagePro : public IOAccelerator
{
    OSDeclareAbstractStructors(ATIRagePro)
};

struct rage2d_op;

class RageLTDevice : public ATIRagePro
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
    struct rage_surface  fSurf[RAGE_MAX_SURF];
    uint32_t               fModeGen, fModeDepth;               /* odd gen = a mode change is in progress; all targets invalid */
    IONotifier          *fFBPublish;
    IOFramebuffer       *fFBs[RAGE_MAX_FB];
    IONotifier          *fFBNotify[RAGE_MAX_FB];
    uint32_t               fNumFB;
    uint32_t               fQuadTcXor, fQuadScXor;   /* experiment: bits flipped in TEX_CNTL / SCALE_3D_CNTL of the quad */
    bool                   fQuadBlend;   /* experiment: add TEX_BLEND_FCN = 2 to the bilinear present */
    int32_t                fQuadU, fQuadV, fQuadX;   /* textured-quad present biases (S/T texel Q8, vertex Q2); experiment knobs, default 0 */
    uint32_t               fFenceSubmitted, fFenceCompleted;   /* PIO phase: completes before submit returns; DMA keeps the same counters */
    /* RAGE_FEAT_DMA (RageLTDma.cpp) */
    class IOBufferMemoryDescriptor *fDmaCmd, *fDmaCmd2, *fDmaRing;       /* two command buffers (the stream being drawn and the one being built), one ring block holding a 16 KB table for each */
    IOMemoryMap         *fDmaCmdMap, *fDmaRingMap;
    volatile uint8_t      *fDmaCmdKva, *fDmaCmdKva2, *fDmaRingKva, *fDmaRingKva0;
    uint32_t               fDmaCmdBus, fDmaCmdBus2, fDmaRingBus, fDmaNDesc, fDmaSaveBus, fDmaEnd, fDmaCur;   /* fDmaEnd = table end the walk of the stream in flight must reach; fDmaCur = buffer the next stream is built in */
    bool                 fDmaOk, fDmaUse, fDmaPending, fGuiPending;   /* fDmaPending: ANYTHING in flight (GUI stream or system transfer); fGuiPending: the GUI stream only */
    /* texture/pixel upload by system transfer (design: docs/re-notes/texture-dma-upload-standalone.md) */
    class IOBufferMemoryDescriptor *fXferStage, *fXferTab;
    volatile uint8_t      *fXferStageKva, *fXferTabKva;
    uint32_t               fXferStageBus, fXferTabBus, fXferStageBytes, fXferSaveBus, fXferEnd, fXferDst, fXferLen;
    bool                 fXferOk, fXferUse, fXferPending;
    IOLock              *fStageLock;     /* serialises the stage fill + kick; taken before fLock, never after */
    uint32_t               fScratch;     /* a word the engine never writes: rewriting it with its own value (CPU) flushes the engine pixel cache before a CPU read of VRAM */
    struct rage_log     *fLog;         /* event ring (RAGE_FEAT_PANIC_LOG), allocated at start */
    IOSimpleLock        *fLogLock;     /* short spin lock: logging must be safe from any context, including an interrupt filter */

public:
    virtual bool start(IOService *provider);
    virtual void stop(IOService *provider);
    virtual void free(void);

    /* A client must obtain an owner id; closing the client calls releaseOwner() so nothing leaks. */
    int          surfAcquire(void);                                  /* slot or -1 */
    void         surfRelease(int slot);
    void         surfSetIDMode(int slot, uint32_t id, uint32_t mode);
    IOReturn     surfSetShape(int slot, const IOAccelDeviceRegion *rgn, uint32_t size);
    void         publishFramebuffer(IOFramebuffer *fb, uint32_t index);   /* IOAccelTypes / IOAccelIndex / IOAccelRevision on the framebuffer */
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
    IOReturn     swapTarget(uint32_t owner, uint32_t dstOff);
    IOReturn     presentEx(uint32_t owner, uint32_t sx, uint32_t sy, uint32_t w, uint32_t h, uint32_t dx, uint32_t dy, uint32_t dw, uint32_t dh, uint32_t filter);
    void         quadConfig(uint32_t key, uint32_t value);
    IOReturn     vramCopyIn(uint32_t owner, uint32_t off, const uint8_t *src, uint32_t len, uint32_t flags);     /* src = kernel buffer */
    IOReturn     vramCopyOut(uint32_t owner, uint32_t off, uint8_t *dst, uint32_t len, uint32_t flags);
    IOReturn     scanoutCopyOut(uint32_t byteOff, uint8_t *dst, uint32_t len);                                    /* debug */
    IOReturn     regDump(uint32_t *pairs, uint32_t maxPairs, uint32_t *n);
    IOReturn     ovlOn(uint32_t owner, uint32_t off, uint32_t pitch_px, uint32_t sw, uint32_t sh, uint32_t dx, uint32_t dy, uint32_t dw, uint32_t dh, uint32_t bpp, uint32_t flags);   /* RAGE_FEAT_OVERLAY; flags: bit0 = SCALE_HORZ_MODE, bit1 = SCALE_VERT_MODE (debug builds only, hypothesis test) */
    IOReturn     ovlOff(uint32_t owner);
    IOReturn     ovlFlip(uint32_t owner, uint32_t off);
    IOReturn     ovlDisableLocked(void);                                                                          /* KEY_CNTL hidden, then SCALE_CNTL 0 (fLock held) */
    bool         ovlActive(void) const { return fOvlOwner != 0; }
    uint32_t     fOvlOwner, fOvlPitch, fOvlH, fOvlBpp, fOvlOff;                                                            /* owner of the enabled overlay (0 = none) and its source geometry */
    IOReturn     ovlB0(uint32_t *before, uint32_t *readback, uint32_t *after);                                     /* debug: overlay plan step B0 (the only write) */                                        /* debug, READ-ONLY fixed register list */
    IOReturn     displayInfo(struct rage_display *out, uint32_t *gen);
    IOReturn     exec2D(const struct rage2d_op *ops, uint32_t n);                 /* B1b: validated abstract 2D ops on the scanout (RageLT2D.cpp) */
    IOReturn     finish2D(void);                                                    /* wait until the engine is idle (type-2 client 'finish') */
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
    IOReturn     setTextures(uint32_t owner, const uint32_t *rec, uint32_t n, uint32_t tfmt, uint32_t tfmt2);   /* n {index,offset} records + the two texel formats under one lock (SUBMIT_EX) */
    IOReturn     idleWait(void);                     /* drain DMA, then engine idle: before any CPU/PIO use of the engine or the aperture */
    uint32_t       dmaMinimalTest(uint32_t variant);   /* DEBUG ONLY */
    void         dmaConfig(uint32_t key, uint32_t value);
    bool         dmaActive(void) const { return fDmaOk && fDmaUse; }
    void         xferInit(void);
    bool         xferActive(void) const { return fXferOk && fXferUse; }
    IOReturn     xferReserve(uint8_t **stage, uint32_t *maxBytes);                 /* takes the stage lock; kIOReturnUnsupported = use the CPU path */
    void         xferAbort(void);                                                    /* releases the stage lock without a kick */
    IOReturn     xferKick(uint32_t owner, uint32_t off, uint32_t len, uint32_t flags);   /* consumes the stage lock; returns after the kick */
    IOReturn     xferWait(void);                     /* caller holds fLock: finish the transfer in flight (bounded) */
    /* pairs[2*i] = register byte offset, pairs[2*i+1] = value.  Validated as a whole by rage_batch_ok(); refused batches change nothing. */
    IOReturn     submitBatch(uint32_t owner, const uint32_t *pairs, uint32_t npairs);

    /* RAGE_FEAT_PANIC_LOG: all of these are no-ops (return success / do nothing) when the feature is compiled out */
    void         logEvent(uint32_t id, uint32_t owner, uint32_t a0, uint32_t a1, uint32_t a2, uint32_t a3, uint32_t a4);
    IOReturn     logInfo(uint32_t *head, uint32_t *capacity, uint32_t *refused, uint32_t *features);
    IOReturn     logRead(uint32_t fromSeq, struct rage_log_read_out *out);
    virtual IOReturn newUserClient(task_t owningTask, void *securityID, UInt32 type, IOUserClient **handler);

    /* DEBUG ONLY (RAGE_DEBUG_ENGTEST): see RageLTSelfTest.cpp */
    IOMemoryDescriptor *apertureMemory(uint32_t owner, uint32_t off, uint32_t len);   /* B1g-5 debug: the caller's own VRAM block as a user-mappable descriptor (caller releases) */
    IOReturn     apertureBench(uint32_t owner, uint32_t off, uint32_t len, uint32_t mode, uint32_t *wns, uint32_t *rns);   /* B1g-4 */
    IOReturn     engineSelfTest(uint32_t mask, uint32_t *passmask, uint32_t *outside, uint32_t *inside, uint32_t *status);

    uint32_t       refusedCount(void) const { return fRefused; }
    bool         engineLive(void) const { return fEngineLive; }
    bool         scanout555(void) const { return fDisp.depth == 3; }
};

/* byte-swap a buffer in place the way vramCopyIn would (RageLTMode.cpp) */
void rage_xfer_inplace(uint8_t *p, uint32_t len, uint32_t flags);

#endif

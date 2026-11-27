/* kext/RageLT2D.cpp - B1b: the 2D context client (type 2) and the device-side executor of validated 2D ops.  Plan: docs/re-notes/b1b-2d-client-plan.md.
 * Containment (design 11.11-11.12 / E1): the client's stream is never given to the chip.  The kernel copies each command buffer, rage2d_validate() checks and interprets it
 * (shadow register file, clamping to the display), and the ops run through the kernel's own engine code (eng_fill / eng_blit_dir / eng_fill_cmp / eng_line), each of which
 * programs the complete engine context, so no client or GL state is inherited (the A5 "replay" cost is the ~14 register writes of every op; measured later). */
#include "RageLT2DClient.h"
#include "RageLTDevice.h"
#include "RageLTEngine.h"
#include <IOKit/IOLib.h>
#include <string.h>

#define super IOUserClient
OSDefineMetaClassAndStructors(RageLT2DClient, IOUserClient)

#define LOG(...) IOLog("RageLT: " __VA_ARGS__)

#ifndef RAGE_OP_DONE
#ifdef RAGE_SYNC_OPS
#define RAGE_OP_DONE(seq) waitFence(seq)
#else
#define RAGE_OP_DONE(seq) ((void)(seq), kIOReturnSuccess)
#endif
#endif

#define RAGE_E_2BE   ((IOReturn)0xe00002be)      /* stock's "no surface bound / not ready" */
#define RAGE_E_2CC   ((IOReturn)0xe00002cc)      /* stock's lock_memory with no surface */

/* ---- device side ---- */
IOReturn RageLTDevice::finish2D(void)
{
    IOReturn rc;
    if (!fEngineLive) return kIOReturnSuccess;
    IOLockLock(fLock);
    rc = idleWait() ? kIOReturnTimeout : kIOReturnSuccess;
    IOLockUnlock(fLock);
    return rc;
}

IOReturn RageLTDevice::exec2D(const struct rage2d_op *ops, uint32_t n)
{
    struct rage_display d; uint32_t i, pixw, cmask, sc_lr, sc_tb, seq; IOReturn rc = kIOReturnSuccess;
    if (!n) return kIOReturnSuccess;
    if (!fEngineLive || (fModeGen & 1u)) return kIOReturnNotReady;
    IOLockLock(fLock);
    d = fDisp;
    if (!fDispReserved || (d.depth != 3 && d.depth != 4 && d.depth != 6)) { IOLockUnlock(fLock); return kIOReturnUnsupported; }
    pixw = d.depth == 6 ? RAGE_PIXW_8888 : d.depth == 3 ? RAGE_PIXW_555 : RAGE_PIXW_565;
    cmask = d.depth == 6 ? 0xffffffffu : 0xffffu;
    sc_lr = (d.w - 1u) << 16; sc_tb = (d.h - 1u) << 16;
    if (idleWait()) { IOLockUnlock(fLock); return kIOReturnTimeout; }
    for (i = 0; i < n && rc == kIOReturnSuccess; i++) {
        const struct rage2d_op *o = &ops[i]; int e = 0;
        /* the validator clamped everything to the display of the mode it was given; re-check against the live mode in case it changed in between */
        switch (o->type) {
        case RAGE2D_OP_FILL:
            if (o->x < 0 || o->y < 0 || (uint32_t)(o->x + o->w) > d.w || (uint32_t)(o->y + o->h) > d.h || o->w <= 0 || o->h <= 0) break;
            if (o->cmp) e = eng_fill_cmp(fRegs, pixw, d.base, d.pitch_px, (uint32_t)o->x, (uint32_t)o->y, (uint32_t)o->w, (uint32_t)o->h, o->color & cmask, sc_lr, sc_tb, o->cmp, o->cmp_clr & cmask, o->cmp_msk & cmask);
            else        e = eng_fill(fRegs, pixw, d.base, d.pitch_px, (uint32_t)o->x, (uint32_t)o->y, (uint32_t)o->w, (uint32_t)o->h, o->color & cmask, sc_lr, sc_tb, 3u);
            break;
        case RAGE2D_OP_COPY:
            if (o->x < 0 || o->y < 0 || o->sx < 0 || o->sy < 0 || (uint32_t)(o->x + o->w) > d.w || (uint32_t)(o->y + o->h) > d.h ||
                (uint32_t)(o->sx + o->w) > d.w || (uint32_t)(o->sy + o->h) > d.h || o->w <= 0 || o->h <= 0) break;
            e = eng_blit_dir(fRegs, pixw, d.base, d.pitch_px, (uint32_t)o->sx, (uint32_t)o->sy, (uint32_t)o->x, (uint32_t)o->y, (uint32_t)o->w, (uint32_t)o->h, o->xrev, o->yrev, sc_lr, sc_tb);
            break;
        case RAGE2D_OP_LINE:
            if (o->x < 0 || o->y < 0 || (uint32_t)o->x >= d.w || (uint32_t)o->y >= d.h) break;
            e = eng_line(fRegs, pixw, d.base, d.pitch_px, (uint32_t)o->x, (uint32_t)o->y, o->dcntl, o->major, o->minor, o->color & cmask, sc_lr, sc_tb);
            break;
        default: break;
        }
        if (e) rc = kIOReturnTimeout;
    }
    if (rc == kIOReturnSuccess) { seq = ++fFenceSubmitted; rc = RAGE_OP_DONE(seq); }
    IOLockUnlock(fLock);
    return rc;
}

/* ---- the client ---- */
bool RageLT2DClient::initWithTask(task_t owningTask, void *securityID, UInt32 type)
{
    if (type != 2) return false;
    if (!super::initWithTask(owningTask, securityID, type)) return false;
    fDev = 0; fCtxBuf = 0; fOps = 0; fLock = 0; fCur = 0; fHanded = false; fSubmits = fRefusedBufs = fExecuted = 0;
    { uint32_t k; for (k = 0; k < RAGE2D_RING; k++) fRing[k] = 0; }
    rage2d_shadow_init(&fShadow);
    return true;
}

void RageLT2DClient::initBuffer(IOBufferMemoryDescriptor *b)
{
    uint32_t *w = (uint32_t *)b->getBytesNoCopy();
    memset(w, 0, RAGE2D_BUFBYTES);
    w[4] = (RAGE2D_BUFBYTES - 0x20u) / 4u;          /* +0x10 capacity words (0x3F8) */
    w[5] = 0;                                        /* +0x14 surface flags: display destination only */
    w[7] = 1;                                        /* +0x1c initial packet header (the plugin overwrites it with 0) */
    w[8] = 0x01000000u;                              /* +0x20 terminator */
}

bool RageLT2DClient::start(IOService *provider)
{
    uint32_t k;
    if (!super::start(provider)) return false;
    fDev = OSDynamicCast(RageLTDevice, provider);
    if (!fDev) return false;
    if (!fDev->chipOK() || fDev->engineEnable() != kIOReturnSuccess || fDev->reserveDisplay() != kIOReturnSuccess) { fDev = 0; return false; }   /* as the GL client: only after the chip is known to be initialised */
    fLock = IOLockAlloc();
    fOps = (struct rage2d_op *)IOMalloc(RAGE2D_MAXOPS * sizeof(struct rage2d_op));
    if (!fLock || !fOps) return false;
    for (k = 0; k < RAGE2D_RING; k++) {
        fRing[k] = IOBufferMemoryDescriptor::withOptions(kIOMemoryKernelUserShared, RAGE2D_BUFBYTES, page_size);
        if (!fRing[k]) return false;
        initBuffer(fRing[k]);
    }
    fCtxBuf = IOBufferMemoryDescriptor::withOptions(kIOMemoryKernelUserShared, RAGE2D_BUFBYTES, page_size);
    if (!fCtxBuf) return false;
    { uint32_t *w = (uint32_t *)fCtxBuf->getBytesNoCopy(); memset(w, 0, RAGE2D_BUFBYTES); w[7] = 1; w[4] = (RAGE2D_BUFBYTES - 0x20u) / 4u - 2u; }   /* context buffer header (stock init_context_buffer_header): +0x1c = 1, +0x10 = (0x1000-0x20)/4-2 */
#define M(i, fn, kind, nin, nout) do { IOExternalMethod e = { (IOService *)0, (IOMethod)&RageLT2DClient::fn, kind, nin, nout }; fMethods[i] = e; } while (0)
    M(0,  mSetSurface,     kIOUCScalarIStructO, 2, 0x30);          /* set_surface (id, mode) -> 0x30-byte buffer info */
    M(1,  mGetConfig,      kIOUCScalarIScalarO, 0, 2);             /* get_config */
    M(2,  mSetSurface,     kIOUCScalarIStructO, 2, 0x30);          /* get_surface_info: same lookup, no state change (we have no state to change) */
    M(3,  mSwap,           kIOUCScalarIScalarO, 1, 1);             /* swap_surface */
    M(4,  mUnsupported3,   kIOUCScalarIScalarO, 3, 0);             /* scale_surface */
    M(5,  mLockMemory,     kIOUCScalarIScalarO, 1, 2);             /* lock_memory */
    M(6,  mUnlockMemory,   kIOUCScalarIScalarO, 1, 1);             /* unlock_memory */
    M(7,  mFinish,         kIOUCScalarIStructI, 1, 0);             /* finish (mode) */
    M(8,  mUnsupported31,  kIOUCScalarIScalarO, 3, 1);             /* declare_image: unused by the GA plugin */
    M(9,  mUnsupported22,  kIOUCScalarIScalarO, 2, 2);             /* create_image */
    M(10, mUnsupported22,  kIOUCScalarIScalarO, 2, 2);             /* create_transfer */
    M(11, mUnsupportedIS,  kIOUCScalarIStructI, 1, 0);             /* delete_image */
    M(12, mUnsupportedIS,  kIOUCScalarIStructI, 1, 0);             /* wait_image */
    M(13, mUnsupportedSS,  kIOUCStructIStructO, 0xc, 0xc);         /* set_surface_paging_options: stock returns 0x2c7 */
    M(14, mUnsupportedSS,  kIOUCStructIStructO, 0xc, 0xc);         /* set_surface_vsync_options: stock returns 0x2c7 */
    M(15, mUnsupportedIS,  kIOUCScalarIStructI, 1, 0);             /* set_macrovision */
#undef M
    return true;
}

void RageLT2DClient::free(void)
{
    uint32_t k;
    for (k = 0; k < RAGE2D_RING; k++) if (fRing[k]) { fRing[k]->release(); fRing[k] = 0; }
    if (fCtxBuf) { fCtxBuf->release(); fCtxBuf = 0; }
    if (fOps) { IOFree(fOps, RAGE2D_MAXOPS * sizeof(struct rage2d_op)); fOps = 0; }
    if (fLock) { IOLockFree(fLock); fLock = 0; }
    super::free();
}

IOReturn RageLT2DClient::clientClose(void)
{
    if (fDev) { fDev->finish2D(); fDev = 0; }            /* nothing of ours stays in flight after the client is gone */
    if (!isInactive()) terminate();                      /* without this the client object (and its 4 buffers) outlives the process */
    return kIOReturnSuccess;
}
IOReturn RageLT2DClient::clientDied(void) { return clientClose(); }

IOExternalMethod *RageLT2DClient::getTargetAndMethodForIndex(IOService **target, UInt32 index)
{
    LOG("2D: selector %u\n", (unsigned)index);
    if (!fDev || index >= RAGE2D_NSEL) return 0;         /* 16-18 (stock's raw register window) do not exist here */
    *target = this;
    return &fMethods[index];
}

/* Copy the buffer the client filled, validate and interpret the copy, run the ops.  A refused buffer is logged and dropped; the map itself must still succeed (the stock plugin
 * dereferences a null pointer when a map fails). */
static unsigned gFills, gCopies, gLines, gCmpFills;   /* rig statistics: what the real WindowServer asks for */
IOReturn RageLT2DClient::process(IOBufferMemoryDescriptor *b)
{
    const uint32_t *src = (const uint32_t *)b->getBytesNoCopy();
    uint32_t nops = 0, nw = (RAGE2D_BUFBYTES - 0x1cu) / 4u; int rc; struct rage_display d; uint32_t gen;
    memcpy(fWords, src + 7, nw * 4u);                    /* the packet header at +0x1c and everything after it */
    if (fDev->displayInfo(&d, &gen)) return kIOReturnNotReady;
    if (!d.w || !d.h) return kIOReturnNotReady;
    rc = rage2d_validate(fWords, nw, &fShadow, 0, (int32_t)d.w, (int32_t)d.h, fOps, RAGE2D_MAXOPS, &nops);
    if (rc) { fRefusedBufs++; LOG("2D: command buffer refused (%d), %u refused so far\n", rc, (unsigned)fRefusedBufs); return kIOReturnBadArgument; }
    fSubmits++;
    { uint32_t i; for (i = 0; i < nops; i++) { if (fOps[i].type == RAGE2D_OP_FILL) { gFills++; if (fOps[i].cmp) gCmpFills++; } else if (fOps[i].type == RAGE2D_OP_COPY) gCopies++; else gLines++; } }
    if (nops) { IOReturn e = fDev->exec2D(fOps, nops); if (e) return e; fExecuted += nops; }
    fDev->setProperty("Rage2DFills", (unsigned long long)gFills, 32); fDev->setProperty("Rage2DCopies", (unsigned long long)gCopies, 32); fDev->setProperty("Rage2DLines", (unsigned long long)gLines, 32); fDev->setProperty("Rage2DKeyedFills", (unsigned long long)gCmpFills, 32);
    fDev->setProperty("Rage2DBufs", (unsigned long long)fSubmits, 32); fDev->setProperty("Rage2DOps", (unsigned long long)fExecuted, 32); fDev->setProperty("Rage2DRefused", (unsigned long long)fRefusedBufs, 32);   /* ioreg visibility for rig checks */
    return kIOReturnSuccess;
}

IOReturn RageLT2DClient::clientMemoryForType(UInt32 type, UInt32 *options, IOMemoryDescriptor **memory)
{
    IOBufferMemoryDescriptor *b;
    LOG("2D: clientMemoryForType(%u)\n", (unsigned)type);
    if (!fDev) return kIOReturnNotReady;
    *options = 0;
    if (type == 1) { fCtxBuf->retain(); *memory = fCtxBuf; return kIOReturnSuccess; }
    if (type != 0) return kIOReturnBadArgument;
    IOLockLock(fLock);
    if (fHanded) { (void)process(fRing[fCur]); fCur = (fCur + 1u) % RAGE2D_RING; }   /* the doorbell: submit what the client wrote, move to the next buffer */
    b = fRing[fCur]; initBuffer(b); fHanded = true;
    b->retain(); *memory = b;
    IOLockUnlock(fLock);
    return kIOReturnSuccess;
}

/* ---- selectors ---- */
IOReturn RageLT2DClient::mSetSurface(UInt32 id, UInt32 mode, void *out, IOByteCount *outSize)
{
    struct rage_display d; uint32_t gen, pix; uint32_t *o = (uint32_t *)out;
    LOG("2D: set_surface id %u mode 0x%x outSize %u\n", (unsigned)id, (unsigned)mode, (unsigned)*outSize);
    if (!fDev || *outSize != 0x30) return kIOReturnBadArgument;
    if (mode & 0x800u) return kIOReturnBadArgument;                       /* surface-backed 2D is not provided (first scope: display destinations) */
    if (id != 0) return kIOReturnBadArgument;                             /* one display, index 0 */
    if (fDev->displayInfo(&d, &gen) || !d.w) return kIOReturnNotReady;
    pix = d.depth == 6 ? 6u : d.depth == 3 ? 3u : d.depth == 4 ? 4u : 2u;
    memset(out, 0, 0x30);
    o[0] = 0;                                                           /* x, y (shorts) */
    o[1] = (d.w << 16) | (d.h & 0xffffu);                                /* +4 short w, +6 short h */
    o[2] = d.w; o[3] = d.h;                                              /* +8 long w, +0xc long h */
    o[4] = d.pitch_px;                                                   /* +0x10 pitch (units not established; pixels) */
    o[7] = 0;                                                           /* +0x1c */
    o[8] = 1;                                                           /* +0x20 */
    o[9] = pix;                                                         /* +0x24 pixel code byte value */
    return kIOReturnSuccess;
}
IOReturn RageLT2DClient::mGetConfig(UInt32 *a, UInt32 *b) { LOG("2D: get_config\n"); *a = 0x04000000u; *b = 0; return kIOReturnSuccess; }     /* LI caps (ga-2d-client-decode.md) */
IOReturn RageLT2DClient::mSwap(UInt32 ignored, UInt32 *out) { (void)ignored; *out = 0; return RAGE_E_2BE; }           /* nothing is bound that could be swapped: stock's "no surface" answer */
IOReturn RageLT2DClient::mLockMemory(UInt32 ignored, UInt32 *o0, UInt32 *o1) { (void)ignored; *o0 = 0xDEADBEEFu; *o1 = 0; return RAGE_E_2CC; }
IOReturn RageLT2DClient::mUnlockMemory(UInt32 arg, UInt32 *out) { (void)arg; *out = 0; return kIOReturnSuccess; }
IOReturn RageLT2DClient::mFinish(UInt32 mode, void *s, UInt32 sz)
{
    (void)s; (void)sz;
    if (mode > 2) return kIOReturnBadArgument;
    return fDev ? fDev->finish2D() : kIOReturnNotReady;
}
IOReturn RageLT2DClient::mUnsupported3(UInt32 a, UInt32 b, UInt32 c) { (void)a; (void)b; (void)c; return kIOReturnUnsupported; }
IOReturn RageLT2DClient::mUnsupportedSS(void *in, void *out, IOByteCount inSize, IOByteCount *outSize) { (void)in; (void)out; (void)inSize; (void)outSize; return kIOReturnUnsupported; }
IOReturn RageLT2DClient::mUnsupportedIS(UInt32 a, void *s, UInt32 sz) { (void)a; (void)s; (void)sz; return kIOReturnUnsupported; }
IOReturn RageLT2DClient::mUnsupported22(UInt32 a, UInt32 b, UInt32 *o0, UInt32 *o1) { (void)a; (void)b; *o0 = 0; *o1 = 0; return kIOReturnUnsupported; }
IOReturn RageLT2DClient::mUnsupported31(UInt32 a, UInt32 b, UInt32 c, UInt32 *o) { (void)a; (void)b; (void)c; *o = 0; return kIOReturnUnsupported; }

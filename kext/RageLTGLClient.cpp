#include "RageLTGLClient.h"
#include "RageLTDevice.h"
#include "rage_guard.h"
#include "RageLTEngine.h"
#include <IOKit/IOLib.h>
#include <IOKit/IOMemoryDescriptor.h>
extern "C" { task_t current_task(void); int copyin(const char *udaddr, void *kaddr, unsigned int nbytes); }

#define super IOUserClient
OSDefineMetaClassAndStructors(RageLTGLClient, IOUserClient)

#define XFER_CHUNK 16384u
#define XFER_DMA_MIN 8192u         /* below this the CPU path costs less than the DMA set-up */

bool RageLTGLClient::initWithTask(task_t owningTask, void *securityID, UInt32 type)
{
    if (type != RAGE_CLIENT_GL) return false;
    if (!super::initWithTask(owningTask, securityID, type)) return false;
    fTask = owningTask; fDev = 0; fOwner = 0;
    return true;
}

bool RageLTGLClient::start(IOService *provider)
{
    if (!super::start(provider)) return false;
    fDev = OSDynamicCast(RageLTDevice, provider);
    if (!fDev) return false;
    /* The first GL client is the point where the chip is first READ (it is known to be initialised by the framebuffer by now). */
    if (fDev->engineEnable() != kIOReturnSuccess) { fDev = 0; return false; }
    if (fDev->reserveDisplay() != kIOReturnSuccess) { fDev = 0; return false; }
    fOwner = fDev->newOwner();
#define M(i, fn, kind, nin, nout) do { IOExternalMethod e = { (IOService *)0, (IOMethod)&RageLTGLClient::fn, kind, nin, nout }; fMethods[i] = e; } while (0)
    M(RAGE_GL_INFO,        selInfo,       kIOUCScalarIScalarO, 0, 4);
    M(RAGE_GL_ALLOC,       selAlloc,      kIOUCScalarIScalarO, 2, 1);
    M(RAGE_GL_FREE,        selFree,       kIOUCScalarIScalarO, 1, 0);
    M(RAGE_GL_SET_TARGET,  selSetTarget,  kIOUCScalarIScalarO, 5, 0);
    M(RAGE_GL_SET_SCISSOR, selSetScissor, kIOUCScalarIScalarO, 4, 0);
    M(RAGE_GL_SET_Z,       selSetZ,       kIOUCScalarIScalarO, 2, 0);
    M(RAGE_GL_FILL,        selFill,       kIOUCScalarIScalarO, 5, 0);
    M(RAGE_GL_SUBMIT,      selSubmit,     kIOUCScalarIScalarO, 2, 0);
    M(RAGE_GL_SUBMIT_EX,   selSubmitEx,   kIOUCScalarIScalarO, 6, 0);
    M(RAGE_GL_FENCE,       selFence,      kIOUCScalarIScalarO, 1, 1);
    M(RAGE_GL_PRESENT,     selPresent,    kIOUCScalarIScalarO, 6, 0);
    M(RAGE_GL_VRAM_WRITE,  selVramWrite,  kIOUCScalarIScalarO, 4, 0);
    M(RAGE_GL_VRAM_READ,   selVramRead,   kIOUCScalarIScalarO, 4, 0);
    M(RAGE_GL_DISPLAY,     selDisplay,    kIOUCScalarIScalarO, 0, 6);
    M(RAGE_GL_SET_TEXTURE, selSetTexture, kIOUCScalarIScalarO, 3, 0);
    M(RAGE_GL_CLEAR_Z,     selClearZ,     kIOUCScalarIScalarO, 1, 0);
    M(RAGE_GL_CONFIG,      selConfig,     kIOUCScalarIScalarO, 2, 0);
    M(RAGE_GL_PRESENT_SCALED, selPresentScaled, kIOUCScalarIScalarO, 5, 0);
    M(RAGE_GL_SCANOUT_READ, selScanoutRead, kIOUCScalarIScalarO, 3, 0);
    M(RAGE_GL_REG_DUMP,    selRegDump,    kIOUCScalarIScalarO, 2, 0);
    M(RAGE_GL_OVL_B0,      selOvlB0,      kIOUCScalarIScalarO, 0, 3);
    M(RAGE_GL_OVL_ON,      selOvlOn,      kIOUCScalarIScalarO, 6, 0);
    M(RAGE_GL_OVL_OFF,     selOvlOff,     kIOUCScalarIScalarO, 0, 0);
    M(RAGE_GL_OVL_FLIP,    selOvlFlip,    kIOUCScalarIScalarO, 1, 0);
#undef M
    fDev->logEvent(RAGE_EV_CLIENT_OPEN, fOwner, RAGE_CLIENT_GL, 0, 0, 0, 0);
    return true;
}

IOExternalMethod *RageLTGLClient::getTargetAndMethodForIndex(IOService **target, UInt32 index)
{
    if (!fDev || index >= RAGE_GL_SEL_COUNT) return 0;
    if ((index == RAGE_GL_SCANOUT_READ || index == RAGE_GL_REG_DUMP || index == RAGE_GL_OVL_B0) && !RAGE_DEBUG_ENGTEST) return 0;
    *target = this;
    return &fMethods[index];
}

IOReturn RageLTGLClient::clientClose(void)
{
    if (fDev) { fDev->logEvent(RAGE_EV_CLIENT_CLOSE, fOwner, RAGE_CLIENT_GL, 0, 0, 0, 0); fDev->releaseOwner(fOwner); fDev = 0; }
    if (!isInactive()) terminate();
    return kIOReturnSuccess;
}
IOReturn RageLTGLClient::clientDied(void) { return clientClose(); }

IOReturn RageLTGLClient::userIn(uint32_t addr, uint8_t *dst, uint32_t len)
{
    /* External methods run on the caller's own thread: when that task is the client's, a plain copyin() does the job (no memory descriptor to build, no page wiring per submit). */
    if (fTask == current_task()) return copyin((const char *)addr, (void *)dst, len) ? kIOReturnVMError : kIOReturnSuccess;
    IOMemoryDescriptor *d = IOMemoryDescriptor::withAddress((vm_address_t)addr, len, kIODirectionOut, fTask);
    IOReturn rc;
    if (!d) return kIOReturnBadArgument;
    rc = d->prepare();
    if (rc == kIOReturnSuccess) { if (d->readBytes(0, dst, len) != len) rc = kIOReturnVMError; d->complete(); }
    d->release();
    return rc;
}

IOReturn RageLTGLClient::userOut(uint32_t addr, const uint8_t *src, uint32_t len)
{
    IOMemoryDescriptor *d = IOMemoryDescriptor::withAddress((vm_address_t)addr, len, kIODirectionIn, fTask);
    IOReturn rc;
    if (!d) return kIOReturnBadArgument;
    rc = d->prepare();
    if (rc == kIOReturnSuccess) { if (d->writeBytes(0, src, len) != len) rc = kIOReturnVMError; d->complete(); }
    d->release();
    return rc;
}

IOReturn RageLTGLClient::selInfo(uint32_t *gen, uint32_t *features, uint32_t *usableEnd, uint32_t *flags)
{
    if (!fDev) return kIOReturnNotAttached;
    *gen = fDev->modeGen(); *features = RAGE_FEATURE_BITS; *usableEnd = RAGE_VRAM_USABLE_END; *flags = (fDev->engineLive() ? RAGE_INFO_ENGINE : 0u) | (fDev->dmaActive() ? RAGE_INFO_DMA : 0u) | (fDev->scanout555() ? RAGE_INFO_TGT555 : 0u);
    return kIOReturnSuccess;
}
IOReturn RageLTGLClient::selAlloc(uint32_t length, uint32_t align, uint32_t *offset)
{
    if (!fDev) return kIOReturnNotAttached;
    if (fDev->modeGen() & 1u) return kIOReturnNotReady;
    return fDev->vramAlloc(fOwner, length, align, offset);
}
IOReturn RageLTGLClient::selFree(uint32_t offset) { return fDev ? fDev->vramFree(fOwner, offset) : kIOReturnNotAttached; }
IOReturn RageLTGLClient::selSetTarget(uint32_t off, uint32_t pitch, uint32_t bpp, uint32_t w, uint32_t h)
{ return fDev ? fDev->setRenderTarget(fOwner, off, pitch, bpp, w, h) : kIOReturnNotAttached; }
IOReturn RageLTGLClient::selSetScissor(uint32_t l, uint32_t r, uint32_t t, uint32_t b)
{ return fDev ? fDev->setScissor(fOwner, (int32_t)l, (int32_t)r, (int32_t)t, (int32_t)b) : kIOReturnNotAttached; }
IOReturn RageLTGLClient::selSetZ(uint32_t off, uint32_t pitch) { return fDev ? fDev->setZBuffer(fOwner, off, pitch) : kIOReturnNotAttached; }
IOReturn RageLTGLClient::selFill(uint32_t x, uint32_t y, uint32_t w, uint32_t h, uint32_t color)
{ return fDev ? fDev->fillRect(fOwner, (int32_t)x, (int32_t)y, (int32_t)w, (int32_t)h, color) : kIOReturnNotAttached; }

IOReturn RageLTGLClient::selClearZ(uint32_t value)
{ return fDev ? fDev->clearZ(fOwner, value) : kIOReturnNotAttached; }

IOReturn RageLTGLClient::selSubmit(uint32_t addr, uint32_t npairs)
{
    uint32_t *buf; uint32_t bytes; IOReturn rc;
    if (!fDev) return kIOReturnNotAttached;
    if (!npairs || npairs > RAGE_BATCH_MAX_PAIRS) return kIOReturnBadArgument;
    bytes = npairs * 8u;
    buf = (uint32_t *)IOMalloc(bytes);
    if (!buf) return kIOReturnNoMemory;
    { uint32_t t0 = tbl32(); rc = userIn(addr, (uint8_t *)buf, bytes); pf_user += (tbl32() - t0) >> 2; }
    if (rc == kIOReturnSuccess) rc = fDev->submitBatch(fOwner, buf, npairs);
    IOFree(buf, bytes);
    return rc;
}

IOReturn RageLTGLClient::selSubmitEx(uint32_t addr, uint32_t npairs, uint32_t texAddr, uint32_t ntex, uint32_t tfmt, uint32_t tfmt2)
{
    uint32_t tex[24], t_t = tbl32(); IOReturn rc = kIOReturnSuccess;
    if (!fDev) return kIOReturnNotAttached;
    if (ntex > 12u || npairs > RAGE_BATCH_MAX_PAIRS) return kIOReturnBadArgument;
    if (ntex) { rc = userIn(texAddr, (uint8_t *)tex, ntex * 8u); if (rc != kIOReturnSuccess) return rc; }
    (void)fDev->setTextures(fOwner, tex, ntex, tfmt, tfmt2);                                       /* same checks as SET_TEXTURE, one lock; a refused offset stays unbound */
    pf_tex += (tbl32() - t_t) >> 2;
    return npairs ? selSubmit(addr, npairs) : kIOReturnSuccess;
}

IOReturn RageLTGLClient::selFence(uint32_t seq, uint32_t *completed)
{
    IOReturn rc;
    if (!fDev) return kIOReturnNotAttached;
    rc = fDev->fenceWait(seq);
    *completed = fDev->fenceCompleted();
    return rc;
}

IOReturn RageLTGLClient::selPresent(uint32_t sx, uint32_t sy, uint32_t w, uint32_t h, uint32_t dx, uint32_t dy)
{ return fDev ? fDev->present(fOwner, sx, sy, w, h, dx, dy) : kIOReturnNotAttached; }

IOReturn RageLTGLClient::selVramWrite(uint32_t off, uint32_t addr, uint32_t len, uint32_t flags)
{
    uint8_t *buf; IOReturn rc = kIOReturnSuccess; uint32_t done = 0;
    if (!fDev) return kIOReturnNotAttached;
    if (fDev->modeGen() & 1u) return kIOReturnNotReady;
    if (!len || (len & 3u) || len > 0x100000u || (flags & ~3u) || (off & 3u)) return kIOReturnBadArgument;
    /* Large uploads: user data -> kernel staging -> system transfer (returns after the kick; the device fences it before any dependent use). */
    while (len - done >= XFER_DMA_MIN && !((off + done) & 15u) && fDev->xferActive()) {   /* 16-byte aligned destinations only (the proven transfers); others use the CPU path */
        uint8_t *stage; uint32_t max, n;
        rc = fDev->xferReserve(&stage, &max);
        if (rc == kIOReturnUnsupported || (rc == kIOReturnTimeout && !fDev->xferActive())) { rc = kIOReturnSuccess; break; }   /* not available (or just disabled by a timeout): the CPU path */
        if (rc) return rc;
        n = len - done > max ? max : len - done;
        rc = userIn(addr + done, stage, n);
        if (rc) { fDev->xferAbort(); return rc; }
        rc = fDev->xferKick(fOwner, off + done, n, flags);
        if (rc) return rc;
        done += n;
    }
    if (done == len) return kIOReturnSuccess;
    buf = (uint8_t *)IOMalloc(XFER_CHUNK);
    if (!buf) return kIOReturnNoMemory;
    while (done < len && rc == kIOReturnSuccess) {
        uint32_t n = len - done > XFER_CHUNK ? XFER_CHUNK : len - done;
        rc = userIn(addr + done, buf, n);
        if (rc == kIOReturnSuccess) rc = fDev->vramCopyIn(fOwner, off + done, buf, n, flags);
        done += n;
    }
    IOFree(buf, XFER_CHUNK);
    return rc;
}

IOReturn RageLTGLClient::selVramRead(uint32_t off, uint32_t addr, uint32_t len, uint32_t flags)
{
    uint8_t *buf; IOReturn rc = kIOReturnSuccess; uint32_t done = 0;
    if (!fDev) return kIOReturnNotAttached;
    if (fDev->modeGen() & 1u) return kIOReturnNotReady;
    if (!len || (len & 3u) || len > 0x100000u) return kIOReturnBadArgument;
    buf = (uint8_t *)IOMalloc(XFER_CHUNK);
    if (!buf) return kIOReturnNoMemory;
    while (done < len && rc == kIOReturnSuccess) {
        uint32_t n = len - done > XFER_CHUNK ? XFER_CHUNK : len - done;
        rc = fDev->vramCopyOut(fOwner, off + done, buf, n, flags);
        if (rc == kIOReturnSuccess) rc = userOut(addr + done, buf, n);
        done += n;
    }
    IOFree(buf, XFER_CHUNK);
    return rc;
}

IOReturn RageLTGLClient::selSetTexture(uint32_t index, uint32_t off, uint32_t fmt)
{ return fDev ? fDev->setTexture(fOwner, index, off, fmt) : kIOReturnNotAttached; }

IOReturn RageLTGLClient::selPresentScaled(uint32_t s, uint32_t sz, uint32_t d, uint32_t dsz, uint32_t filter)
{ return fDev ? fDev->presentEx(fOwner, s & 0xffffu, s >> 16, sz & 0xffffu, sz >> 16, d & 0xffffu, d >> 16, dsz & 0xffffu, dsz >> 16, filter) : kIOReturnNotAttached; }

IOReturn RageLTGLClient::selConfig(uint32_t key, uint32_t value)
{
    if (!fDev) return kIOReturnNotAttached;
    if (key >= 3 && key <= 8) { fDev->quadConfig(key, value); return kIOReturnSuccess; }   /* textured-quad present biases (experiment) */
    if (key != 1 && key != 2) return kIOReturnBadArgument;      /* 1 = DMA batch submission, 2 = DMA texture/pixel upload */
    fDev->dmaConfig(key, value);
    return kIOReturnSuccess;
}

IOReturn RageLTGLClient::selDisplay(uint32_t *gen, uint32_t *w, uint32_t *h, uint32_t *pitch, uint32_t *bpp, uint32_t *base)
{
    struct rage_display d; uint32_t g;
    if (!fDev) return kIOReturnNotAttached;
    fDev->displayInfo(&d, &g);
    *gen = g; *w = d.w; *h = d.h; *pitch = d.pitch_px; *bpp = d.bpp; *base = d.base;
    return kIOReturnSuccess;
}

IOReturn RageLTGLClient::selOvlOn(uint32_t off, uint32_t pitch, uint32_t src, uint32_t dst, uint32_t dsz, uint32_t bpp)
{
    if (!fDev) return kIOReturnNotAttached;
    return fDev->ovlOn(fOwner, off, pitch, src & 0xffffu, src >> 16, dst & 0xffffu, dst >> 16, dsz & 0xffffu, dsz >> 16, bpp);
}
IOReturn RageLTGLClient::selOvlOff(void)
{
    if (!fDev) return kIOReturnNotAttached;
    return fDev->ovlOff(fOwner);
}
IOReturn RageLTGLClient::selOvlFlip(uint32_t off)
{
    if (!fDev) return kIOReturnNotAttached;
    return fDev->ovlFlip(fOwner, off);
}
#if RAGE_DEBUG_ENGTEST
IOReturn RageLTGLClient::selScanoutRead(uint32_t byteOff, uint32_t addr, uint32_t len)
{
    uint8_t *buf; IOReturn rc = kIOReturnSuccess; uint32_t done = 0;
    if (!fDev) return kIOReturnNotAttached;
    buf = (uint8_t *)IOMalloc(XFER_CHUNK);
    if (!buf) return kIOReturnNoMemory;
    while (done < len && rc == kIOReturnSuccess) {
        uint32_t n = len - done > XFER_CHUNK ? XFER_CHUNK : len - done;
        rc = fDev->scanoutCopyOut(byteOff + done, buf, n);
        if (rc == kIOReturnSuccess) rc = userOut(addr + done, buf, n);
        done += n;
    }
    IOFree(buf, XFER_CHUNK);
    return rc;
}
IOReturn RageLTGLClient::selRegDump(uint32_t addr, uint32_t len)
{
    enum { kDumpPairs = 48 };
    uint32_t pairs[2 * kDumpPairs], n = 0; IOReturn rc;                  /* a pair is TWO words: the first version sized this in words and overran the kernel stack (panic Nov 25 22:2x) */
    if (!fDev) return kIOReturnNotAttached;
    rc = fDev->regDump(pairs, kDumpPairs, &n);
    if (rc != kIOReturnSuccess || n > kDumpPairs) return rc != kIOReturnSuccess ? rc : kIOReturnOverrun;
    if (len > n * 8) len = n * 8;
    return userOut(addr, (const uint8_t *)pairs, len & ~3u);
}
IOReturn RageLTGLClient::selOvlB0(uint32_t *before, uint32_t *readback, uint32_t *after)
{
    if (!fDev) return kIOReturnNotAttached;
    return fDev->ovlB0(before, readback, after);
}
#else
IOReturn RageLTGLClient::selScanoutRead(uint32_t, uint32_t, uint32_t) { return kIOReturnUnsupported; }
IOReturn RageLTGLClient::selRegDump(uint32_t, uint32_t) { return kIOReturnUnsupported; }
IOReturn RageLTGLClient::selOvlB0(uint32_t *, uint32_t *, uint32_t *) { return kIOReturnUnsupported; }
#endif

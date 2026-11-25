#include "RageLTGLClient.h"
#include "RageLTDevice.h"
#include "rage_guard.h"
#include <IOKit/IOLib.h>
#include <IOKit/IOMemoryDescriptor.h>

#define super IOUserClient
OSDefineMetaClassAndStructors(RageLTGLClient, IOUserClient)

#define XFER_CHUNK 16384u

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
    M(RAGE_GL_FENCE,       selFence,      kIOUCScalarIScalarO, 1, 1);
    M(RAGE_GL_PRESENT,     selPresent,    kIOUCScalarIScalarO, 6, 0);
    M(RAGE_GL_VRAM_WRITE,  selVramWrite,  kIOUCScalarIScalarO, 4, 0);
    M(RAGE_GL_VRAM_READ,   selVramRead,   kIOUCScalarIScalarO, 4, 0);
    M(RAGE_GL_DISPLAY,     selDisplay,    kIOUCScalarIScalarO, 0, 6);
    M(RAGE_GL_SET_TEXTURE, selSetTexture, kIOUCScalarIScalarO, 3, 0);
    M(RAGE_GL_CLEAR_Z,     selClearZ,     kIOUCScalarIScalarO, 1, 0);
    M(RAGE_GL_CONFIG,      selConfig,     kIOUCScalarIScalarO, 2, 0);
    M(RAGE_GL_SCANOUT_READ, selScanoutRead, kIOUCScalarIScalarO, 3, 0);
#undef M
    fDev->logEvent(RAGE_EV_CLIENT_OPEN, fOwner, RAGE_CLIENT_GL, 0, 0, 0, 0);
    return true;
}

IOExternalMethod *RageLTGLClient::getTargetAndMethodForIndex(IOService **target, UInt32 index)
{
    if (!fDev || index >= RAGE_GL_SEL_COUNT) return 0;
    if (index == RAGE_GL_SCANOUT_READ && !RAGE_DEBUG_ENGTEST) return 0;
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
    *gen = fDev->modeGen(); *features = RAGE_FEATURE_BITS; *usableEnd = RAGE_VRAM_USABLE_END; *flags = (fDev->engineLive() ? 1u : 0u) | (fDev->dmaActive() ? 2u : 0u);
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
    rc = userIn(addr, (uint8_t *)buf, bytes);
    if (rc == kIOReturnSuccess) rc = fDev->submitBatch(fOwner, buf, npairs);
    IOFree(buf, bytes);
    return rc;
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
    if (!len || (len & 3u) || len > 0x100000u) return kIOReturnBadArgument;
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

IOReturn RageLTGLClient::selConfig(uint32_t key, uint32_t value)
{
    if (!fDev) return kIOReturnNotAttached;
    if (key != 1) return kIOReturnBadArgument;
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
#else
IOReturn RageLTGLClient::selScanoutRead(uint32_t, uint32_t, uint32_t) { return kIOReturnUnsupported; }
#endif

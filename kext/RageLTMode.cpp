/* kext/RageLTMode.cpp - display coexistence (design 3.4): follows the ATY/NDRV framebuffer's mode changes, keeps the scanout reservation and the
 * mode generation, and does the things that need to know the scanout: present blit, VRAM copies for pixel/texture traffic.  No floating point.
 *
 * Mode change protocol (same event mapping as Apple's IOATIRProAccelerator::display_change_handler, docs/re-notes/display-mode-change.md):
 *   will-change: odd generation; wait for the engine to drain; invalidate every context's target (clients get kIOReturnNotReady and must
 *                re-read DISPLAY); release the scanout reservation.
 *   did-change:  restore BUS_EXT_REG_EN (the chip is re-initialised by the framebuffer), re-initialise the engine, re-read the CRTC, re-reserve
 *                the new scanout; even generation.
 * DMA/bus-master state belongs to the kernel and is re-established here, never "disabled for the life of the process" (ledger L6). */
#include "RageLTDevice.h"
#include <IOKit/IOLib.h>
#include <libkern/OSAtomic.h>
#include "rage_guard.h"
#include "RageLTEngine.h"

#define LOG(...) IOLog("RageLT: " __VA_ARGS__)

static bool fbPublishedTramp(void *target, void *ref, IOService *svc) { (void)ref; return ((RageLTDevice *)target)->fbPublished(svc); }
static IOReturn fbEventTramp(OSObject *self, void *ref, IOFramebuffer *fb, IOIndex event, void *info)
{
    (void)ref;
    return ((RageLTDevice *)self)->fbEvent(fb, event, info);
}

void RageLTDevice::modeStart(void)
{
    uint32_t i;
    fModeGen = 0; fModeDepth = 0; fNumFB = 0; fFBPublish = 0;
    for (i = 0; i < RAGE_MAX_FB; i++) { fFBs[i] = 0; fFBNotify[i] = 0; }
    fDisp.base = fDisp.pitch_px = fDisp.w = fDisp.h = fDisp.bpp = fDisp.depth = 0;
    fFBPublish = addNotification(gIOPublishNotification, serviceMatching("IOFramebuffer"), (IOServiceNotificationHandler)&fbPublishedTramp, this, 0);
    if (!fFBPublish) LOG("no IOFramebuffer publish notification (mode changes will not be tracked)\n");
}

void RageLTDevice::modeStop(void)
{
    uint32_t i;
    if (fFBPublish) { fFBPublish->remove(); fFBPublish = 0; }
    for (i = 0; i < fNumFB; i++) if (fFBNotify[i]) { fFBNotify[i]->remove(); fFBNotify[i] = 0; }
    fNumFB = 0;
}

bool RageLTDevice::fbPublished(IOService *svc)
{
    IOFramebuffer *fb = OSDynamicCast(IOFramebuffer, svc);
    IOService *s; uint32_t i; bool have = false;
    if (!fb || !fLock) return true;
    for (s = fb; s && s != fPCI; s = s->getProvider()) ;
    if (!s) return true;                                       /* another card's framebuffer */
    IOLockLock(fLock);
    for (i = 0; i < fNumFB; i++) if (fFBs[i] == fb) have = true;
    if (!have && fNumFB < RAGE_MAX_FB) {
        IONotifier *n = fb->addFramebufferNotification(&fbEventTramp, this, 0);
        if (n) { fFBs[fNumFB] = fb; fFBNotify[fNumFB] = n; publishFramebuffer(fb, fNumFB); fNumFB++; }
        LOG("tracking mode changes of %s (%s)\n", fb->getName(), n ? "ok" : "FAILED");
    }
    IOLockUnlock(fLock);
    return true;
}

IOReturn RageLTDevice::fbEvent(IOFramebuffer *fb, IOIndex event, void *info)
{
    bool will = (event == kIOFBNotifyDisplayModeWillChange) || (event == kIOFBNotifyWillSleep && !info);
    bool did  = (event == kIOFBNotifyDisplayModeDidChange)  || (event == kIOFBNotifyDidWake && !info);
    (void)fb;
    if (will) { if (OSIncrementAtomic((SInt32 *)&fModeDepth) == 0) modeWillChange(); }
    else if (did) { if (fModeDepth > 0 && OSDecrementAtomic((SInt32 *)&fModeDepth) == 1) modeDidChange(); }
    return kIOReturnSuccess;
}

void RageLTDevice::modeWillChange(void)
{
    uint32_t k;
    OSIncrementAtomic((SInt32 *)&fModeGen);                     /* odd: changing */
    IOLockLock(fLock);
    if (fEngineLive) (void)idleWait();                /* drain; a stuck engine is reset at the next engineEnable */
    for (k = 0; k < RAGE_MAX_CTX; k++) if (fCtx[k].owner) { fCtx[k].tgt_bpp = 0; fCtx[k].z_set = 0; }   /* every cached offset is stale after the framebuffer re-lays VRAM */
    if (fDispReserved) {
        rage_vram_unreserve(&fVram, fDisp.base);
        rage_vram_unreserve(&fVram, 0x770000u);
        fDispReserved = false;
    }
    IOLockUnlock(fLock);
    logEvent(RAGE_EV_MODE_WILL, 0, fModeGen, 0, 0, 0, 0);
}

void RageLTDevice::modeDidChange(void)
{
    if (fEngineLive || true) {
        uint32_t bus = eng_rd(fRegs, MACH64_BUS_CNTL);
        if (!(bus & (1u << 27))) eng_wr(fRegs, MACH64_BUS_CNTL, bus | (1u << 27));        /* BUS_EXT_REG_EN (ledger L10) */
        fEngineLive = false;
        engineEnable();
        reserveDisplay();
    }
    OSIncrementAtomic((SInt32 *)&fModeGen);                     /* even: new layout */
    logEvent(RAGE_EV_MODE_DID, 0, fModeGen, fDisp.base, fDisp.pitch_px, (fDisp.w << 16) | (fDisp.h & 0xffffu), fDisp.bpp);
}

/* Reads the CRTC.  Only after the engine was enabled (the chip is known to be initialised). */
IOReturn RageLTDevice::modeRegsRead(void)
{
    uint32_t gen = eng_rd(fRegs, RAGE_R_CRTC_GEN_CNTL), op = eng_rd(fRegs, MACH64_CRTC_OFF_PITCH), vtd = eng_rd(fRegs, MACH64_CRTC_V_TOTAL_DISP);
    uint32_t htd = eng_rd(fRegs, RAGE_R_CRTC_H_TOTAL_DISP);
    uint32_t depth = (gen >> 8) & 7u;
    fDisp.depth = depth;
    fDisp.bpp = depth == 2 ? 1 : (depth == 3 || depth == 4) ? 2 : depth == 5 ? 3 : 4;
    fDisp.pitch_px = ((op >> 22) & 0x3ffu) * 8u;
    fDisp.base = (op & 0xfffffu) * 8u;
    fDisp.h = ((vtd >> 16) & 0x7ffu) + 1u;
    fDisp.w = (((htd >> 16) & 0xffu) + 1u) * 8u;
    return kIOReturnSuccess;
}

IOReturn RageLTDevice::displayInfo(struct rage_display *out, uint32_t *gen)
{
    IOLockLock(fLock);
    *out = fDisp; *gen = fModeGen;
    IOLockUnlock(fLock);
    return kIOReturnSuccess;
}

/* Target -> scanout, clipped on both sides.  Raw copy: the target must have the scanout's pixel size (16->16 and 32->32; depth conversion / scaled present are
 * RAGE_FEAT_FS_SCALE work). */
#ifndef RAGE_OP_DONE
#ifdef RAGE_SYNC_OPS
#define RAGE_OP_DONE(seq) waitFence(seq)
#else
#define RAGE_OP_DONE(seq) ((void)(seq), kIOReturnSuccess)
#endif
#endif
/* Present, general form.  Same-format 1:1 = the 2D engine's raw blit.  Anything else (the target and the scanout differ in pixel size, or the destination rectangle is a different size
 * = scaled present) = a textured quad of the 3D engine reading the target as a SQUARE power-of-two texture (the bundle allocates the target with a power-of-two pitch when it needs this).
 * The engine converts between pixel formats itself.  scale3d: nearest for the 1:1 conversion, bilinear when scaling and filter != 0.  Both write the scanout only inside the
 * clipped destination rectangle. */
IOReturn RageLTDevice::presentEx(uint32_t owner, uint32_t sx, uint32_t sy, uint32_t w, uint32_t h, uint32_t dx, uint32_t dy, uint32_t dw, uint32_t dh, uint32_t filter)
{
    struct rage_ctx c; struct rage_display d; IOReturn rc; uint32_t seq, pixw; int scaled = (w != dw || h != dh);
    if (!fEngineLive || (fModeGen & 1u)) return kIOReturnNotReady;
    IOLockLock(fLock);
    { struct rage_ctx *p = ctxFor(owner, false); if (!p || !p->tgt_bpp) { IOLockUnlock(fLock); return kIOReturnNotReady; } c = *p; }
    d = fDisp;
    if (!fDispReserved || (d.depth != 3 && d.depth != 4 && d.depth != 6)) { IOLockUnlock(fLock); return kIOReturnUnsupported; }
    pixw = d.depth == 6 ? RAGE_PIXW_8888 : d.depth == 3 ? RAGE_PIXW_555 : RAGE_PIXW_565;
    if (d.bpp == 2 && c.tgt_bpp == 2 && (c.tgt_555 != 0) != (d.depth == 3)) { IOLockUnlock(fLock); return kIOReturnNotReady; }   /* the mode changed depth under this target: the client must rebuild it */
    if (!w || !h || !dw || !dh) { IOLockUnlock(fLock); return kIOReturnSuccess; }
    if (sx >= c.tgt_w || sy >= c.tgt_h || dx >= d.w || dy >= d.h) { IOLockUnlock(fLock); return kIOReturnSuccess; }
    if (w > c.tgt_w - sx || h > c.tgt_h - sy) { IOLockUnlock(fLock); return kIOReturnBadArgument; }    /* the source rectangle must lie inside the target */
    if (!scaled && c.tgt_bpp == d.bpp) {
        /* same-format raw copy: clip the source to the target and the destination to the scanout (both shift the other side to match) */
        if (w > d.w - dx) w = d.w - dx;
        if (h > d.h - dy) h = d.h - dy;
        if (idleWait()) { IOLockUnlock(fLock); return kIOReturnTimeout; }
        rc = (IOReturn)eng_blit(fRegs, pixw, c.tgt_off, c.tgt_pitch, d.base, d.pitch_px, sx, sy, dx, dy, w, h, ((d.w - 1u) << 16), ((d.h - 1u) << 16));
    } else {
        uint32_t wl = 0, texfmt = c.tgt_bpp == 4 ? 6u : (c.tgt_555 ? 3u : 4u), right, bottom;
        int32_t ub = fQuadU, vb = fQuadV, xb = fQuadX;
        while ((1u << wl) < c.tgt_pitch) wl++;
        if ((1u << wl) != c.tgt_pitch || wl < 3 || wl > 10 || c.tgt_h > c.tgt_pitch || dw > 4000u || dh > 4000u || dx > 4000u || dy > 4000u) { IOLockUnlock(fLock); return kIOReturnUnsupported; }
        right = dx + dw; if (right > d.w) right = d.w;
        bottom = dy + dh; if (bottom > d.h) bottom = d.h;
        if (idleWait()) { IOLockUnlock(fLock); return kIOReturnTimeout; }
        rc = (IOReturn)eng_texquad(fRegs, pixw, d.base, d.pitch_px, texfmt, c.tgt_off, wl,
                                   (int32_t)(dx * 4u) + xb, (int32_t)(dy * 4u) + xb, (int32_t)((dx + dw) * 4u) + xb, (int32_t)((dy + dh) * 4u) + xb,
                                   (int32_t)(sx * 256u) + ub, (int32_t)(sy * 256u) + vb, (int32_t)((sx + w) * 256u) + ub, (int32_t)((sy + h) * 256u) + vb,
                                   ((right - 1u) << 16) | dx, ((bottom - 1u) << 16) | dy, ((scaled && filter) ? (RAGE_SCALE3D_TEX_BILINEAR | (fQuadBlend ? RAGE_SCALE3D_TEX_BLEND2 : 0u)) : RAGE_SCALE3D_TEX_NEAREST) ^ fQuadScXor, fQuadTcXor);
    }
    if (rc) { IOLockUnlock(fLock); return kIOReturnTimeout; }
    seq = ++fFenceSubmitted;
    rc = RAGE_OP_DONE(seq);
    IOLockUnlock(fLock);
    return rc;
}

IOReturn RageLTDevice::present(uint32_t owner, uint32_t sx, uint32_t sy, uint32_t w, uint32_t h, uint32_t dx, uint32_t dy)
{ return presentEx(owner, sx, sy, w, h, dx, dy, w, h, 0); }

void RageLTDevice::quadConfig(uint32_t key, uint32_t value)     /* experiment knobs of the textured-quad present (signed values): 3 = texel bias Q8 (S), 4 = (T), 5 = vertex bias Q2 */
{
    if (key == 3) fQuadU = (int32_t)value; else if (key == 4) fQuadV = (int32_t)value; else if (key == 5) fQuadX = (int32_t)value; else if (key == 6) fQuadBlend = value != 0; else if (key == 7) fQuadTcXor = value; else if (key == 8) fQuadScXor = value;
}

/* ---- VRAM copies: byte image of the user buffer == byte image in VRAM (no hidden endian assumption); flags may swap 16/32-bit units ---- */
static inline uint32_t xfer_word(const uint8_t *p, uint32_t flags)
{
    uint32_t b0 = p[0], b1 = p[1], b2 = p[2], b3 = p[3];
    if (flags & RAGE_XFER_SWAP32) { uint32_t t; t = b0; b0 = b3; b3 = t; t = b1; b1 = b2; b2 = t; }
    if (flags & RAGE_XFER_SWAP16) { uint32_t t; t = b0; b0 = b1; b1 = t; t = b2; b2 = b3; b3 = t; }
    return b0 | (b1 << 8) | (b2 << 16) | (b3 << 24);          /* little-endian assembly: byte i of the buffer lands at VRAM byte i */
}
static inline void unxfer_word(uint8_t *p, uint32_t v, uint32_t flags)
{
    uint32_t b0 = v & 0xff, b1 = (v >> 8) & 0xff, b2 = (v >> 16) & 0xff, b3 = v >> 24;
    if (flags & RAGE_XFER_SWAP16) { uint32_t t; t = b0; b0 = b1; b1 = t; t = b2; b2 = b3; b3 = t; }
    if (flags & RAGE_XFER_SWAP32) { uint32_t t; t = b0; b0 = b3; b3 = t; t = b1; b1 = b2; b2 = t; }
    p[0] = (uint8_t)b0; p[1] = (uint8_t)b1; p[2] = (uint8_t)b2; p[3] = (uint8_t)b3;
}

void rage_xfer_inplace(uint8_t *p, uint32_t len, uint32_t flags)
{
    uint32_t i;
    for (i = 0; i + 4 <= len; i += 4) OSWriteLittleInt32((void *)p, i, xfer_word(p + i, flags));
}

IOReturn RageLTDevice::vramCopyIn(uint32_t owner, uint32_t off, const uint8_t *src, uint32_t len, uint32_t flags)
{
    volatile uint8_t *vram = (volatile uint8_t *)fMap->getVirtualAddress(); uint32_t i; IOReturn rc = kIOReturnBadArgument;
    if ((off | len) & 3u || !len || len > 0x100000u || (flags & ~3u)) return kIOReturnBadArgument;
    IOLockLock(fLock);
    if (rage_vram_owns(&fVram, off, len, owner)) {
        rc = kIOReturnSuccess;
        if (fEngineLive && idleWait()) rc = kIOReturnTimeout;      /* the CPU must not touch the aperture while the engine writes it */
        else {
            for (i = 0; i < len; i += 4) OSWriteLittleInt32((void *)vram, off + i, xfer_word(src + i, flags));
            __asm__ volatile("sync" ::: "memory");
            (void)OSReadLittleInt32((void *)vram, off + len - 4);       /* read back from the last word written: posted writes must have reached VRAM before the engine reads them as texels */
            __asm__ volatile("sync" ::: "memory");
        }
    }
    IOLockUnlock(fLock);
    return rc;
}

IOReturn RageLTDevice::vramCopyOut(uint32_t owner, uint32_t off, uint8_t *dst, uint32_t len, uint32_t flags)
{
    volatile uint8_t *vram = (volatile uint8_t *)fMap->getVirtualAddress(); uint32_t i; IOReturn rc = kIOReturnBadArgument;
    if ((off | len) & 3u || !len || len > 0x100000u || (flags & ~3u)) return kIOReturnBadArgument;
    IOLockLock(fLock);
    if (rage_vram_owns(&fVram, off, len, owner)) {
        rc = kIOReturnSuccess;
        if (fEngineLive && idleWait()) rc = kIOReturnTimeout;
        else {
            /* The engine's destination pixel cache reaches the aperture view only after a CPU write to VRAM (measured Nov 20: without it a readback shows the
             * previous frame).  Rewrite a word the engine never writes, NOT the data being read (rewriting a word the engine still caches would lose it). */
            if (fScratch) { uint32_t t = OSReadLittleInt32((void *)vram, fScratch); OSWriteLittleInt32((void *)vram, fScratch, t); __asm__ volatile("sync" ::: "memory"); (void)OSReadLittleInt32((void *)vram, fScratch); (void)idleWait(); }
            for (i = 0; i < len; i += 4) unxfer_word(dst + i, OSReadLittleInt32((void *)vram, off + i), flags);
            if (len >= 4) (void)OSReadLittleInt32((void *)vram, off);
        }
    }
    IOLockUnlock(fLock);
    return rc;
}

#if RAGE_DEBUG_ENGTEST
/* READ-ONLY, debug builds: a FIXED list (the caller cannot choose addresses).  Block-1 overlay/scaler registers sit at window offsets idx*4 (below the block-0 base 0x400);
 * block-0 entries give the mode context.  Nothing here writes, and no engine wait is needed (plain register reads). */
IOReturn RageLTDevice::regDump(uint32_t *pairs, uint32_t maxPairs, uint32_t *n)
{
    static const uint16_t b1[] = { 0x00,0x01,0x02,0x03,0x04,0x05,0x06,0x07,0x08,0x09,0x0a,0x0b,0x0c,0x0d,0x0e,0x0f,0x10,0x11,0x12,0x13,0x14,0x15, 0x54,0x55,0x56,0x57,0x58,0x59 };
    static const uint16_t b0[] = { 0x00, 0x08, 0x14, 0x1c, 0xb0, 0xe0 };   /* CRTC_H_TOTAL_DISP, CRTC_V_TOTAL_DISP, CRTC_OFF_PITCH, CRTC_GEN_CNTL, MEM_CNTL, CONFIG_CHIP_ID */
    uint32_t i, k = 0;
    if (maxPairs < sizeof b1 / 2 + sizeof b0 / 2 + 1) return kIOReturnNoSpace;
    IOLockLock(fLock);
    for (i = 0; i < sizeof b1 / 2; i++) { pairs[2 * k] = 0x100u | b1[i]; pairs[2 * k + 1] = OSReadLittleInt32((volatile void *)fRegs, b1[i] * 4u); k++; }
    for (i = 0; i < sizeof b0 / 2; i++) { pairs[2 * k] = b0[i]; pairs[2 * k + 1] = OSReadLittleInt32((volatile void *)fRegs, 0x400u + b0[i]); k++; }
    pairs[2 * k] = 0xf00u; pairs[2 * k + 1] = fPCI->configRead32(0x08); k++;
    IOLockUnlock(fLock);
    *n = k;
    return kIOReturnSuccess;
}

/* Overlay plan step B0, the ONLY register write of the step: OVERLAY_VIDEO_KEY_CLR (block 1, window offset 0x008) is a colour-key compare value with no visible effect while the overlay is
 * disabled (SCALE_CNTL = 0, KEY_CNTL = 0; checked below).  Write 0x005A5A5A, read it back, write the original value back, read again.  Proves block 1 latches writes. */
IOReturn RageLTDevice::ovlB0(uint32_t *before, uint32_t *readback, uint32_t *after)
{
    const uint32_t kKeyClr = 0x008, kScaleCntl = 0x024, kKeyCntl = 0x018;
    IOReturn rc = kIOReturnSuccess; uint32_t orig;
    IOLockLock(fLock);
    if (OSReadLittleInt32((volatile void *)fRegs, kScaleCntl) != 0 || OSReadLittleInt32((volatile void *)fRegs, kKeyCntl) != 0) rc = kIOReturnNotPermitted;   /* the overlay must be idle */
    else {
        orig = OSReadLittleInt32((volatile void *)fRegs, kKeyClr);
        *before = orig;
        OSWriteLittleInt32((volatile void *)fRegs, kKeyClr, 0x005A5A5Au); eieio();
        *readback = OSReadLittleInt32((volatile void *)fRegs, kKeyClr);
        OSWriteLittleInt32((volatile void *)fRegs, kKeyClr, orig); eieio();
        *after = OSReadLittleInt32((volatile void *)fRegs, kKeyClr);
    }
    IOLockUnlock(fLock);
    return rc;
}
#endif

IOReturn RageLTDevice::scanoutCopyOut(uint32_t byteOff, uint8_t *dst, uint32_t len)
{
    volatile uint8_t *vram = (volatile uint8_t *)fMap->getVirtualAddress(); uint32_t i; IOReturn rc = kIOReturnBadArgument;
    uint64_t size;
    if ((byteOff | len) & 3u || !len || len > 0x100000u) return kIOReturnBadArgument;
    IOLockLock(fLock);
    size = (uint64_t)fDisp.pitch_px * fDisp.h * fDisp.bpp;
    if (fDispReserved && byteOff + (uint64_t)len <= size && rage_vram_span_ok(fDisp.base + byteOff, len)) {
        rc = kIOReturnSuccess;
        if (idleWait()) rc = kIOReturnTimeout;
        else for (i = 0; i < len; i += 4) unxfer_word(dst + i, OSReadLittleInt32((void *)vram, fDisp.base + byteOff + i), 0);
    }
    IOLockUnlock(fLock);
    return rc;
}

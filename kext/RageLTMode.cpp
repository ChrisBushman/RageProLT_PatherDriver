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
    rage_pm_init(&fPm); fAperEndianBad = false; fPmLock = IOLockAlloc();
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
    if (fPmLock) { IOLockFree(fPmLock); fPmLock = 0; }
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
    uint32_t inf = (uint32_t)(uintptr_t)info; int a;
    bool will = (event == kIOFBNotifyDisplayModeWillChange) || (event == kIOFBNotifyWillSleep && !inf);
    bool did  = (event == kIOFBNotifyDisplayModeDidChange)  || (event == kIOFBNotifyDidWake && !inf);
    (void)fb;
#if RAGE_FEAT_SLEEPWAKE
    if (event == kIOFBNotifyWillSleep && inf) { systemWillSleep(); return kIOReturnSuccess; }      /* the NDRV's chip suspend pair (plan 2.2): RageLTPower.cpp */
    if (event == kIOFBNotifyDidWake && inf)   { systemDidWake();   return kIOReturnSuccess; }
    if (!will && !did) { logEvent(RAGE_EV_MARK, 0, 0xe0050000u | ((uint32_t)event & 0xffffu), inf, 0, 0, 0); return kIOReturnSuccess; }   /* events 5-10: not ours (same as stock) */
#endif
    if (will) {
        IOLockLock(fPmLock); a = rage_pm_will(&fPm); IOLockUnlock(fPmLock);
        if (a & RAGE_PM_RUN_WILL) modeWillChange();
    } else if (did) {
        IOLockLock(fPmLock); a = rage_pm_did(&fPm); IOLockUnlock(fPmLock);
        if (a & RAGE_PM_RUN_DID) modeDidChange();
        else if (a & RAGE_PM_DUP) logEvent(RAGE_EV_MARK, 0, 0xe0050001u, (uint32_t)event, inf, 0, 0);       /* a did without its will: ignored, visible in the log */
    }
    return kIOReturnSuccess;
}

void RageLTDevice::modeWillChange(void)
{
    uint32_t k;
    OSIncrementAtomic((SInt32 *)&fModeGen);                     /* odd: changing */
    IOLockLock(fLock);
    if (fOvlOwner) { (void)ovlDisableLocked(); fOvlOwner = 0; }     /* the scaler reads VRAM the framebuffer is about to re-lay out */
    if (fEngineLive && chipLive()) (void)idleWait();  /* drain; a stuck engine is reset at the next engineEnable (never while the chip is asleep) */
    for (k = 0; k < RAGE_MAX_CTX; k++) if (fCtx[k].owner) { fCtx[k].tgt_bpp = 0; fCtx[k].z_set = 0; }   /* every cached offset is stale after the framebuffer re-lays VRAM */
    if (fDispReserved) {
        rage_vram_unreserve(&fVram, fDisp.base);
        if (fDisp.base) rage_vram_unreserve(&fVram, 0);     /* the block below the frame buffer */
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
/* v-sync experiment: poll CRTC_VLINE_CRNT_VLINE until the beam enters vertical blank (line >= V_DISP), at most 40 ms.  Called with fLock held before the present blit so the
 * engine copy runs while (and ahead of) the scan-out.  Read-only register polling (the chip is live: the engine is enabled).  Totals are published as ioreg properties. */
void RageLTDevice::vsyncWait(void)
{
    AbsoluteTime t0, t1; uint64_t n0, n1; uint32_t vd, line, spins = 0, entryLine, nsl = 0; bool inVbl, timedOut = false;
    if (!fVsync) return;
    if (fVsync == 2) {      /* granularity probe for a sleep-then-spin wait: how long does IOSleep(1) really take on this kernel?  (no vblank wait in this mode) */
        uint32_t k; uint64_t tot = 0, mx = 0;
        for (k = 0; k < 20; k++) { AbsoluteTime a, b; uint64_t na, nb; clock_get_uptime(&a); absolutetime_to_nanoseconds(a, &na); IOSleep(1); clock_get_uptime(&b); absolutetime_to_nanoseconds(b, &nb); tot += nb - na; if (nb - na > mx) mx = nb - na; }
        setProperty("RageIOSleep1AvgUs", (unsigned long long)(tot / 20000ull), 32); setProperty("RageIOSleep1MaxUs", (unsigned long long)(mx / 1000ull), 32);
        fVsync = 0; return;
    }
    clock_get_uptime(&t0); absolutetime_to_nanoseconds(t0, &n0);
    vd = ((eng_rd(fRegs, MACH64_CRTC_V_TOTAL_DISP) >> 16) & 0x7ffu) + 1u;
    line = (eng_rd(fRegs, MACH64_CRTC_VLINE_CRNT_VLINE) >> 16) & 0x7ffu;
    inVbl = line >= vd; entryLine = line;
    if (fVsync == 3 && !inVbl) {         /* sleep-then-spin: IOSleep(1) steps while more than fVsyncMarginUs remain (key 10, default 2500), then poll; line rate (lines per ms x 1000) re-measured on every wake */
        uint32_t vt = (eng_rd(fRegs, MACH64_CRTC_V_TOTAL_DISP) & 0x7ffu) + 1u, last = line, sleeps = 0; uint64_t rate = (uint64_t)vt * 1000ull / 15ull, lastNs = n0;
        for (;;) {
            uint64_t remainUs = (uint64_t)(vd - line) * 1000000ull / rate;
            if (remainUs <= (uint64_t)fVsyncMarginUs || sleeps > 20) break;
            IOSleep(1); sleeps++;
            clock_get_uptime(&t1); absolutetime_to_nanoseconds(t1, &n1);
            line = (eng_rd(fRegs, MACH64_CRTC_VLINE_CRNT_VLINE) >> 16) & 0x7ffu;
            if (line >= vd) { inVbl = true; break; }
            if (line > last && n1 > lastNs) rate = (uint64_t)(line - last) * 1000000000ull / (n1 - lastNs);
            last = line; lastNs = n1;
        }
        fVsyncSleeps += sleeps; nsl = sleeps;
        setProperty("RageVsyncSleeps", (unsigned long long)fVsyncSleeps, 32);
    }
    while (!inVbl) {
        line = (eng_rd(fRegs, MACH64_CRTC_VLINE_CRNT_VLINE) >> 16) & 0x7ffu;
        if (line >= vd) { uint32_t l2 = (eng_rd(fRegs, MACH64_CRTC_VLINE_CRNT_VLINE) >> 16) & 0x7ffu, vt = (eng_rd(fRegs, MACH64_CRTC_V_TOTAL_DISP) & 0x7ffu) + 1u;
            if (l2 >= vd && l2 <= vt && l2 >= line) break; }       /* a glitched single read (seen: 0x1ff) must not end the wait: confirm with a second read, in range and not going backwards */
        if ((++spins & 255u) == 0) { clock_get_uptime(&t1); absolutetime_to_nanoseconds(t1, &n1); if (n1 - n0 > 40000000ull) { timedOut = true; break; } }
    }
    clock_get_uptime(&t1); absolutetime_to_nanoseconds(t1, &n1);
    { static uint32_t ring = 0; char nm[24]; nm[0]='R';nm[1]='V';nm[2]='r';nm[3]='0'+(char)(ring%8);nm[4]=0; ring++;
      setProperty(nm, (unsigned long long)(((unsigned long long)entryLine << 48) | ((unsigned long long)nsl << 40) | ((unsigned long long)line << 24) | ((n1 - n0) / 1000ull & 0xffffffull)), 64); }
    fVsyncCalls++; if (inVbl) fVsyncInVbl++; if (timedOut) fVsyncTimeouts++; fVsyncNs += n1 - n0;
    setProperty("RageVsyncCalls", (unsigned long long)fVsyncCalls, 32); setProperty("RageVsyncAlreadyInVblank", (unsigned long long)fVsyncInVbl, 32);
    setProperty("RageVsyncTimeouts", (unsigned long long)fVsyncTimeouts, 32); setProperty("RageVsyncWaitUs", (unsigned long long)(fVsyncNs / 1000ull), 64);
}

/* The 3D engine's destination pixel cache is not written back to VRAM when the engine goes idle; only a CPU write into VRAM flushes it (measured Nov 20, see vramCopyOut).  A present
 * blit / quad reads the render target as its source, so it needs the same flush after idleWait(): without it the last-drawn pixels of frame N are still in the cache and the screen
 * gets frame N-1's pixels there (small stale blobs, 3 rows x 10-35 px, scattered and timing dependent -- the DVD video "thin lines").  Rewrite a word the engine never writes. */
#define RAGE_FLUSH_DST_CACHE() do { if (fScratch) { volatile uint8_t *v_ = (volatile uint8_t *)fMap->getVirtualAddress(); uint32_t t_ = OSReadLittleInt32((void *)v_, fScratch); \
    OSWriteLittleInt32((void *)v_, fScratch, t_); __asm__ volatile("sync" ::: "memory"); (void)OSReadLittleInt32((void *)v_, fScratch); __asm__ volatile("sync" ::: "memory"); } } while (0)

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
        RAGE_FLUSH_DST_CACHE();
        vsyncWait();
        rc = (IOReturn)eng_blit(fRegs, pixw, c.tgt_off, c.tgt_pitch, d.base, d.pitch_px, sx, sy, dx, dy, w, h, ((d.w - 1u) << 16), ((d.h - 1u) << 16));
    } else {
        uint32_t wl = 0, texfmt = c.tgt_bpp == 4 ? 6u : (c.tgt_555 ? 3u : 4u), right, bottom;
        int32_t ub = fQuadU, vb = fQuadV, xb = fQuadX;
        while ((1u << wl) < c.tgt_pitch) wl++;
        if ((1u << wl) != c.tgt_pitch || wl < 3 || wl > 10 || c.tgt_h > c.tgt_pitch || dw > 4000u || dh > 4000u || dx > 4000u || dy > 4000u) { IOLockUnlock(fLock); return kIOReturnUnsupported; }
        right = dx + dw; if (right > d.w) right = d.w;
        bottom = dy + dh; if (bottom > d.h) bottom = d.h;
        if (idleWait()) { IOLockUnlock(fLock); return kIOReturnTimeout; }
        RAGE_FLUSH_DST_CACHE();
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

/* Copy the whole render target into another block of the same owner (same pitch, size and format as the target) and make that block the render target: one 2D-engine blit
 * between two VRAM surfaces, the same hardware pattern as the 1:1 present.  Used by the overlay present: after the scaler is pointed at the finished frame the other block
 * receives a copy and becomes the target, so the target still holds the frame after a present, exactly like the quad present (apps and tests may read it back or draw on top
 * of it).  Scissor, Z binding and all other context state are untouched (SET_TARGET would reset them). */
IOReturn RageLTDevice::swapTarget(uint32_t owner, uint32_t dstOff)
{
    struct rage_ctx c; IOReturn rc; uint32_t pixw, bytes;
    if (!fEngineLive || (fModeGen & 1u)) return kIOReturnNotReady;
    IOLockLock(fLock);
    { struct rage_ctx *p = ctxFor(owner, false); if (!p || !p->tgt_bpp) { IOLockUnlock(fLock); return kIOReturnNotReady; } c = *p; }
    bytes = c.tgt_bpp == 4 ? 4u : 2u;
    if ((dstOff & 7u) || dstOff == c.tgt_off || !c.tgt_w || !c.tgt_h || (c.tgt_bpp != 2 && c.tgt_bpp != 4) ||
        !rage_vram_owns(&fVram, dstOff, (uint64_t)c.tgt_pitch * bytes * c.tgt_h, owner)) { IOLockUnlock(fLock); return kIOReturnBadArgument; }
    pixw = c.tgt_bpp == 4 ? RAGE_PIXW_8888 : c.tgt_555 ? RAGE_PIXW_555 : RAGE_PIXW_565;
    if (idleWait()) { IOLockUnlock(fLock); return kIOReturnTimeout; }
    rc = (IOReturn)eng_blit(fRegs, pixw, c.tgt_off, c.tgt_pitch, dstOff, c.tgt_pitch, 0, 0, 0, 0, c.tgt_w, c.tgt_h, ((c.tgt_w - 1u) << 16), ((c.tgt_h - 1u) << 16));
    if (rc) { IOLockUnlock(fLock); return kIOReturnTimeout; }
    { struct rage_ctx *p = ctxFor(owner, false); if (p) p->tgt_off = dstOff; }        /* the other block is the target from now on (the copy above is in flight; later submits are ordered after it) */
    (void)++fFenceSubmitted;
    rc = RAGE_OP_DONE(fFenceSubmitted);
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
    if (!chipOK() || fAperEndianBad) { IOLockUnlock(fLock); return kIOReturnNotReady; }      /* sleep / mode change / swapped aperture: no chip or aperture access */
    if (rage_vram_owns(&fVram, off, len, owner)) {
        rc = kIOReturnSuccess;
        if (fEngineLive && idleWait()) rc = kIOReturnTimeout;      /* the CPU must not touch the aperture while the engine writes it */
        else {
            volatile uint8_t *wv = fWCMap ? (volatile uint8_t *)fWCMap->getVirtualAddress() : vram;
            for (i = 0; i < len; i += 4) OSWriteLittleInt32((void *)wv, off + i, xfer_word(src + i, flags));
            __asm__ volatile("sync" ::: "memory");
            (void)OSReadLittleInt32((void *)vram, off + len - 4);       /* read back from the last word written: posted writes must have reached VRAM before the engine reads them as texels */
            __asm__ volatile("sync" ::: "memory");
        }
    }
    IOLockUnlock(fLock);
    return rc;
}

IOReturn RageLTDevice::vramCopyRows(uint32_t owner, uint32_t off, const uint8_t *src, uint32_t rowBytes, uint32_t nrows, uint32_t dstPitch, uint32_t flags)
{
    volatile uint8_t *vram = (volatile uint8_t *)fMap->getVirtualAddress(); uint32_t i, r; IOReturn rc = kIOReturnBadArgument;
    if ((off | rowBytes | dstPitch) & 3u || !rowBytes || !nrows || nrows > 1024u || rowBytes > 4112u || dstPitch < rowBytes || (flags & ~3u)) return kIOReturnBadArgument;
    IOLockLock(fLock);
    if (!chipOK() || fAperEndianBad) { IOLockUnlock(fLock); return kIOReturnNotReady; }      /* sleep / mode change / swapped aperture: no chip or aperture access */
    if (rage_vram_owns(&fVram, off, (uint64_t)(nrows - 1u) * dstPitch + rowBytes, owner)) {
        rc = kIOReturnSuccess;
        if (fEngineLive && idleWait()) rc = kIOReturnTimeout;      /* the CPU must not touch the aperture while the engine writes it */
        else {
            volatile uint8_t *wv = fWCMap ? (volatile uint8_t *)fWCMap->getVirtualAddress() : vram;
            for (r = 0; r < nrows; r++)
                for (i = 0; i < rowBytes; i += 4) OSWriteLittleInt32((void *)wv, off + r * dstPitch + i, xfer_word(src + (size_t)r * rowBytes + i, flags));
            __asm__ volatile("sync" ::: "memory");
            (void)OSReadLittleInt32((void *)vram, off + (nrows - 1u) * dstPitch + rowBytes - 4u);     /* read back from the last word written: posted writes must have reached VRAM before the engine reads them */
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
    if (!chipOK() || fAperEndianBad) { IOLockUnlock(fLock); return kIOReturnNotReady; }      /* sleep / mode change / swapped aperture: no chip or aperture access */
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

#if RAGE_FEAT_OVERLAY
/* Hardware overlay scaler (RAGE_FEAT_OVERLAY): one client-owned buffer shown stretched over the scanout.  Block 1 registers at window offset idx*4.  Write order and values: docs/re-notes/overlay-scaler-plan.md section 2
 * (approved list).  NOT written: bit 7 of SCALE_CNTL, the CAPTURE_CONFIG buffer bits, any block-0/CRTC/PLL/engine register, Win98's 0x8000211 key value. */
static inline void ovlw(volatile uint8_t *regs, uint32_t off, uint32_t v) { OSWriteLittleInt32((volatile void *)regs, off, v); eieio(); }
IOReturn RageLTDevice::ovlDisableLocked(void)
{
    if (!chipLive()) return kIOReturnNotReady;         /* asleep: the overlay was disabled before the chip suspended; never write a suspended chip */
    ovlw(fRegs, 0x018, 0x110u);          /* OVERLAY_KEY_CNTL: video never shown */
    ovlw(fRegs, 0x024, 0u);              /* OVERLAY_SCALE_CNTL: scaler and overlay off */
    return kIOReturnSuccess;
}
IOReturn RageLTDevice::ovlOff(uint32_t owner)
{
    IOReturn rc = kIOReturnNotPermitted;
    IOLockLock(fLock);
    if (fOvlOwner && fOvlOwner == owner) { rc = ovlDisableLocked(); fOvlOwner = 0; }
    IOLockUnlock(fLock);
    return rc;
}
IOReturn RageLTDevice::ovlOn(uint32_t owner, uint32_t off, uint32_t pitch_px, uint32_t sw, uint32_t sh, uint32_t dx, uint32_t dy, uint32_t dw, uint32_t dh, uint32_t bpp, uint32_t flags)
{
    IOReturn rc = kIOReturnBadArgument; uint32_t hinc, vinc, fmt, scalerIn, scaleCntl = 0xc0000000u, coefSet, cf0, cf1, cf2, cf3, cf4, bytes;
    if (flags & ~0x1fu) return kIOReturnBadArgument;
#if !RAGE_DEBUG_ENGTEST
    if (flags || bpp == 3u) return kIOReturnUnsupported;                       /* the interpolation-mode bits are an unproven hypothesis: debug builds only */
#endif
    coefSet = (flags >> 2) & 7u;                               /* debug: 0 = Win98/X defaults, 1 = identity 0x00002000 x5, 2 = flat 50/50 blend 0x00101000 x5, 3 = identity in COEFF0-3 and flat blend in COEFF4 only */
    cf0 = 0x00002000u; cf1 = 0x0d06200du; cf2 = 0x0d0a1c0du; cf3 = 0x0c0e1a0cu; cf4 = 0x0c14140cu;
    if (coefSet == 1u) cf0 = cf1 = cf2 = cf3 = cf4 = 0x00002000u;
    else if (coefSet == 2u) cf0 = cf1 = cf2 = cf3 = cf4 = 0x00101000u;
    else if (coefSet == 3u) { cf0 = cf1 = cf2 = cf3 = 0x00002000u; cf4 = 0x00101000u; }       /* debug: only the last register (COEFF4) is the flat blend */
    else if (coefSet >= 4u && coefSet <= 6u) { cf0 = coefSet == 4u ? 0x00200000u : coefSet == 5u ? 0x20000000u : 0x00000020u; cf1 = cf2 = cf3 = cf4 = 0x00002000u; }   /* debug: COEFF0 = a single weight 0x20 in byte 2 / byte 3 / byte 0, the rest identity */
    else if (coefSet == 7u) return kIOReturnBadArgument;
    scaleCntl |= ((flags & 1u) << 2) | ((flags & 2u) << 2);     /* SCALE_HORZ_MODE (bit 2) / SCALE_VERT_MODE (bit 3) */
    if (!owner || !sw || !sh || !dw || !dh || sw > 720u || (pitch_px & 3u) || pitch_px < sw || (off & 7u) || (bpp < 2u || bpp > 4u) || ((pitch_px * (bpp == 4u ? 4u : 2u)) & 7u)) return kIOReturnBadArgument;   /* quadword-aligned offset and pitch, width limit of this family */
    bytes = bpp == 4u ? 4u : 2u;                                   /* bpp 3 (debug builds only) = packed VYUY 4:2:2 video, 2 bytes per pixel */
    IOLockLock(fLock);
    if (!chipOK()) { IOLockUnlock(fLock); return kIOReturnNotReady; }
    if (fOvlOwner) rc = kIOReturnBusy;
    else if (OSReadLittleInt32((volatile void *)fRegs, 0x024) != 0 ||
             (OSReadLittleInt32((volatile void *)fRegs, 0x018) != 0 && OSReadLittleInt32((volatile void *)fRegs, 0x018) != 0x110u)) rc = kIOReturnNotPermitted;   /* the overlay must be idle: scaler off, key control power-on (0) or our hidden state (0x110) */
    else if (!fDispReserved || dx + dw > fDisp.w || dy + dh > fDisp.h) rc = kIOReturnBadArgument;
    else if (!rage_vram_owns(&fVram, off, (uint64_t)pitch_px * bytes * sh, owner)) rc = kIOReturnNotPermitted;                                    /* the source block belongs to this client */
    else {
        scalerIn = bpp == 4u ? 6u : bpp == 3u ? 0xbu : (fDisp.depth == 3 ? 3u : 4u);               /* SCALER_IN: 32 bpp / 15 bpp 1555 target (15 bit scanout) / 16 bpp 565 target */
        hinc = (sw << 12) / dw; vinc = (sh << 12) / dh;                          /* 12 fractional bits, truncated (PRG 8.8.1) */
        if (hinc == 0 || vinc == 0 || hinc > 0xffffu || vinc > 0xffffu) rc = kIOReturnBadArgument;
        else {
            ovlw(fRegs, 0x018, 0x110u);                                          /* hide while programming */
            ovlw(fRegs, 0x000, (dx << 16) | dy);                                 /* OVERLAY_Y_X_START (lock bit clear) */
            ovlw(fRegs, 0x004, ((dx + dw - 1u) << 16) | (dy + dh - 1u));         /* OVERLAY_Y_X_END */
            ovlw(fRegs, 0x008, 0u); ovlw(fRegs, 0x00c, 0u); ovlw(fRegs, 0x010, 0u); ovlw(fRegs, 0x014, 0u);   /* colour keys unused */
            ovlw(fRegs, 0x020, (hinc << 16) | vinc);                             /* OVERLAY_SCALE_INC */
            ovlw(fRegs, 0x028, (sw << 16) | sh);                                 /* SCALER_HEIGHT_WIDTH */
            ovlw(fRegs, 0x034, off); ovlw(fRegs, 0x038, off); ovlw(fRegs, 0x03c, pitch_px);   /* SCALER_BUF0/BUF1_OFFSET, SCALER_BUF_PITCH (pixels) */
            fmt = OSReadLittleInt32((volatile void *)fRegs, 0x048);
            ovlw(fRegs, 0x048, (fmt & ~0x000f0000u) | (scalerIn << 16));         /* VIDEO_FORMAT.SCALER_IN, other bits preserved */
            ovlw(fRegs, 0x150, 0x00101000u);                                     /* SCALER_COLOUR_CNTL neutral */
            ovlw(fRegs, 0x154, cf0); ovlw(fRegs, 0x158, cf1); ovlw(fRegs, 0x15c, cf2); ovlw(fRegs, 0x160, cf3); ovlw(fRegs, 0x164, cf4);   /* SCALER_H_COEFF0-4 */
            ovlw(fRegs, 0x018, 0x011u);                                          /* OVERLAY_KEY_CNTL: VIDEO_FN true | GRAPHICS_FN true, OR: always show video */
            ovlw(fRegs, 0x024, scaleCntl);                                      /* OVERLAY_SCALE_CNTL: SCALE_EN | OVERLAY_EN (+ the debug mode bits), last */
            fOvlOwner = owner; fOvlPitch = pitch_px; fOvlH = sh; fOvlBpp = bytes; fOvlOff = off; rc = kIOReturnSuccess;
        }
    }
    IOLockUnlock(fLock);
    return rc;
}

IOReturn RageLTDevice::ovlFlip(uint32_t owner, uint32_t off)
{
    IOReturn rc = kIOReturnNotPermitted;
    if (off & 7u) return kIOReturnBadArgument;
    IOLockLock(fLock);
    if (!chipOK()) { IOLockUnlock(fLock); return kIOReturnNotReady; }
    if (fOvlOwner && fOvlOwner == owner) {
        if (!rage_vram_owns(&fVram, off, (uint64_t)fOvlPitch * fOvlBpp * fOvlH, owner)) rc = kIOReturnBadArgument;
        else { ovlw(fRegs, 0x034, off); ovlw(fRegs, 0x038, off); fOvlOff = off; rc = kIOReturnSuccess; }          /* SCALER_BUF0/BUF1_OFFSET */
    }
    IOLockUnlock(fLock);
    return rc;
}
#else
IOReturn RageLTDevice::ovlDisableLocked(void) { return kIOReturnUnsupported; }
IOReturn RageLTDevice::ovlOff(uint32_t) { return kIOReturnUnsupported; }
IOReturn RageLTDevice::ovlOn(uint32_t, uint32_t, uint32_t, uint32_t, uint32_t, uint32_t, uint32_t, uint32_t, uint32_t, uint32_t, uint32_t) { return kIOReturnUnsupported; }
IOReturn RageLTDevice::ovlFlip(uint32_t, uint32_t) { return kIOReturnUnsupported; }
#endif

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
    if (!chipOK()) { IOLockUnlock(fLock); return kIOReturnNotReady; }
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
    if (!chipOK()) { IOLockUnlock(fLock); return kIOReturnNotReady; }
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

#if RAGE_DEBUG_ENGTEST
/* MC experiment stage 1 (docs/re-notes/mc-experiment-plan.md).  One 16x16 luma prediction through the front-end scaler pipe, forward/single source, exactly the register
 * sequence the Win98 MC12 HAL uses (docs/re-notes/lt-pro-mc-analysis.md): init stream (ATIMCRAE FUN_b0103f60) then the per-block words of ATID32AE FUN_b00c6a40's
 * one-way case.  Every value is either a verbatim Win98 constant or computed from the caller's geometry; the caller picks no register and no address: both planes must lie
 * inside the caller's own VRAM blocks, the source block incl. the +1 interpolation row/column must lie inside its plane, and src and dst must not overlap.  Touched
 * registers are saved first and restored afterwards (the other kernel paths write everything they need again, this only keeps the desktop path untouched). */
IOReturn RageLTDevice::mcExp(uint32_t owner, uint32_t srcOff, uint32_t dstOff, uint32_t pitchH, uint32_t xy, uint32_t mv, uint32_t *out)
{
    static const uint16_t kSave[] = { 0x5c0, 0x778, 0x5fc, 0x550, 0x6d0, 0x530, 0x774, 0x708, 0x6c4, 0x5dc, 0x5e0, 0x5f0, 0x5f4, 0x79c, 0x7d0, 0x6a8, 0x6b4, 0x6d8, 0x6d4, 0x500, 0x788, 0x78c, 0x304, 0x54c, 0x6cc, 0x750, 0x740 };
    uint32_t sv[sizeof kSave / sizeof kSave[0]], i;
    uint32_t mv_in = mv, pitch = pitchH & 0xffffu, h = (pitchH >> 16) & 0x3fffu, noRestore = pitchH >> 31, docMode = (pitchH >> 30) & 1u, X = xy & 0xffffu, Y = xy >> 16, size, x2, y2, ax, ay; int32_t mvx = (int16_t)(mv & 0xffffu), mvy = (int16_t)(mv >> 16), sx, sy;
    IOReturn rc = kIOReturnSuccess;
    uint32_t gui = 0, fifo = 0, status = 2;
    *out = 2;
    if ((pitch & 7u) || pitch < 16 || pitch > 1024 || h < 16 || h > 1024 || (dstOff & 7u) || X + 16 > pitch || Y + 16 > h) return kIOReturnBadArgument;
    if (docMode && (mv_in >> 31)) {                  /* diagnosis: log every non-zero register 0x500..0x7fc of the freshly reset engine */
        uint32_t r; for (r = 0x500; r < 0x800; r += 4) { uint32_t v; if (r == 0x738 || r == 0x740 || r == 0x77c) continue; v = eng_rd(fRegs, r); if (v) IOLog("RageLT: MCDUMP %03x = %08x\n", (unsigned)r, (unsigned)v); }
        mv_in = 0;
    }
    if (docMode) { mvx = 0; mvy = 0; }
    sx = mvx + 2 * (int32_t)X; sy = mvy + 2 * (int32_t)Y;
    if (sx < 0 || sy < 0) return kIOReturnBadArgument;
    x2 = (uint32_t)sx; y2 = (uint32_t)sy; ax = x2 >> 1; ay = y2 >> 1;
    if (ax + 17 > pitch || ay + 17 > h) return kIOReturnBadArgument;                     /* the interpolation reads one more column and row */
    size = pitch * h;
    if (srcOff < dstOff ? srcOff + size > dstOff : dstOff + size > srcOff) return kIOReturnBadArgument;
    IOLockLock(fLock);
    if (!chipOK() || (fModeGen & 1u) || !fEngineLive) { IOLockUnlock(fLock); return kIOReturnNotReady; }
    if (!rage_vram_owns(&fVram, srcOff, size, owner) || !rage_vram_owns(&fVram, dstOff, size, owner)) { IOLockUnlock(fLock); return kIOReturnNotPermitted; }
    if (idleWait()) { IOLockUnlock(fLock); return kIOReturnTimeout; }
    if (eng_reset(fRegs, 1)) { IOLockUnlock(fLock); return kIOReturnTimeout; }                 /* diagnosis: a pass leaves the engine so that the NEXT one hangs; start each from a reset engine */
    for (i = 0; i < sizeof kSave / sizeof kSave[0]; i++) sv[i] = eng_rd(fRegs, kSave[i]);
    /* the kernel's standard engine state (what every kernel op writes first; eng_fill): plain integer setup, no Z, all planes writable.  The Win98 stream assumes the 3D driver left these. */
    eng_wq(fRegs, 0x304, 0); eng_wq(fRegs, MACH64_Z_CNTL, 0); eng_wq(fRegs, 0x6cc, 0xffffffffu);
    if (docMode) {                                   /* diagnosis: the DOCUMENTED front-end-scaler blt (RAGE PRO Programmer's Guide 8.9.2), whole 16x16 block copied from the source at the same position, no blend, no MC registers */
        { uint32_t pi, pv = (mv_in >> 24) & 3u;               /* diagnosis: texture palette ramp (variant 1: i*0x010101, 2: i, 3: i<<8) in case the Y8 source is read as a palette index */
          if (pv) { for (pi = 0; pi < 256; pi++) { eng_wq(fRegs, 0x740, pi); eng_wq(fRegs, 0x77c, pv == 1 ? pi * 0x010101u : pv == 2 ? pi : pi << 8); } mv_in &= 0x00ffffffu; } }
        eng_wq(fRegs, 0x550, 0); eng_wq(fRegs, 0x774, 0);                              /* ALPHA_TST_CNTL = 0, TEX_CNTL = 0 */
        eng_wq(fRegs, 0x5fc, mv_in ? mv_in : 0x40u);                                        /* SCALE_3D_CNTL: function = scale (diagnosis: mv overrides) */
        eng_wq(fRegs, 0x5c0, srcOff + Y * pitch + X);                                  /* SCALE_OFF: the block's first byte */
        eng_wq(fRegs, 0x750, pitch);                                                   /* SCALE_PITCH (index 0xd4), pixels */
        eng_wq(fRegs, 0x5dc, 16); eng_wq(fRegs, 0x5e0, 16);                            /* SCALE_WIDTH, SCALE_HEIGHT */
        eng_wq(fRegs, 0x5f0, 0x10000u); eng_wq(fRegs, 0x5f4, 0x10000u);                /* SCALE_X_INC / Y_INC: Win98's 1.0 */
        eng_wq(fRegs, 0x6d0, 0x80080888u); eng_wq(fRegs, 0x6d8, 0x505u); eng_wq(fRegs, 0x6d4, 0x70003u); eng_wq(fRegs, 0x530, 0x23u);
        eng_wq(fRegs, 0x6a8, pitch << 16); eng_wq(fRegs, 0x6b4, h << 16);
        eng_wq(fRegs, 0x500, (pitch << 19) | (dstOff >> 3));
        eng_wq(fRegs, MACH64_DST_X_Y, (Y << 16) | X);                                 /* X low, Y high (as eng_fill) */
        eng_wq(fRegs, MACH64_DST_WIDTH_HEIGHT, (16u << 16) | 16u);                    /* trigger, as eng_fill */
        goto done;
    }
    /* init stream (FUN_b0103f60), register index * 4 + 0x400 */
    eng_wq(fRegs, 0x5c0, 0);                    /* SCALE_OFF */
    eng_wq(fRegs, 0x778, 0);                    /* SECONDARY_SCALE_OFF */
    eng_wq(fRegs, 0x5fc, 0x98a40u);             /* SCALE_3D_CNTL */
    eng_wq(fRegs, 0x550, 0x8000u);              /* ALPHA_TST_CNTL */
    eng_wq(fRegs, 0x6d0, 0x80080888u);          /* DP_PIX_WIDTH (8 bpp) */
    eng_wq(fRegs, 0x530, 0x8e023u);             /* DST_CNTL */
    eng_wq(fRegs, 0x774, 0x40800180u);          /* TEX_CNTL */
    eng_wq(fRegs, 0x708, 0);                    /* CLR_CMP_CNTL */
    eng_wq(fRegs, 0x6c4, 0);                    /* DP_FOG_CLR */
    eng_wq(fRegs, 0x5dc, pitch);                /* index 0x77 (Win98: 2 * its width parameter): plane width */
    eng_wq(fRegs, 0x5e0, h);                    /* SCALE_HEIGHT */
    eng_wq(fRegs, 0x5f0, 0x10000u);             /* SCALE_X_INC  1.0 */
    eng_wq(fRegs, 0x5f4, 0x10000u);             /* SCALE_Y_INC  1.0 (index 0x7d) */
    eng_wq(fRegs, 0x79c, 0x10000u);             /* SECONDARY_SCALE_X_INC */
    eng_wq(fRegs, 0x7d0, 0x10000u);             /* SECONDARY_SCALE_Y_INC */
    eng_wq(fRegs, 0x6d8, 0x505u);               /* DP_SRC */
    eng_wq(fRegs, 0x6d4, 0x70003u);             /* DP_MIX */
    eng_wq(fRegs, 0x6a8, pitch << 16);          /* SC_LEFT_RIGHT */
    eng_wq(fRegs, 0x6b4, h << 16);              /* SC_TOP_BOTTOM */
    /* per block (one-way, frame, luma 16x16) */
    eng_wq(fRegs, 0x5fc, 0x98a40u);
    eng_wq(fRegs, 0x780, (pitch << 16) | pitch);                                   /* SCALE_PITCH_BOTH */
    eng_wq(fRegs, 0x500, (pitch << 19) | (dstOff >> 3));                           /* DST_OFF_PITCH */
    eng_wq(fRegs, 0x788, (ay * pitch + ax + srcOff) | ((((y2 & 1u) << 2) | (x2 & 1u)) << 28));   /* SCALE_OFF_ACC: address, half-pel flags in bits 30:28 */
    eng_wq(fRegs, 0x78c, (X << 16) | Y);                                           /* SCALE_DST_Y_X */
    eng_wq(fRegs, 0x790, 0x100010u);                                               /* trigger: 16 x 16 */
done:
    if (eng_wait_idle(fRegs)) {                  /* diagnosis: sample the status registers 4096 times: busy bit and FIFO-not-empty counts */
        uint32_t k, nb = 0, nf = 0, g, f; status = 1;
        for (k = 0; k < 4096; k++) { g = eng_rd(fRegs, MACH64_GUI_STAT); f = eng_rd(fRegs, MACH64_FIFO_STAT); nb += g & 1u; nf += (f & 0xffffu) ? 1u : 0u; gui = g; fifo = f; }
        fifo = nb; gui = (nf >> 4) << 16;                                   /* reported: busy count in the FIFO field, FIFO-nonempty count / 16 in GUI[23:16] */
        (void)eng_reset(fRegs, 1);
    }
                 /* reported through the status byte (rc stays success so the caller sees GUI_STAT) */
    else { status = 0; gui = eng_rd(fRegs, MACH64_GUI_STAT); fifo = eng_rd(fRegs, MACH64_FIFO_STAT); }
    if (!noRestore) for (i = 0; i < sizeof kSave / sizeof kSave[0]; i++) eng_wq(fRegs, kSave[i], sv[i]);      /* bit 31 of pitchH (diagnosis): leave the registers as the sequence set them */
    (void)eng_wait_idle(fRegs);
    IOLockUnlock(fLock);
    *out = status | ((fifo & 0xffffu) << 8) | ((gui >> 16) << 24);              /* status | FIFO_STAT[15:0] << 8 | GUI_STAT[23:16] << 24 */
    return rc;
}
#endif

IOReturn RageLTDevice::scanoutCopyOut(uint32_t byteOff, uint8_t *dst, uint32_t len)
{
    volatile uint8_t *vram = (volatile uint8_t *)fMap->getVirtualAddress(); uint32_t i; IOReturn rc = kIOReturnBadArgument;
    uint64_t size;
    if ((byteOff | len) & 3u || !len || len > 0x100000u) return kIOReturnBadArgument;
    IOLockLock(fLock);
    if (!chipOK() || fAperEndianBad) { IOLockUnlock(fLock); return kIOReturnNotReady; }
    size = (uint64_t)fDisp.pitch_px * fDisp.h * fDisp.bpp;
    if (fDispReserved && byteOff + (uint64_t)len <= size && rage_vram_span_ok(fDisp.base + byteOff, len)) {
        rc = kIOReturnSuccess;
        if (idleWait()) rc = kIOReturnTimeout;
        else for (i = 0; i < len; i += 4) unxfer_word(dst + i, OSReadLittleInt32((void *)vram, fDisp.base + byteOff + i), 0);
    }
    IOLockUnlock(fLock);
    return rc;
}

/* kext/RageLTEngine.cpp - the engine class (design 3.1): per-client contexts, kernel-owned render target / Z / scissor, validated command
 * submission, 2D fill, fence.  PIO phase: every submit drains through the FIFO and waits (bounded) for idle before it returns, so the fence is
 * complete on return; the DMA phase (RAGE_FEAT_DMA) keeps the same counters but returns earlier.  No floating point. */
#include "RageLTDevice.h"
#include <IOKit/IOLib.h>
#include "rage_guard.h"
#include "RageLTEngine.h"

#define LOG(...) IOLog("RageLT: " __VA_ARGS__)

struct rage_ctx *RageLTDevice::ctxFor(uint32_t owner, bool create)
{
    uint32_t k; struct rage_ctx *freeSlot = 0;
    if (!owner) return 0;
    for (k = 0; k < RAGE_MAX_CTX; k++) {
        if (fCtx[k].owner == owner) return &fCtx[k];
        if (!fCtx[k].owner && !freeSlot) freeSlot = &fCtx[k];
    }
    if (create && freeSlot) {
        uint8_t *b = (uint8_t *)freeSlot; uint32_t i;
        for (i = 0; i < sizeof *freeSlot; i++) b[i] = 0;
        freeSlot->owner = owner;
        return freeSlot;
    }
    return 0;
}

/* Reads the chip.  Only call when the boot init is known to have run: on an uninitialised chip a PPC read blocks forever. */
IOReturn RageLTDevice::engineEnable(void)
{
    uint32_t bus, gen;
    if (fEngineLive) return kIOReturnSuccess;
    bus = eng_rd(fRegs, MACH64_BUS_CNTL); gen = eng_rd(fRegs, RAGE_R_CRTC_GEN_CNTL);
    if (!(bus & (1u << 27)) || !(gen & (1u << 25))) { logEvent(RAGE_EV_ERROR, 0, 0xe0010000u, bus, gen, 0, 0); return kIOReturnNotReady; }
    if (eng_reset(fRegs, 0)) { logEvent(RAGE_EV_ERROR, 0, 0xe0020000u, 0, 0, 0, 0); return kIOReturnNotReady; }
    eng_init_context(fRegs, 8);
    eng_wq(fRegs, 0x0304, 0);                                  /* SETUP_CNTL: plain integer coordinates (design 11.11) */
    if (eng_wait_idle(fRegs)) return kIOReturnNotReady;
    fEngineLive = true;
    logEvent(RAGE_EV_MARK, 0, 0xe0030000u, bus, gen, 0, 0);
    return kIOReturnSuccess;
}

IOReturn RageLTDevice::reserveDisplay(void)
{
    uint32_t bytes; int rc = 0;
    IOLockLock(fLock);
    if (fDispReserved) { IOLockUnlock(fLock); return kIOReturnSuccess; }
    IOLockUnlock(fLock);
    modeRegsRead();
    bytes = (fDisp.pitch_px * fDisp.h * fDisp.bpp + 0xfffu) & ~0xfffu;
    IOLockLock(fLock);
    if (!fDispReserved) {
        rc = rage_vram_reserve(&fVram, fDisp.base, bytes) || rage_vram_reserve(&fVram, 0x770000u, RAGE_VRAM_USABLE_END - 0x770000u);
        if (rc) rage_vram_unreserve(&fVram, fDisp.base);
        else fDispReserved = true;
    }
    IOLockUnlock(fLock);
    logEvent(RAGE_EV_MARK, 0, 0xe0040000u, fDisp.base, fDisp.pitch_px, (fDisp.w << 16) | (fDisp.h & 0xffffu), fDisp.bpp);
    return rc ? kIOReturnNoSpace : kIOReturnSuccess;
}

IOReturn RageLTDevice::setRenderTarget(uint32_t owner, uint32_t off, uint32_t pitch_px, uint32_t bpp, uint32_t w, uint32_t h)
{
    struct rage_ctx *c; IOReturn r = kIOReturnBadArgument;
    uint64_t len = (uint64_t)pitch_px * h * bpp;
    if (fModeGen & 1u) return kIOReturnNotReady;
    IOLockLock(fLock);
    if ((bpp == 2 || bpp == 4) && w && h && w <= pitch_px && !(pitch_px & 7u) && pitch_px <= 8184u && !(off & 7u) && h <= 8191u &&
        rage_vram_owns(&fVram, off, (uint32_t)len, owner) && len < 0x800000u && (c = ctxFor(owner, true)) != 0) {
        c->tgt_off = off; c->tgt_pitch = pitch_px; c->tgt_bpp = bpp; c->tgt_w = w; c->tgt_h = h;
        c->sc_l = 0; c->sc_r = w - 1; c->sc_t = 0; c->sc_b = h - 1;      /* default scissor = the whole target */
        c->z_set = 0;
        r = kIOReturnSuccess;
    }
    IOLockUnlock(fLock);
    logEvent(RAGE_EV_MARK, owner, 0xe0050000u, off, (pitch_px << 8) | bpp, (w << 16) | (h & 0xffffu), r == kIOReturnSuccess ? 0u : 1u);
    return r;
}

IOReturn RageLTDevice::setScissor(uint32_t owner, int32_t l, int32_t r, int32_t t, int32_t b)
{
    struct rage_ctx *c; IOReturn rc = kIOReturnNotReady;
    IOLockLock(fLock);
    c = ctxFor(owner, false);
    if (c && c->tgt_bpp) {                                    /* clamp to the target: the hardware clip then keeps every primitive inside it */
        if (l < 0) l = 0;
        if (t < 0) t = 0;
        if (r > (int32_t)c->tgt_w - 1) r = (int32_t)c->tgt_w - 1;
        if (b > (int32_t)c->tgt_h - 1) b = (int32_t)c->tgt_h - 1;
        c->sc_l = (uint32_t)l; c->sc_r = (uint32_t)r; c->sc_t = (uint32_t)t; c->sc_b = (uint32_t)b;   /* l > r etc.: stored as-is, draws nothing (proven) */
        if (r < 0) c->sc_r = 0, c->sc_l = 1;                  /* entirely off the target on the negative side: make it an empty (inverted) box */
        if (b < 0) c->sc_b = 0, c->sc_t = 1;
        rc = kIOReturnSuccess;
    }
    IOLockUnlock(fLock);
    return rc;
}

IOReturn RageLTDevice::setZBuffer(uint32_t owner, uint32_t off, uint32_t pitch_px)
{
    struct rage_ctx *c; IOReturn rc = kIOReturnBadArgument;
    IOLockLock(fLock);
    c = ctxFor(owner, false);
    if (c && c->tgt_bpp && !(off & 7u) && !(pitch_px & 7u) && pitch_px >= c->tgt_w && pitch_px <= 8184u &&
        rage_vram_owns(&fVram, off, pitch_px * c->tgt_h * 2u, owner)) {         /* 16-bit Z, same height as the target */
        c->z_off = off; c->z_pitch = pitch_px; c->z_set = 1; rc = kIOReturnSuccess;
    }
    IOLockUnlock(fLock);
    return rc;
}

/* The kernel's own prologue: everything that decides WHERE the engine writes. */
static void eng_prologue(volatile uint8_t *regs, const struct rage_ctx *c)
{
    eng_wq(regs, RAGE_R_DST_CNTL, 3u);
    eng_wq(regs, MACH64_DP_PIX_WIDTH, c->tgt_bpp == 2 ? RAGE_PIXW_565 : 0x60060666u);
    eng_wq(regs, MACH64_DST_OFF_PITCH, ((c->tgt_pitch / 8u) << 22) | (c->tgt_off >> 3));
    eng_wq(regs, MACH64_SC_LEFT_RIGHT, (c->sc_r << 16) | c->sc_l);
    eng_wq(regs, MACH64_SC_TOP_BOTTOM, (c->sc_b << 16) | c->sc_t);
    if (c->z_set) eng_wq(regs, MACH64_Z_OFF_PITCH, ((c->z_pitch / 8u) << 22) | (c->z_off >> 3));
}

IOReturn RageLTDevice::waitFence(uint32_t seq)
{
    if ((int32_t)(fFenceCompleted - seq) >= 0) return kIOReturnSuccess;
    if (eng_wait_idle(fRegs)) {
        logEvent(RAGE_EV_ERROR, 0, 0xe0060000u, seq, eng_rd(fRegs, MACH64_FIFO_STAT), eng_rd(fRegs, MACH64_GUI_STAT), 0);
        eng_reset(fRegs, 1);
        fFenceCompleted = fFenceSubmitted;                     /* the stuck work is abandoned */
        return kIOReturnTimeout;
    }
    fFenceCompleted = fFenceSubmitted;
    return kIOReturnSuccess;
}

IOReturn RageLTDevice::submitBatch(uint32_t owner, const uint32_t *pairs, uint32_t npairs)
{
    uint32_t i, seq; IOReturn rc; struct rage_ctx c;
    if (!rage_batch_ok(pairs, npairs)) {                                               /* whole batch dropped */
        fRefused++;
        logEvent(RAGE_EV_BATCH_REFUSED, owner, npairs, pairs ? pairs[0] : 0, pairs ? pairs[1] : 0, fRefused, 0);
        return kIOReturnBadArgument;
    }
    if (!fEngineLive || (fModeGen & 1u)) return kIOReturnNotReady;
    IOLockLock(fLock);
    { struct rage_ctx *p = ctxFor(owner, false); if (!p || !p->tgt_bpp) { IOLockUnlock(fLock); return kIOReturnNotReady; } c = *p; }
    logEvent(RAGE_EV_BATCH, owner, npairs, pairs[0], pairs[1], 0, 0);
    if (eng_wait_idle(fRegs)) { IOLockUnlock(fLock); return kIOReturnTimeout; }        /* the previous batch is still in flight (DMA phase): block here only */
    eng_prologue(fRegs, &c);
    for (i = 0; i < npairs; i++) {
        uint32_t reg = pairs[2 * i], val = pairs[2 * i + 1];
        if (reg == MACH64_Z_CNTL && !c.z_set) val = 0;         /* no Z buffer of its own: depth test off, so Z_OFF_PITCH is never used */
        eng_wq(fRegs, reg, val);
    }
    seq = ++fFenceSubmitted;
    rc = waitFence(seq);
    IOLockUnlock(fLock);
    return rc;
}

IOReturn RageLTDevice::fillRect(uint32_t owner, int32_t x, int32_t y, int32_t w, int32_t h, uint32_t color565)
{
    struct rage_ctx c; IOReturn rc; uint32_t seq; int32_t x1, y1;
    if (!fEngineLive || (fModeGen & 1u)) return kIOReturnNotReady;
    IOLockLock(fLock);
    { struct rage_ctx *p = ctxFor(owner, false); if (!p || p->tgt_bpp != 2) { IOLockUnlock(fLock); return kIOReturnNotReady; } c = *p; }
    if (w <= 0 || h <= 0) { IOLockUnlock(fLock); return kIOReturnSuccess; }
    x1 = x + w; y1 = y + h;                                    /* clip to the scissor (already inside the target) in the kernel too */
    if (x < (int32_t)c.sc_l) x = (int32_t)c.sc_l;
    if (y < (int32_t)c.sc_t) y = (int32_t)c.sc_t;
    if (x1 > (int32_t)c.sc_r + 1) x1 = (int32_t)c.sc_r + 1;
    if (y1 > (int32_t)c.sc_b + 1) y1 = (int32_t)c.sc_b + 1;
    if (x1 <= x || y1 <= y) { IOLockUnlock(fLock); return kIOReturnSuccess; }
    if (eng_wait_idle(fRegs)) { IOLockUnlock(fLock); return kIOReturnTimeout; }
    rc = (IOReturn)eng_fill16(fRegs, c.tgt_off, c.tgt_pitch, (uint32_t)x, (uint32_t)y, (uint32_t)(x1 - x), (uint32_t)(y1 - y), color565 & 0xffffu,
                              (c.sc_r << 16) | c.sc_l, (c.sc_b << 16) | c.sc_t, 3u);
    if (rc) { IOLockUnlock(fLock); return kIOReturnTimeout; }
    seq = ++fFenceSubmitted;
    rc = waitFence(seq);
    IOLockUnlock(fLock);
    return rc;
}

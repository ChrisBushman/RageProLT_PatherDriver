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
    if (idleWait()) return kIOReturnNotReady;
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
        c->tgt_555 = (bpp == 2 && fDisp.depth == 3) ? 1u : 0u;
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

IOReturn RageLTDevice::setTexture(uint32_t owner, uint32_t index, uint32_t off, uint32_t fmt)
{
    struct rage_ctx *c; IOReturn rc = kIOReturnBadArgument;
    if (index > 11u || !(fmt == 0 || fmt == 3 || fmt == 4 || fmt == 6 || fmt == 8 || fmt == 11 || fmt == 12 || fmt == 15)) return rc;
    IOLockLock(fLock);
    c = ctxFor(owner, false);
    if (c) {
        if (off == 0xfffffffeu) {                                                      /* format only */
            if (fmt) { if (index == 11u) c->tex2_fmt = fmt; else c->tex_fmt = fmt; }
            rc = kIOReturnSuccess;
        }
        else if (off == 0xffffffffu) { c->tex_set &= ~(1u << index); rc = kIOReturnSuccess; }
        else if (!(off & 7u) && rage_vram_owns(&fVram, off, 8u, owner)) {            /* only the start is checked: the engine READS beyond it, never writes */
            c->tex_off[index] = off; c->tex_set |= 1u << index;
            if (fmt) { if (index == 11u) c->tex2_fmt = fmt; else c->tex_fmt = fmt; }
            rc = kIOReturnSuccess;
        }
    }
    IOLockUnlock(fLock);
    return rc;
}

IOReturn RageLTDevice::setTextures(uint32_t owner, const uint32_t *rec, uint32_t n, uint32_t tfmt, uint32_t tfmt2)
{
    struct rage_ctx *c; uint32_t k;
    if (!n && !(tfmt && tfmt != 6u) && !tfmt2) return kIOReturnSuccess;
    IOLockLock(fLock);
    c = ctxFor(owner, false);
    if (c) {
        for (k = 0; k < n; k++) {                                                      /* as setTexture(owner, index, off, 0) */
            uint32_t index = rec[2 * k], off = rec[2 * k + 1];
            if (index > 11u) continue;
            if (off == 0xfffffffeu) continue;
            if (off == 0xffffffffu) c->tex_set &= ~(1u << index);
            else if (!(off & 7u) && rage_vram_owns(&fVram, off, 8u, owner)) { c->tex_off[index] = off; c->tex_set |= 1u << index; }
        }
        if (tfmt && tfmt != 6u && (tfmt == 3 || tfmt == 4 || tfmt == 8 || tfmt == 11 || tfmt == 12 || tfmt == 15)) c->tex_fmt = tfmt;
        if (tfmt2 && (tfmt2 == 3 || tfmt2 == 4 || tfmt2 == 6 || tfmt2 == 8 || tfmt2 == 11 || tfmt2 == 12 || tfmt2 == 15)) c->tex2_fmt = tfmt2;
    }
    IOLockUnlock(fLock);
    return kIOReturnSuccess;
}

/* The kernel's own prologue: everything that decides WHERE the engine writes or reads. */
uint32_t RageLTDevice::prologuePairs(const struct rage_ctx *c, uint32_t *o)
{
    uint32_t n = 0, k, dpw = c->tgt_bpp == 2 ? (c->tgt_555 ? RAGE_PIXW_555 : RAGE_PIXW_565) : RAGE_PIXW_8888;
    if (c->tex_fmt) dpw = (dpw & ~0xf0000000u) | (c->tex_fmt << 28);
    if (c->tex2_fmt) dpw = (dpw & ~0xf0u) | (c->tex2_fmt << 4);
#define PR(r, v) do { o[n++] = (r); o[n++] = (v); } while (0)
    PR(RAGE_R_DST_CNTL, 3u);
    PR(MACH64_DP_PIX_WIDTH, dpw);
    PR(MACH64_DST_OFF_PITCH, ((c->tgt_pitch / 8u) << 22) | (c->tgt_off >> 3));
    PR(MACH64_SC_LEFT_RIGHT, (c->sc_r << 16) | c->sc_l);
    PR(MACH64_SC_TOP_BOTTOM, (c->sc_b << 16) | c->sc_t);
    if (c->z_set) PR(MACH64_Z_OFF_PITCH, ((c->z_pitch / 8u) << 22) | (c->z_off >> 3));
    for (k = 0; k < 11u; k++) if (c->tex_set & (1u << k)) PR(MACH64_TEX_0_OFF + 4u * k, c->tex_off[k]);
    if (c->tex_set & (1u << 11)) PR(MACH64_SECONDARY_TEX_OFF, c->tex_off[11]);
#undef PR
    return n / 2;
}

static void eng_prologue(volatile uint8_t *regs, const struct rage_ctx *c)
{
    uint32_t p[64], n = RageLTDevice::prologuePairs(c, p), i;
    for (i = 0; i < n; i++) eng_wq(regs, p[2 * i], p[2 * i + 1]);
}

/* Fills, Z clears and presents return once the engine has been programmed: every later engine use (the next submit's kick, a CPU read or upload, a mode change, FENCE) drains first, so the
 * CPU builds the next batches while the engine runs the operation.  -DRAGE_SYNC_OPS restores the wait for completion. */
#ifdef RAGE_SYNC_OPS
#define RAGE_OP_DONE(seq) waitFence(seq)
#else
#define RAGE_OP_DONE(seq) ((void)(seq), kIOReturnSuccess)
#endif

IOReturn RageLTDevice::fenceWait(uint32_t seq)
{
    IOReturn rc;
    IOLockLock(fLock);
    rc = waitFence(seq);
    IOLockUnlock(fLock);
    return rc;
}

IOReturn RageLTDevice::waitFence(uint32_t seq)
{
    if ((int32_t)(fFenceCompleted - seq) >= 0) return kIOReturnSuccess;
    if (fDmaPending && dmaWait()) return kIOReturnTimeout;
    if (idleWait()) {
        pf_err++;
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
    uint32_t t_v = tbl32(), t_v2; int ok_ = rage_batch_ok(pairs, npairs); t_v2 = tbl32(); pf_valid += (t_v2 - t_v) >> 2;
    if (!ok_) {                                               /* whole batch dropped */
        fRefused++;
        logEvent(RAGE_EV_BATCH_REFUSED, owner, npairs, pairs ? pairs[0] : 0, pairs ? pairs[1] : 0, fRefused, 0);
        return kIOReturnBadArgument;
    }
    if (!fEngineLive || (fModeGen & 1u)) return kIOReturnNotReady;
    IOLockLock(fLock);
    { struct rage_ctx *p = ctxFor(owner, false); if (!p || !p->tgt_bpp) { IOLockUnlock(fLock); return kIOReturnNotReady; } c = *p; }
    logEvent(RAGE_EV_BATCH, owner, npairs, pairs[0], pairs[1], 0, 0);
    if (dmaActive() && npairs + 8u <= 8192u + 8u) {
        rc = dmaSubmit(&c, pairs, npairs);
        if (rc == kIOReturnSuccess) { ++fFenceSubmitted; IOLockUnlock(fLock); return rc; }   /* returns without waiting: the next engine operation drains */
        IOLockUnlock(fLock); return rc;
    }
    if (idleWait()) { IOLockUnlock(fLock); return kIOReturnTimeout; }
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

/* Z clear: the Z buffer is 16 bits per pixel with the target's size and pitch, so it is an RGB565-width solid fill of the bound Z block. */
IOReturn RageLTDevice::clearZ(uint32_t owner, uint32_t value)
{
    struct rage_ctx c; IOReturn rc; uint32_t seq;
    if (!fEngineLive || (fModeGen & 1u)) return kIOReturnNotReady;
    IOLockLock(fLock);
    { struct rage_ctx *p = ctxFor(owner, false); if (!p || !p->z_set || !p->tgt_bpp) { IOLockUnlock(fLock); return kIOReturnNotReady; } c = *p; }
    if (idleWait()) { IOLockUnlock(fLock); return kIOReturnTimeout; }
    rc = (IOReturn)eng_fill16(fRegs, c.z_off, c.z_pitch, 0, 0, c.tgt_w, c.tgt_h, value & 0xffffu,
                              ((c.tgt_w - 1u) << 16), ((c.tgt_h - 1u) << 16), 3u);
    if (rc) { IOLockUnlock(fLock); return kIOReturnTimeout; }
    seq = ++fFenceSubmitted;
    rc = RAGE_OP_DONE(seq);
    IOLockUnlock(fLock);
    return rc;
}

/* colour: RGB565 for a 16 bpp target, ARGB8888 (0xAARRGGBB) for a 32 bpp target */
IOReturn RageLTDevice::fillRect(uint32_t owner, int32_t x, int32_t y, int32_t w, int32_t h, uint32_t color565)
{
    struct rage_ctx c; IOReturn rc; uint32_t seq; int32_t x1, y1;
    if (!fEngineLive || (fModeGen & 1u)) return kIOReturnNotReady;
    IOLockLock(fLock);
    { struct rage_ctx *p = ctxFor(owner, false); if (!p || (p->tgt_bpp != 2 && p->tgt_bpp != 4)) { IOLockUnlock(fLock); return kIOReturnNotReady; } c = *p; }
    if (w <= 0 || h <= 0) { IOLockUnlock(fLock); return kIOReturnSuccess; }
    x1 = x + w; y1 = y + h;                                    /* clip to the scissor (already inside the target) in the kernel too */
    if (x < (int32_t)c.sc_l) x = (int32_t)c.sc_l;
    if (y < (int32_t)c.sc_t) y = (int32_t)c.sc_t;
    if (x1 > (int32_t)c.sc_r + 1) x1 = (int32_t)c.sc_r + 1;
    if (y1 > (int32_t)c.sc_b + 1) y1 = (int32_t)c.sc_b + 1;
    if (x1 <= x || y1 <= y) { IOLockUnlock(fLock); return kIOReturnSuccess; }
    if (c.tgt_bpp == 2 && c.tgt_555) color565 = ((color565 >> 1) & 0x7fe0u) | (color565 & 0x1fu);     /* RGB565 -> 1555: drop the green LSB, red/green move down one bit (the fill writes the raw 16 bits) */
    if (idleWait()) { IOLockUnlock(fLock); return kIOReturnTimeout; }
    rc = (IOReturn)eng_fill(fRegs, c.tgt_bpp == 4 ? RAGE_PIXW_8888 : c.tgt_555 ? RAGE_PIXW_555 : RAGE_PIXW_565, c.tgt_off, c.tgt_pitch, (uint32_t)x, (uint32_t)y, (uint32_t)(x1 - x), (uint32_t)(y1 - y),
                            c.tgt_bpp == 4 ? color565 : (color565 & 0xffffu), (c.sc_r << 16) | c.sc_l, (c.sc_b << 16) | c.sc_t, 3u);
    if (rc) { IOLockUnlock(fLock); return kIOReturnTimeout; }
    seq = ++fFenceSubmitted;
    rc = RAGE_OP_DONE(seq);
    IOLockUnlock(fLock);
    return rc;
}

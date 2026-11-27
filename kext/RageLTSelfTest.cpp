/* kext/RageLTSelfTest.cpp - DEBUG ONLY (RAGE_DEBUG_ENGTEST): proves the hardware scissor contains every engine write (design E1).
 *
 * Safety design: every draw is bounded to ONE 512 KB block this test allocates from the VRAM allocator, so even a total scissor failure
 * cannot touch anything else.  Before any engine write the test checks the chip is alive and idle (CRTC enabled, extended registers
 * on).  Scissor edges are measured, not assumed: results go to the event ring (MARK events with a0 = 0xE1xxxxxx / 0xE2xxxxxx). */
#include "RageLTDevice.h"
#include "RageLTEngine.h"
#include <IOKit/IOLib.h>

#if RAGE_DEBUG_ENGTEST
#include <kern/clock.h>

#define SURF_PX     256u                    /* test surface 256 x 256, 16 bpp */
#define SURF_BYTES  (SURF_PX * SURF_PX * 2u)
#define BLOCK_BYTES 0x80000u                /* surface [0,0x20000) | guard | source surface at 0x40000 [0x40000,0x60000) | guard */
#define SRC_AT      0x40000u
#define SENT16      0xa5a5u
#define COL_FILL    0xf81fu                 /* magenta 565 */
#define COL_SRC     0x07e0u                 /* green 565 */

struct tcase { uint32_t id; int blit; int fastfill; uint32_t L, R, T, B; uint32_t dx, dy, w, h; uint32_t dc; };

static uint16_t vr16(volatile uint8_t *vram, uint32_t o) { return OSReadLittleInt16((void *)vram, o); }
static uint32_t vr32(volatile uint8_t *vram, uint32_t o) { return OSReadLittleInt32((void *)vram, o); }
static void     vw32(volatile uint8_t *vram, uint32_t o, uint32_t v) { OSWriteLittleInt32((void *)vram, o, v); }


/* Scans the whole block.  Counts changed pixels outside the expected set, correct pixels inside it, and (pass 0/1) logs where the missing
 * inside pixels are and what they hold. */
static void verify(volatile uint8_t *vram, uint32_t off0, const struct tcase *c, uint32_t *outside_c, uint32_t *inside_ok, uint32_t *inside_exp,
                   uint32_t *minx, uint32_t *miny, uint32_t *maxx, uint32_t *maxy, uint32_t owner, int second, RageLTDevice *dev)
{
    uint32_t o, ms_minx = 0xffff, ms_miny = 0xffff, ms_maxx = 0, ms_maxy = 0, nmiss = 0, samples[5] = { 0, 0, 0, 0, 0 }, ns = 0;
    uint8_t rowcnt[80], rowfirst[80], rowlast[80]; uint32_t ri;
    for (ri = 0; ri < 80; ri++) { rowcnt[ri] = 0; rowfirst[ri] = 255; rowlast[ri] = 0; }
    uint32_t ch_n = 0, ch_x0 = 0xffff, ch_y0 = 0xffff, ch_x1 = 0, ch_y1 = 0;
    *outside_c = 0; *inside_ok = 0; *inside_exp = 0; *minx = 0xffff; *miny = 0xffff; *maxx = 0; *maxy = 0;
    for (o = 0; o < BLOCK_BYTES; o += 2) {
        uint16_t v = vr16(vram, off0 + o), expect = SENT16; int in_surf = o < SURF_BYTES; uint32_t px = o >> 1, x = px % SURF_PX, y = px / SURF_PX;
        if (in_surf) {
            int inrect = x >= c->dx && x < c->dx + c->w && y >= c->dy && y < c->dy + c->h;
            int inscis = x >= c->L && x <= c->R && y >= c->T && y <= c->B;
            if (inrect && inscis) { expect = c->blit ? COL_SRC : COL_FILL; (*inside_exp)++; }
        } else if (c->blit && o >= SRC_AT && o < SRC_AT + SURF_BYTES) expect = COL_SRC;
        if (in_surf && v != SENT16) { ch_n++; if (x < ch_x0) ch_x0 = x; if (x > ch_x1) ch_x1 = x; if (y < ch_y0) ch_y0 = y; if (y > ch_y1) ch_y1 = y; }
        if (v == expect) { if (in_surf && expect != SENT16) { (*inside_ok)++; if (y >= c->T && y - c->T < 80) { rowcnt[y - c->T]++; if (x < rowfirst[y - c->T]) rowfirst[y - c->T] = (uint8_t)x; if (x > rowlast[y - c->T]) rowlast[y - c->T] = (uint8_t)x; } } }
        else if (in_surf && expect != SENT16) {                                     /* an inside pixel that did not get the colour */
            nmiss++; if (x < ms_minx) ms_minx = x; if (x > ms_maxx) ms_maxx = x; if (y < ms_miny) ms_miny = y; if (y > ms_maxy) ms_maxy = y;
            if (ns < 5 && (nmiss == 1 || (nmiss % 211) == 0)) samples[ns++] = (x << 24) | (y << 16) | v;
        } else {
            (*outside_c)++;
            if (in_surf) { if (x < *minx) *minx = x; if (x > *maxx) *maxx = x; if (y < *miny) *miny = y; if (y > *maxy) *maxy = y; }
        }
    }
    if (!second) dev->logEvent(RAGE_EV_MARK, owner, 0xec000000u | c->id, ch_n, (ch_x0 << 16) | (ch_y0 & 0xffffu), (ch_x1 << 16) | (ch_y1 & 0xffffu), 0);
    if (nmiss && !second) {          /* per-row map of the scissor region: drawn count, first and last drawn x, 4 rows per word, 20 rows per event */
        uint32_t blk, arr, w[5], q;
        for (arr = 0; arr < 3; arr++) for (blk = 0; blk < 4; blk++) {
            const uint8_t *src = arr == 0 ? rowcnt : arr == 1 ? rowfirst : rowlast;
            for (q = 0; q < 5; q++) { uint32_t b = blk * 20 + q * 4; w[q] = ((uint32_t)src[b] << 24) | ((uint32_t)src[b + 1] << 16) | ((uint32_t)src[b + 2] << 8) | src[b + 3]; }
            dev->logEvent(RAGE_EV_MARK, owner, 0xe6000000u | (arr << 8) | blk, w[0], w[1], w[2], w[3] ); dev->logEvent(RAGE_EV_MARK, owner, 0xe7000000u | (arr << 8) | blk, w[4], 0, 0, 0);
        }
    }
    if (nmiss) {
        dev->logEvent(RAGE_EV_MARK, owner, 0xe3000000u | c->id | (second ? 0x100u : 0), nmiss, (ms_minx << 16) | (ms_miny & 0xffffu), (ms_maxx << 16) | (ms_maxy & 0xffffu), 0);
        dev->logEvent(RAGE_EV_MARK, owner, 0xe4000000u | c->id | (second ? 0x100u : 0), samples[0], samples[1], samples[2], samples[3]);
    }
}

IOReturn RageLTDevice::engineSelfTest(uint32_t mask, uint32_t *passmask, uint32_t *outside, uint32_t *inside, uint32_t *status)
{
    volatile uint8_t *vram = (volatile uint8_t *)fMap->getVirtualAddress();
    uint32_t bus, gen, crtcOP, vtd, off0 = 0, owner, k;
    static const struct tcase cases[] = {
        /* id  blit ff   L   R   T   B    dx  dy   w    h */
        {  1,  0,  0,   64, 127, 32,  95,   0,  0, 300, 300, 3 },   /* fill far larger than the scissor, wraps past the pitch */
        {  2,  0,  1,   64, 127, 32,  95,   0,  0, 300, 300, 3 },   /* same with the chip's AUTO fast-fill / block-write enabled */
        {  3,  1,  0,   64, 127, 32,  95,   0,  0, 300, 300, 3 },   /* blit */
        {  4,  0,  0,  100,  50, 32,  95,   0,  0, 300, 300, 3 },   /* inverted scissor (right < left) */
        {  5,  0,  0,   64, 127, 32,  95, 200, 200,  50,  50, 3 },  /* rect entirely outside the scissor */
        {  6,  0,  0,    0, 255,  0, 255,   0,  0, 300, 300, 3 },   /* scissor = whole surface, rect overshoots right and bottom */
        {  7,  0,  1,    0, 255,  0, 255,   0,  0, 300, 300, 3 },   /* same with fast-fill */
        {  8,  1,  0,    0, 255,  0, 255,   0,  0, 300, 300, 3 },   /* blit overshooting */
        {  9,  0,  0,    0, 255,  0, 255,  64, 32,  64,  64, 3 },   /* control: plain 64x64 fill inside the surface, whole-surface scissor */
        { 10,  0,  0,   64, 127, 32,  95,  64, 32,  64,  64, 3 },   /* control: same rect, scissor exactly equal to it */
        { 11,  0,  0,   64, 127, 32,  95,  32, 16, 128, 128, 3 },   /* rect larger than the scissor but wholly inside the surface (no overshoot) */
        { 12,  0,  0,    0, 255,  0, 255,   0,  0, 256, 256, 3 },   /* exactly the whole surface */
        { 13,  0,  0,    0,0x1fff, 0,0x1fff, 0,  0, 256, 256, 3 },  /* same, scissor = the old proven "no clip" value */
        { 14,  0,  0,    0,0x1fff, 0,0x1fff, 0,  0,  64,  64, 3 },  /* small rect at the origin, no clip */
        { 15,  0,  0,    0,0x1fff, 0,0x1fff, 100, 0,  40,  40, 3 }, /* probe: x offset only */
        { 16,  0,  0,    0,0x1fff, 0,0x1fff, 0, 100,  40,  40, 3 }, /* probe: y offset only */
        { 17,  0,  0,    0,0x1fff, 0,0x1fff, 0,  0,  40, 100, 3 },  /* probe: tall thin */
    };
    *passmask = 0; *outside = 0; *inside = 0; *status = 0;

    /* ---- preconditions: reads only, then refuse to write unless the chip is alive and idle ---- */
    bus = eng_rd(fRegs, MACH64_BUS_CNTL); gen = eng_rd(fRegs, RAGE_R_CRTC_GEN_CNTL);
    crtcOP = eng_rd(fRegs, MACH64_CRTC_OFF_PITCH); vtd = eng_rd(fRegs, MACH64_CRTC_V_TOTAL_DISP);
    logEvent(RAGE_EV_MARK, 0, 0xe0000000u, bus, gen, crtcOP, vtd);
    logEvent(RAGE_EV_MARK, 0, 0xe0000003u, eng_rd(fRegs, MACH64_FIFO_STAT), eng_rd(fRegs, MACH64_GUI_STAT), eng_rd(fRegs, MACH64_HW_DEBUG), eng_rd(fRegs, RAGE_R_MEM_CNTL));   /* engine status: read-only */
    if (!(bus & (1u << 27))) { *status = 1; return kIOReturnNotReady; }          /* extended registers off: engine writes would not land */
    if (!(gen & (1u << 25))) { *status = 2; return kIOReturnNotReady; }          /* CRTC not enabled: chip not initialised */
    { uint32_t rr = (uint32_t)eng_reset(fRegs, 0); logEvent(RAGE_EV_MARK, 0, 0xe0000007u, rr, eng_rd(fRegs, RAGE_R_DST_CNTL), eng_rd(fRegs, 0x130), eng_rd(fRegs, MACH64_GUI_STAT)); }   /* experiment: reset restores draw-engine register defaults? */
    eng_init_context(fRegs, SURF_PX); (void)eng_wait_idle(fRegs);
    if (eng_wait_idle(fRegs)) {                                                  /* engine busy: try the documented reset, mildest form first */
        uint32_t st1, st2 = 0;
        logEvent(RAGE_EV_MARK, 0, 0xe0000004u, eng_rd(fRegs, MACH64_FIFO_STAT), eng_rd(fRegs, MACH64_GUI_STAT), 0, 0);
        st1 = (uint32_t)eng_reset(fRegs, 0);
        logEvent(RAGE_EV_MARK, 0, 0xe0000005u, st1, eng_rd(fRegs, MACH64_FIFO_STAT), eng_rd(fRegs, MACH64_GUI_STAT), eng_rd(fRegs, MACH64_BUS_CNTL));
        if (st1) { st2 = (uint32_t)eng_reset(fRegs, 1); logEvent(RAGE_EV_MARK, 0, 0xe0000006u, st2, eng_rd(fRegs, MACH64_FIFO_STAT), eng_rd(fRegs, MACH64_GUI_STAT), eng_rd(fRegs, MACH64_BUS_CNTL)); }
        if (st1 && st2) { *status = 3; return kIOReturnNotReady; }
    }

    /* ---- reservations: scanout (from the live CRTC) and the framebuffer/cursor block at the top ---- */
    if (reserveDisplay() != kIOReturnSuccess) { *status = 4; return kIOReturnNoSpace; }
    owner = newOwner();
    if (vramAlloc(owner, BLOCK_BYTES, 4096, &off0)) { releaseOwner(owner); *status = 5; return kIOReturnNoSpace; }
    logEvent(RAGE_EV_MARK, owner, 0xe0000002u, off0, BLOCK_BYTES, 0, 0);


    if (mask & 0x10000000u) { if (!(mask & 1u)) engineEnable(); *passmask = dmaMinimalTest(mask & 3u); *inside = 0; *outside = 0; return kIOReturnSuccess; }
    if (mask & 0x20000000u) {                          /* ---- mode-change protocol (simulated will/did events; the G4 framebuffer never changes mode) ---- */
        uint32_t pass = 0, g0 = modeGen(), offA = off0, t2 = 0, base0 = fDisp.base; struct rage_display d0 = fDisp;
        uint32_t one[2] = { MACH64_DP_FRGD_CLR, 1 };
        engineEnable();
        if (setRenderTarget(owner, offA, 256, 2, 256, 256) == kIOReturnSuccess && fillRect(owner, 0, 0, 10, 10, 0x1234) == kIOReturnSuccess) pass |= 1u << 0;
        modeWillChange();
        if ((modeGen() & 1u) && modeGen() == g0 + 1) pass |= 1u << 1;                                   /* odd while changing */
        if (fillRect(owner, 0, 0, 10, 10, 0x1234) == kIOReturnNotReady && submitBatch(owner, one, 1) == kIOReturnNotReady) pass |= 1u << 2;   /* refused */
        if (setRenderTarget(owner, offA, 256, 2, 256, 256) == kIOReturnNotReady) pass |= 1u << 3;
        modeDidChange();
        if (!(modeGen() & 1u) && modeGen() == g0 + 2) pass |= 1u << 4;                                  /* even, new generation */
        if (fDispReserved && fDisp.base == base0 && fDisp.pitch_px == d0.pitch_px && fDisp.h == d0.h) pass |= 1u << 5;   /* reservation re-established */
        if (fillRect(owner, 0, 0, 10, 10, 0x1234) == kIOReturnNotReady) pass |= 1u << 6;                 /* old target stays invalid until set again */
        if (setRenderTarget(owner, offA, 256, 2, 256, 256) == kIOReturnSuccess && fillRect(owner, 0, 0, 10, 10, 0x1234) == kIOReturnSuccess) pass |= 1u << 7;
        if (vramAlloc(owner, 4096, 4096, &t2) == kIOReturnSuccess && t2 >= 0x1000 && vramFree(owner, t2) == kIOReturnSuccess) pass |= 1u << 8;
        *passmask = pass; *inside = 0; *outside = 0;
        releaseOwner(owner);
        return kIOReturnSuccess;
    }

    if (mask & 0x40000000u) {                          /* ---- engine API suite: contexts, kernel-owned target/scissor, validated batches, fence ---- */
        uint32_t a, b2 = 0, offA = 0, offB = 0, pass = 0, o, x0 = 0;
        uint32_t ownB = newOwner(); uint32_t col = 0, bad_outside = 0;
        static const uint32_t C1 = 0x1234, C2 = 0xf81f, C3 = 0x07e0;
        int32_t vx[3] = { 10, 250, 60 }, vy[3] = { 10, 20, 240 };
        uint32_t tri[2 * 32]; uint32_t n = 0, v;
        if (engineEnable() != kIOReturnSuccess) { *status = 7; releaseOwner(ownB); releaseOwner(owner); return kIOReturnNotReady; }
        a = owner; (void)a; (void)x0; (void)col;
        if (vramAlloc(ownB, BLOCK_BYTES, 4096, &offB)) { *status = 5; releaseOwner(ownB); releaseOwner(owner); return kIOReturnNoSpace; }
        (void)b2; offA = off0;
#define SCAN(name, expect_fn) do { (void)idleWait(); bad_outside = 0; for (o = 0; o < SURF_BYTES; o += 2) { uint32_t px = o >> 1, x = px % SURF_PX, y = px / SURF_PX; uint16_t got = vr16(vram, offA + o), ex; expect_fn; if (got != ex) bad_outside++; } logEvent(RAGE_EV_MARK, owner, 0xd0000000u | (name), bad_outside, 0, 0, 0); } while (0)
        for (o = 0; o < BLOCK_BYTES; o += 4) vw32(vram, offA + o, 0xa5a5a5a5u);
        /* 0: a target the client does not own (offset 0 = scanout reservation) is refused; a target overrunning its block is refused; B cannot use A's block */
        if (setRenderTarget(owner, 0, 256, 2, 256, 256) != kIOReturnSuccess && setRenderTarget(owner, offA, 256, 2, 256, 2048) != kIOReturnSuccess &&
            setRenderTarget(ownB, offA, 256, 2, 256, 256) != kIOReturnSuccess && setRenderTarget(owner, offA, 250, 2, 250, 10) != kIOReturnSuccess) pass |= 1u << 0;
        /* 1: valid target, full-target fill (rect larger than the target is clipped in the kernel too) */
        if (setRenderTarget(owner, offA, 256, 2, 400, 400) != kIOReturnSuccess && setRenderTarget(owner, offA, 256, 2, 256, 256) == kIOReturnSuccess &&
            fillRect(owner, -50, -50, 600, 600, C1) == kIOReturnSuccess) {
            SCAN(1, ex = C1);
            if (!bad_outside) pass |= 1u << 1;
        }
        /* 2: scissor requested far larger than the target is clamped; a fill then stays inside the scissor box */
        if (setScissor(owner, -100, 1000, -100, 1000) == kIOReturnSuccess && setScissor(owner, 64, 127, 32, 95) == kIOReturnSuccess &&
            fillRect(owner, 0, 0, 300, 300, C2) == kIOReturnSuccess) {
            SCAN(2, ex = (x >= 64 && x <= 127 && y >= 32 && y <= 95) ? C2 : C1);
            if (!bad_outside) pass |= 1u << 2;
        }
        /* 3: client batch (state + triangle, no address/scissor registers) is clipped by the KERNEL's scissor */
        tri[n++] = 0x0304; tri[n++] = 0; tri[n++] = MACH64_Z_CNTL; tri[n++] = 0x1; tri[n++] = MACH64_SCALE_3D_CNTL; tri[n++] = MACH64_SCALE_3D_CNTL_SOLID;
        tri[n++] = MACH64_DP_FRGD_CLR; tri[n++] = C3; tri[n++] = MACH64_DP_WRITE_MASK; tri[n++] = 0xffffffffu; tri[n++] = MACH64_DP_MIX; tri[n++] = 0x00070007u;
        tri[n++] = MACH64_DP_SRC; tri[n++] = MACH64_DP_SRC_FLAT; tri[n++] = MACH64_CLR_CMP_CNTL; tri[n++] = 0;
        for (v = 0; v < 3; v++) {
            uint32_t bb = MACH64_VERTEX_SLOT(v);
            tri[n++] = bb + MACH64_V_S; tri[n++] = 0; tri[n++] = bb + MACH64_V_T; tri[n++] = 0; tri[n++] = bb + MACH64_V_W; tri[n++] = 0x3f800000u;
            tri[n++] = bb + MACH64_V_SPEC_ARGB; tri[n++] = 0; tri[n++] = bb + MACH64_V_Z; tri[n++] = 0; tri[n++] = bb + MACH64_V_ARGB; tri[n++] = 0xffffffffu;
            tri[n++] = bb + MACH64_V_X_Y; tri[n++] = (((uint32_t)(vx[v] * 4)) << 16) | (((uint32_t)(vy[v] * 4)) & 0xffffu);
        }
        tri[n++] = MACH64_ONE_OVER_AREA_UC; tri[n++] = 0x37995b4du;
        { uint32_t s0 = fenceSubmitted();
          if (submitBatch(owner, tri, n / 2) == kIOReturnSuccess && fenceSubmitted() == s0 + 1 && waitFence(fenceSubmitted()) == kIOReturnSuccess && fenceCompleted() == fenceSubmitted()) {   /* DMA returns early; the fence completes at waitFence */
              SCAN(3, ex = (x >= 64 && x <= 127 && y >= 32 && y <= 95) ? C3 : C1);     /* triangle covers the whole box (4096 px), none outside */
              if (!bad_outside) pass |= 1u << 3;
          } }
        /* 4: a batch carrying a kernel-owned register (scissor, address) is refused as a whole and draws nothing */
        { uint32_t evil[6] = { MACH64_DP_FRGD_CLR, 0x1111, MACH64_SC_LEFT_RIGHT, 0x1fff0000u, MACH64_DST_OFF_PITCH, 0 }; uint32_t before = refusedCount();
          if (submitBatch(owner, evil, 3) == kIOReturnBadArgument && refusedCount() == before + 1) pass |= 1u << 4; }
        /* 5: a context with no target cannot submit; Z_CNTL without a Z buffer is neutralised (batch still runs, nothing outside the scissor changes) */
        { uint32_t one[2] = { MACH64_DP_FRGD_CLR, 1 };
          if (submitBatch(ownB, one, 1) == kIOReturnNotReady) pass |= 1u << 5; }
        /* 6: release frees the context and the VRAM; the slot can be reused */
        releaseOwner(ownB);
        { uint32_t t2;
          if (vramAlloc(owner, 4096, 4096, &t2) == kIOReturnSuccess && vramFree(owner, t2) == kIOReturnSuccess) pass |= 1u << 6; }
        *passmask = pass; *inside = 0; *outside = 0;
        releaseOwner(owner);
        return kIOReturnSuccess;
    }

    if (mask & 0x80000000u) {                          /* ---- triangle scissor-containment suite (low bits select cases) ---- */
        static const struct { uint32_t L, R, T, B; int32_t vx[3], vy[3]; uint32_t ooa; int expect_draw; } tc[] = {
            {  64, 127,  32,  95, { 10, 250,  60 }, {  10,  20, 240 }, 0x37995b4du, 1 },   /* big triangle, scissor box wholly inside it */
            {  64, 127,  32,  95, { 90, 250, 100 }, {  10,  60, 200 }, 0x380c4719u, 1 },   /* triangle edge crosses the scissor box */
            {  64, 127,  32,  95, {150, 240, 190 }, { 150, 150, 240 }, 0x3901742eu, 0 },   /* triangle entirely outside the scissor */
            {   0, 255,   0, 255, { 10, 250,  60 }, {  10,  20, 240 }, 0x37995b4du, 1 },   /* whole-surface scissor: shape check */
            { 100,  50,  32,  95, { 10, 250,  60 }, {  10,  20, 240 }, 0x37995b4du, 0 },   /* inverted scissor */
            {  64, 127,  32,  95, { 40, 120,  70 }, { 20,  40, 120 }, 0x390db310u, 1 },   /* small triangle partly in the scissor box (ooa filled in below) */
        };
        uint32_t t, tri_pass = 0, tri_bad = 0;
        for (t = 0; t < sizeof tc / sizeof tc[0]; t++) {
            uint32_t o, outside_c = 0, missing = 0, drawn = 0, amb = 0, sample = 0;
            int32_t ax = tc[t].vx[0], ay = tc[t].vy[0], bx = tc[t].vx[1], by = tc[t].vy[1], cx = tc[t].vx[2], cy = tc[t].vy[2];
            int rc;
            if (!(mask & (1u << t))) continue;
            for (o = 0; o < BLOCK_BYTES; o += 4) vw32(vram, off0 + o, 0xa5a5a5a5u);
            rc = eng_tri16(fRegs, off0, SURF_PX, tc[t].vx, tc[t].vy, COL_FILL, (tc[t].R << 16) | tc[t].L, (tc[t].B << 16) | tc[t].T, tc[t].ooa);
            if (!rc) rc = eng_wait_idle(fRegs);
            if (rc) { logEvent(RAGE_EV_ERROR, owner, 0xf1000000u | t, 0xdeadu, 0, 0, 0); *status = 6; break; }
            IOSleep(5);
            vw32(vram, off0 + 4u, vr32(vram, off0 + 4u));
            for (o = 0; o < BLOCK_BYTES; o += 2) {
                uint16_t v = vr16(vram, off0 + o); uint32_t px = o >> 1, x = px % SURF_PX, y = px / SURF_PX;
                int in_surf = o < SURF_BYTES, inscis = in_surf && x >= tc[t].L && x <= tc[t].R && y >= tc[t].T && y <= tc[t].B;
                if (v == SENT16) {
                    if (inscis) {                      /* undrawn pixel in the scissor box: a clear miss only if clearly inside the triangle */
                        int32_t qx = (int32_t)(x * 4 + 2), qy = (int32_t)(y * 4 + 2);
                        int32_t e0 = (bx - ax) * 4 * (qy - ay * 4) - (by - ay) * 4 * (qx - ax * 4);
                        int32_t e1 = (cx - bx) * 4 * (qy - by * 4) - (cy - by) * 4 * (qx - bx * 4);
                        int32_t e2 = (ax - cx) * 4 * (qy - cy * 4) - (ay - cy) * 4 * (qx - cx * 4);
                        int32_t m0 = 8 * ((bx - ax < 0 ? ax - bx : bx - ax) + (by - ay < 0 ? ay - by : by - ay)) * 4;
                        int32_t m1 = 8 * ((cx - bx < 0 ? bx - cx : cx - bx) + (cy - by < 0 ? by - cy : cy - by)) * 4;
                        int32_t m2 = 8 * ((ax - cx < 0 ? cx - ax : ax - cx) + (ay - cy < 0 ? cy - ay : ay - cy)) * 4;
                        if (e0 > m0 && e1 > m1 && e2 > m2) { missing++; if (!sample) sample = (x << 16) | y; }
                        else amb++;
                    }
                } else if (in_surf && v == COL_FILL && inscis) drawn++;
                else outside_c++;                      /* anything else that changed: outside the scissor, or a wrong colour */
            }
            logEvent(RAGE_EV_MARK, owner, 0xf0000000u | t, outside_c, missing, drawn, amb);
            if (sample) logEvent(RAGE_EV_MARK, owner, 0xf2000000u | t, sample, 0, 0, 0);
            if (outside_c == 0 && missing == 0 && (tc[t].expect_draw ? drawn > 0 : drawn == 0)) { *passmask |= 1u << t; tri_pass++; } else tri_bad++;
            *outside += outside_c; *inside += drawn;
        }
        releaseOwner(owner);
        return *status ? kIOReturnError : kIOReturnSuccess;
    }

    for (k = 0; k < sizeof cases / sizeof cases[0]; k++) {
        const struct tcase *c = &cases[k];
        uint32_t o, outside_c = 0, inside_ok = 0, inside_exp = 0, minx = 0xffff, miny = 0xffff, maxx = 0, maxy = 0;
        uint32_t hd = 0, hd_saved = 0; int rc, ffchanged = 0;
        if (!(mask & (1u << (c->id - 1)))) continue;

        /* sentinel everywhere; the source surface gets a pattern */
        for (o = 0; o < BLOCK_BYTES; o += 4) vw32(vram, off0 + o, 0xa5a5a5a5u);
        if (c->blit) for (o = 0; o < SURF_BYTES; o += 4) vw32(vram, off0 + SRC_AT + o, ((uint32_t)COL_SRC << 16) | COL_SRC);

        if (c->fastfill) { hd_saved = hd = eng_rd(fRegs, MACH64_HW_DEBUG); hd &= ~(MACH64_HW_DEBUG_AUTO_FF_DIS | MACH64_HW_DEBUG_AUTO_BLKWRT_DIS); eng_wr(fRegs, MACH64_HW_DEBUG, hd); ffchanged = 1; }
        {
            uint32_t sc_lr = (c->R << 16) | c->L, sc_tb = (c->B << 16) | c->T;
            rc = c->blit ? eng_blit16(fRegs, off0 + SRC_AT, off0, SURF_PX, 0, 0, c->dx, c->dy, c->w, c->h, sc_lr, sc_tb)
                         : eng_fill16(fRegs, off0, SURF_PX, c->dx, c->dy, c->w, c->h, COL_FILL, sc_lr, sc_tb, c->dc);
        }
        if (!rc) rc = eng_wait_idle(fRegs);
        if (ffchanged) eng_wr(fRegs, MACH64_HW_DEBUG, hd_saved);                 /* always restore */
        if (rc) { logEvent(RAGE_EV_ERROR, owner, 0xe1000000u | c->id, 0xdeadu, eng_rd(fRegs, MACH64_FIFO_STAT), eng_rd(fRegs, MACH64_GUI_STAT), (uint32_t)rc); *status = 6; break; }
        logEvent(RAGE_EV_MARK, owner, 0xe8000000u | c->id, eng_rd(fRegs, MACH64_GUI_STAT), eng_rd(fRegs, MACH64_FIFO_STAT), eng_rd(fRegs, MACH64_DST_OFF_PITCH), eng_rd(fRegs, MACH64_DST_Y_X));
        logEvent(RAGE_EV_MARK, owner, 0xe9000000u | c->id, eng_rd(fRegs, 0x06a0), eng_rd(fRegs, 0x06a4), eng_rd(fRegs, 0x06ac), eng_rd(fRegs, 0x06b0));   /* SC_LEFT, SC_RIGHT, SC_TOP, SC_BOTTOM */
        logEvent(RAGE_EV_MARK, owner, 0xea000000u | c->id, eng_rd(fRegs, 0x0530), eng_rd(fRegs, MACH64_GUI_TRAJ_CNTL), eng_rd(fRegs, MACH64_DP_SRC), eng_rd(fRegs, MACH64_DP_MIX));
        /* The engine's destination pixel cache is only flushed to the aperture by a CPU write INSIDE the render target (found with the old suite,
         * Nov 20): rewrite one word of the surface that is outside every scissor in this test (pixels 2..3 of row 0) with its own value. */
        IOSleep(50);                                                              /* experiment: no CPU access to VRAM at all for 50 ms after idle */
        vw32(vram, off0 + 4u, vr32(vram, off0 + 4u));

        /* verify every 16-bit pixel of the block; then once more after another CPU write + delay (is a short count just a stale engine cache?) */
        verify(vram, off0, c, &outside_c, &inside_ok, &inside_exp, &minx, &miny, &maxx, &maxy, owner, 0, this);
        if (inside_ok != inside_exp) {
            uint32_t ok2, ex2, oc2, a1, a2, a3, a4;
            IOSleep(20);
            vw32(vram, off0 + 8u, vr32(vram, off0 + 8u));                                        /* touch the surface again (outside the scissor, same value) */
            IOSleep(20);
            verify(vram, off0, c, &oc2, &ok2, &ex2, &a1, &a2, &a3, &a4, owner, 1, this);
            logEvent(RAGE_EV_MARK, owner, 0xe5000000u | c->id, oc2, ok2, ex2, 0);
        }
        *outside += outside_c; *inside += inside_ok;
        logEvent(RAGE_EV_MARK, owner, 0xe1000000u | c->id, outside_c, inside_ok, inside_exp, (maxx << 16) | (maxy & 0xffffu));
        logEvent(RAGE_EV_MARK, owner, 0xe2000000u | c->id, (minx << 16) | (miny & 0xffffu), (maxx << 16) | (maxy & 0xffffu), c->blit, c->fastfill);
        if (outside_c == 0 && inside_ok == inside_exp) *passmask |= 1u << (c->id - 1);
    }
    releaseOwner(owner);
    return *status ? kIOReturnError : kIOReturnSuccess;
}
#else
IOReturn RageLTDevice::engineSelfTest(uint32_t, uint32_t *, uint32_t *, uint32_t *, uint32_t *) { return kIOReturnUnsupported; }
#endif

#if RAGE_DEBUG_ENGTEST
IOMemoryDescriptor *RageLTDevice::apertureMemory(uint32_t owner, uint32_t off, uint32_t len)
{
    IOMemoryDescriptor *d = 0;
    if (!len || (off | len) & 4095u) return 0;
    IOLockLock(fLock);
    if (rage_vram_owns(&fVram, off, len, owner)) d = IODeviceMemory::withSubRange(fFB, off, len);
    IOLockUnlock(fLock);
    return d;
}

/* B1g-4: what do the CPU cache modes do to aperture store/load speed?  Only the caller's own allocated block is touched. */
IOReturn RageLTDevice::apertureBench(uint32_t owner, uint32_t off, uint32_t len, uint32_t mode, uint32_t *wns, uint32_t *rns)
{
    static const IOOptionBits opt[5] = { kIOMapInhibitCache, kIOMapWriteCombineCache, kIOMapWriteThruCache, kIOMapCopybackCache, kIOMapDefaultCache };
    IOMemoryMap *m; volatile uint8_t *v; uint32_t pass, i, sink = 0; IOReturn rc = kIOReturnBadArgument; AbsoluteTime t0, t1; uint64_t n0, n1, n2;
    uint32_t viaBcopy = mode & 0x100u; uint8_t *tmp = 0;       /* B8 experiment: mode | 0x100 = write with the kernel's bcopy (wide stores) from a cached buffer instead of word stores */
    mode &= 0xffu;
    if (mode > 4 || (off | len) & 3u || !len || len > 0x100000u) return kIOReturnBadArgument;
    m = (mode == 0) ? fMap : fFB->map(opt[mode]);
    if (!m) return kIOReturnNoMemory;
    if (mode == 0) m->retain();
    v = (volatile uint8_t *)m->getVirtualAddress();
    if (viaBcopy) { tmp = (uint8_t *)IOMalloc(len); if (!tmp) { m->release(); return kIOReturnNoMemory; } for (i = 0; i < len; i += 4) *(uint32_t *)(tmp + i) = i * 2654435761u; }
    IOLockLock(fLock);
    if (rage_vram_owns(&fVram, off, len, owner)) {
        rc = kIOReturnSuccess;
        if (fEngineLive && idleWait()) rc = kIOReturnTimeout;
        else {
            clock_get_uptime(&t0); absolutetime_to_nanoseconds(t0, &n0);
            if (viaBcopy) for (pass = 0; pass < 8; pass++) { bcopy(tmp, (void *)(v + off), len); __asm__ volatile("sync" ::: "memory"); }
            else for (pass = 0; pass < 8; pass++) { for (i = 0; i < len; i += 4) OSWriteLittleInt32((void *)v, off + i, i ^ pass); __asm__ volatile("sync" ::: "memory"); }
            (void)OSReadLittleInt32((void *)v, off + len - 4); __asm__ volatile("sync" ::: "memory");
            clock_get_uptime(&t1); absolutetime_to_nanoseconds(t1, &n1);
            for (pass = 0; pass < 8; pass++) for (i = 0; i < len; i += 4) sink += OSReadLittleInt32((void *)v, off + i);
            clock_get_uptime(&t0); absolutetime_to_nanoseconds(t0, &n2);
            *wns = (uint32_t)((n1 - n0) / 8u); *rns = (uint32_t)((n2 - n1) / 8u) + (sink == 0x12345678u ? 1u : 0u);
        }
    }
    IOLockUnlock(fLock);
    if (tmp) IOFree(tmp, len);
    m->release();
    return rc;
}
#endif

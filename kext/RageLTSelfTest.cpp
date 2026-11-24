/* kext/RageLTSelfTest.cpp - DEBUG ONLY (RAGE_DEBUG_ENGTEST): proves the hardware scissor contains every engine write (design E1).
 *
 * Safety design: every draw is bounded to ONE 512 KB block this test allocates from the VRAM allocator, so even a total scissor failure
 * cannot touch anything else.  Before any engine write the test checks the chip is alive and idle (CRTC enabled, extended registers
 * on).  Scissor edges are measured, not assumed: results go to the event ring (MARK events with a0 = 0xE1xxxxxx / 0xE2xxxxxx). */
#include "RageLTDevice.h"
#include "RageLTEngine.h"
#include <IOKit/IOLib.h>

#if RAGE_DEBUG_ENGTEST

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
        { 18,  0,  0,    0,0x1fff, 0,0x1fff, 100,100,  40,  40, 0 },  /* probe: DST_CNTL = 0 (right-to-left, bottom-to-top) */
        { 19,  0,  0,    0,0x1fff, 0,0x1fff, 100,100,  40,  40, 0x08000003u },  /* probe: DST_CNTL = 3 | bit 27 */
        { 20,  0,  0,    0,0x1fff, 0,0x1fff, 100,100,  40,  40, 0x00100003u },  /* probe: DST_CNTL = 3 | bit 20 */
        { 21,  0,  0,    0,0x1fff, 0,0x1fff, 100,100,  40,  40, 0x00010003u },  /* probe: DST_CNTL = 3 | bit 16 */
        { 22,  0,  0,    0,0x1fff, 0,0x1fff, 100,100,  40,  40, 0x00020003u },  /* probe: DST_CNTL = 3 | bit 17 */
        { 23,  0,  0,    0,0x1fff, 0,0x1fff, 100,100,  40,  40, 0x00040003u },  /* probe: DST_CNTL = 3 | bit 18 */
        { 24,  0,  0,    0,0x1fff, 0,0x1fff, 100,100,  40,  40, 0x00080003u },  /* probe: DST_CNTL = 3 | bit 19 */
        { 25,  0,  0,    0,0x1fff, 0,0x1fff, 100,100,  40,  40, 0x00200003u },  /* probe: DST_CNTL = 3 | bit 21 */
        { 26,  0,  0,    0,0x1fff, 0,0x1fff, 100,100,  40,  40, 0x00400003u },  /* probe: DST_CNTL = 3 | bit 22 */
        { 27,  0,  0,    0,0x1fff, 0,0x1fff, 100,100,  40,  40, 0x00800003u },  /* probe: DST_CNTL = 3 | bit 23 */
        { 28,  0,  0,    0,0x1fff, 0,0x1fff, 100,100,  40,  40, 0x01000003u },  /* probe: DST_CNTL = 3 | bit 24 */
        { 29,  0,  0,    0,0x1fff, 0,0x1fff, 100,100,  40,  40, 0x02000003u },  /* probe: DST_CNTL = 3 | bit 25 */
        { 30,  0,  0,    0,0x1fff, 0,0x1fff, 100,100,  40,  40, 0x04000003u },  /* probe: DST_CNTL = 3 | bit 26 */
        { 31,  0,  0,    0,0x1fff, 0,0x1fff, 100,100,  40,  40, 0x10000003u },  /* probe: DST_CNTL = 3 | bit 28 */
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
    {
        uint32_t pitch_px = ((crtcOP >> 22) & 0x3ffu) * 8u, base = (crtcOP & 0xfffffu) * 8u, lines = ((vtd >> 16) & 0x7ffu) + 1u;
        uint32_t depth = (gen >> 8) & 7u, bpp = depth == 2 ? 1 : depth == 3 || depth == 4 ? 2 : depth == 5 ? 3 : 4;
        uint32_t bytes = (pitch_px * lines * bpp + 0xfffu) & ~0xfffu;
        logEvent(RAGE_EV_MARK, 0, 0xe0000001u, base, pitch_px, lines, bpp);
        IOLockLock(fLock);
        if (!fDispReserved) {
            if (rage_vram_reserve(&fVram, base, bytes) || rage_vram_reserve(&fVram, 0x770000u, RAGE_VRAM_USABLE_END - 0x770000u)) {
                IOLockUnlock(fLock); *status = 4; return kIOReturnNoSpace;
            }
            fDispReserved = true;
        }
        IOLockUnlock(fLock);
    }
    owner = newOwner();
    if (vramAlloc(owner, BLOCK_BYTES, 4096, &off0)) { releaseOwner(owner); *status = 5; return kIOReturnNoSpace; }
    logEvent(RAGE_EV_MARK, owner, 0xe0000002u, off0, BLOCK_BYTES, 0, 0);

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

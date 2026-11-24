/* common/rage_guard.h - the ONE place that decides what a client may make the chip do.  Portable: no libc, no floating point,
 * header-only, so the same code runs in the kext and in host tests (tests/guard_test.c).
 *
 * Three checks (docs/memory-map.md, design 3.5):
 *   1. rage_vram_span_ok()   a VRAM span must lie wholly inside the usable range [0, 0x7FF800); the top 2 KB of the aperture IS the
 *                            register file, so a surface/Z/texture reaching it is a register write.
 *   2. rage_reg_ok()         a client may write only registers on the allow-list, with per-register forbidden bits.  Display
 *                            (CRTC), clocks, HW_DEBUG, bus control and the bus-master table registers are kernel-owned and refused.
 *   3. rage_batch_ok()       a whole {reg,val} batch passes or the whole batch is dropped (no partial application).
 *
 * KNOWN LIMIT (Phase 1): address registers are checked for their BASE only.  The extent (pitch x height, scissor) is not tracked
 * here; the engine class must track the destination rectangle and call rage_vram_span_ok() on the real extent before any trigger
 * register is written (design 3.1 RageLTEngine; open item E1 in docs/standalone-driver-design.md). */
#ifndef RAGE_GUARD_H
#define RAGE_GUARD_H

#include "rage_types.h"
#include "mach64_regs.h"

#define RAGE_VRAM_USABLE_END   MACH64_VRAM_USABLE_END     /* 0x7FF800 */
#define RAGE_BATCH_MAX_PAIRS   8192u                       /* sanity cap per submit */

static inline int rage_vram_span_ok(uint32_t off, uint64_t len)
{
    return off < RAGE_VRAM_USABLE_END && len <= (uint64_t)(RAGE_VRAM_USABLE_END - off);
}

enum { RAGE_ADDR_NONE = 0, RAGE_ADDR_OFF_PITCH = 1, RAGE_ADDR_BYTES = 2 };

struct rage_reg_rule {
    uint16_t lo, hi;      /* inclusive byte-offset range, dword aligned */
    uint8_t  addr;        /* RAGE_ADDR_*: how to read a VRAM base out of the value */
    uint32_t forbid;      /* value bits a client may not set */
};

/* Derived from the registers the emission core and the register header define for drawing (Phase 1 list; the measured 45-register
 * audit of Nov 20 was never saved, so this is rebuilt from source and re-audited in Phase 3 by the kext's own counters). */
static const struct rage_reg_rule rage_reg_rules[] = {
    { 0x0240, 0x02fc, RAGE_ADDR_NONE,      0 },            /* vertex setup block */
    { 0x0300, 0x0300, RAGE_ADDR_NONE,      0 },            /* ONE_OVER_AREA_UC */
    { 0x0304, 0x0304, RAGE_ADDR_NONE,      0 },            /* SETUP_CNTL */
    { 0x0500, 0x0500, RAGE_ADDR_OFF_PITCH, 0 },            /* DST_OFF_PITCH */
    { 0x050c, 0x0518, RAGE_ADDR_NONE,      0 },            /* DST_Y_X .. DST_HEIGHT_WIDTH */
    { 0x0548, 0x0548, RAGE_ADDR_OFF_PITCH, 0 },            /* Z_OFF_PITCH */
    { 0x054c, 0x0550, RAGE_ADDR_NONE,      0 },            /* Z_CNTL, ALPHA_TST_CNTL */
    { 0x0580, 0x0580, RAGE_ADDR_OFF_PITCH, 0 },            /* SRC_OFF_PITCH */
    { 0x058c, 0x0590, RAGE_ADDR_NONE,      0 },            /* SRC_Y_X, SRC_WIDTH1 */
    { 0x05b4, 0x05b4, RAGE_ADDR_NONE,      0x00000f00u },  /* SRC_CNTL: bus-master bits 8..11 are the kernel's */
    { 0x05c0, 0x05e8, RAGE_ADDR_BYTES,     0 },            /* TEX_0_OFF .. TEX_10_OFF (raw byte offsets) */
    { 0x05fc, 0x05fc, RAGE_ADDR_NONE,      0 },            /* SCALE_3D_CNTL */
    { 0x0600, 0x063c, RAGE_ADDR_NONE,      0 },            /* HOST_DATA0..15 */
    { 0x0680, 0x0684, RAGE_ADDR_NONE,      0 },            /* PAT_REG0/1 */
    { 0x06a8, 0x06a8, RAGE_ADDR_NONE,      0 },            /* SC_LEFT_RIGHT */
    { 0x06b4, 0x06b4, RAGE_ADDR_NONE,      0 },            /* SC_TOP_BOTTOM */
    { 0x06c4, 0x06d8, RAGE_ADDR_NONE,      0 },            /* DP_FRGD_CLR .. DP_SRC */
    { 0x06e8, 0x06ec, RAGE_ADDR_NONE,      0 },            /* DST_X_Y, DST_WIDTH_HEIGHT */
    { 0x0708, 0x0708, RAGE_ADDR_NONE,      0 },            /* CLR_CMP_CNTL */
    { 0x0730, 0x0730, RAGE_ADDR_NONE,      0 },            /* GUI_TRAJ_CNTL */
    { 0x0770, 0x0774, RAGE_ADDR_NONE,      0 },            /* TEX_SIZE_PITCH, TEX_CNTL */
    { 0x0778, 0x0778, RAGE_ADDR_BYTES,     0 },            /* SECONDARY_TEX_OFF */
};
#define RAGE_NREG_RULES ((uint32_t)(sizeof rage_reg_rules / sizeof rage_reg_rules[0]))

static inline int rage_reg_ok(uint32_t reg, uint32_t val)
{
    uint32_t i;
    if (reg & 3u || reg > MACH64_DRI_REG_MAX) return 0;
    for (i = 0; i < RAGE_NREG_RULES; i++) {
        const struct rage_reg_rule *r = &rage_reg_rules[i];
        if (reg < r->lo || reg > r->hi) continue;
        if (val & r->forbid) return 0;
        if (r->addr == RAGE_ADDR_OFF_PITCH) return rage_vram_span_ok((val & 0x3fffffu) << 3, 8u);   /* base (offset>>3 in bits 21:0) */
        if (r->addr == RAGE_ADDR_BYTES)     return rage_vram_span_ok(val, 8u);
        return 1;
    }
    return 0;
}

/* pairs[2*i] = register byte offset, pairs[2*i+1] = value.  All or nothing. */
static inline int rage_batch_ok(const uint32_t *pairs, uint32_t npairs)
{
    uint32_t i;
    if (!pairs || npairs == 0 || npairs > RAGE_BATCH_MAX_PAIRS) return 0;
    for (i = 0; i < npairs; i++) if (!rage_reg_ok(pairs[2 * i], pairs[2 * i + 1])) return 0;
    return 1;
}

#endif

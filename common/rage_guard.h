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
 * E1 (closed Nov 24, design 11.11-11.12): a client can no longer set any address, scissor, pixel-width or 2D-trigger register.  The
 * kernel's RageLTEngine owns the render target, scissor and extent, emits fills/blits itself, and the hardware clip contains every
 * client primitive (proven on the G4 for fills, blits and triangles).  rage_vram_span_ok() is applied to kernel-chosen extents.  */
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

/* User-writable registers.  Everything not listed is refused (display, clocks, bus control, bus-master, engine addresses, scissor, 2D triggers). */
static const struct rage_reg_rule rage_reg_rules[] = {
    /* Phase 3 / E1 (hardware-proven containment, design 11.11-11.12): the KERNEL owns everything that decides WHERE the engine writes - destination,
     * Z and source surfaces (OFF_PITCH), texture bases, the scissor, DP_PIX_WIDTH, and the 2D rectangle/blit registers and triggers.  A client
     * stream carries only per-primitive state and vertices; the kernel prepends the scissor/target state and clamps to it. */
    { 0x0240, 0x02fc, RAGE_ADDR_NONE,      0 },            /* vertex setup block (the draw itself; clipped by the kernel's scissor) */
    { 0x0300, 0x0300, RAGE_ADDR_NONE,      0 },            /* ONE_OVER_AREA_UC (triangle trigger) */
    { 0x0328, 0x033c, RAGE_ADDR_NONE,      0 },            /* SECONDARY_S/T/W of vertices 2 and 3 (multitexture; setup inputs, no address) */
    { 0x0304, 0x0304, RAGE_ADDR_NONE,      0 },            /* SETUP_CNTL (no memory-addressing bits) */
    { 0x054c, 0x0550, RAGE_ADDR_NONE,      0 },            /* Z_CNTL, ALPHA_TST_CNTL */
    { 0x05fc, 0x05fc, RAGE_ADDR_NONE,      0 },            /* SCALE_3D_CNTL */
    { 0x0680, 0x0684, RAGE_ADDR_NONE,      0 },            /* PAT_REG0/1 */
    { 0x06c4, 0x06cc, RAGE_ADDR_NONE,      0 },            /* DP_FRGD_CLR, DP_BKGD_CLR, DP_WRITE_MASK (0x6d0 DP_PIX_WIDTH is the kernel's) */
    { 0x06d4, 0x06d8, RAGE_ADDR_NONE,      0 },            /* DP_MIX, DP_SRC */
    { 0x0708, 0x0708, RAGE_ADDR_NONE,      0 },            /* CLR_CMP_CNTL */
    { 0x0770, 0x0774, RAGE_ADDR_NONE,      0 },            /* TEX_SIZE_PITCH, TEX_CNTL */
};
#define RAGE_NREG_RULES ((uint32_t)(sizeof rage_reg_rules / sizeof rage_reg_rules[0]))

/* Fast path: one byte per register, 1 when the FIRST matching rule is a plain one (no address, no forbidden bits) so any value passes.  Built once, from the rules above;
 * everything else (forbidden bits, addresses, unlisted registers) takes the exact scan.  The result is identical to the scan (tests/guard_test.c compares them). */
#define RAGE_PLAIN_N 0x200u
static unsigned char rage_plain[RAGE_PLAIN_N]; static int rage_plain_ready;
static inline void rage_plain_build(void)
{
    uint32_t reg, i;
    for (reg = 0; reg < RAGE_PLAIN_N * 4u; reg += 4u) {
        unsigned char ok = 0;
        for (i = 0; i < RAGE_NREG_RULES; i++) {
            const struct rage_reg_rule *r = &rage_reg_rules[i];
            if (reg < r->lo || reg > r->hi) continue;
            ok = (r->addr == RAGE_ADDR_NONE && r->forbid == 0) ? 1 : 0;
            break;
        }
        if (reg > MACH64_DRI_REG_MAX) ok = 0;
        rage_plain[reg >> 2] = ok;
    }
    rage_plain_ready = 1;
}

static inline int rage_reg_ok(uint32_t reg, uint32_t val)
{
    uint32_t i;
    if (!rage_plain_ready) rage_plain_build();
    if (!(reg & 3u) && (reg >> 2) < RAGE_PLAIN_N && rage_plain[reg >> 2]) return 1;
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

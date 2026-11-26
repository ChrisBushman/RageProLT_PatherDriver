/* tests/guard_test.c - host tests for common/rage_guard.h and common/rage_vram.h.  Build: see Makefile target `hosttest`. */
#include <stdio.h>
#include <string.h>
#include <stdint.h>
#include "rage_vram.h"

static int fails, checks;
#define CHECK(name, cond) do { checks++; if (!(cond)) { fails++; printf("FAIL  %s\n", name); } } while (0)

static void t_span(void)
{
    CHECK("span: 0..1 ok", rage_vram_span_ok(0, 1));
    CHECK("span: whole usable range ok", rage_vram_span_ok(0, 0x7ff800));
    CHECK("span: one byte past usable refused", !rage_vram_span_ok(0, 0x7ff801));
    CHECK("span: starts at register window refused", !rage_vram_span_ok(0x7ff800, 1));
    CHECK("span: straddles window refused", !rage_vram_span_ok(0x7ff000, 0x1000));
    CHECK("span: last byte ok", rage_vram_span_ok(0x7ff7ff, 1));
    CHECK("span: 32-bit overflow refused", !rage_vram_span_ok(0xfffff000u, 0x2000));
    CHECK("span: huge length refused", !rage_vram_span_ok(0x1000, 0xffffffffffull));
    CHECK("span: past 8MB refused", !rage_vram_span_ok(0x900000, 64));
}

/* the table fast path in rage_reg_ok must agree with the plain rule scan for every register and a spread of values */
static int ref_reg_ok(uint32_t reg, uint32_t val)
{
    uint32_t i;
    if (reg & 3u || reg > MACH64_DRI_REG_MAX) return 0;
    for (i = 0; i < RAGE_NREG_RULES; i++) {
        const struct rage_reg_rule *r = &rage_reg_rules[i];
        if (reg < r->lo || reg > r->hi) continue;
        if (val & r->forbid) return 0;
        if (r->addr == RAGE_ADDR_OFF_PITCH) return rage_vram_span_ok((val & 0x3fffffu) << 3, 8u);
        if (r->addr == RAGE_ADDR_BYTES)     return rage_vram_span_ok(val, 8u);
        return 1;
    }
    return 0;
}
static void t_fast_equiv(void)
{
    static const uint32_t vals[] = { 0, 1, 0x80000000u, 0xffffffffu, 0x00400000u, 0x007ff800u, 0x1fff0000u, 0xdeadbeefu, 0x60040464u, 0x12345678u };
    uint32_t reg, k; int bad = 0;
    for (reg = 0; reg < 0x1000u; reg++) for (k = 0; k < sizeof vals / sizeof vals[0]; k++) if (rage_reg_ok(reg, vals[k]) != ref_reg_ok(reg, vals[k])) bad++;
    CHECK("reg: table fast path == rule scan (all regs 0..0xfff x 10 values)", bad == 0);
}

static void t_regs(void)
{
    uint32_t good_pitch = (80u << 22) | (0x97000u >> 3);          /* 640 px pitch, offset 0x97000 */
    /* E1: everything that decides where the engine writes is kernel-owned; a client may not set it, whatever the value */
    CHECK("reg: DST_OFF_PITCH is kernel-owned (even a sane value)", !rage_reg_ok(MACH64_DST_OFF_PITCH, good_pitch));
    CHECK("reg: Z_OFF_PITCH kernel-owned", !rage_reg_ok(MACH64_Z_OFF_PITCH, good_pitch));
    CHECK("reg: SRC_OFF_PITCH kernel-owned", !rage_reg_ok(MACH64_SRC_OFF_PITCH, good_pitch));
    CHECK("reg: TEX_0_OFF .. TEX_10_OFF kernel-owned", !rage_reg_ok(MACH64_TEX_0_OFF, 0x400000) && !rage_reg_ok(0x5e8, 0x1000));
    CHECK("reg: SECONDARY_TEX_OFF kernel-owned", !rage_reg_ok(MACH64_SECONDARY_TEX_OFF, 0x1000));
    CHECK("reg: scissor kernel-owned", !rage_reg_ok(MACH64_SC_LEFT_RIGHT, 0x1fff0000u) && !rage_reg_ok(MACH64_SC_TOP_BOTTOM, 0x1fff0000u));
    CHECK("reg: scissor single registers (SC_LEFT/RIGHT/TOP/BOTTOM) refused", !rage_reg_ok(0x6a0, 0) && !rage_reg_ok(0x6a4, 0) && !rage_reg_ok(0x6ac, 0) && !rage_reg_ok(0x6b0, 0));
    CHECK("reg: DP_PIX_WIDTH kernel-owned", !rage_reg_ok(MACH64_DP_PIX_WIDTH, 0x60040464u));
    CHECK("reg: 2D rectangle / blit registers and triggers kernel-owned",
          !rage_reg_ok(MACH64_DST_Y_X, 0) && !rage_reg_ok(MACH64_DST_HEIGHT_WIDTH, 0x100010) && !rage_reg_ok(MACH64_DST_X_Y, 0) &&
          !rage_reg_ok(MACH64_DST_WIDTH_HEIGHT, 0x100010) && !rage_reg_ok(MACH64_SRC_Y_X, 0) && !rage_reg_ok(MACH64_SRC_WIDTH1, 0));
    CHECK("reg: SRC_CNTL (bus-master source) refused", !rage_reg_ok(MACH64_SRC_CNTL, 0) && !rage_reg_ok(MACH64_SRC_CNTL, 1u << 8));
    CHECK("reg: HOST_DATA window and GUI_TRAJ_CNTL refused", !rage_reg_ok(MACH64_HOST_DATA0, 0xdeadbeef) && !rage_reg_ok(0x63c, 0) && !rage_reg_ok(MACH64_GUI_TRAJ_CNTL, 3));
    CHECK("reg: BUS_CNTL refused", !rage_reg_ok(MACH64_BUS_CNTL, 0));
    CHECK("reg: BM_ADDR refused", !rage_reg_ok(MACH64_BM_ADDR, 0x1000));
    CHECK("reg: BM_GUI_TABLE_CMD refused", !rage_reg_ok(MACH64_BM_GUI_TABLE_CMD, 0x1000));
    CHECK("reg: BM_GUI_TABLE refused", !rage_reg_ok(MACH64_BM_GUI_TABLE, 0x1000));
    CHECK("reg: CRTC_OFF_PITCH refused", !rage_reg_ok(MACH64_CRTC_OFF_PITCH, 0));
    CHECK("reg: CRTC_V_TOTAL_DISP refused", !rage_reg_ok(MACH64_CRTC_V_TOTAL_DISP, 0));
    CHECK("reg: HW_DEBUG refused", !rage_reg_ok(MACH64_HW_DEBUG, 0));
    CHECK("reg: GEN_TEST_CNTL (engine reset) / DST_CNTL refused", !rage_reg_ok(0x04d0, 0) && !rage_reg_ok(0x0530, 0));
    CHECK("reg: unaligned refused", !rage_reg_ok(0x0244 + 2, 0));
    CHECK("reg: past DRI max refused", !rage_reg_ok(0x800, 0) && !rage_reg_ok(0xfffffffcu, 0));
    CHECK("reg: vertex block ok", rage_reg_ok(0x240, 1) && rage_reg_ok(0x2fc, 1));
    CHECK("reg: below vertex block refused", !rage_reg_ok(0x23c, 1));
    CHECK("reg: SETUP_CNTL / ONE_OVER_AREA ok", rage_reg_ok(MACH64_SETUP_CNTL, 0) && rage_reg_ok(MACH64_ONE_OVER_AREA_UC, 0x3f800000u));
    CHECK("reg: per-primitive state ok (Z, alpha test, SCALE_3D, DP colours/mix/src, CLR_CMP, TEX size/cntl, PAT)",
          rage_reg_ok(MACH64_Z_CNTL, 0) && rage_reg_ok(0x550, 0) && rage_reg_ok(MACH64_SCALE_3D_CNTL, 0x100c1u) &&
          rage_reg_ok(MACH64_DP_FRGD_CLR, 1) && rage_reg_ok(MACH64_DP_WRITE_MASK, 0xffffffffu) && rage_reg_ok(MACH64_DP_MIX, 0x70003u) &&
          rage_reg_ok(MACH64_DP_SRC, 0x101) && rage_reg_ok(MACH64_CLR_CMP_CNTL, 0) && rage_reg_ok(0x770, 0) && rage_reg_ok(0x774, 0) &&
          rage_reg_ok(0x680, 0) && rage_reg_ok(0x684, 0));
    CHECK("reg: FIFO_STAT / GUI_STAT (read-only status) refused", !rage_reg_ok(MACH64_FIFO_STAT, 0) && !rage_reg_ok(MACH64_GUI_STAT, 0));
}

static void t_batch(void)
{
    uint32_t ok[4] = { MACH64_Z_CNTL, 0, MACH64_DP_FRGD_CLR, 0x1234 };
    uint32_t bad[4] = { MACH64_Z_CNTL, 0, MACH64_BUS_CNTL, 0 };
    uint32_t addr[4] = { MACH64_Z_CNTL, 0, MACH64_DST_OFF_PITCH, (80u << 22) | (0x97000u >> 3) };
    CHECK("batch: good batch ok", rage_batch_ok(ok, 2));
    CHECK("batch: one bad register drops the whole batch", !rage_batch_ok(bad, 2));
    CHECK("batch: a kernel-owned address register drops the whole batch", !rage_batch_ok(addr, 2));
    CHECK("batch: NULL refused", !rage_batch_ok(0, 2));
    CHECK("batch: empty refused", !rage_batch_ok(ok, 0));
    CHECK("batch: over cap refused", !rage_batch_ok(ok, RAGE_BATCH_MAX_PAIRS + 1));
}

static void t_vram(void)
{
    static struct rage_vram v; uint32_t a, b, c, d, i;
    rage_vram_init(&v);
    CHECK("vram: reserve scanout 0..0x96000", rage_vram_reserve(&v, 0, 0x96000) == 0);
    CHECK("vram: overlapping reserve refused", rage_vram_reserve(&v, 0x95000, 0x2000) < 0);
    CHECK("vram: reserve in window refused", rage_vram_reserve(&v, 0x7ff800, 8) < 0);
    CHECK("vram: reserve cursor block 0x770000..0x7ff800", rage_vram_reserve(&v, 0x770000, 0x7f800) == 0);
    CHECK("vram: alloc 1 MB", rage_vram_alloc(&v, 0x100000, 8, 1, &a) == 0 && a >= 0x96000 && a + 0x100000 <= 0x770000);
    CHECK("vram: alloc aligned 4 KB", rage_vram_alloc(&v, 100, 4096, 1, &b) == 0 && (b & 0xfff) == 0 && b >= a + 0x100000);
    CHECK("vram: owner 0 may not allocate", rage_vram_alloc(&v, 8, 8, 0, &c) < 0);
    CHECK("vram: bad alignment refused", rage_vram_alloc(&v, 8, 12, 1, &c) < 0 && rage_vram_alloc(&v, 8, 4, 1, &c) < 0);
    CHECK("vram: zero length refused", rage_vram_alloc(&v, 0, 8, 1, &c) < 0);
    CHECK("vram: larger than all free space refused", rage_vram_alloc(&v, 0x800000, 8, 1, &c) < 0);
    CHECK("vram: wrong owner cannot free", rage_vram_free(&v, a, 2) < 0);
    CHECK("vram: reservation cannot be freed", rage_vram_free(&v, 0, 1) < 0 && rage_vram_free(&v, 0, 0) < 0);
    CHECK("vram: owns() inside", rage_vram_owns(&v, a + 16, 0x1000, 1) && !rage_vram_owns(&v, a + 16, 0x1000, 2));
    CHECK("vram: owns() overrun refused", !rage_vram_owns(&v, a, 0x100001, 1));
    CHECK("vram: free once ok, twice refused", rage_vram_free(&v, a, 1) == 0 && rage_vram_free(&v, a, 1) < 0);
    CHECK("vram: freed hole reused (best fit)", rage_vram_alloc(&v, 0x80000, 8, 3, &c) == 0 && c == a);
    CHECK("vram: free_owner releases all of one client", rage_vram_alloc(&v, 0x1000, 8, 4, &d) == 0 && rage_vram_alloc(&v, 0x1000, 8, 4, &d) == 0 && rage_vram_free_owner(&v, 4) == 2);
    CHECK("vram: free_owner(0) frees nothing", rage_vram_free_owner(&v, 0) == 0);
    /* exhaust the table: never overflows, never overlaps */
    for (i = 0; i < 400; i++) if (rage_vram_alloc(&v, 8, 8, 9, &d)) break;
    CHECK("vram: extent table full is an error, not corruption", i < 400);
    { uint32_t k, ok2 = 1; for (k = 0; k + 1 < v.n; k++) if (v.e[k].off + v.e[k].len > v.e[k + 1].off) ok2 = 0;
      CHECK("vram: extents stay sorted and non-overlapping", ok2); }
    rage_vram_free_owner(&v, 9);
    CHECK("vram: all of owner 9 released", rage_vram_alloc(&v, 8, 8, 9, &d) == 0);
}

int main(void)
{
    t_span(); t_regs(); t_fast_equiv(); t_batch(); t_vram();
    printf("guard_test: %d checks, %d failed\n", checks, fails);
    return fails ? 1 : 0;
}

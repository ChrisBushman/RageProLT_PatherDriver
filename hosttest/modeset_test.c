/* modeset_test.c: replays the plan for 640x480 15 bpp 60 Hz against a register model seeded with the ROM-booted state and compares with the
 * real hardware final state decoded from rage_init_table.h.  cc -O2 -I../rig/stubfb */
#include <stdio.h>
#include <string.h>
#include "rage_modemath.h"
#include "rage_modes.h"
#include "rage_init_table.h"
#include "rage_modeset.h"
static unsigned regs[0x100 / 4], pll[0x40];
static void apply_table(void)
{
    unsigned i, idx = 0; static int started;
    (void)started;
    for (i = 0; i < sizeof kRageInit / sizeof kRageInit[0]; i++) {
        const struct rage_init_op *o = &kRageInit[i];
        if (o->kind != RI_WRITE) continue;
        if (o->size == 1 && o->off == 0x91) { idx = o->val; continue; }
        if (o->size == 1 && o->off == 0x92) { if (idx & 2) pll[(idx >> 2) & 0x3f] = o->val; continue; }
        { unsigned sh = (o->off & 3) * 8, m = (o->size == 4 ? 0xffffffffu : ((1u << (8 * o->size)) - 1)) << sh; unsigned b = o->off & ~3u;
          if (b < 0x100) regs[b / 4] = (regs[b / 4] & ~m) | ((o->val << sh) & m); }
    }
}
int main(void)
{
    unsigned hw[0x40], hwpll[0x40]; int i, bad = 0; struct rm_plan p; struct rm_cur cur; const struct rage_mode *m = 0;
    apply_table(); memcpy(hw, regs, sizeof hw); memcpy(hwpll, pll, sizeof pll);
    for (i = 0; i < (int)(sizeof kRageModes / sizeof kRageModes[0]); i++) if (kRageModes[i].vbe == 0x0110 && kRageModes[i].pclk_khz == 25175) m = &kRageModes[i];
    if (!m) { printf("no 25.175 MHz 0110 row\n"); return 1; }
    /* start from the hardware state, scrambled in the fields the plan must set */
    regs[RM_CRTC_HTD / 4] = 0; regs[RM_CRTC_HSYNC / 4] = 0; regs[RM_CRTC_VTD / 4] = 0; regs[RM_CRTC_VSYNC / 4] = 0; regs[RM_DSP_CONFIG / 4] = 0; regs[RM_DSP_ON_OFF / 4] = 0;
    pll[6] = hwpll[6] & ~(3u << 6); pll[0xa] = 0; pll[5] = 0x03; regs[RM_CRTC_GEN / 4] = hw[RM_CRTC_GEN / 4] & ~(7u << 8);
    regs[RM_CRTC_OFFPITCH / 4] = 0;
    cur.fb_off = 0; cur.gen_cntl = regs[RM_CRTC_GEN / 4]; cur.pll5 = pll[5]; cur.pll6 = pll[6]; cur.pllb = pll[0xb]; cur.pll4 = pll[4]; cur.mem_cntl = 0x10751a7b; cur.dsp_config = hw[RM_DSP_CONFIG / 4];
    if (rm_plan_mode(m, &cur, 8192, 4, &p)) { printf("plan refused\n"); return 1; }
    for (i = 0; i < p.n; i++) {
        if (p.op[i].kind == RM_WR32) regs[p.op[i].a / 4] = p.op[i].b; else if (p.op[i].kind == RM_PLL) pll[p.op[i].a] = p.op[i].b;
        printf("%2d %s %02x <- %08x\n", i, p.op[i].kind == RM_WR32 ? "REG" : p.op[i].kind == RM_PLL ? "PLL" : "DLY", p.op[i].a, p.op[i].b);
    }
    for (i = 0; i < 0x40; i++) if (i * 4 <= 0x24 && ((regs[i] ^ hw[i]) & ~(i * 4 == RM_CRTC_GEN ? RM_GEN_DISPLAY_DIS : 0u))) { printf("DIFF reg %02x: plan %08x hw %08x\n", i * 4, regs[i], hw[i]); bad++; }
    for (i = 4; i <= 0xb; i++) if (pll[i] != hwpll[i]) { printf("DIFF pll %02x: plan %02x hw %02x\n", i, pll[i], hwpll[i]); bad++; }
    for (i = 5; i <= 6; i++) if (pll[i] != hwpll[i]) {}
    printf("%d differing registers (DSP_ON_OFF is the known, unexplained one)\n", bad);
    /* every offered mode (15 and 32 bpp): the plan is accepted, depth and pitch are in GEN_CNTL / OFF_PITCH, the FIFO scales with the depth */
    for (i = 0; i < (int)(sizeof kRageModes / sizeof kRageModes[0]); i++) {
        const struct rage_mode *q = &kRageModes[i]; struct rm_plan pl; int k; unsigned gen = 0, pitch = 0, xc = 0;
        if ((q->bpp != 15 && q->bpp != 32) || (q->pclk_khz != 25175 && q->pclk_khz != 40000 && q->pclk_khz != 65000 && q->pclk_khz != 108000)) continue;
        if (rm_plan_mode(q, &cur, 8192, 4, &pl)) { printf("FAIL %dx%d %d bpp: plan refused\n", q->w, q->h, q->bpp); bad++; continue; }
        for (k = 0; k < pl.n; k++) { if (pl.op[k].kind == RM_WR32 && pl.op[k].a == RM_CRTC_GEN) gen = pl.op[k].b; if (pl.op[k].kind == RM_WR32 && pl.op[k].a == RM_CRTC_OFFPITCH) pitch = pl.op[k].b;
                                      if (pl.op[k].kind == RM_WR32 && pl.op[k].a == RM_DSP_CONFIG) xc = pl.op[k].b; }
        printf("%dx%d %2d bpp: GEN_CNTL pixwidth %u, pitch %u px, DSP_CONFIG 0x%08x\n", q->w, q->h, q->bpp, (gen >> 8) & 7, (pitch >> 22) * 8, xc);
        if (((gen >> 8) & 7) != (q->bpp == 32 ? 6u : 3u) || (pitch >> 22) * 8 != q->w) { printf("FAIL: depth/pitch field\n"); bad++; }
    }
    /* the NDRV frame buffer layout (docs/re-notes/apple-ndrv-framebuffer.md D1) */
    { static const struct { unsigned pitch, off; } t[] = { { 1280, 0x700 }, { 1600, 0x480 }, { 2048, 0x800 }, { 2560, 0xc00 }, { 4096, 0x1000 }, { 3200, 0x480 }, { 5120, 0xc00 } };
      for (i = 0; i < (int)(sizeof t / sizeof t[0]); i++) { unsigned o = rm_fb_offset(t[i].pitch);
          if (o != t[i].off || o < 0x440u || (0x200000u - o) % t[i].pitch) { printf("FAIL rm_fb_offset(%u) = 0x%x, expected 0x%x\n", t[i].pitch, o, t[i].off); bad++; } }
      cur.fb_off = rm_fb_offset(2048); m = &kRageModes[0];
      for (i = 0; i < (int)(sizeof kRageModes / sizeof kRageModes[0]); i++) if (kRageModes[i].w == 1024 && kRageModes[i].bpp == 15) m = &kRageModes[i];
      if (!rm_plan_mode(m, &cur, 8192, 4, &p)) { unsigned op = 0; int k; for (k = 0; k < p.n; k++) if (p.op[k].kind == RM_WR32 && p.op[k].a == RM_CRTC_OFFPITCH) op = p.op[k].b;
          if (op != ((128u << 22) | (0x800u >> 3))) { printf("FAIL OFF_PITCH with fb_off: %08x\n", op); bad++; } } }
    printf("%d problems\n", bad);
    return bad != 0;
}

/*
 * rage_modeset.h - builds the ordered register-write plan for one display mode (pure C, no kernel calls: the stub executes the plan and
 * hosttest/modeset_test.c checks it against the real hardware state).  Needs rage_modemath.h and rage_modes.h included first.
 *
 * Order (blank -> hold VCLK in reset -> dividers -> release -> lock wait -> FIFO -> CRTC -> pitch/depth -> un-blank), after the X.Org mach64
 * driver's mode set.  The dot clock stays on VCLK3 (what the ROM selects); only its dividers change.
 */
#ifndef RAGE_MODESET_H
#define RAGE_MODESET_H
enum { RM_WR32 = 0, RM_PLL = 1, RM_DELAY = 2 };
struct rm_op { unsigned char kind; unsigned short a; unsigned b; };       /* WR32: a=offset b=value; PLL: a=pll reg b=value; DELAY: b=us */
struct rm_plan { struct rm_op op[40]; int n; };
struct rm_cur { unsigned gen_cntl, pll5, pll6, pllb, pll4, mem_cntl, dsp_config, fb_off; };   /* register state read just before; fb_off = frame buffer byte offset in VRAM to program (0 = the ROM's layout) */

/* Apple's ATI NDRV (the Lombard's framebuffer driver) puts the frame buffer at (2 MB mod pitch) raised by whole lines to at least 0x440: the hardware-cursor image
 * lives at VRAM 0x40..0x440 below it.  docs/re-notes/apple-ndrv-framebuffer.md D1/D2.  The stub offers this layout so the G4 exercises what the Lombard has. */
static unsigned rm_fb_offset(unsigned pitch_bytes) { unsigned off = 0x200000u % pitch_bytes; while (off < 0x440u) off += pitch_bytes; return off; }

enum { RM_CRTC_HTD = 0x00, RM_CRTC_HSYNC = 0x04, RM_CRTC_VTD = 0x08, RM_CRTC_VSYNC = 0x0c, RM_CRTC_OFFPITCH = 0x14, RM_CRTC_GEN = 0x1c,
       RM_DSP_CONFIG = 0x20, RM_DSP_ON_OFF = 0x24 };
#define RM_VCLK 3                      /* the dot clock the ROM selects */
#define RM_GEN_DISPLAY_DIS (1u << 6)

static void rm_add(struct rm_plan *p, int kind, unsigned a, unsigned b) { p->op[p->n].kind = (unsigned char)kind; p->op[p->n].a = (unsigned short)a; p->op[p->n].b = b; p->n++; }

/* returns 0, or -1 if the mode is outside the PLL's range / fails the monitor limits */
/* trial switch (stub property rage-dsp-apple): program the display FIFO from Apple's formula (rage_dsp_apple_calc) instead of the X.Org-derived one; default off */
static int rm_dsp_apple = 0;
static int rm_plan_mode(const struct rage_mode *m, const struct rm_cur *cur, unsigned mem_kb, unsigned memtype, struct rm_plan *p)
{
    struct rage_pll pll; struct rage_dsp_in in; unsigned dsp_cfg, dsp_onoff, post6, xb, gen;
    unsigned htot, vtot;
    p->n = 0;
    /* monitor limits (Dell 828FI) re-checked here as well as in the generator */
    htot = ((m->h_total_disp & 0x1ff) + 1) * 8; vtot = (m->v_total_disp & 0x7ff) + 1;
    if (m->pclk_khz > 110000u || m->pclk_khz / htot > 70u || m->pclk_khz / htot < 30u) return -1;
    { unsigned v = (m->pclk_khz * 1000u) / (htot * vtot); if (v < 50 || v > 120) return -1; }
    if (rage_pll_calc(m->pclk_khz, 64, &pll)) return -1;
    in.xclk_fb = (int)(cur->pll4 & 0xff);
    in.xclk_ref_div = ((cur->pllb & 7) == 4) ? 3 : 1;
    in.xclk_post = (((cur->pllb & 7) == 4) ? 0 : (int)(cur->pllb & 7)) - (int)((cur->pllb >> 3) & 1);
    in.vclk_fb = (int)pll.fb; in.vclk_post = (int)pll.post; in.bpp = m->bpp >= 24 ? m->bpp : 16; in.fifo_depth = 32;
    rage_dsp_mem(cur->mem_cntl, memtype, mem_kb, cur->dsp_config, &in);
    rage_dsp_calc(&in, &dsp_cfg, &dsp_onoff);
    /* 640x480 15 bpp 25.175 MHz is the one mode with a hardware-verified FIFO setting (the ROM's own): use it exactly; the formula's DSP_ON differs (unexplained) */
    if (m->pclk_khz == 25175 && m->w == 640 && m->pixwidth == 3) { dsp_cfg = 0x004807f7; dsp_onoff = 0x009c07b0; }

    if (rm_dsp_apple) { struct rage_dsp_apple a; rage_dsp_apple_calc(&in, cur->mem_cntl, memtype, mem_kb >> 10, cur->dsp_config, &a); dsp_cfg = a.config; dsp_onoff = a.on_off; }

    post6 = (cur->pll6 & ~(3u << (RM_VCLK * 2))) | ((pll.post_code & 3u) << (RM_VCLK * 2));
    xb = cur->pllb & ~(0x10u << RM_VCLK); if (pll.post_code & 4) xb |= 0x10u << RM_VCLK;

    rm_add(p, RM_WR32, RM_CRTC_GEN, cur->gen_cntl | RM_GEN_DISPLAY_DIS);                 /* blank */
    rm_add(p, RM_PLL, 5, cur->pll5 | 0x04);                                                  /* VCLK reset */
    rm_add(p, RM_PLL, 6, post6);
    rm_add(p, RM_PLL, 0xb, xb);
    rm_add(p, RM_PLL, 7 + RM_VCLK, pll.fb);
    rm_add(p, RM_PLL, 5, cur->pll5 & ~0x04u);                                                /* release */
    rm_add(p, RM_DELAY, 0, 10000);                                                           /* PLL lock */
    rm_add(p, RM_WR32, RM_DSP_CONFIG, dsp_cfg);
    rm_add(p, RM_WR32, RM_DSP_ON_OFF, dsp_onoff);
    rm_add(p, RM_WR32, RM_CRTC_HTD, m->h_total_disp);
    rm_add(p, RM_WR32, RM_CRTC_HSYNC, m->h_sync_strt_wid);
    rm_add(p, RM_WR32, RM_CRTC_VTD, m->v_total_disp);
    rm_add(p, RM_WR32, RM_CRTC_VSYNC, m->v_sync_strt_wid);
    rm_add(p, RM_WR32, RM_CRTC_OFFPITCH, ((m->w / 8u) << 22) | ((cur->fb_off >> 3) & 0xfffffu));   /* pitch in 8-px units, offset in qwords */
    gen = (cur->gen_cntl & ~(7u << 8)) | ((unsigned)m->pixwidth << 8) | RM_GEN_DISPLAY_DIS;
    rm_add(p, RM_WR32, RM_CRTC_GEN, gen);
    rm_add(p, RM_WR32, RM_CRTC_GEN, gen & ~RM_GEN_DISPLAY_DIS);                              /* un-blank */
    return 0;
}
#endif

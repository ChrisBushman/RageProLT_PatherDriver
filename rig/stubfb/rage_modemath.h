/*
 * rage_modemath.h - integer-only dot-clock PLL and display-FIFO (DSP) calculation for the Rage LT Pro (no floating point, no 64-bit division,
 * no kernel dependencies: also built by hosttest).
 *
 * Sources: PLL recipe and divider tables from the X.Org mach64 driver (aticlock.c "Internal" clock); FIFO formula from its atidsp.c.
 * Calibrated against the one real hardware point we have (640x480 15 bpp, 25.175 MHz, the ROM's own programming): the formula gives
 * DSP_CONFIG precision 4, XCLKS_PER_QW 2039 and loop latency 8 exactly as the ROM did, DSP_OFF 1966 (ROM 1968). DSP_ON comes out 192 vs the ROM's 156: UNEXPLAINED (the ROM's memory-timing
 * inputs are not recoverable); do not assume the larger value is safe until checked on hardware (hosttest/modemath_test.c).
 */
#ifndef RAGE_MODEMATH_H
#define RAGE_MODEMATH_H

#define RAGE_REF_2X_KHZ   58998u      /* 2 x 29.498928 MHz (ATI's reference crystal) */
#define RAGE_VCO_MIN_KHZ  125000u     /* VCO window; the ROM's own choices (25.175 MHz -> x8 = 201 MHz) lie inside it */
#define RAGE_VCO_MAX_KHZ  250000u

/* PLL_VCLK_POST_DIV code -> divider (X.Org ATI264xTPostDividers): codes 4..7 use the extra XDIV bit (code >> 2) */
static const unsigned char rage_post_div[8] = { 1, 2, 4, 8, 3, 0, 6, 12 };

struct rage_pll { unsigned post_code, post, fb, ref_div, achieved_khz; };

/* choose post-divider and feedback divider for `pclk_khz`; ref_div is the chip's PLL_REF_DIV (0x40 on this card). Returns 0 on success. */
static int rage_pll_calc(unsigned pclk_khz, unsigned ref_div, struct rage_pll *o)
{
    static const unsigned char order[7] = { 0, 1, 2, 3, 4, 6, 7 };     /* plain power-of-two dividers first, extended ones only if needed */
    int i;
    for (i = 0; i < 7; i++) {
        unsigned code = order[i], post = rage_post_div[code], vco, fb;
        if (!post) continue;
        vco = pclk_khz * post;
        if (vco < RAGE_VCO_MIN_KHZ || vco > RAGE_VCO_MAX_KHZ) continue;
        fb = (pclk_khz * ref_div * post + RAGE_REF_2X_KHZ / 2) / RAGE_REF_2X_KHZ;
        if (fb < 2 || fb > 255) continue;
        o->post_code = code; o->post = post; o->fb = fb; o->ref_div = ref_div;
        o->achieved_khz = (RAGE_REF_2X_KHZ * fb) / (ref_div * post);
        return 0;
    }
    return -1;
}

/* X.Org ATIDivide: (n * 2^shift) / d, rounding floor (<0), nearest (0) or ceiling (>0) */
static int rage_idiv(int n, int d, int shift, int rounding)
{
    unsigned num = (unsigned)n, den = (unsigned)d, r = 0;
    if (shift >= 0) num <<= shift; else den <<= -shift;
    if (rounding == 0) r = den >> 1; else if (rounding > 0) r = den - 1;
    return (int)((num + r) / den);
}

struct rage_dsp_in {
    int xclk_fb;          /* PLL_MCLK_FB_DIV                                                            */
    int xclk_post;        /* XCLKPostDivider = XCLK_SRC_SEL (1 if 4 -> ref divider 3) - MFB_TIMES_4_2B */
    int xclk_ref_div;     /* 1, or 3 when XCLK_SRC_SEL == 4                                             */
    int vclk_fb, vclk_post;/* the dot-clock dividers (post = divider value, not code)                    */
    int bpp;              /* bits per pixel in memory (16 for 15/16 bpp, 32)                            */
    int fifo_depth;       /* 32 on VT4 and later (LT Pro)                                               */
    int page_fault, max_ras, latency; /* from rage_dsp_mem()                                            */
};

/* memory-timing inputs: mem_cntl fields TRP 9:8, TRCD 11:10, TCRD 12, TRAS 18:16; memtype CONFIG_STAT0[2:0]: 0 DRAM, 1 EDO, 2 pseudo-EDO, 3 SDRAM, 4 SGRAM */
static void rage_dsp_mem(unsigned mem_cntl, unsigned memtype, unsigned vram_kb, unsigned dsp_config_now, struct rage_dsp_in *in)
{
    int trp = (mem_cntl >> 8) & 3, trcd = (mem_cntl >> 10) & 3, tcrd = (mem_cntl >> 12) & 1, tras = (mem_cntl >> 16) & 7;
    int pf = trcd + tcrd + trp + 2, ras = tras + trp + 2, lat;
    switch (memtype) {
    case 0:  if (vram_kb <= 1024) lat = 10; else { lat = 8; pf += 2; } break;
    case 1: case 2: if (vram_kb <= 1024) lat = 9; else { lat = 8; pf++; } break;
    case 3:  if (vram_kb <= 1024) lat = 11; else { lat = 10; pf++; } break;
    case 4:  lat = 8; pf += 3; break;
    default: lat = 11; pf += 3; break;
    }
    if (ras <= pf) ras = pf + 1;
    if (dsp_config_now) lat = (dsp_config_now >> 16) & 15;     /* the running value (the ROM's) wins, as in X.Org */
    in->page_fault = pf; in->max_ras = ras; in->latency = lat;
}

static void rage_dsp_calc(const struct rage_dsp_in *in, unsigned *dsp_config, unsigned *dsp_on_off)
{
    int mult = in->xclk_fb * in->vclk_post, div = in->vclk_fb * in->xclk_ref_div * (in->bpp / 4);
    int ras_mult = in->max_ras, ras_div = 1;
    int vshift = (6 - 2) - in->xclk_post, xshift, tmp, prec, on, off, xclks;
    const int maxprec = 7;
    tmp = rage_idiv(mult * in->fifo_depth, div, vshift, -1);
    for (prec = -5; tmp; prec++) tmp >>= 1;
    if (prec < 0) prec = 0; else if (prec > maxprec) prec = maxprec;
    xshift = 6 - prec; vshift += xshift;
    off = rage_idiv(mult * (in->fifo_depth - 1), div, vshift, -1) - rage_idiv(1, 1, vshift - xshift, 1);
    on = rage_idiv(mult, div, vshift, 1);
    tmp = rage_idiv(ras_mult, ras_div, xshift, 1);
    if (on < tmp) on = tmp;
    on += tmp * 2 + rage_idiv(in->page_fault, 1, xshift, 1);
    tmp = ((1 << (maxprec - prec)) - 1) >> 1;
    on = ((on + tmp) / (tmp + 1)) * (tmp + 1);
    if (on >= ((off / (tmp + 1)) * (tmp + 1))) {
        on = off - rage_idiv(mult, div, vshift, -1);
        on = (on / (tmp + 1)) * (tmp + 1);
    }
    xclks = rage_idiv(mult, div, vshift + 5, 1);
    *dsp_on_off = ((unsigned)on << 16) | ((unsigned)off & 0x7ff);
    *dsp_config = ((unsigned)prec << 20) | ((unsigned)xclks & 0x3fff) | ((unsigned)in->latency << 16);
}
#endif

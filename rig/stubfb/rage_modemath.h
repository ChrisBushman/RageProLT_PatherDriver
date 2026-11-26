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

/*
 * Apple's display-FIFO formula (NDRV ATY,264LTPro 1.3.42, function at 0x10017ae0; docs/re-notes/apple-ndrv-framebuffer.md section 10), integer only.
 * NOT used by the stub yet: it is here so the two formulas can be compared (hosttest/modemath_test.c) before anything is programmed.
 *   x      = xclks per quadword = 64 * XCLK / (VCLK * bits per pixel)         (the same ratio as the X.Org code's mult/div)
 *   prec   = clamp(max(bitlen(int x) - 3, bitlen(int 32x) - 5), 0, 7)           (bitlen: position of the top set bit + 1, 1 for 0)
 *   depth  = min(32, int(2^(prec+5) / x))                                       (X.Org: always 32)
 *   OFF    = int(int(x*(depth-1) - 0.99999) * 2^(6-prec))
 *   ON     = int(3 * max(pf, ras) * 2^(6-prec))                                 (X.Org: max(on, ras*..) + 2*ras*.. + pf*..)
 *   XCLKS  = int(x * 2^(11-prec))
 *   CONFIG = XCLKS | ((latency) & 15) << 16 | prec << 20 | (old & 0x8000)       latency = {8,6,9,8}[(memgroup>=2)*2 + (vram_mb>=2)] + 2, memgroup = memtype>>1
 *   pf = trp + trcd + tcrd + 2 + {3,2,2,1}[same index]; ras = trp + tras + 2 (MEM_CNTL 0xb1/0xb2 fields as in rage_dsp_mem).
 * Not modelled (Apple's flagged paths): the second head, the 4096/n scale factor (display property 0x10), and the cap of 24 for display mode id 0x50.
 * vram_mb comes from a driver field at +0x524 shifted right by 20 (inferred to be the VRAM size).
 */
struct rage_dsp_apple { unsigned prec, depth, xclks, latency, on, off, max_ras, config, on_off; };
static unsigned rage_bitlen(unsigned v) { unsigned n = 0; while (v) { n++; v >>= 1; } return n ? n : 1; }
static void rage_dsp_apple_calc(const struct rage_dsp_in *in, unsigned mem_cntl, unsigned memtype, unsigned vram_mb, unsigned dsp_config_now, struct rage_dsp_apple *o)
{
    static const unsigned char lat_tbl[4] = { 8, 6, 9, 8 }, pf_tbl[4] = { 3, 2, 2, 1 };
    unsigned mult = (unsigned)in->xclk_fb * (unsigned)in->vclk_post, div = (unsigned)in->vclk_fb * (unsigned)in->xclk_ref_div * ((unsigned)in->bpp / 4u);
    int sh = 4 - in->xclk_post;                       /* x = mult * 2^sh / div */
    unsigned num, idx = ((memtype >> 1) >= 2 ? 2u : 0u) + (vram_mb >= 2 ? 1u : 0u);
    unsigned trp = (mem_cntl >> 8) & 3, trcd = (mem_cntl >> 10) & 3, tcrd = (mem_cntl >> 12) & 1, tras = (mem_cntl >> 16) & 7, pf, ras, q, r;
    int prec, b8, b9;
    if (sh >= 0) num = mult << sh; else { num = mult; div <<= -sh; }
    b8 = (int)rage_bitlen(num / div); b9 = (int)rage_bitlen((num << 5) / div);
    prec = (b8 - 3 < b9 - 5) ? b9 - 5 : b8 - 3; if (prec < 0) prec = 0; if (prec > 7) prec = 7;
    q = (div << (prec + 5)) / num; o->depth = q < 33u ? q : 32u;                     /* int(2^(prec+5) / x) capped at 32 */
    q = (num * (o->depth - 1u)) / div; r = (num * (o->depth - 1u)) % div;
    q = q - 1u + ((r * 100000u >= 99999u * div) ? 1u : 0u);                           /* int(x*(depth-1) - 0.99999) */
    pf = trp + trcd + tcrd + 2u + pf_tbl[idx]; ras = trp + tras + 2u; o->max_ras = pf > ras ? pf : ras;
    o->prec = (unsigned)prec;
    o->off = prec <= 6 ? q << (6 - prec) : q >> 1;
    o->on  = prec <= 6 ? (3u * o->max_ras) << (6 - prec) : (3u * o->max_ras) >> 1;
    o->xclks = (num << (11 - prec)) / div;
    o->latency = lat_tbl[idx] + 2u;
    o->config = o->xclks | ((o->latency & 15u) << 16) | ((unsigned)prec << 20) | (dsp_config_now & 0x8000u);
    o->on_off = (o->on << 16) | o->off;
}
#endif

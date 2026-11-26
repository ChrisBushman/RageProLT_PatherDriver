/* modemath_test.c: PLL/DSP math against the one real hardware calibration point and the monitor limits. cc -O2 -I../rig/stubfb */
#include <stdio.h>
#include "rage_modemath.h"
int main(void)
{
    struct rage_pll p; struct rage_dsp_in in; unsigned cfg, onoff; int bad = 0, i;
    static const unsigned clocks[] = { 25175, 36000, 40000, 56250, 65000, 78750, 108000 };
    for (i = 0; i < 7; i++) {
        if (rage_pll_calc(clocks[i], 64, &p)) { printf("PLL %u kHz: no solution\n", clocks[i]); bad++; continue; }
        printf("PLL %6u kHz: post code %u (/%u) fb %3u -> %u kHz (%+.3f%%) VCO %u kHz\n", clocks[i], p.post_code, p.post, p.fb, p.achieved_khz,
               100.0 * ((double)p.achieved_khz - clocks[i]) / clocks[i], p.achieved_khz * p.post);
    }
    /* calibration: hardware 640x480 15 bpp, VCLK3 fb 218 post /8, MCLK fb 217, XCLK post 1, SGRAM, MEM_CNTL 0x10751a7b */
    rage_pll_calc(25175, 64, &p);
    if (p.fb != 218 || p.post != 8) { printf("FAIL: 25.175 MHz gives fb %u post %u (ROM: 218, 8)\n", p.fb, p.post); bad++; }
    in.xclk_fb = 217; in.xclk_post = 1; in.xclk_ref_div = 1; in.vclk_fb = 218; in.vclk_post = 8; in.bpp = 16; in.fifo_depth = 32;
    rage_dsp_mem(0x10751a7b, 4, 8192, 0x004807f7, &in);
    rage_dsp_calc(&in, &cfg, &onoff);
    printf("DSP calibration: config 0x%08x (ROM 0x004807f7)  on_off 0x%08x (ROM 0x009c07b0)\n", cfg, onoff);
    if (cfg != 0x004807f7) { printf("FAIL: DSP_CONFIG differs\n"); bad++; }
    if ((onoff & 0x7ff) < 1960 || (onoff & 0x7ff) > 1975) { printf("FAIL: DSP_OFF %u too far from the ROM's 1968\n", onoff & 0x7ff); bad++; }
    printf(bad ? "FAILED (%d)\n" : "OK\n", bad);
    return bad != 0;
}

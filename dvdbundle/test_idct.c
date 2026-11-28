/* Host test (Linux): IEEE 1180-style accuracy of rc_idct8x8 vs a double-precision reference, plus mismatch/scan table sanity. */
#include <stdio.h>
#include <stdlib.h>
#include <math.h>
#include <string.h>
#include "recon.h"
static double cc[8][8];
static long rnd_state = 1;
static int rnd(int L, int H) { /* IEEE 1180 generator */
    long i, j;
    rnd_state &= 0x7fffffff;
    rnd_state = rnd_state * 1103515245 + 12345;
    i = rnd_state & 0x7ffffffe; j = (double)i / 0x7fffffff * (L + H + 1) - L; return (int)j;
}
static void fdct(const int in[64], double out[64])
{
    int u, v, x, y;
    for (v = 0; v < 8; v++) for (u = 0; u < 8; u++) {
        double s = 0; for (y = 0; y < 8; y++) for (x = 0; x < 8; x++) s += in[y*8+x] * cc[u][x] * cc[v][y];
        out[v*8+u] = s; }
}
static void idct_ref(const int in[64], int out[64])
{
    int u, v, x, y;
    for (y = 0; y < 8; y++) for (x = 0; x < 8; x++) {
        double s = 0; for (v = 0; v < 8; v++) for (u = 0; u < 8; u++) s += in[v*8+u] * cc[u][x] * cc[v][y];
        long r = (long)floor(s + 0.5); out[y*8+x] = r < -256 ? -256 : r > 255 ? 255 : (int)r; }
}
int main(void)
{
    int u, x, i, n, t, bad = 0;
    for (u = 0; u < 8; u++) for (x = 0; x < 8; x++) cc[u][x] = (u == 0 ? sqrt(0.125) : 0.5) * cos((2*x+1)*u*M_PI/16);
    { int seen[64]; memset(seen,0,sizeof seen); for (i=0;i<64;i++){ seen[rc_zigzag[i]]++; } for (i=0;i<64;i++) if(seen[i]!=1) bad=1;
      memset(seen,0,sizeof seen); for (i=0;i<64;i++){ seen[rc_altscan[i]]++; } for (i=0;i<64;i++) if(seen[i]!=1) bad=1; printf("scan tables permutations: %s\n", bad?"BAD":"ok"); }
    struct { int L, H, sign; } cfg[] = { {256,255,1}, {256,255,-1}, {5,5,1}, {5,5,-1}, {300,300,1}, {300,300,-1} };
    for (t = 0; t < 6; t++) {
        long peak = 0; double mse[64] = {0}, mean[64] = {0}; int N = 10000, ok = 1;
        rnd_state = 1;
        for (n = 0; n < N; n++) {
            int pix[64], co_i[64], ref[64]; double co[64]; rc_s16 blk[64];
            for (i = 0; i < 64; i++) pix[i] = rnd(cfg[t].L, cfg[t].H) * cfg[t].sign;
            fdct(pix, co);
            for (i = 0; i < 64; i++) { long r = (long)floor(co[i] + 0.5); if (r > 2047) r = 2047; if (r < -2048) r = -2048; co_i[i] = (int)r; blk[i] = (rc_s16)r; }
            idct_ref(co_i, ref);
            rc_idct8x8(blk);
            for (i = 0; i < 64; i++) { int e = blk[i] - ref[i]; if (labs(e) > peak) peak = labs(e); mse[i] += e*e; mean[i] += e; }
        }
        { double pm = 0, om = 0, worst_mse = 0, worst_mean = 0; for (i = 0; i < 64; i++) { double m = mse[i]/N, mm = mean[i]/N; if (m > worst_mse) worst_mse = m; if (fabs(mm) > worst_mean) worst_mean = fabs(mm); pm += m; om += mm; }
          pm /= 64; om /= 64;
          if (peak > 1 || worst_mse > 0.06 || pm > 0.02 || worst_mean > 0.015 || fabs(om) > 0.0015) ok = 0;
          printf("L=%d H=%d sign=%d: peak=%ld worst_pel_mse=%.4f overall_mse=%.5f worst_pel_mean=%.4f overall_mean=%.5f %s\n", cfg[t].L, cfg[t].H, cfg[t].sign, peak, worst_mse, pm, worst_mean, om, ok ? "PASS" : "FAIL"); }
    }
    { rc_s16 b[64]; memset(b, 0, sizeof b); b[0] = 8; rc_mismatch(b); printf("mismatch: even sum -> b[63]=%d (expect 1)\n", b[63]);
      memset(b, 0, sizeof b); b[0] = 7; rc_mismatch(b); printf("mismatch: odd sum  -> b[63]=%d (expect 0)\n", b[63]); }
    return 0;
}

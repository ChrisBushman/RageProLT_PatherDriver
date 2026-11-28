/* mc_batch.c - verify RAGE_GL_MC_BATCH against a CPU model: frame systems, field systems (stride 2 x pitch), same-plane field-to-field, one-way, two-way,
 * residual pre-loaded in the destination (signed bytes), all four block sizes.  The CPU model rounds hv half-pel twice (hardware behaviour, mc-experiment-plan.md). */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "ragegl.h"

static int PW = 64, PH = 64;     /* plane pitch == width, frame lines (argv[1], argv[2]) */
#define MAXB (800 * 600)
static ragegl_t c;
static uint32_t rs = 0xC0FFEE11u;
static uint32_t rnd(void) { rs ^= rs << 13; rs ^= rs >> 17; rs ^= rs << 5; return rs; }
static const int BW[4] = { 16, 16, 8, 8 }, BH[4] = { 16, 8, 8, 4 };

static unsigned char A[MAXB], B[MAXB], R[MAXB];      /* source plane, destination plane (with residual), expected */

/* prediction of one reference: base = host copy of the plane at line 0 of the system, stride in bytes */
static int pred(const unsigned char *base, int stride, int x2, int y2, int i, int j, int chain)
{
    int ax = x2 >> 1, ay = y2 >> 1, fx = x2 & 1, fy = y2 & 1;
    int a = base[(ay + j) * stride + ax + i], b = fx ? base[(ay + j) * stride + ax + i + 1] : 0;
    int cc = fy ? base[(ay + j + 1) * stride + ax + i] : 0, d = (fx && fy) ? base[(ay + j + 1) * stride + ax + i + 1] : 0;
    (void)chain;
    if (fx && fy) return (((a + b + 1) >> 1) + ((cc + d + 1) >> 1) + 1) >> 1;
    if (fx) return (a + b + 1) >> 1;
    if (fy) return (a + cc + 1) >> 1;
    return a;
}
static int clip(int v) { return v < 0 ? 0 : v > 255 ? 255 : v; }

int main(int argc, char **argv)
{
    if (argc > 2) { PW = atoi(argv[1]); PH = atoi(argv[2]); }
    if ((PH + 3) * PW > MAXB) { printf("too big\n"); return 9; }
    uint32_t offA, offB; int round, bad = 0, total = 0, rounds = 0;
    if (ragegl_open(&c)) { printf("open failed\n"); return 1; }
    if (argc > 3) { uint32_t padoff; if (ragegl_alloc(c, (uint32_t)atoi(argv[3]) * 1024u, 8, &padoff)) { printf("pad alloc failed\n"); return 7; } }
    if (ragegl_alloc(c, (PH + 3) * PW, 8, &offA) || ragegl_alloc(c, (PH + 3) * PW, 8, &offB)) { printf("alloc failed\n"); return 2; }
    printf("planes A 0x%x B 0x%x\n", offA, offB);
    if (argc > 4) {                                   /* big batch: every 16x16 block of the plane, random vectors, frame system, zero residual */
        static RageMcCmd big[RAGE_MC_MAX_CMDS]; int bx, by, n = 0, i, diff = 0, w = PW - (PW % 16 ? PW % 16 : 0), wid = atoi(argv[4]);
        for (i = 0; i < (PH + 3) * PW; i++) { A[i] = (unsigned char)rnd(); B[i] = 0; R[i] = 0; }
        for (by = 0; by + 16 <= PH && n < RAGE_MC_MAX_CMDS; by += 16) for (bx = 0; bx + 16 <= wid && n < RAGE_MC_MAX_CMDS; bx += 16) {
            RageMcCmd *m = &big[n]; int mvx = (int)(rnd() % 9) - 4, mvy = (int)(rnd() % 9) - 4, j;
            if (mvx + 2 * bx < 0 || mvy + 2 * by < 0 || ((mvx + 2 * bx) >> 1) + 17 > wid || ((mvy + 2 * by) >> 1) + 17 > PH) { mvx = 0; mvy = 0; }
            m->srcOff = offA; m->srcOff2 = offA; m->dstOff = offB; m->stride = (uint16_t)PW; m->width = (uint16_t)wid; m->rows = (uint16_t)PH; m->size = 0; m->flags = 0;
            m->x = (int16_t)bx; m->y = (int16_t)by; m->mvx = (int16_t)mvx; m->mvy = (int16_t)mvy; n++;
            for (j = 0; j < 16; j++) { int ii; for (ii = 0; ii < 16; ii++) R[(by + j) * PW + bx + ii] = (unsigned char)pred(A, PW, mvx + 2 * bx, mvy + 2 * by, ii, j, 1); }
        }
        ragegl_vwrite(c, offA, A, (PH + 3) * PW, 0); ragegl_vwrite(c, offB, B, (PH + 3) * PW, 0);
        { kern_return_t kr = RGL_CALL(RAGE_GL_MC_BATCH, 2, 0, (uint32_t)(uintptr_t)big, (uint32_t)n); printf("big batch %d cmds kr 0x%x\n", n, kr); }
        { static unsigned char o2[MAXB]; ragegl_vread(c, offB, o2, (PH + 3) * PW, 0); for (by = 0; by < PH; by++) for (bx = 0; bx < wid; bx++) if (o2[by * PW + bx] != R[by * PW + bx]) diff++;
          printf("%d of %d pixels differ\n", diff, PH * wid); }
        return diff != 0;
    }
    for (round = 0; round < 60; round++) {
        /* mode 0 frame system, 1 field system A(par p) -> B(par q), 2 field system inside one plane (src par 0 -> dst par 1) */
        int mode = round % 3, twoway = (round / 3) & 1, i, j, k, n = 0;
        RageMcCmd cmds[8]; unsigned char *srcPlane = A, *dstPlane = B; uint32_t sOff = offA, dOff = offB, stride, rows, sp = 0, dp = 0;
        for (i = 0; i < (PH + 3) * PW; i++) { A[i] = (unsigned char)rnd(); B[i] = (unsigned char)rnd(); }
        if (mode == 2) { memcpy(B, A, sizeof A); dstPlane = B; srcPlane = B; sOff = dOff = offB; sp = 0; dp = 1; for (i = 0; i < (PH + 3) * PW; i++) if (((i / PW) & 1) == 1) B[i] = (unsigned char)(rnd() % 256); }
        if (mode == 1) { sp = rnd() & 1; dp = rnd() & 1; }
        stride = mode ? 2 * PW : PW; rows = mode ? PH / 2 : PH;
        memcpy(R, B, sizeof B);
        for (k = 0; k < 6; k++) {
            RageMcCmd *m = &cmds[n]; int sz = rnd() % 4, bw = BW[sz], bh = BH[sz], x, y, ph1 = rnd() % 4, ph2 = rnd() % 4, mv1x, mv1y, mv2x, mv2y, ok = 1;
            /* disjoint destination blocks: 3 columns x 2 row bands */
            x = (k % 3) * (PW / 3 & ~1) + (rnd() % ((PW / 3) - bw > 0 ? (PW / 3) - bw : 1)); y = (k / 3) * (rows / 2) + (rnd() % (rows / 2 - bh + 1));
            if (x + bw > PW || y + bh > (int)rows) continue;
            mv1x = (int)(rnd() % 13) - 6 + (ph1 & 1) * 0; mv1y = (int)(rnd() % 9) - 4;
            mv1x = (mv1x & ~1) | (ph1 & 1); mv1y = (mv1y & ~1) | ((ph1 >> 1) & 1);
            mv2x = (int)(rnd() % 13) - 6; mv2y = (int)(rnd() % 9) - 4; mv2x = (mv2x & ~1) | (ph2 & 1); mv2y = (mv2y & ~1) | ((ph2 >> 1) & 1);
            if (mv1x + 2 * x < 0 || mv1y + 2 * y < 0 || mv2x + 2 * x < 0 || mv2y + 2 * y < 0) ok = 0;
            if (((mv1x + 2 * x) >> 1) + bw + ((mv1x + 2 * x) & 1) > PW || ((mv1y + 2 * y) >> 1) + bh + ((mv1y + 2 * y) & 1) > (int)rows) ok = 0;
            if (((mv2x + 2 * x) >> 1) + bw + ((mv2x + 2 * x) & 1) > PW || ((mv2y + 2 * y) >> 1) + bh + ((mv2y + 2 * y) & 1) > (int)rows) ok = 0;
            if (!ok) continue;
            m->srcOff = sOff + sp * PW; m->srcOff2 = m->srcOff; m->dstOff = dOff + dp * PW; m->stride = (uint16_t)stride; m->width = PW; m->rows = (uint16_t)rows;
            m->size = (uint8_t)sz; m->flags = twoway ? RAGE_MC_TWOWAY : 0; m->x = (int16_t)x; m->y = (int16_t)y; m->mvx = (int16_t)mv1x; m->mvy = (int16_t)mv1y;
            m->mv2x = (int16_t)mv2x; m->mv2y = (int16_t)mv2y;
            /* expected */
            for (j = 0; j < bh; j++) for (i = 0; i < bw; i++) {
                const unsigned char *sb = srcPlane + sp * PW; unsigned char *db = R + dp * PW;
                int p = pred(sb, (int)stride, mv1x + 2 * x, mv1y + 2 * y, i, j, 1), res = (signed char)dstPlane[dp * PW + (y + j) * stride + x + i];
                if (twoway) p = (p + pred(sb, (int)stride, mv2x + 2 * x, mv2y + 2 * y, i, j, 1) + 1) >> 1;
                db[(y + j) * stride + x + i] = (unsigned char)clip(p + res);
            }
            n++;
        }
        if (!n) continue;
        if (ragegl_vwrite(c, offA, A, (PH + 3) * PW, 0) || ragegl_vwrite(c, offB, B, (PH + 3) * PW, 0)) { printf("vwrite failed\n"); return 3; }
        { kern_return_t kr = RGL_CALL(RAGE_GL_MC_BATCH, 2, 0, (uint32_t)(uintptr_t)cmds, (uint32_t)n); if (kr) { printf("round %d mode %d MC_BATCH failed 0x%x\n", round, mode, kr); return 4; } }
        { static unsigned char out[MAXB]; int diff = 0; ragegl_vread(c, offB, out, (PH + 3) * PW, 0);
          for (i = 0; i < PH * PW; i++) { total++; if (out[i] != R[i]) diff++; }
          if (diff) { printf("round %d mode %d twoway %d: %d of %d bytes differ\n", round, mode, twoway, diff, PH * PW); bad++; }
          rounds++; }
    }
    printf("%d rounds, %d with differences\n", rounds, bad);
    ragegl_free(c, offA); ragegl_free(c, offB);
    return bad != 0;
}

/* tools/ragegl/mc_exp.c - LT Pro motion-compensation experiment, stage 1 (docs/re-notes/mc-experiment-plan.md).
 * One 16x16 luma prediction through the scaler pipe (RAGE_GL_MC_EXP, debug kext only), compared in user space with MPEG-2 prediction computed on the CPU.
 *   mc_exp              calibrate the aperture swap, then run the half-pel phases and report rounding behaviour
 *   mc_exp cal          only the calibration (integer-pel copies)
 * Nothing here is written to the chip except through the kext's fixed sequence. */
#include "ragegl.h"
#include <string.h>
#define P 64
#define H 64
static uint32_t rs = 0x2468ace1u;
static uint32_t rnd(void) { rs ^= rs << 13; rs ^= rs >> 17; rs ^= rs << 5; return rs; }
static unsigned char src[P * H], dst0[P * H], out[P * H];
static uint32_t lastfifo, norestore;
static ragegl_t c; static uint32_t srcOff, dstOff;

static int run(int swap, int X, int Y, int mvx, int mvy, uint32_t *gui, uint32_t *st)
{
    kern_return_t kr;
    if (ragegl_vwrite(c, dstOff, dst0, P * H, swap)) { printf("vwrite dst failed\n"); return -1; }
    uint32_t o = 0;
    kr = RGL_CALL(RAGE_GL_MC_EXP, 5, 1, srcOff, dstOff, P | (H << 16) | ((norestore & 1u) << 31) | (((norestore >> 1) & 1u) << 30), (uint32_t)X | ((uint32_t)Y << 16), ((uint32_t)mvx & 0xffffu) | ((uint32_t)mvy << 16), &o);
    *gui = (o >> 24) << 16; *st = o & 0xffu; lastfifo = (o >> 8) & 0xffffu;
    if (kr) { printf("MC_EXP failed 0x%x (0xe00002c2 bad arg, 0xe00002e2 not ready; needs a RAGE_DEBUG_ENGTEST kext)\n", (unsigned)kr); return -1; }
    if (*st) { printf("MC_EXP status %u (GUI_STAT[23:16] 0x%08x, FIFO_STAT 0x%04x)\n", (unsigned)*st, (unsigned)*gui, (unsigned)lastfifo); return -1; }
    if (ragegl_vread(c, dstOff, out, P * H, swap)) { printf("vread failed\n"); return -1; }
    return 0;
}
static int ref(int X, int Y, int mvx, int mvy, int rule, unsigned char *r)       /* rule 0 = MPEG-2 (a+b+1)>>1, (a+b+c+d+2)>>2; rule 1 = truncating */
{
    int x2 = mvx + 2 * X, y2 = mvy + 2 * Y, ax = x2 >> 1, ay = y2 >> 1, fx = x2 & 1, fy = y2 & 1, i, j;
    for (j = 0; j < 16; j++) for (i = 0; i < 16; i++) {
        int a = src[(ay + j) * P + ax + i], b = src[(ay + j) * P + ax + i + 1], cc = src[(ay + j + 1) * P + ax + i], d = src[(ay + j + 1) * P + ax + i + 1], v;
        if (!fx && !fy) v = a;
        else if (fx && !fy) v = rule ? (a + b) >> 1 : (a + b + 1) >> 1;
        else if (!fx && fy) v = rule ? (a + cc) >> 1 : (a + cc + 1) >> 1;
        else v = rule ? (a + b + cc + d) >> 2 : (a + b + cc + d + 2) >> 2;
        r[j * 16 + i] = (unsigned char)v;
    }
    return 0;
}
static void cmp(int X, int Y, int mvx, int mvy, int *eq0, int *eq1, int *hist, int *outside)
{
    unsigned char r0[256], r1[256]; int i, j, bad0 = 0, bad1 = 0;
    ref(X, Y, mvx, mvy, 0, r0); ref(X, Y, mvx, mvy, 1, r1);
    for (j = 0; j < 16; j++) for (i = 0; i < 16; i++) {
        int o = out[(Y + j) * P + X + i], d = o - r0[j * 16 + i];
        if (o != r0[j * 16 + i]) bad0++;
        if (o != r1[j * 16 + i]) bad1++;
        if (d >= -2 && d <= 2) hist[d + 2]++; else hist[5]++;
    }
    { int k; *outside = 0; for (k = 0; k < P * H; k++) { int x = k % P, y = k / P; if ((x < X || x >= X + 16 || y < Y || y >= Y + 16) && out[k] != dst0[k]) (*outside)++; } }
    *eq0 = bad0; *eq1 = bad1;
}
int main(int argc, char **argv)
{
    uint32_t gui, st, gen, feat, end, fl; int i, swap, goodswap = -1, onlycal = argc > 1 && argv[1][0] == 'c';
    if (ragegl_open(&c)) { printf("cannot open the GL client\n"); return 99; }
    if (ragegl_info(c, &gen, &feat, &end, &fl)) { printf("info failed\n"); return 98; }
    for (i = 0; i < P * H; i++) { src[i] = (unsigned char)rnd(); dst0[i] = (unsigned char)(getenv("FILL") ? strtol(getenv("FILL"), 0, 0) : 0xA5); }
    if (ragegl_alloc(c, P * H, 64, &srcOff) || ragegl_alloc(c, P * H, 64, &dstOff)) { printf("alloc failed\n"); return 97; }
    printf("planes: src 0x%x dst 0x%x, %dx%d 8 bpp\n", srcOff, dstOff, P, H);
    if (argc > 1 && argv[1][0] == 'o') {                      /* "one": a single zero-vector run, print what arrived (diagnosis) */
        int sw = argc > 2 ? atoi(argv[2]) : 0, r, k, changed = 0; norestore = argc > 3 ? (uint32_t)atoi(argv[3]) : 0; if (argc > 5) norestore |= 2u;
        if (ragegl_vwrite(c, srcOff, src, P * H, sw)) return 96;
        if (run(sw, 16, 16, 0, 0, &gui, &st)) { ragegl_free(c, srcOff); ragegl_free(c, dstOff); return 95; }
        { unsigned char chk[P * H]; int bad = 0; if (ragegl_vread(c, srcOff, chk, P * H, sw)) printf("src readback failed\n"); else { for (k = 0; k < P * H; k++) if (chk[k] != src[k]) bad++; printf("source plane read back: %d of %d bytes differ from what was written\n", bad, P * H); } }
        for (k = 0; k < P * H; k++) if (out[k] != dst0[k]) changed++;
        printf("swap %d: GUI_STAT 0x%08x status %u, %d of %d bytes differ from the 0xA5 fill\n", sw, (unsigned)gui, (unsigned)st, changed, P * H);
        for (r = 14; r < 34; r++) { if (argc > 4) { printf("  out row %2d:", r); for (k = 12; k < 36; k++) printf(" %02x", out[r * P + k]); printf("\n"); continue; } printf("  src row %2d:", r); for (k = 12; k < 24; k++) printf(" %02x", src[r * P + k]); printf("\n  out row %2d:", r); for (k = 12; k < 24; k++) printf(" %02x", out[r * P + k]); printf("\n"); }
        ragegl_free(c, srcOff); ragegl_free(c, dstOff); return 0;
    }
    if (argc > 1 && argv[1][0] == 'w') {                      /* DP_PIX_WIDTH scale nibble x dst nibble: out(16,16) for src 0xff / 0x80, dst 0 */
        static const int sn[] = { 2, 3, 4, 6, 7, 8, 9, 11, 14, 15 }, dn[] = { 2, 3, 4, 6, 7, 8 };
        int i, j, a;
        norestore |= 2u;
        for (i = 0; i < 10; i++) { printf("scale nib %2d:", sn[i]);
            for (j = 0; j < 6; j++) { printf("  dst%d", dn[j]);
                for (a = 0x80; a <= 0xff; a += 0x7f) {
                    memset(src, a, sizeof src); memset(dst0, 0, sizeof dst0);
                    ragegl_vwrite(c, srcOff, src, P * H, 0);
                    if (run(0, 16, 16, 0, (sn[i] << 4 | dn[j]) | 0x100, &gui, &st)) { printf(" fail\n"); goto wdone; }
                    printf(":%02x", out[16 * P + 16]);
                }
            }
            printf("\n");
        }
wdone:  ragegl_free(c, srcOff); ragegl_free(c, dstOff); return 0;
    }
    if (argc > 1 && argv[1][0] == 's') {                      /* address-unit test: SCALE_OFF = addr >> shift, src 0xff constant, dst 0 */
        int sh;
        for (sh = 0; sh <= 4; sh++) {
            norestore |= 2u; memset(src, 0xff, sizeof src); memset(dst0, 0, sizeof dst0);
            ragegl_vwrite(c, srcOff, src, P * H, 0);
            if (run(0, 16, 16, 0, sh << 10, &gui, &st)) { printf("fail sh %d\n", sh); break; }
            printf("shift %d: out(16,16)=%02x (block min/max below)\n", sh, out[16 * P + 16]);
        }
        ragegl_free(c, srcOff); ragegl_free(c, dstOff); return 0;
    }
    if (argc > 1 && argv[1][0] == 'p') {                      /* palette variants 1..3: src ramp 0x00..0xff over the block columns, dst 0 */
        int pv, i, j;
        for (pv = 1; pv <= 3; pv++) {
            norestore |= 2u;
            for (j = 0; j < H; j++) for (i = 0; i < P; i++) src[j * P + i] = (unsigned char)(i * 4);
            memset(dst0, 0, sizeof dst0);
            ragegl_vwrite(c, srcOff, src, P * H, 0);
            if (run(0, 16, 16, 0, pv << 8, &gui, &st)) { printf("fail pv %d\n", pv); break; }
            printf("palette variant %d: src(16..31,16)=", pv); for (i = 16; i < 32; i += 3) printf(" %02x", src[16 * P + i]);
            printf("\n   out="); for (i = 16; i < 32; i += 3) printf(" %02x", out[16 * P + i]); printf("  st %u\n", (unsigned)st);
        }
        ragegl_free(c, srcOff); ragegl_free(c, dstOff); return 0;
    }
    if (argc > 1 && argv[1][0] == 'd') {                      /* dump default engine registers to the kernel log */
        uint32_t g, s; norestore |= 2u; memset(src, 0x80, sizeof src); memset(dst0, 0, sizeof dst0);
        ragegl_vwrite(c, srcOff, src, P * H, 0); run(0, 16, 16, 0xffff, 0x8000, &g, &s); printf("dumped, st %u\n", (unsigned)s);
        ragegl_free(c, srcOff); ragegl_free(c, dstOff); return 0;
    }
    if (argc > 1 && argv[1][0] == 'v') {                      /* sweep SCALE_3D_CNTL in documented-blit mode: src 0x80 / 0xff constant, dst 0 */
        static const uint32_t cv[] = { 0x40, 0x0, 0x8240, 0x240, 0x1040, 0x2040, 0x4040, 0x8040, 0x10040, 0x20040, 0x40040, 0x80040, 0x100040, 0x200040, 0x400040, 0x800040, 0x1000040, 0x80, 0xc0, 0x100, 0x200, 0x400, 0x800 };
        unsigned n; norestore |= 2u;
        for (n = 0; n < sizeof cv / sizeof cv[0]; n++) {
            int a; printf("3D_CNTL %08x:", cv[n]);
            for (a = 0x80; a <= 0xff; a += 0x7f) {
                memset(src, a, sizeof src); memset(dst0, 0, sizeof dst0);
                if (ragegl_vwrite(c, srcOff, src, P * H, 0) || run(0, 16, 16, cv[n] & 0xffff, cv[n] >> 16, &gui, &st)) { printf(" fail\n"); goto vdone; }
                printf("  s=%02x -> %02x [st%u]", a, out[16 * P + 16], (unsigned)st);
            }
            printf("\n");
        }
vdone:  ragegl_free(c, srcOff); ragegl_free(c, dstOff); return 0;
    }
    if (argc > 1 && argv[1][0] == 'x') {                      /* "xfer": constant source x constant destination, zero vector: the transfer function of the pass */
        static const int sv[] = { 0x10, 0x60, 0xa0, 0xc0, 0xe0, 0xf0, 0xf8, 0xfc, 0xff }, dv[] = { 0x00, 0x80 };
        int a, b, k; if (argc > 2 && argv[2][0] == 'd') norestore |= 2u;
        printf("rows: destination fill d, columns: source constant s; cell = output byte at (16,16) [min..max over the block]\n      ");
        for (a = 0; a < 9; a++) printf("   s=%02x      ", sv[a]);
        printf("\n");
        for (b = 0; b < 2; b++) {
            printf("d=%02x:", dv[b]);
            for (a = 0; a < 9; a++) {
                int mn = 255, mx = 0;
                memset(src, sv[a], sizeof src); memset(dst0, dv[b], sizeof dst0);
                if (ragegl_vwrite(c, srcOff, src, P * H, 0) || run(0, 16, 16, 0, 0, &gui, &st)) { ragegl_free(c, srcOff); ragegl_free(c, dstOff); return 95; }
                { int i, j; for (j = 0; j < 16; j++) for (i = 0; i < 16; i++) { k = out[(16 + j) * P + 16 + i]; if (k < mn) mn = k; if (k > mx) mx = k; } }
                printf("  %02x [%02x..%02x]", out[16 * P + 16], mn, mx);
            }
            printf("\n");
        }
        ragegl_free(c, srcOff); ragegl_free(c, dstOff); return 0;
    }
    /* 1. calibration: integer-pel shifts (0,0) (2,0) (0,2): the output must be an exact copy for the right swap mode */
    for (swap = 0; swap < 4; swap++) {
        int ok = 1, t; static const int mv[3][2] = { {0, 0}, {2, 0}, {0, 2} };
        if (ragegl_vwrite(c, srcOff, src, P * H, swap)) { printf("vwrite src failed\n"); return 96; }
        for (t = 0; t < 3 && ok; t++) {
            int eq0, eq1, hist[6] = {0}, outside;
            if (run(swap, 16, 16, mv[t][0], mv[t][1], &gui, &st)) { ragegl_free(c, srcOff); ragegl_free(c, dstOff); return 95; }
            cmp(16, 16, mv[t][0], mv[t][1], &eq0, &eq1, hist, &outside);
            printf("  swap %d mv(%d,%d): %d of 256 differ, %d bytes changed outside the block, GUI_STAT 0x%08x\n", swap, mv[t][0], mv[t][1], eq0, outside, gui);
            if (eq0 || outside) ok = 0;
        }
        if (ok && goodswap < 0) goodswap = swap;
    }
    if (goodswap < 0) { printf("CALIBRATION FAILED: no swap mode gives exact integer copies\n"); ragegl_free(c, srcOff); ragegl_free(c, dstOff); return 2; }
    printf("calibration: swap mode %d gives exact integer-pel copies\n", goodswap);
    if (!onlycal) {
        static const int pos[4][2] = { {16, 16}, {8, 24}, {32, 8}, {40, 40} };
        static const char *name[4] = { "full", "h half", "v half", "hv half" };
        int ph, p, total[4] = {0}, tot0[4] = {0}, tot1[4] = {0}, hs[4][6] = {{0}}, outsideAll = 0;
        if (ragegl_vwrite(c, srcOff, src, P * H, goodswap)) return 96;
        for (ph = 0; ph < 4; ph++) for (p = 0; p < 4; p++) {
            int mvx = (ph & 1) ? 3 : 4, mvy = (ph & 2) ? -3 : 2, eq0, eq1, outside, k;
            if (ph & 1) mvx += (p & 1) ? 2 : -2;                  /* odd, both signs of the integer part */
            if (!(ph & 1)) mvx = (p & 1) ? 6 : -4;
            if (ph & 2) mvy = (p & 2) ? -5 : 3; else mvy = (p & 2) ? -4 : 6;
            if (run(goodswap, pos[p][0], pos[p][1], mvx, mvy, &gui, &st)) break;
            cmp(pos[p][0], pos[p][1], mvx, mvy, &eq0, &eq1, hs[(mvx & 1) | ((mvy & 1) << 1)], &outside);
            total[(mvx & 1) | ((mvy & 1) << 1)] += 256; tot0[(mvx & 1) | ((mvy & 1) << 1)] += eq0; tot1[(mvx & 1) | ((mvy & 1) << 1)] += eq1; outsideAll += outside; (void)k;
        }
        for (ph = 0; ph < 4; ph++)
            printf("%-8s: %4d samples, differ from MPEG-2 rounding %4d, from truncating %4d; diff histogram (hw - mpeg) -2..+2,other: %d %d %d %d %d %d\n", name[ph], total[ph], tot0[ph], tot1[ph],
                   hs[ph][0], hs[ph][1], hs[ph][2], hs[ph][3], hs[ph][4], hs[ph][5]);
        printf("bytes changed outside the blocks: %d\n", outsideAll);
    }
    ragegl_free(c, srcOff); ragegl_free(c, dstOff); ragegl_close(c);
    return 0;
}

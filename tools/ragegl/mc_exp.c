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
static uint32_t texvar, lastfifo, norestore, szcode, twoway;
static const int BW[4] = { 16, 16, 8, 8 }, BH[4] = { 16, 8, 8, 4 };
static ragegl_t c; static uint32_t srcOff, dstOff;

static int run(int swap, int X, int Y, int mvx, int mvy, uint32_t *gui, uint32_t *st)
{
    kern_return_t kr;
    if (ragegl_vwrite(c, dstOff, dst0, P * H, swap)) { printf("vwrite dst failed\n"); return -1; }
    uint32_t o = 0;
    kr = RGL_CALL(RAGE_GL_MC_EXP, 5, 1, srcOff, dstOff, P | (H << 16) | (szcode << 27) | (twoway << 29) | ((norestore & 1u) << 31) | (((norestore >> 1) & 1u) << 30), ((uint32_t)X & 0xffu) | (texvar << 8) | ((uint32_t)Y << 16), ((uint32_t)mvx & 0xffffu) | ((uint32_t)mvy << 16), &o);
    *gui = (o >> 24) << 16; *st = o & 0xffu; lastfifo = (o >> 8) & 0xffffu;
    if (kr) { printf("MC_EXP failed 0x%x (0xe00002c2 bad arg, 0xe00002e2 not ready; needs a RAGE_DEBUG_ENGTEST kext)\n", (unsigned)kr); return -1; }
    if (*st) { printf("MC_EXP status %u (GUI_STAT[23:16] 0x%08x, FIFO_STAT 0x%04x)\n", (unsigned)*st, (unsigned)*gui, (unsigned)lastfifo); return -1; }
    if (ragegl_vread(c, dstOff, out, P * H, swap)) { printf("vread failed\n"); return -1; }
    return 0;
}
static int ref(int X, int Y, int mvx, int mvy, int rule, unsigned char *r)       /* rule 0 = MPEG-2 (a+b+1)>>1, (a+b+c+d+2)>>2; rule 1 = truncating */
{
    int x2 = mvx + 2 * X, y2 = mvy + 2 * Y, ax = x2 >> 1, ay = y2 >> 1, fx = x2 & 1, fy = y2 & 1, i, j;
    for (j = 0; j < BH[szcode]; j++) for (i = 0; i < BW[szcode]; i++) {
        int a = src[(ay + j) * P + ax + i], b = src[(ay + j) * P + ax + i + 1], cc = src[(ay + j + 1) * P + ax + i], d = src[(ay + j + 1) * P + ax + i + 1], v;
        if (!fx && !fy) v = a;
        else if (fx && !fy) v = rule ? (a + b) >> 1 : (a + b + 1) >> 1;
        else if (!fx && fy) v = rule ? (a + cc) >> 1 : (a + cc + 1) >> 1;
        else v = rule ? ((((a + b + 1) >> 1) + ((cc + d + 1) >> 1) + 1) >> 1) : (a + b + cc + d + 2) >> 2;   /* rule 1 for hv: chained rounding, H first */
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
    if (argc > 1 && argv[1][0] == 'z') {                      /* block sizes: all 4 size codes x 4 phases x positions; one-way, vs MPEG-2 (hv expected chained) */
        int z, ph, n, bad = 0, badhv = 0, tot = 0, tothv = 0, outside = 0;
        for (z = 0; z < 4; z++) for (ph = 0; ph < 4; ph++) {
            int mvx = ph & 1 ? 1 : 0, mvy = ph & 2 ? 1 : 0; bad = 0; tot = 0; outside = 0;
            for (n = 0; n < 8; n++) {
                int X = 8 + 4 * n, Y = 8 + 5 * (n & 3), i, j; unsigned char r0[256], r1[256];
                szcode = z; norestore = 0;
                for (i = 0; i < P * H; i++) src[i] = (unsigned char)rnd();
                memset(dst0, 0, sizeof dst0);
                if (ragegl_vwrite(c, srcOff, src, P * H, 0) || run(0, X, Y, mvx, mvy, &gui, &st) || st) { printf("run failed z%d ph%d st%u\n", z, ph, (unsigned)st); goto zdone; }
                ref(X, Y, mvx, mvy, 0, r0); ref(X, Y, mvx, mvy, 1, r1);
                for (j = 0; j < BH[z]; j++) for (i = 0; i < BW[z]; i++) { int o = out[(Y + j) * P + X + i]; tot++; if (o != ((ph == 3) ? r1 : r0)[j * 16 + i]) bad++; }
                { int k; for (k = 0; k < P * H; k++) { int x = k % P, y = k / P; if ((x < X || x >= X + BW[z] || y < Y || y >= Y + BH[z]) && out[k] != 0) outside++; } }
            }
            printf("size %dx%d phase %d (%s): %d of %d differ from %s; %d bytes outside the block\n", BW[z], BH[z], ph, ph == 0 ? "full" : ph == 1 ? "h" : ph == 2 ? "v" : "hv", bad, tot, ph == 3 ? "chained rounding" : "MPEG-2", outside);
        }
zdone:  szcode = 0; ragegl_free(c, srcOff); ragegl_free(c, dstOff); return 0;
    }
    if (argc > 1 && argv[1][0] == 'h') {                      /* hv half-pel vs SCALE_3D_CNTL low 6 bits: mismatches against MPEG-2 exact and chained model */
        int b, n;
        for (b = 0; b < 64; b++) {
            int bad0 = 0, bad1 = 0, tot = 0;
            for (n = 0; n < 4; n++) {
                int X = 10 + 5 * n, Y = 12 + 4 * n, i, j; unsigned char r0[256], r1[256];
                szcode = 0; twoway = 0; norestore = 0; texvar = 16 + b;
                for (i = 0; i < P * H; i++) { src[i] = (unsigned char)rnd(); dst0[i] = 0; }
                ragegl_vwrite(c, srcOff, src, P * H, 0);
                if (run(0, X, Y, 1, 1, &gui, &st) || st) { printf("fail b=%d\n", b); goto hdone; }
                ref(X, Y, 1, 1, 0, r0); ref(X, Y, 1, 1, 1, r1);
                for (j = 0; j < 16; j++) for (i = 0; i < 16; i++) { int o = out[(Y + j) * P + X + i]; tot++; if (o != r0[j * 16 + i]) bad0++; if (o != r1[j * 16 + i]) bad1++; }
            }
            if (bad0 == 0 || bad1 != 0 || b == 0x00) printf("3D_CNTL low6 %02x: %d of %d differ from MPEG-2 exact, %d from chained\n", b, bad0, tot, bad1);
        }
hdone:  texvar = 0; ragegl_free(c, srcOff); ragegl_free(c, dstOff); return 0;
    }
    if (argc > 1 && argv[1][0] == 'r') {                      /* residual: dst pre-loaded with signed residual (two's complement), MC pass adds the prediction; expect clip(pred + res) */
        int ph, n, bad, tot, edge;
        for (ph = 0; ph < 3; ph++) {
            int mvx = ph == 1 ? 1 : 0, mvy = ph == 2 ? 1 : 0; bad = tot = edge = 0;
            for (n = 0; n < 8; n++) {
                int X = 8 + 4 * n, Y = 12 + 3 * n, i, j; unsigned char r0[256];
                szcode = 0; twoway = 0; norestore = 0;
                for (i = 0; i < P * H; i++) { src[i] = (unsigned char)rnd(); dst0[i] = (unsigned char)((int)(rnd() % 256) - 128 + 128 == 0 ? 0 : (rnd() % 256)); }
                ragegl_vwrite(c, srcOff, src, P * H, 0);
                if (run(0, X, Y, mvx, mvy, &gui, &st) || st) { printf("fail st%u\n", (unsigned)st); goto rdone; }
                ref(X, Y, mvx, mvy, 0, r0);
                for (j = 0; j < 16; j++) for (i = 0; i < 16; i++) {
                    int res = (signed char)dst0[(Y + j) * P + X + i], e = r0[j * 16 + i] + res, o = out[(Y + j) * P + X + i];
                    if (e < 0) { e = 0; edge++; } if (e > 255) { e = 255; edge++; }
                    tot++; if (o != e) bad++;
                }
            }
            printf("residual+pred phase %d: %d of %d differ from clip(pred+res) (%d clipped samples)\n", ph, bad, tot, edge);
        }
rdone:  ragegl_free(c, srcOff); ragegl_free(c, dstOff); return 0;
    }
    if (argc > 1 && argv[1][0] == 'u') {                      /* secondary probe: left half 0x20, right half 0x60; primary at left, secondary 30 px to the right */
        int i, j, k;
        static const int sec[] = { 0, 1 };
        for (k = 0; k < 2 * 10; k++) {
            szcode = 0; twoway = sec[k & 1]; texvar = k >> 1; if (twoway == 0) continue; norestore = 0;
            for (j = 0; j < H; j++) for (i = 0; i < P; i++) src[j * P + i] = i < 32 ? 0x20 : 0x60;
            memset(dst0, 0, sizeof dst0);
            ragegl_vwrite(c, srcOff, src, P * H, 0);
            if (run(0, 8, 8, twoway ? 0 : 0, twoway ? 60 : 0, &gui, &st)) { printf("fail\n"); break; }
            printf("TEX_CNTL variant %u: two-way out(8,8)=%02x out(15,15)=%02x st %u\n", (unsigned)texvar, out[8 * P + 8], out[15 * P + 15], (unsigned)st);
        }
        twoway = 0; ragegl_free(c, srcOff); ragegl_free(c, dstOff); return 0;
    }
    if (argc > 1 && argv[1][0] == 't') {                      /* two-way: (p1 + p2 + 1) >> 1 of two MPEG-2 predictions, all 16 phase pairs, 16x16 and 8x8 */
        int z, p1, p2, n; static const int zs[2] = { 0, 2 };
        for (z = 0; z < 2; z++) for (p1 = 0; p1 < 4; p1++) for (p2 = 0; p2 < 4; p2++) {
            int bad = 0, tot = 0, hist[3] = { 0, 0, 0 }, outside = 0;
            for (n = 0; n < 6; n++) {
                int X = 10 + 4 * n, Y = 10 + 3 * n, i, j, m1x = p1 & 1 ? 1 : 0, m1y = p1 & 2 ? 1 : 0, m2x = (p2 & 1 ? 1 : 0) + 6, m2y = (p2 & 2 ? 1 : 0) - 4; unsigned char a[256], b[256];
                szcode = zs[z]; twoway = 1; norestore = 0;
                for (i = 0; i < P * H; i++) src[i] = (unsigned char)rnd();
                memset(dst0, 0, sizeof dst0);
                if (ragegl_vwrite(c, srcOff, src, P * H, 0) || run(0, X, Y, (m1x & 0xff) | ((m1y & 0xff) << 8) , ((m2x & 0xff) | ((m2y & 0xff) << 8)), &gui, &st) || st) { printf("run failed st%u\n", (unsigned)st); goto tdone; }
                ref(X, Y, m1x, m1y, p1 == 3, a); ref(X, Y, m2x, m2y, p2 == 3, b);       /* hv chained as measured */
                for (j = 0; j < BH[szcode]; j++) for (i = 0; i < BW[szcode]; i++) { int o = out[(Y + j) * P + X + i], e = (a[j * 16 + i] + b[j * 16 + i] + 1) >> 1, d = o - e; tot++; if (d) bad++; hist[d < 0 ? 0 : d == 0 ? 1 : 2]++; }
                { int k; for (k = 0; k < P * H; k++) { int x = k % P, y = k / P; if ((x < X || x >= X + BW[szcode] || y < Y || y >= Y + BH[szcode]) && out[k] != 0) outside++; } }
            }
            printf("two-way %s p1=%d p2=%d: %d of %d differ from (a+b+1)>>1 (hw<ref %d, hw>ref %d), %d outside\n", z ? "8x8" : "16x16", p1, p2, bad, tot, hist[0], hist[2], outside);
        }
tdone:  szcode = 0; twoway = 0; ragegl_free(c, srcOff); ragegl_free(c, dstOff); return 0;
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

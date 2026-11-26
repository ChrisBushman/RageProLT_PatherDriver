/* tools/ragegl/quad_test.c - textured-quad present (16 bpp target -> 32 bpp scanout): sweeps the sampling biases (RAGE_GL_CONFIG keys 3/4/5) to find the exact
 * 1:1 mapping, then checks conversion exactness and a scaled present.  Debug kext (scanout readback); the scanout must be 32 bpp.  Usage: quad_test [sweep|check ub vb xb] */
#include "ragegl.h"
#include <stdlib.h>
#include <sys/time.h>
#define TW 256u
static uint32_t rd32(const uint8_t *b, uint32_t o) { return (uint32_t)b[o] | ((uint32_t)b[o + 1] << 8) | ((uint32_t)b[o + 2] << 16) | ((uint32_t)b[o + 3] << 24); }
static uint16_t pat(uint32_t x, uint32_t y) { return (uint16_t)((((x * 5 + y * 3) & 31) << 11) | (((x * 7) ^ (y * 11)) & 63) << 5 | ((x + y * 2) & 31)); }
static uint32_t expand(uint16_t v, int mode)   /* 565 -> 0xffRRGGBB: mode 0 = bit replication, 1 = shift only */
{
    uint32_t r = v >> 11, g = (v >> 5) & 63, b = v & 31;
    if (mode == 0) return 0xff000000u | (((r << 3) | (r >> 2)) << 16) | (((g << 2) | (g >> 4)) << 8) | ((b << 3) | (b >> 2));
    return 0xff000000u | ((r << 3) << 16) | ((g << 2) << 8) | (b << 3);
}
static ragegl_t c; static uint32_t pitch, bpp;
/* present (0,0,sw,sh) -> (dx,dy,dw,dh), read the scanout rectangle back */
static int run(uint32_t sw, uint32_t sh, uint32_t dx, uint32_t dy, uint32_t dw, uint32_t dh, uint32_t filt, uint32_t *out)
{
    uint32_t y; uint8_t *line = malloc(pitch * 4);
    ragegl_scissor(c, 0, 255, 0, 255);
    if (ragegl_present_scaled(c, 0, 0, sw, sh, dx, dy, dw, dh, filt)) { free(line); return -1; }
    { uint32_t d; ragegl_fence(c, 1, &d); }
    for (y = 0; y < dh; y++) {
        if (ragegl_scanout_read(c, (dy + y) * pitch * 4, line, pitch * 4)) { free(line); return -2; }
        { uint32_t x; for (x = 0; x < dw; x++) out[y * dw + x] = rd32(line, (dx + x) * 4); }
    }
    free(line); return 0;
}
static void cfg(int ub, int vb, int xb) { ragegl_config(c, 6, 0); ragegl_config(c, 3, (uint32_t)ub); ragegl_config(c, 4, (uint32_t)vb); ragegl_config(c, 5, (uint32_t)xb); }
static int fails, checks;
#define CHECK(name, cond) do { checks++; if (cond) printf("PASS  %s\n", name); else { fails++; printf("FAIL  %s\n", name); } } while (0)
int main(int argc, char **argv)
{
    uint32_t gen, w, h, base, off, zoff, x, y, *got; uint16_t *img; int sweep = argc >= 2 && !strcmp(argv[1], "sweep");
    if (ragegl_open(&c)) { printf("cannot open the GL client\n"); return 99; }
    ragegl_display(c, &gen, &w, &h, &pitch, &bpp, &base);
    printf("display %ux%u bpp %u\n", w, h, bpp);
    if (bpp != 4) { printf("SKIP: scanout is not 32 bpp\n"); return 77; }
    if (argc >= 2 && !strcmp(argv[1], "bench")) {         /* full-screen present speed: 1024x768 16 bpp target (pitch 1024) -> the scanout, 1:1 and scaled */
        uint32_t o2, n, i, d; struct timeval t0, t1; double ms;
        if (w > 1024 || h > 768) { printf("bench needs a scanout up to 1024x768\n"); return 1; }
        if (ragegl_alloc(c, 1024 * 768 * 2, 4096, &o2) || ragegl_target(c, o2, 1024, 2, w, h)) { printf("bench target failed\n"); return 1; }
        for (n = 0; n < 2; n++) {
            ragegl_fence(c, 1, &d); gettimeofday(&t0, 0);
            for (i = 0; i < 40; i++) if (n ? ragegl_present_scaled(c, 0, 0, w / 2, h / 2, 0, 0, w, h, 1) : ragegl_present(c, 0, 0, w, h, 0, 0)) { printf("present failed\n"); { uint32_t g2; kern_return_t k2 = ragegl_present_scaled(c, 0, 0, w / 2, h / 2, 0, 0, w, h, 1); (void)g2; printf("retry returns 0x%x\n", k2); } return 1; }
            ragegl_fence(c, 1, &d); gettimeofday(&t1, 0);
            ms = ((t1.tv_sec - t0.tv_sec) * 1000.0 + (t1.tv_usec - t0.tv_usec) / 1000.0) / 40.0;
            printf("%s %ux%u: %.2f ms per present (%.1f Mpix/s)\n", n ? "scaled bilinear half -> full" : "1:1 16 -> 32", w, h, ms, w * h / ms / 1000.0);
        }
        return 0;
    }
    if (argc >= 2 && !strcmp(argv[1], "cache")) {          /* bilinear 128x96 -> 256x192 from a pitch-1024 texture under register variations (TEX_CNTL / SCALE_3D_CNTL bit flips) */
        static const uint32_t tcx[] = { 0, 1u << 30, 0, 1u << 30, 1u << 9, 1u << 17, 1u << 18, (1u << 17) | (1u << 18) }, scx[] = { 0, 0, 1u << 9, 1u << 9, 0, 0, 0, 0 }; uint32_t k, d, o2;
        if (ragegl_alloc(c, 1024 * 1024 * 2, 4096, &o2) || ragegl_target(c, o2, 1024, 2, 256, 256)) { printf("target failed\n"); return 1; }
        for (k = 0; k < 8; k++) {
            struct timeval t0, t1; kern_return_t kr, ks; double ms;
            ragegl_config(c, 7, tcx[k]); ragegl_config(c, 8, scx[k]);
            ragegl_present_scaled(c, 0, 0, 16, 16, 0, 0, 16, 16, 0); ragegl_present_scaled(c, 0, 0, 16, 16, 0, 0, 16, 16, 0); ragegl_fence(c, 1, &d);
            gettimeofday(&t0, 0); kr = ragegl_present_scaled(c, 0, 0, 128, 96, 0, 0, 256, 192, 1); ks = ragegl_present_scaled(c, 0, 0, 16, 16, 0, 0, 16, 16, 0); gettimeofday(&t1, 0);
            ms = (t1.tv_sec - t0.tv_sec) * 1000.0 + (t1.tv_usec - t0.tv_usec) / 1000.0;
            printf("TEX_CNTL ^ %08x, SCALE_3D ^ %08x: 0x%x 0x%x %.1f ms\n", tcx[k], scx[k], kr, ks, ms);
        }
        ragegl_config(c, 7, 0); ragegl_config(c, 8, 0);
        return 0;
    }
    if (argc >= 2 && !strcmp(argv[1], "pitch")) {          /* does the texture pitch matter for bilinear speed? same 128x96 -> 256x192 bilinear from targets of different pitch */
        static const uint32_t pit[] = { 256, 512, 1024 }; uint32_t k, d, o2;
        for (k = 0; k < 3; k++) {
            struct timeval t0, t1; kern_return_t kr, ks; double ms;
            if (ragegl_alloc(c, pit[k] * pit[k] * 2, 4096, &o2) || ragegl_target(c, o2, pit[k], 2, 256, 256)) { printf("target failed\n"); return 1; }
            ragegl_present_scaled(c, 0, 0, 16, 16, 0, 0, 16, 16, 0); ragegl_present_scaled(c, 0, 0, 16, 16, 0, 0, 16, 16, 0); ragegl_fence(c, 1, &d);
            gettimeofday(&t0, 0); kr = ragegl_present_scaled(c, 0, 0, 128, 96, 0, 0, 256, 192, 1); ks = ragegl_present_scaled(c, 0, 0, 16, 16, 0, 0, 16, 16, 0); gettimeofday(&t1, 0);
            ms = (t1.tv_sec - t0.tv_sec) * 1000.0 + (t1.tv_usec - t0.tv_usec) / 1000.0;
            printf("pitch %4u: bilinear 128x96 -> 256x192: 0x%x 0x%x %.1f ms\n", pit[k], kr, ks, ms);
        }
        return 0;
    }
    if (argc >= 2 && !strcmp(argv[1], "probe")) {          /* engine time of scaled presents: issue one, then a tiny settle present, which waits for the engine to go idle */
        static const uint32_t dsz[][2] = { {128,96}, {256,192}, {384,288}, {512,384}, {640,480}, {800,600}, {1024,768} };
        uint32_t o2, i, f, d; if (ragegl_alloc(c, 1024 * 768 * 2, 4096, &o2) || ragegl_target(c, o2, 1024, 2, w, h)) { printf("target failed\n"); return 1; }
        for (f = 0; f < 2; f++) for (i = 0; i < sizeof dsz / sizeof dsz[0]; i++) {
            struct timeval t0, t1; kern_return_t k, ks; double ms; uint32_t tries = 0;
            do { ks = ragegl_present_scaled(c, 0, 0, 16, 16, 0, 0, 16, 16, 0); ragegl_fence(c, 1, &d); } while (ks && ++tries < 4);       /* recover from the previous case */
            gettimeofday(&t0, 0);
            k = ragegl_present_scaled(c, 0, 0, dsz[i][0] / 2, dsz[i][1] / 2, 0, 0, dsz[i][0], dsz[i][1], f);
            ks = ragegl_present_scaled(c, 0, 0, 16, 16, 0, 0, 16, 16, 0);
            gettimeofday(&t1, 0); ms = (t1.tv_sec - t0.tv_sec) * 1000.0 + (t1.tv_usec - t0.tv_usec) / 1000.0;
            printf("%s half -> %4ux%-4u: present 0x%x, wait for idle 0x%x, %7.1f ms (recover tries %u)\n", f ? "bilinear" : "nearest ", dsz[i][0], dsz[i][1], k, ks, ms, tries);
        }
        return 0;
    }
    if (ragegl_alloc(c, TW * TW * 2, 4096, &off) || ragegl_target(c, off, TW, 2, TW, TW)) { printf("target failed\n"); return 1; }
    (void)zoff;
    img = malloc(TW * TW * 2); for (y = 0; y < TW; y++) for (x = 0; x < TW; x++) img[y * TW + x] = pat(x, y);
    if (ragegl_vwrite(c, off, img, TW * TW * 2, RAGE_XFER_SWAP16)) { printf("upload failed\n"); return 1; }
    got = malloc(64 * 48 * 4 * 16);
    if (sweep) {
        int ub, vb, xb, best = -1, bu = 0, bv = 0, bx = 0;
        for (ub = -128; ub <= 128; ub += 128) for (vb = -128; vb <= 128; vb += 128) for (xb = -2; xb <= 2; xb += 2) {
            uint32_t ok0 = 0, ok1 = 0, near = 0, i; int rc;
            cfg(ub, vb, xb); rc = run(64, 48, 20, 20, 64, 48, 0, got);
            if (rc) { printf("run failed %d\n", rc); return 1; }
            for (y = 0; y < 48; y++) for (x = 0; x < 64; x++) {
                uint32_t g0 = got[y * 64 + x] | 0xff000000u, e0 = expand(pat(x, y), 0), e1 = expand(pat(x, y), 1);
                ok0 += g0 == e0; ok1 += g0 == e1;
                { int dr = (int)((g0 >> 16) & 255) - (int)((e0 >> 16) & 255), dg = (int)((g0 >> 8) & 255) - (int)((e0 >> 8) & 255), db = (int)(g0 & 255) - (int)(e0 & 255);
                  near += (dr < 9 && dr > -9 && dg < 9 && dg > -9 && db < 9 && db > -9); }
            }
            (void)i;
            printf("ub %4d vb %4d xb %2d : replicate-exact %4u/3072  shift-exact %4u  within-8 %4u\n", ub, vb, xb, ok0, ok1, near);
            if ((int)(ok0 > ok1 ? ok0 : ok1) > best) { best = (int)(ok0 > ok1 ? ok0 : ok1); bu = ub; bv = vb; bx = xb; }
        }
        printf("best: ub %d vb %d xb %d (%d/3072 exact)\n", bu, bv, bx, best);
    } else if (argc >= 5) {
        int ub = atoi(argv[2]), vb = atoi(argv[3]), xb = atoi(argv[4]); uint32_t ok = 0;
        cfg(ub, vb, xb);
        if (run(64, 48, 20, 20, 64, 48, 0, got)) { printf("run failed\n"); return 1; }
        for (y = 0; y < 48; y++) for (x = 0; x < 64; x++) ok += (got[y * 64 + x] | 0xff000000u) == expand(pat(x, y), 0);
        printf("1:1 exact %u/3072\n", ok);
    } else {
        uint32_t ok, bad, maxe, i; double sum;
        cfg(0, 0, 0);
        CHECK("1:1 present runs", run(64, 48, 20, 20, 64, 48, 0, got) == 0);
        for (ok = 0, y = 0; y < 48; y++) for (x = 0; x < 64; x++) ok += (got[y * 64 + x] | 0xff000000u) == expand(pat(x, y), 0);
        printf("      1:1 exact %u/3072\n", ok); CHECK("16 -> 32 conversion exact (5/6 bit replication)", ok == 3072);
        CHECK("2x nearest runs", run(32, 24, 30, 30, 64, 48, 0, got) == 0);
        for (ok = 0, y = 0; y < 48; y++) for (x = 0; x < 64; x++) ok += (got[y * 64 + x] | 0xff000000u) == expand(pat(x / 2, y / 2), 0);
        printf("      2x nearest exact %u/3072\n", ok); CHECK("2x nearest upscale exact", ok == 3072);
        CHECK("2x bilinear runs", run(32, 24, 40, 40, 64, 48, 1, got) == 0);
        for (sum = 0, maxe = 0, bad = 0, y = 1; y < 47; y++) for (x = 1; x < 63; x++) {          /* interior: the software reference is the exact 2x2 bilinear at the pixel centre */
            uint32_t g = got[y * 64 + x], ch; int fx = (int)(x * 2 + 1) - 2, fy = (int)(y * 2 + 1) - 2;   /* source position * 4 minus the half texel: units of 1/4 texel */
            int sx0 = fx >> 2, sy0 = fy >> 2, wx = fx & 3, wy = fy & 3;
            for (ch = 0; ch < 3; ch++) {
                uint32_t sh2 = ch * 8; uint32_t e00 = (expand(pat(sx0, sy0), 0) >> sh2) & 255, e10 = (expand(pat(sx0 + 1, sy0), 0) >> sh2) & 255, e01 = (expand(pat(sx0, sy0 + 1), 0) >> sh2) & 255, e11 = (expand(pat(sx0 + 1, sy0 + 1), 0) >> sh2) & 255;
                int ref = (int)(((e00 * (4 - wx) + e10 * wx) * (4 - wy) + (e01 * (4 - wx) + e11 * wx) * wy) / 16), v = (int)((g >> sh2) & 255), e = v > ref ? v - ref : ref - v;
                sum += e; if ((uint32_t)e > maxe) maxe = (uint32_t)e; if (e > 40) bad++;
            }
            if (sx0 + 1 >= 32 || sy0 + 1 >= 24) continue;
        }
        printf("      bilinear vs software reference: mean error %.2f, max %u, %u samples off by >40 (of %u)\n", sum / (62.0 * 46.0 * 3.0), maxe, bad, 62u * 46u * 3u);
        CHECK("bilinear upscale close to the software reference (mean error < 12)", sum / (62.0 * 46.0 * 3.0) < 12.0);
        { uint32_t row[16]; (void)row; }
        CHECK("present clipped at the right edge runs", ragegl_present_scaled(c, 0, 0, 64, 48, w - 20, 100, 64, 48, 0) == 0);
        CHECK("present beyond the scanout is a no-op", ragegl_present_scaled(c, 0, 0, 64, 48, w + 10, 10, 64, 48, 0) == 0);
        CHECK("source outside the target refused", ragegl_present_scaled(c, 200, 0, 64, 48, 20, 20, 64, 48, 0) != 0);
        { uint32_t d; CHECK("fence", ragegl_fence(c, 1, &d) == 0); }
        (void)i;
    }
    printf("quad_test: %d checks, %d failed\n", checks, fails);
    cfg(0, 0, 0);
    ragegl_close(c);
    return fails;
}

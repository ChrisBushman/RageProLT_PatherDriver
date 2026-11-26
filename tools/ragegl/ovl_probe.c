/* tools/ragegl/ovl_probe.c - overlay decoding probe with YES/NO questions.  32x24 source stretched to 1024x768 (every source pixel = a 32x32 block) on a neutral grey (64,64,64) background.
 * usage: ovl_probe [src_bpp 2|4 (default 2)] [flags (debug kext): 1 HORZ_MODE, 2 VERT_MODE, 4 identity table, 8 flat 50/50 table, 12 = identity + flat in COEFF4 only] [hold s; 0 = until SIGTERM (default 0)]
 * Five bands (screen rows, top to bottom); each is THREE contiguous cells at source columns 4,5,6 (screen x 128-223), marked by a bright RED cell two columns to the left (col 2) and a bright GREEN cell two columns to the right (col 8) with one grey gap cell on each side, so the test cells are exactly the three cells between the red and green markers even when the filter shifts or widens things:
 *   band 1: red, red, black     band 2: green, green, black     band 3: blue, blue, black     band 4: white, white, black     band 5: black, white, white
 * Question per band 1-4: is the SECOND cell the same colour as the FIRST?   Band 5: is the first (black) cell pure black everywhere, or does it have a coloured band (where)?
 * Needs a 1024x768 scanout.  Writes: the step B overlay list.  Always disables the overlay on exit. */
#include "ragegl.h"
#include <stdlib.h>
#include <unistd.h>
#include <signal.h>
static ragegl_t c; static uint32_t bpp = 2; static volatile int stop_req;
static void off(int sig) { (void)sig; stop_req = 1; }
static uint32_t col(uint32_t r, uint32_t g, uint32_t b) { return bpp == 4 ? 0xff000000u | (r << 16) | (g << 8) | b : (((r >> 3) << 11) | ((g >> 2) << 5) | (b >> 3)); }
int main(int argc, char **argv)
{
    uint32_t gen, dw, dh, pitch, dbpp, base, o, hold, flags, K, W, R, G, B; int band, i, vert, rowbytes; kern_return_t kr; uint32_t pitch_px = 1024;
    setvbuf(stdout, NULL, _IONBF, 0);
    bpp = argc > 1 ? (uint32_t)atoi(argv[1]) : 2; flags = argc > 2 ? (uint32_t)atoi(argv[2]) : 0; hold = argc > 3 ? (uint32_t)atoi(argv[3]) : 0;
    vert = argc > 4 ? atoi(argv[4]) : 0;
    if (bpp < 2 || bpp > 4) { printf("usage: ovl_probe [2|4|3=YUV(debug)] [flags] [hold s] [pattern 0 = horizontal groups, 1 = vertical stacks]\n"); return 64; }
    K = col(0,0,0); W = col(255,255,255); R = col(255,0,0); G = col(0,255,0); B = col(0,0,255);
    if (ragegl_open(&c)) { printf("cannot open the GL client\n"); return 99; }
    ragegl_display(c, &gen, &dw, &dh, &pitch, &dbpp, &base);
    printf("display %ux%u, %u bytes/pixel; source 32x24 (32x), %u bytes/pixel, flags %u\n", dw, dh, dbpp, bpp, flags);
    if (dw != 1024 || dh != 768) { printf("SKIP: needs a 1024x768 scanout\n"); return 77; }
    rowbytes = (int)(pitch_px * (bpp == 4 ? 4u : 2u));
    if (bpp == 3) {            /* YUV test (debug kext): packed VYUY 4:2:2, memory bytes per 2 pixels = Y0, U, Y1, V; chroma neutral (0x80); luma only */
        static uint8_t buf[2048 * 24]; int x, y; const uint8_t Yk = 16, Yw = 235, Yb = 64, Ym = 160;
        uint8_t yv[32][24]; static const uint8_t bandp[5][3] = { {1,1,0}, {0,1,1}, {1,0,1}, {0,0,1}, {1,1,1} };   /* 1 = white, 0 = black */
        for (x = 0; x < 32; x++) for (y = 0; y < 24; y++) yv[x][y] = Yb;
        for (band = 0; band < 5; band++) for (y = 1 + band * 4; y < 4 + band * 4; y++) { for (i = 0; i < 3; i++) yv[4 + i][y] = bandp[band][i] ? Yw : Yk; yv[2][y] = Ym; yv[8][y] = Ym; }
        for (y = 0; y < 24; y++) for (x = 0; x < 32; x += 2) { uint8_t *m = buf + y * rowbytes + x * 2; m[0] = yv[x][y]; m[1] = 0x80; m[2] = yv[x + 1][y]; m[3] = 0x80; }
        if (ragegl_alloc(c, (uint32_t)rowbytes * 24u, 4096, &o) || ragegl_vwrite(c, o, buf, (uint32_t)rowbytes * 24u, 0)) { printf("alloc/write failed\n"); return 1; }
        { uint32_t d; ragegl_fence(c, 1, &d); }
    } else {
    if (ragegl_alloc(c, pitch_px * bpp * 24u, 4096, &o) || ragegl_target(c, o, pitch_px, bpp, 32, 24)) { printf("alloc/target failed\n"); return 1; }
    ragegl_scissor(c, 0, 31, 0, 23);
    ragegl_fill(c, 0, 0, 32, 24, col(64,64,64));
    { uint32_t row[5][3] = { {R,R,K}, {G,G,K}, {B,B,K}, {W,W,K}, {K,W,W} };
      if (!vert) { for (band = 0; band < 5; band++) for (i = 0; i < 3; i++) ragegl_fill(c, 4 + i, 1 + band * 4, 1, 3, row[band][i]); ragegl_fill(c, 2, 1 + band * 4, 1, 3, R); ragegl_fill(c, 8, 1 + band * 4, 1, 3, G); }
      else { for (band = 0; band < 5; band++) { for (i = 0; i < 3; i++) ragegl_fill(c, 2 + band * 6, 8 + i, 4, 1, row[band][i]); ragegl_fill(c, 2 + band * 6, 6, 4, 1, R); ragegl_fill(c, 2 + band * 6, 12, 4, 1, G); } } }
    { uint32_t d; ragegl_fence(c, 1, &d); }
    }
    signal(SIGINT, off); signal(SIGTERM, off);
    kr = ragegl_overlay_on(c, o, pitch_px, 32u, 24u, 0u, 0u, 1024u, 768u, bpp | (flags << 8));
    if (kr) { printf("OVL_ON failed 0x%x\n", (unsigned)kr); ragegl_free(c, o); return 2; }
    printf("overlay ENABLED (flags %u), holding %s\n", flags, hold ? "(timed)" : "until SIGTERM");
    if (hold) sleep(hold); else while (!stop_req) sleep(1);
    kr = ragegl_overlay_off(c);
    printf("overlay disabled (0x%x)\n", (unsigned)kr);
    ragegl_free(c, o); ragegl_close(c);
    return 0;
}

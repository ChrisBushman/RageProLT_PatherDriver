/* tools/ragegl/ovl_probe.c - overlay decoding probe with YES/NO questions.  32x24 source stretched to 1024x768 (every source pixel = a 32x32 block) on a neutral grey (64,64,64) background.
 * usage: ovl_probe [src_bpp 2|4 (default 2)] [flags (debug kext): 1 HORZ_MODE, 2 VERT_MODE, 4 identity table, 8 flat 50/50 table, 12 = identity + flat in COEFF4 only] [hold s; 0 = until SIGTERM (default 0)]
 * Five bands (screen rows, top to bottom); each is THREE contiguous cells at source columns 4,5,6 (screen x 128-223):
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
    uint32_t gen, dw, dh, pitch, dbpp, base, o, hold, flags, K, W, R, G, B; int band, i; kern_return_t kr; uint32_t pitch_px = 1024;
    setvbuf(stdout, NULL, _IONBF, 0);
    bpp = argc > 1 ? (uint32_t)atoi(argv[1]) : 2; flags = argc > 2 ? (uint32_t)atoi(argv[2]) : 0; hold = argc > 3 ? (uint32_t)atoi(argv[3]) : 0;
    if (bpp != 2 && bpp != 4) { printf("usage: ovl_probe [2|4] [flags] [hold s]\n"); return 64; }
    K = col(0,0,0); W = col(255,255,255); R = col(255,0,0); G = col(0,255,0); B = col(0,0,255);
    if (ragegl_open(&c)) { printf("cannot open the GL client\n"); return 99; }
    ragegl_display(c, &gen, &dw, &dh, &pitch, &dbpp, &base);
    printf("display %ux%u, %u bytes/pixel; source 32x24 (32x), %u bytes/pixel, flags %u\n", dw, dh, dbpp, bpp, flags);
    if (dw != 1024 || dh != 768) { printf("SKIP: needs a 1024x768 scanout\n"); return 77; }
    if (ragegl_alloc(c, pitch_px * bpp * 24u, 4096, &o) || ragegl_target(c, o, pitch_px, bpp, 32, 24)) { printf("alloc/target failed\n"); return 1; }
    ragegl_scissor(c, 0, 31, 0, 23);
    ragegl_fill(c, 0, 0, 32, 24, col(64,64,64));
    { uint32_t row[5][3] = { {R,R,K}, {G,G,K}, {B,B,K}, {W,W,K}, {K,W,W} }; for (band = 0; band < 5; band++) for (i = 0; i < 3; i++) ragegl_fill(c, 4 + i, 1 + band * 4, 1, 3, row[band][i]); }
    { uint32_t d; ragegl_fence(c, 1, &d); }
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

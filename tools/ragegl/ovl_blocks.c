/* tools/ragegl/ovl_blocks.c - overlay filter decoding test: a TINY source (64x48 or 32x24) stretched to 1024x768, so every source pixel is a 16x16 (or 32x32) block on screen
 * and any blending, colour-channel shift or fringe is visible as a fraction of a block.
 * usage: ovl_blocks [cols 64|32 (default 64 = 16x; 32 = 32x)] [src_bpp 2|4 (default 2)] [hold s; 0 = until SIGTERM (default 0)] [flags (debug kext): 1 HORZ_MODE, 2 VERT_MODE, 4 identity table, 8 flat 50/50 table]
 * Layout (cell = one source pixel; rows counted in the 48-row layout, the 32x24 source halves every dimension):
 *   rows 0-7   black background: white vertical lines 1, 2 and 3 cells wide; two 1-cell lines with a 1-cell gap; a 1-cell white/black checker
 *   rows 8-15  white background: the same shapes in black
 *   rows 16-23 black background, rows 24-31 white background: 4-cell blocks of red, green, blue, cyan, magenta, yellow, white (gaps between)
 *   rows 32-39 eight adjacent grey steps 0 .. 100 % (no gaps)
 *   rows 40-47 horizontal 1-cell lines: white on black (left half), black on white (right half)
 * Needs a 1024x768 scanout.  Writes: the step B overlay list; always disables the overlay on exit. */
#include "ragegl.h"
#include <stdlib.h>
#include <unistd.h>
#include <signal.h>
static ragegl_t c; static uint32_t bpp = 2; static volatile int stop_req; static int k;   /* k = cell scale divisor (1 for 64x48, 2 for 32x24) */
static void off(int sig) { (void)sig; stop_req = 1; }
static uint32_t col(uint32_t r, uint32_t g, uint32_t b) { return bpp == 4 ? 0xff000000u | (r << 16) | (g << 8) | b : (((r >> 3) << 11) | ((g >> 2) << 5) | (b >> 3)); }
static void cell(int x, int y, int w, int h, uint32_t v)     /* 64x48 layout units */
{
    int x0 = x / k, y0 = y / k, x1 = (x + w) / k, y1 = (y + h) / k; if (x1 <= x0) x1 = x0 + 1; if (y1 <= y0) y1 = y0 + 1;
    ragegl_fill(c, x0, y0, x1 - x0, y1 - y0, v);
}
int main(int argc, char **argv)
{
    uint32_t gen, dw, dh, pitch, dbpp, base, o, hold, flags; int sw, sh, i, y, cols; kern_return_t kr; uint32_t pitch_px = 1024;
    uint32_t BLK, WHT, rgb[7], grey[8]; static const uint8_t pc[7][3] = { {255,0,0},{0,255,0},{0,0,255},{0,255,255},{255,0,255},{255,255,0},{255,255,255} };
    setvbuf(stdout, NULL, _IONBF, 0);
    cols = argc > 1 ? atoi(argv[1]) : 64; bpp = argc > 2 ? (uint32_t)atoi(argv[2]) : 2; hold = argc > 3 ? (uint32_t)atoi(argv[3]) : 0; flags = argc > 4 ? (uint32_t)atoi(argv[4]) : 0;
    if ((cols != 64 && cols != 32) || (bpp != 2 && bpp != 4)) { printf("usage: ovl_blocks [64|32] [2|4] [hold s] [flags]\n"); return 64; }
    k = 64 / cols; sw = cols; sh = 48 / k;
    BLK = col(0,0,0); WHT = col(255,255,255);
    for (i = 0; i < 7; i++) rgb[i] = col(pc[i][0], pc[i][1], pc[i][2]);
    for (i = 0; i < 8; i++) { uint32_t g = (uint32_t)(i * 255 / 7); grey[i] = col(g, g, g); }
    if (ragegl_open(&c)) { printf("cannot open the GL client\n"); return 99; }
    ragegl_display(c, &gen, &dw, &dh, &pitch, &dbpp, &base);
    printf("display %ux%u, %u bytes/pixel; source %dx%d (%dx), %u bytes/pixel, flags %u\n", dw, dh, dbpp, sw, sh, 1024 / sw, bpp, flags);
    if (dw != 1024 || dh != 768) { printf("SKIP: needs a 1024x768 scanout\n"); return 77; }
    if (ragegl_alloc(c, pitch_px * bpp * (uint32_t)sh, 4096, &o) || ragegl_target(c, o, pitch_px, bpp, (uint32_t)sw, (uint32_t)sh)) { printf("alloc/target failed\n"); return 1; }
    ragegl_scissor(c, 0, sw - 1, 0, sh - 1);
    /* backgrounds */
    cell(0, 0, 64, 8, BLK); cell(0, 8, 64, 8, WHT); cell(0, 16, 64, 8, BLK); cell(0, 24, 64, 8, WHT);
    /* rows 0-7 white shapes on black; rows 8-15 black shapes on white */
    for (i = 0; i < 2; i++) {
        int y0 = i * 8; uint32_t fg = i ? BLK : WHT, bg = i ? WHT : BLK;
        cell(4, y0, 1, 8, fg); cell(10, y0, 2, 8, fg); cell(17, y0, 3, 8, fg); cell(26, y0, 1, 8, fg); cell(28, y0, 1, 8, fg);
        { int x; for (x = 34; x < 48; x += 2) cell(x, y0, 1, 8, fg); (void)bg; }
    }
    /* colour blocks */
    for (i = 0; i < 7; i++) { cell(2 + i * 8, 16, 4, 8, rgb[i]); cell(2 + i * 8, 24, 4, 8, rgb[i]); }
    /* grey steps */
    for (i = 0; i < 8; i++) cell(i * 8, 32, 8, 8, grey[i]);
    /* horizontal lines */
    cell(0, 40, 32, 8, BLK); cell(32, 40, 32, 8, WHT);
    cell(0, 43, 32, 1, WHT); cell(32, 43, 32, 1, BLK);
    for (y = 0; y < 0; y++) ;
    { uint32_t d; ragegl_fence(c, 1, &d); }
    signal(SIGINT, off); signal(SIGTERM, off);
    kr = ragegl_overlay_on(c, o, pitch_px, (uint32_t)sw, (uint32_t)sh, 0u, 0u, 1024u, 768u, bpp | (flags << 8));
    if (kr) { printf("OVL_ON failed 0x%x\n", (unsigned)kr); ragegl_free(c, o); return 2; }
    printf("overlay ENABLED (flags %u): %dx%d -> 1024x768, %s\n", flags, sw, sh, hold ? "timed hold" : "holding until SIGTERM");
    if (hold) sleep(hold); else while (!stop_req) sleep(1);
    kr = ragegl_overlay_off(c);
    printf("overlay disabled (0x%x)\n", (unsigned)kr);
    ragegl_free(c, o); ragegl_close(c);
    return 0;
}

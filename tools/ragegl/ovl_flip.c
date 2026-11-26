/* tools/ragegl/ovl_flip.c - overlay plan step C1 hardware test: TWO 640x480 buffers shown through the overlay scaler, flipped with RAGE_GL_OVL_FLIP.
 *   buffer A = colour bars + 32 px grid; buffer B = diagonals + circle on dark blue (to judge stair-stepping of the bilinear scaling).
 * usage: ovl_flip [src_bpp 2|4 (default 2)] [flips (default 30)] [ms per frame (default 1000)]
 * src_bpp 2 = the 16 bpp target format of the scanout (RGB565 on a 32 bpp scanout: the FS_SCALE case; ARGB1555 on a 15 bit scanout), 4 = ARGB8888.  Writes: the step B overlay list, then repeated
 * SCALER_BUF0/BUF1_OFFSET rewrites.  Always disables the overlay on exit.  Debug or release kext (RAGE_FEAT_OVERLAY). */
#include "ragegl.h"
#include <stdlib.h>
#include <unistd.h>
#include <signal.h>
static ragegl_t c; static uint32_t bpp = 2;
static void off(int sig) { (void)sig; ragegl_overlay_off(c); _exit(1); }
static uint32_t col(uint32_t r, uint32_t g, uint32_t b) { return bpp == 4 ? 0xff000000u | (r << 16) | (g << 8) | b : (((r >> 3) << 11) | ((g >> 2) << 5) | (b >> 3)); }
static void px(int x, int y, uint32_t v) { if (x >= 0 && x < 640 && y >= 0 && y < 480) ragegl_fill(c, x, y, 1, 1, v); }
static void line(int x0, int y0, int x1, int y1, uint32_t v)
{
    int dx = abs(x1 - x0), sx = x0 < x1 ? 1 : -1, dy = -abs(y1 - y0), sy = y0 < y1 ? 1 : -1, e = dx + dy, e2;
    for (;;) { px(x0, y0, v); if (x0 == x1 && y0 == y1) break; e2 = 2 * e; if (e2 >= dy) { e += dy; x0 += sx; } if (e2 <= dx) { e += dx; y0 += sy; } }
}
static void circle(int cx, int cy, int r, uint32_t v)
{
    int x = r, y = 0, e = 1 - r;
    while (x >= y) { px(cx+x,cy+y,v); px(cx+y,cy+x,v); px(cx-y,cy+x,v); px(cx-x,cy+y,v); px(cx-x,cy-y,v); px(cx-y,cy-x,v); px(cx+y,cy-x,v); px(cx+x,cy-y,v);
        y++; if (e < 0) e += 2 * y + 1; else { x--; e += 2 * (y - x) + 1; } }
}
int main(int argc, char **argv)
{
    uint32_t gen, dw, dh, pitch, dbpp, base, a, b, i, flips, ms; kern_return_t kr; uint32_t pitch_px = 1024; static const uint8_t bars[8][3] = { {255,255,255},{255,255,0},{0,255,255},{0,255,0},{255,0,255},{255,0,0},{0,0,255},{0,0,0} };
    setvbuf(stdout, NULL, _IONBF, 0);
    bpp = argc > 1 ? (uint32_t)atoi(argv[1]) : 2; flips = argc > 2 ? (uint32_t)atoi(argv[2]) : 30; ms = argc > 3 ? (uint32_t)atoi(argv[3]) : 1000;
    if (bpp != 2 && bpp != 4) { printf("src_bpp must be 2 or 4\n"); return 64; }
    if (ragegl_open(&c)) { printf("cannot open the GL client\n"); return 99; }
    ragegl_display(c, &gen, &dw, &dh, &pitch, &dbpp, &base);
    printf("display %ux%u, %u bytes/pixel; source buffers %u bytes/pixel\n", dw, dh, dbpp, bpp);
    if (dw != 1024 || dh != 768) { printf("SKIP: needs a 1024x768 scanout\n"); return 77; }
    if (ragegl_alloc(c, pitch_px * bpp * 480u, 4096, &a) || ragegl_alloc(c, pitch_px * bpp * 480u, 4096, &b)) { printf("alloc failed\n"); return 1; }
    /* buffer A: bars + grid */
    ragegl_target(c, a, pitch_px, bpp, 640, 480); ragegl_scissor(c, 0, 639, 0, 479);
    for (i = 0; i < 8; i++) ragegl_fill(c, (int)(i * 80), 0, 80, 480, col(bars[i][0], bars[i][1], bars[i][2]));
    for (i = 0; i <= 640; i += 32) { int x = i == 640 ? 639 : (int)i; ragegl_fill(c, x, 0, 1, 240, col(0,0,0)); ragegl_fill(c, x, 240, 1, 240, col(255,255,255)); }
    for (i = 0; i <= 480; i += 32) { int y = i == 480 ? 479 : (int)i; ragegl_fill(c, 0, y, 640, 1, col(0,0,0)); }
    /* buffer B: diagonals + circle on dark blue */
    ragegl_target(c, b, pitch_px, bpp, 640, 480); ragegl_scissor(c, 0, 639, 0, 479);
    ragegl_fill(c, 0, 0, 640, 480, col(0, 0, 96));
    line(0, 0, 639, 479, col(255,255,255)); line(639, 0, 0, 479, col(255,255,255)); line(0, 479, 639, 200, col(255,255,0)); line(0, 100, 639, 140, col(0,255,255));
    circle(320, 240, 200, col(255,128,0)); circle(320, 240, 120, col(0,255,0)); circle(320, 240, 40, col(255,0,255));
    { uint32_t d; ragegl_fence(c, 1, &d); }
    printf("buffers drawn: A (bars+grid) at 0x%x, B (diagonals+circles) at 0x%x\n", a, b);
    signal(SIGINT, off); signal(SIGTERM, off);
    kr = ragegl_overlay_on(c, a, pitch_px, 640u, 480u, 0u, 0u, 1024u, 768u, bpp);
    if (kr) { printf("OVL_ON failed 0x%x\n", (unsigned)kr); return 2; }
    printf("overlay ENABLED showing A; flipping A<->B %u times, %u ms each\n", flips, ms);
    for (i = 0; i < flips; i++) {
        usleep(ms * 1000u);
        kr = ragegl_overlay_flip(c, (i & 1) ? a : b);
        if (kr) { printf("OVL_FLIP failed 0x%x\n", (unsigned)kr); break; }
        printf("flip %u -> %s\n", i + 1, (i & 1) ? "A (bars)" : "B (diagonals)");
    }
    usleep(ms * 1000u);
    kr = ragegl_overlay_off(c);
    printf("overlay disabled (0x%x)\n", (unsigned)kr);
    ragegl_free(c, a); ragegl_free(c, b);
    ragegl_close(c);
    return 0;
}

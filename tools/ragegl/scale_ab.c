/* tools/ragegl/scale_ab.c - blind A/B for the FS_SCALE upscale of a 640x480 drawable on a 1024x768 scanout: the SAME 640x480 picture shown through the hardware overlay scaler (nearest/replicating)
 * or through the kernel's textured-quad present (bilinear).  Picture: dark blue field, white/yellow/cyan diagonals, three circles, a horizontal RGB gradient strip at the top and a 1-pixel
 * black/white line box at the bottom left.  usage: scale_ab ovl|quad [hold seconds, default 20]   (needs a 1024x768 scanout; leaves the picture on screen when it exits: repaint afterwards) */
#include "ragegl.h"
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <signal.h>
static ragegl_t c; static int f555;
static uint32_t col(uint32_t r, uint32_t g, uint32_t b) { return f555 ? (((r >> 3) << 10) | ((g >> 3) << 5) | (b >> 3)) : (((r >> 3) << 11) | ((g >> 2) << 5) | (b >> 3)); }
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
static void off(int sig) { (void)sig; ragegl_overlay_off(c); _exit(1); }
int main(int argc, char **argv)
{
    uint32_t gen, feat, endv, flags, dw, dh, pitch, dbpp, base, a, i; int ovl, hold = argc > 2 ? atoi(argv[2]) : 20, x;
    if (argc < 2) return 64;
    ovl = !strcmp(argv[1], "ovl");
    if (ragegl_open(&c)) { printf("cannot open the GL client\n"); return 99; }
    ragegl_display(c, &gen, &dw, &dh, &pitch, &dbpp, &base);
    if (dw != 1024 || dh != 768) { printf("SKIP: needs a 1024x768 scanout (got %ux%u)\n", dw, dh); return 77; }
    ragegl_info(c, &gen, &feat, &endv, &flags); f555 = (flags & RAGE_INFO_TGT555) != 0;
    if (ragegl_alloc(c, 1024u * 2u * 480u, 4096, &a)) { printf("alloc failed\n"); return 1; }
    ragegl_target(c, a, 1024, 2, 640, 480); ragegl_scissor(c, 0, 639, 0, 479);
    ragegl_fill(c, 0, 0, 640, 480, col(0, 0, 96));
    line(0, 60, 639, 479, col(255,255,255)); line(639, 60, 0, 479, col(255,255,255)); line(0, 479, 639, 200, col(255,255,0)); line(0, 100, 639, 140, col(0,255,255));
    circle(320, 270, 180, col(255,128,0)); circle(320, 270, 110, col(0,255,0)); circle(320, 270, 36, col(255,0,255));
    for (x = 0; x < 640; x++) { int t = x * 255 / 639; ragegl_fill(c, x, 0, 1, 24, col(t, 255 - t, 128)); ragegl_fill(c, x, 24, 1, 24, col(t, t, t)); }
    ragegl_fill(c, 0, 360, 160, 120, col(255,255,255));
    for (i = 0; i < 160; i += 2) ragegl_fill(c, (int)i, 360, 1, 60, col(0,0,0));
    for (i = 0; i < 60; i += 2) ragegl_fill(c, 0, 420 + (int)i, 160, 1, col(0,0,0));
    { uint32_t d; ragegl_fence(c, 1, &d); }
    signal(SIGINT, off); signal(SIGTERM, off);
    if (ovl) { if (ragegl_overlay_on(c, a, 1024, 640u, 480u, 0u, 0u, 1024u, 768u, 2)) { printf("OVL_ON failed\n"); return 2; } printf("overlay on (%s)\n", f555 ? "1555" : "565"); }
    else { if (ragegl_present_scaled(c, 0, 0, 640, 480, 0, 0, 1024, 768, 1)) { printf("present_scaled failed\n"); return 2; } printf("quad present (bilinear) done (%s)\n", f555 ? "1555" : "565"); }
    sleep(hold);
    if (ovl) ragegl_overlay_off(c);
    ragegl_free(c, a); ragegl_close(c); return 0;
}

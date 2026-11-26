/* tools/ragegl/ovl_sweep.c - overlay filter test: a frequency-sweep ("multiburst") pattern with a numbered scale, shown through the overlay scaler.
 * usage: ovl_sweep [mode 1|2 (1 = 640x480 source = 1.6x, 2 = 320x240 source = 3.2x; default 1)] [src_bpp 2|4 (default 2)] [hold seconds; 0 = stay up until SIGTERM/SIGINT (default 0)] [mode flags: 1 = SCALE_HORZ_MODE, 2 = SCALE_VERT_MODE; debug kext only (default 0)]
 * Pattern (source pixels; every number is the bar PERIOD in source pixels per black+white cycle at that spot - 16 = wide bars, 2 = one pixel black / one pixel white):
 *   top band    : vertical bars, period 16 at the left narrowing to 2 at the right (tests the HORIZONTAL filter); the numbers sit above the band
 *   middle band : horizontal bars, period 16 at the top narrowing to 2 at the bottom (tests the VERTICAL filter); the numbers sit in the left margin
 *   bottom band : left = hard black/white vertical edge, right = 1-pixel black/white stripes
 * Needs a 1024x768 scanout.  Writes: the overlay list approved for step B (OVL_ON), nothing else.  Always disables the overlay on exit. */
#include "ragegl.h"
#include <stdlib.h>
#include <unistd.h>
#include <signal.h>
static ragegl_t c; static uint32_t bpp = 2;
static volatile int stop_req;
static void off(int sig) { (void)sig; stop_req = 1; }   /* the main loop then disables the overlay and frees the block (the kernel also disables it if we die) */
static uint32_t col(uint32_t r, uint32_t g, uint32_t b) { return bpp == 4 ? 0xff000000u | (r << 16) | (g << 8) | b : (((r >> 3) << 11) | ((g >> 2) << 5) | (b >> 3)); }
static uint32_t BLK, WHT, GRY;
static const uint16_t font[10] = { 0x7b6f, 0x2c97, 0x73e7, 0x73cf, 0x5bc9, 0x79cf, 0x79ef, 0x7249, 0x7bef, 0x7bcf };   /* 3x5 digits, MSB = top-left, row-major */
static int digits(int x, int y, int n, int s)       /* draws n at (x,y) with pixel size s, returns the width used */
{
    char t[8]; int len = 0, i, r, q;
    if (n >= 10) t[len++] = (char)(n / 10); t[len++] = (char)(n % 10);
    for (i = 0; i < len; i++) for (r = 0; r < 5; r++) for (q = 0; q < 3; q++) if (font[(int)t[i]] & (1u << (14 - (r * 3 + q)))) ragegl_fill(c, x + (i * 4 + q) * s, y + r * s, s, s, BLK);
    return len * 4 * s - s;
}
static int period_at(int pos, int len) { return 16 - (14 * pos + len / 2) / len; }   /* 16 .. 2 */
int main(int argc, char **argv)
{
    uint32_t gen, dw, dh, pitch, dbpp, base, o, hold, flags; int mode, sw, sh, s, x, y, p, k, lab, bandH, v0, v1, bot, mx; kern_return_t kr; uint32_t pitch_px = 1024;
    setvbuf(stdout, NULL, _IONBF, 0);
    mode = argc > 1 ? atoi(argv[1]) : 1; bpp = argc > 2 ? (uint32_t)atoi(argv[2]) : 2; hold = argc > 3 ? (uint32_t)atoi(argv[3]) : 0; flags = argc > 4 ? (uint32_t)atoi(argv[4]) : 0;
    if ((mode != 1 && mode != 2) || (bpp != 2 && bpp != 4)) { printf("usage: ovl_sweep [1|2] [2|4] [hold s, 0 = until signalled]\n"); return 64; }
    sw = mode == 1 ? 640 : 320; sh = mode == 1 ? 480 : 240; s = mode == 1 ? 3 : 2;
    BLK = col(0,0,0); WHT = col(255,255,255); GRY = col(128,128,128);
    if (ragegl_open(&c)) { printf("cannot open the GL client\n"); return 99; }
    ragegl_display(c, &gen, &dw, &dh, &pitch, &dbpp, &base);
    printf("display %ux%u, %u bytes/pixel; source %dx%d (%.1fx), %u bytes/pixel\n", dw, dh, dbpp, sw, sh, 1024.0 / sw, bpp);
    if (dw != 1024 || dh != 768) { printf("SKIP: needs a 1024x768 scanout\n"); return 77; }
    if (ragegl_alloc(c, pitch_px * bpp * (uint32_t)sh, 4096, &o) || ragegl_target(c, o, pitch_px, bpp, (uint32_t)sw, (uint32_t)sh)) { printf("alloc/target failed\n"); return 1; }
    ragegl_scissor(c, 0, sw - 1, 0, sh - 1);
    ragegl_fill(c, 0, 0, sw, sh, WHT);
    lab = 5 * s + 2 * s; mx = 8 * s;                      /* label strip height; left margin width (two digits) */
    bandH = (sh - lab - 4 * s - 8 * s) / 4;                /* top band height (about a quarter) */
    v0 = lab + bandH + 4 * s + lab / 2;                    /* vertical-sweep band start */
    bot = sh - bandH / 2; v1 = bot - 2 * s;                /* vertical-sweep band end; bottom band start */
    /* top band: horizontal sweep */
    for (x = 0; x < sw; ) { int w; p = period_at(x, sw); w = p / 2; if (w < 1) w = 1; ragegl_fill(c, x, lab, w, bandH, BLK); x += p; }
    printf("TOP band (horizontal filter), numbers above it = bar period at that spot; position in the source:\n");
    for (k = 0; k <= 7; k++) { x = k * (sw - 8) / 7; p = period_at(x, sw); { int dx = x; if (dx < 0) dx = 0; digits(dx, 0, p, s); } printf("  label %2d at source x=%3d (screen x~%4.0f)\n", p, x, x * 1024.0 / sw); ragegl_fill(c, x, lab - s, s, s, BLK); }
    /* middle band: vertical sweep, bars span the full width right of the margin */
    for (y = v0; y < v1; ) { int h; p = period_at(y - v0, v1 - v0); h = p / 2; if (h < 1) h = 1; ragegl_fill(c, mx, y, sw - mx, h, BLK); y += p; }
    printf("MIDDLE band (vertical filter), numbers in the left margin = bar period at that row:\n");
    for (k = 0; k <= 6; k++) { y = v0 + k * (v1 - v0 - 6 * s) / 6; p = period_at(y - v0, v1 - v0); digits(0, y, p, s); printf("  label %2d at source y=%3d (screen y~%4.0f)\n", p, y, y * 768.0 / sh); }
    /* bottom band: hard edge (left half) and 1-pixel stripes (right half) */
    ragegl_fill(c, mx, bot, (sw - mx) / 4, sh - bot, BLK);
    for (x = mx + (sw - mx) / 2; x < sw; x += 2) ragegl_fill(c, x, bot, 1, sh - bot, BLK);
    { uint32_t d; ragegl_fence(c, 1, &d); }
    printf("BOTTOM band: hard black|white edge at source x=%d; 1-pixel stripes from x=%d\n", mx + (sw - mx) / 4, mx + (sw - mx) / 2);
    signal(SIGINT, off); signal(SIGTERM, off);
    kr = ragegl_overlay_on(c, o, pitch_px, (uint32_t)sw, (uint32_t)sh, 0u, 0u, 1024u, 768u, bpp | (flags << 8));
    if (kr) { printf("OVL_ON failed 0x%x\n", (unsigned)kr); ragegl_free(c, o); return 2; }
    printf("overlay ENABLED (mode flags %u): %dx%d -> 1024x768, %s\n", flags, sw, sh, hold ? "timed hold" : "holding until SIGTERM");
    if (hold) sleep(hold); else while (!stop_req) sleep(1);
    kr = ragegl_overlay_off(c);
    printf("overlay disabled (0x%x)\n", (unsigned)kr);
    ragegl_free(c, o); ragegl_close(c);
    return 0;
}

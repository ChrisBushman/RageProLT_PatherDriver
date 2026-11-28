/* tools/ragegl/ovl_yuv.c - planar YUV 4:2:0 overlay test (docs/re-notes/overlay-yuv-plan.md): a 704x480 YCbCr (BT.601 limited range) test picture in three VRAM planes, shown stretched over the scanout by the
 * hardware scaler with RAGE_GL_OVL_ON_YUV, then flipped to a second set of planes (bar order reversed) with RAGE_GL_OVL_FLIP_YUV.
 * usage: ovl_yuv [hold seconds per phase, default 20] [swap: 1 = give U and V the other way round].  Always disables the overlay on exit. */
#include "ragegl.h"
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <signal.h>
static ragegl_t c;
static void off(int sig) { (void)sig; IOConnectMethodScalarIScalarO(c, RAGE_GL_OVL_OFF, 0, 0); _exit(1); }
#define W 704
#define H 480
#define PY 768
#define PC 384
static const unsigned char bar[8][3] = { {235,128,128}, {210,16,146}, {170,166,16}, {145,54,34}, {106,202,222}, {81,90,240}, {41,240,110}, {16,128,128} };   /* Y, Cb, Cr */
static void picture(unsigned char *y, unsigned char *u, unsigned char *v, int reverse)
{
    int x, r;
    memset(y, 16, PY * (H + 3)); memset(u, 128, PC * (H / 2 + 3)); memset(v, 128, PC * (H / 2 + 3));
    for (r = 0; r < H; r++) for (x = 0; x < W; x++) {
        int b = (x * 8) / W; const unsigned char *k; unsigned char Y;
        if (reverse) b = 7 - b;
        k = bar[b]; Y = k[0];
        if (r >= 360) {                                    /* bottom quarter: grey ramp left half, diagonal + 8 px grid right half */
            if (x < W / 2) { Y = (unsigned char)(16 + ((x * 219) / (W / 2))); }
            else { int dx = x - W / 2; Y = (dx - (r - 360) * 2 == 0 || dx - (r - 360) * 2 == 1 || (dx % 32) == 0 || ((r - 360) % 32) == 0) ? 235 : 16; }
        }
        y[r * PY + x] = Y;
        if (r < 360 && !(r & 1) && !(x & 1)) { u[(r / 2) * PC + x / 2] = k[1]; v[(r / 2) * PC + x / 2] = k[2]; }
    }
}
int main(int argc, char **argv)
{
    uint32_t gen, dw, dh, pitch, bpp, base, hold = argc > 1 ? (uint32_t)atoi(argv[1]) : 20, swap = argc > 2 ? (uint32_t)atoi(argv[2]) : 0, s, o[2][3]; kern_return_t kr; RageOvlYuv a;
    static unsigned char y[PY * (H + 3)], u[PC * (H / 2 + 3)], v[PC * (H / 2 + 3)];
    setvbuf(stdout, NULL, _IONBF, 0);
    if (ragegl_open(&c)) { printf("cannot open the GL client\n"); return 99; }
    ragegl_display(c, &gen, &dw, &dh, &pitch, &bpp, &base);
    printf("display %ux%u, %u bytes/pixel\n", dw, dh, bpp);
    for (s = 0; s < 2; s++) {
        if (ragegl_alloc(c, PY * (H + 3), 4096, &o[s][0]) || ragegl_alloc(c, PC * (H / 2 + 3), 4096, &o[s][1]) || ragegl_alloc(c, PC * (H / 2 + 3), 4096, &o[s][2])) { printf("alloc failed\n"); return 1; }
        picture(y, u, v, (int)s);
        if (ragegl_vwrite(c, o[s][0], y, PY * (H + 3), 0) || ragegl_vwrite(c, o[s][1], u, PC * (H / 2 + 3), 0) || ragegl_vwrite(c, o[s][2], v, PC * (H / 2 + 3), 0)) { printf("upload failed\n"); return 1; }
        printf("set %u: Y 0x%x U 0x%x V 0x%x\n", s, o[s][0], o[s][1], o[s][2]);
    }
    memset(&a, 0, sizeof a);
    a.offY = o[0][0]; a.offU = o[0][swap ? 2 : 1]; a.offV = o[0][swap ? 1 : 2]; a.pitchY = PY; a.srcW = W; a.srcH = H; a.dstX = 0; a.dstY = 0; a.dstW = (uint16_t)dw; a.dstH = (uint16_t)dh;
    signal(SIGINT, off); signal(SIGTERM, off);
    kr = IOConnectMethodScalarIScalarO(c, RAGE_GL_OVL_ON_YUV, 1, 0, (uint32_t)(uintptr_t)&a);
    if (kr) { printf("OVL_ON_YUV failed 0x%x\n", (unsigned)kr); return 2; }
    printf("YUV overlay ENABLED (set 0, bars left to right: white yellow cyan green magenta red blue black; bottom: grey ramp | diagonal+grid), holding %u s\n", hold);
    sleep(hold);
    kr = IOConnectMethodScalarIScalarO(c, RAGE_GL_OVL_FLIP_YUV, 3, 0, o[1][0], o[1][swap ? 2 : 1], o[1][swap ? 1 : 2]);
    printf("flip to set 1 (bars reversed): 0x%x, holding %u s\n", (unsigned)kr, hold);
    sleep(hold);
    kr = IOConnectMethodScalarIScalarO(c, RAGE_GL_OVL_OFF, 0, 0);
    printf("overlay disabled (0x%x)\n", (unsigned)kr);
    for (s = 0; s < 2; s++) { ragegl_free(c, o[s][0]); ragegl_free(c, o[s][1]); ragegl_free(c, o[s][2]); }
    ragegl_close(c);
    return 0;
}

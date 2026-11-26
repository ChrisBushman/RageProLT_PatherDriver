/* tools/ragegl/ovl_b.c - overlay plan step B: a 640x480 15 bpp test pattern (colour bars + 32 px grid) in a private VRAM block, shown by the hardware overlay scaler stretched over a 1024x768 scanout.
 * usage: ovl_b [hold seconds, default 25].  Needs a debug kext (RAGE_GL_OVL_ON/OFF) and a 1024x768 15 bpp display (cgmode ... 1024 768 SECS force).  Always disables the overlay on exit. */
#include "ragegl.h"
#include <stdlib.h>
#include <unistd.h>
#include <signal.h>
static ragegl_t c;
static void off(int sig) { (void)sig; IOConnectMethodScalarIScalarO(c, RAGE_GL_OVL_OFF, 0, 0); _exit(1); }
int main(int argc, char **argv)
{
    static const uint32_t bar[8] = { 0xffff, 0xffe0, 0x07ff, 0x07e0, 0xf81f, 0xf800, 0x001f, 0x0000 };   /* RGB565: white yellow cyan green magenta red blue black */
    uint32_t gen, dw, dh, pitch, bpp, base, o, i, hold = argc > 1 ? (uint32_t)atoi(argv[1]) : 25; kern_return_t kr;
    if (ragegl_open(&c)) { printf("cannot open the GL client\n"); return 99; }
    ragegl_display(c, &gen, &dw, &dh, &pitch, &bpp, &base);
    printf("display %ux%u, %u bytes/pixel\n", dw, dh, bpp);
    if (dw != 1024 || dh != 768 || bpp != 2) { printf("SKIP: needs a 1024x768 15/16 bpp scanout\n"); return 77; }
    if (ragegl_alloc(c, 1024u * 2u * 480u, 4096, &o) || ragegl_target(c, o, 1024, 2, 640, 480)) { printf("alloc/target failed\n"); return 1; }
    ragegl_scissor(c, 0, 639, 0, 479);
    for (i = 0; i < 8; i++) ragegl_fill(c, (int)(i * 80), 0, 80, 480, bar[i]);
    for (i = 0; i <= 640; i += 32) { int x = i == 640 ? 639 : (int)i; ragegl_fill(c, x, 0, 1, 240, 0x0000); ragegl_fill(c, x, 240, 1, 240, 0xffff); }
    for (i = 0; i <= 480; i += 32) { int y = i == 480 ? 479 : (int)i; ragegl_fill(c, 0, y, 640, 1, 0x0000); }
    { uint32_t d; ragegl_fence(c, 1, &d); }
    printf("pattern drawn at VRAM 0x%x (640x480, pitch 1024 px)\n", o);
    signal(SIGINT, off); signal(SIGTERM, off);
    kr = RGL_CALL(RAGE_GL_OVL_ON, 5, 0, o, 1024u, 640u | (480u << 16), 0u, 1024u | (768u << 16));
    if (kr) { printf("OVL_ON failed 0x%x\n", (unsigned)kr); return 2; }
    printf("overlay ENABLED: 640x480 -> 1024x768, holding %u s\n", hold);
    sleep(hold);
    kr = IOConnectMethodScalarIScalarO(c, RAGE_GL_OVL_OFF, 0, 0);
    printf("overlay disabled (0x%x)\n", (unsigned)kr);
    ragegl_free(c, o);
    ragegl_close(c);
    return 0;
}

/* sleep_probe.c: v-sync experiment, IOSleep(1) granularity probe (RAGE_GL_CONFIG key 9 = 2 makes the next present time 20 x IOSleep(1) in the kernel and publish the result). Needs a render target. */
#include "ragegl.h"
int main(void)
{
    ragegl_t c; uint32_t gen, w, h, pitch, bpp, base, off;
    if (ragegl_open(&c) || ragegl_display(c, &gen, &w, &h, &pitch, &bpp, &base) || ragegl_alloc(c, pitch * h * bpp, 4096, &off) || ragegl_target(c, off, pitch, bpp, w, h)) { printf("setup failed\n"); return 2; }
    { kern_return_t a = ragegl_config(c, 9, 2), b = ragegl_present(c, 0, 0, 16, 16, 0, 0); printf("config %x present %x\n", a, b); } ragegl_free(c, off); ragegl_close(c); return 0;
}

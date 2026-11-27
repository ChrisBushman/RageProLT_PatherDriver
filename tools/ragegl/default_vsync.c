/* tools/ragegl/default_vsync.c - check the kext's DEFAULT v-sync mode: present N times with no RAGE_GL_CONFIG call; ~60 fps means the default waits for vblank (sleep-then-spin or poll), several hundred fps means off. */
#include "ragegl.h"
#include <stdlib.h>
#include <sys/time.h>
static double now(void) { struct timeval t; gettimeofday(&t, 0); return t.tv_sec + t.tv_usec * 1e-6; }
int main(int argc, char **argv)
{
    ragegl_t c; uint32_t gen, w, h, pitch, bpp, base, off; int n = argc > 1 ? atoi(argv[1]) : 120, i; double t0;
    if (ragegl_open(&c) || ragegl_display(c, &gen, &w, &h, &pitch, &bpp, &base)) return 2;
    if (ragegl_alloc(c, pitch * h * bpp, 4096, &off) || ragegl_target(c, off, pitch, bpp, w, h)) return 2;
    ragegl_fill(c, 0, 0, w, h, 0x00203080u); ragegl_present(c, 0, 0, w, h, 0, 0);
    t0 = now(); for (i = 0; i < n; i++) ragegl_present(c, 0, 0, w, h, 0, 0);
    printf("default v-sync: %d presents = %.1f fps\n", n, n / (now() - t0));
    ragegl_free(c, off); ragegl_close(c); return 0;
}

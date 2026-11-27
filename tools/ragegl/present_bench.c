/* tools/ragegl/present_bench.c - v-sync experiment: present a full-screen render target to the scanout N times with the kext's vblank poll off / on and report frames per second and CPU
 * time per frame (getrusage: the poll runs in the kernel, so it shows as system time).  The screen shows the test pattern while it runs (repaint afterwards).  usage: present_bench [frames] */
#include "ragegl.h"
#include <stdlib.h>
#include <sys/time.h>
#include <sys/resource.h>
static double now(void) { struct timeval t; gettimeofday(&t, 0); return t.tv_sec + t.tv_usec * 1e-6; }
static double cpu(void) { struct rusage r; getrusage(RUSAGE_SELF, &r); return r.ru_utime.tv_sec + r.ru_stime.tv_sec + (r.ru_utime.tv_usec + r.ru_stime.tv_usec) * 1e-6; }
int main(int argc, char **argv)
{
    ragegl_t c; uint32_t gen, w, h, pitch, bpp, base, off, i, mode; int frames = argc > 1 ? atoi(argv[1]) : 300, margins[4] = { 2500, 3500, 4500, 6000 };
    if (ragegl_open(&c)) { printf("no GL client\n"); return 2; }
    if (ragegl_display(c, &gen, &w, &h, &pitch, &bpp, &base)) { printf("no display info\n"); return 2; }
    printf("display %ux%u, %u bytes/pixel, pitch %u px\n", w, h, bpp, pitch);
    if (ragegl_alloc(c, pitch * h * bpp, 4096, &off)) { printf("alloc failed\n"); return 2; }
    if (ragegl_target(c, off, pitch, bpp, w, h)) { printf("target failed\n"); return 2; }
    ragegl_fill(c, 0, 0, w, h, 0x00203080u);
    for (mode = 0; mode < 6; mode++) {
        if (argc > 2 && (uint32_t)atoi(argv[2]) != mode) continue;     /* optional: run one mode only */
        static const uint32_t vsm[6] = { 0, 1, 3, 3, 3, 3 }; static const char *vsn[4] = { "off", "poll", "-", "sleep+spin" };
        double t0, t1, c0, c1; uint32_t vs = vsm[mode]; int margin = mode >= 2 ? margins[mode - 2] : 0;
        ragegl_config(c, 9, vs); if (margin) ragegl_config(c, 10, margin); ragegl_present(c, 0, 0, w, h, 0, 0);
        t0 = now(); c0 = cpu();
        for (i = 0; i < (uint32_t)frames; i++) { if (ragegl_present(c, 0, 0, w, h, 0, 0)) { printf("present failed\n"); break; } }
        t1 = now(); c1 = cpu();
        printf("vsync %-10s margin %4d us: %d presents in %.3f s = %6.1f fps, CPU %.2f ms/frame (%.0f%% of one CPU)\n", vsn[vs], margin, frames, t1 - t0, frames / (t1 - t0), (c1 - c0) * 1000.0 / frames, 100.0 * (c1 - c0) / (t1 - t0)); fflush(stdout);
    }
    ragegl_config(c, 9, 0); ragegl_free(c, off); ragegl_close(c); return 0;
}

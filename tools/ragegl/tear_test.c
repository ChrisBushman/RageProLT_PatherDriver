/* tools/ragegl/tear_test.c - visual tearing check for the v-sync switch.  Draws moving vertical white bars into an off-screen VRAM target (position set by wall-clock time, so the motion is the same
 * whatever the frame rate) and presents the target to the scanout with the chosen v-sync mode.  A tear shows as bars broken into horizontally offset pieces.
 * usage: tear_test off|poll|sleep [seconds]   (screen shows the pattern while it runs: repaint afterwards) */
#include "ragegl.h"
#include <stdlib.h>
#include <string.h>
#include <sys/time.h>
static double now(void) { struct timeval t; gettimeofday(&t, 0); return t.tv_sec + t.tv_usec * 1e-6; }
int main(int argc, char **argv)
{
    ragegl_t c; uint32_t gen, w, h, pitch, bpp, base, off, vs = 0; double secs = argc > 2 ? atof(argv[2]) : 12, t0, t; int frames = 0, k;
    if (argc < 2) return 1;
    if (!strcmp(argv[1], "poll")) vs = 1; else if (!strcmp(argv[1], "sleep")) vs = 3;
    if (ragegl_open(&c)) { printf("no GL client\n"); return 2; }
    if (ragegl_display(c, &gen, &w, &h, &pitch, &bpp, &base)) { printf("no display info\n"); return 2; }
    if (ragegl_alloc(c, pitch * h * bpp, 4096, &off)) { printf("alloc failed\n"); return 2; }
    if (ragegl_target(c, off, pitch, bpp, w, h)) { printf("target failed\n"); return 2; }
    ragegl_config(c, 9, vs); if (vs == 3) ragegl_config(c, 10, 2500);
    t0 = now();
    while ((t = now() - t0) < secs) {
        int x0 = (int)(t * 1800.0) % (int)w;                                   /* 1800 px/s = 30 px per frame at 60 Hz */
        ragegl_fill(c, 0, 0, w, h, 0x00102060u);
        for (k = 0; k < 4; k++) { int x = (x0 + k * (int)w / 4) % (int)w; int bw = 40; if (x + bw > (int)w) bw = (int)w - x; ragegl_fill(c, x, 0, bw, h, 0x00ffffffu); }
        if (ragegl_present(c, 0, 0, w, h, 0, 0)) { printf("present failed\n"); break; }
        frames++;
    }
    printf("%s: %d frames in %.1f s = %.1f fps\n", argv[1], frames, now() - t0, frames / (now() - t0));
    ragegl_config(c, 9, 0); ragegl_free(c, off); ragegl_close(c); return 0;
}

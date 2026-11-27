/* tools/ragegl/aperture_bench.c - B1g-4: CPU store/load speed of the VRAM aperture under each cache mode (DEBUG kext).  Uses a 256 KB block of the caller's own VRAM; the screen is not touched. */
#include "ragegl.h"
int main(void)
{
    static const char *nm[5] = { "inhibit (what the kext uses)", "write-combine", "write-through", "copyback", "default" };
    ragegl_t c; uint32_t off, len = 0x40000, mode, w, r;
    if (ragegl_open(&c)) { printf("no GL client\n"); return 2; }
    if (ragegl_alloc(c, len, 4096, &off)) { printf("alloc failed\n"); return 2; }
    for (mode = 0; mode < 5; mode++) {
        kern_return_t kr = RGL_CALL(RAGE_GL_APERTURE_BENCH, 3, 2, off, len, mode, &w, &r);
        if (kr) { printf("%-30s error 0x%x\n", nm[mode], kr); continue; }
        printf("%-30s store %7.1f MB/s   load %7.1f MB/s\n", nm[mode], len / (w * 1e-9) / 1048576.0, len / (r * 1e-9) / 1048576.0); fflush(stdout);
    }
    ragegl_free(c, off); ragegl_close(c);
    return 0;
}

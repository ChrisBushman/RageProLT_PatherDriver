/* tools/ragegl/aperture_user.c - B1g-5: CPU store/load speed of a USER-SPACE mapping of VRAM under different cache options (DEBUG kext, GL client memory type 200 = the block of the last
 * aperture benchmark).  Settles whether a user task's kIOMapWriteCombineCache mapping is really faster than the default one (what WindowServer gets). */
#include "ragegl.h"
#include <sys/time.h>
#include <string.h>
static double now(void) { struct timeval t; gettimeofday(&t, 0); return t.tv_sec + t.tv_usec * 1e-6; }
int main(void)
{
    static const struct { const char *n; unsigned o; } m[] = { { "default (0)", 0 }, { "inhibit (0x100)", 0x100 }, { "write-through (0x200)", 0x200 }, { "copyback (0x300)", 0x300 }, { "write-combine (0x400)", 0x400 } };
    ragegl_t c; uint32_t off, len = 0x40000, w, r, k, p, i; int mi;
    if (ragegl_open(&c)) { printf("no GL client\n"); return 2; }
    if (ragegl_alloc(c, len, 4096, &off)) { printf("alloc failed\n"); return 2; }
    RGL_CALL(RAGE_GL_APERTURE_BENCH, 3, 2, off, len, 0, &w, &r);          /* records the block for map type 200 */
    for (mi = 0; mi < 5; mi++) {
        vm_address_t a = 0; vm_size_t sz = 0; volatile uint32_t *v; double t0, t1, t2; uint32_t sink = 0;
        kern_return_t kr = IOConnectMapMemory(c, 200, mach_task_self(), &a, &sz, kIOMapAnywhere | m[mi].o);
        if (kr || !a) { printf("%-24s map failed 0x%x\n", m[mi].n, kr); fflush(stdout); continue; }
        v = (volatile uint32_t *)a;
        t0 = now(); for (p = 0; p < 8; p++) { for (i = 0; i < len / 4; i++) v[i] = i ^ p; __asm__ volatile("sync" ::: "memory"); } t1 = now();
        for (p = 0; p < 8; p++) for (i = 0; i < len / 4; i++) sink += v[i]; t2 = now();
        printf("%-24s store %7.1f MB/s   load %7.1f MB/s%s\n", m[mi].n, 8.0 * len / (t1 - t0) / 1048576.0, 8.0 * len / (t2 - t1) / 1048576.0, sink == 1 ? " " : ""); fflush(stdout);
        { static uint32_t src[0x10000]; double m0, m1; for (i = 0; i < 0x10000; i++) src[i] = i * 2654435761u;
          m0 = now(); for (p = 0; p < 8; p++) { memcpy((void *)a, src, len); __asm__ volatile("sync" ::: "memory"); } m1 = now();
          printf("%-24s memcpy %6.1f MB/s (libc, wide stores)\n", "", 8.0 * len / (m1 - m0) / 1048576.0); fflush(stdout); }
        (void)k; IOConnectUnmapMemory(c, 200, mach_task_self(), a);
    }
    ragegl_free(c, off); ragegl_close(c); return 0;
}

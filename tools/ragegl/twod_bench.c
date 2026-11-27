/* tools/ragegl/twod_bench.c - B1g-2 / A5: speed of the type-2 2D path against the CPU path, and GL client open alongside.
 * Hardware: plugin-style streams through the type-2 client (fills and copies, one op per buffer = what the plugin does, and 32 ops per buffer), each followed by finish(),
 * so every figure includes the kernel's validation, per-op engine programming and idle wait.
 * CPU proxy: the same byte counts through the GL client's VRAM read/write (a kernel CPU copy through the aperture) on an allocated scratch area, since the real CPU path
 * (Quartz writing the mapped framebuffer) is the same aperture access; it never touches the screen.
 * Draws on the Rage scanout (fills/copies in the area x 0..W-1, y 0..H-1): run on an idle display. */
#include "ragegl.h"
#include <stdlib.h>
#include <sys/time.h>

static ragegl_t gl; static io_connect_t c2;
static uint32_t gen, W, H, PITCH, BPP, BASE;
static uint32_t *buf; static uint32_t nw, hdr_at;
static double now(void) { struct timeval t; gettimeofday(&t, 0); return t.tv_sec + t.tv_usec * 1e-6; }
static int map_buffer(void)
{
    vm_address_t a = 0; vm_size_t sz = 0;
    if (IOConnectMapMemory(c2, 0, mach_task_self(), &a, &sz, kIOMapAnywhere) != KERN_SUCCESS || !a) return -1;
    buf = (uint32_t *)a; buf[7] = 0; hdr_at = 7; nw = 8; return 0;
}
static void P(uint32_t r, uint32_t v) { buf[nw++] = r; buf[nw++] = v; }
static void endpkt(void) { buf[hdr_at] |= (nw - hdr_at); }
static uint32_t YX(int x, int y) { return ((uint32_t)x << 16) | ((uint32_t)y & 0xffffu); }
static void submit(void) { endpkt(); buf[nw++] = 0x01000000u; if (map_buffer()) exit(2); IOConnectMethodScalarIStructureI(c2, 7, 1, 0, 0, (void *)0); }
static void fill_op(int x, int y, int w, int h, uint32_t col)
{ P(0x2d8, 0x100); P(0x2d4, 0x70003); P(0x130, 3); P(0x2c4, col); P(0x10c, YX(x, y)); P(0x118, ((uint32_t)w << 16) | (uint32_t)h); }
static void copy_op(int first, int sx, int sy, int dx, int dy, int w, int h)
{
    if (first) { buf[7] = 0x0d000000u; hdr_at = 7; nw = 8; } else { endpkt(); hdr_at = nw; buf[nw++] = 0x0d000000u; }
    buf[nw++] = 0; P(0x1b4, 0); P(0x2d8, 0x300); P(0x2d4, 0x70003); P(0x130, 3); P(0x18c, YX(sx, sy)); P(0x190, (uint32_t)w); P(0x10c, YX(dx, dy)); P(0x118, ((uint32_t)w << 16) | (uint32_t)h);
}
static void report(const char *name, int w, int h, int ops, double dt)
{
    double bytes = (double)w * h * BPP * ops;
    printf("%-34s %4dx%-4d  %8.1f us/op  %7.2f MB/s\n", name, w, h, dt / ops * 1e6, bytes / dt / 1048576.0);
}
int main(void)
{
    static const int sz[][2] = { {16, 16}, {64, 64}, {128, 128}, {256, 256}, {512, 384} };
    mach_port_t master; io_service_t svc; uint32_t off, i; int s, k, it;
    if (ragegl_open(&gl)) { printf("no GL client\n"); return 2; }       /* GL client open for the whole run = the A5 coexistence case */
    if (ragegl_display(gl, &gen, &W, &H, &PITCH, &BPP, &BASE)) return 2;
    IOMasterPort(MACH_PORT_NULL, &master); svc = IOServiceGetMatchingService(master, IOServiceMatching("RageLTDevice"));
    if (IOServiceOpen(svc, mach_task_self(), 2, &c2)) { printf("no 2D client\n"); return 2; }
    if (map_buffer()) return 2;
    if (ragegl_alloc(gl, 512 * 384 * BPP * 2, 4096, &off)) { printf("alloc failed\n"); return 2; }
    printf("display %ux%u, %u bytes/pixel; GL client and 2D client open together\n", W, H, BPP);
    for (s = 0; s < 5; s++) {
        int w = sz[s][0], h = sz[s][1]; int n = (w * h <= 4096) ? 400 : (w * h <= 65536 ? 100 : 20); double t0, t1; uint8_t *tmp = (uint8_t *)malloc((size_t)w * h * BPP);
        memset(tmp, 0x5a, (size_t)w * h * BPP);
        t0 = now(); for (i = 0; i < (uint32_t)n; i++) { fill_op(10, 10, w, h, 0x00112233 + i); submit(); } t1 = now(); report("HW fill, 1 op per buffer", w, h, n, t1 - t0);
        t0 = now(); for (i = 0; i < (uint32_t)n / 32 + 1; i++) { for (k = 0; k < 32; k++) fill_op(10, 10, w, h, 0x00112233 + k); submit(); } t1 = now(); report("HW fill, 32 ops per buffer", w, h, (n / 32 + 1) * 32, t1 - t0);
        t0 = now(); for (i = 0; i < (uint32_t)n; i++) { copy_op(1, 10, 10, W - w - 10 > 0 ? (int)W - w - 10 : 0, 10, w, h); submit(); } t1 = now(); report("HW copy, 1 op per buffer", w, h, n, t1 - t0);
        t0 = now(); for (i = 0; i < (uint32_t)n / 32 + 1; i++) { for (k = 0; k < 32; k++) copy_op(k == 0, 10, 10, W - w - 10 > 0 ? (int)W - w - 10 : 0, 10, w, h); submit(); } t1 = now(); report("HW copy, 32 ops per buffer", w, h, (n / 32 + 1) * 32, t1 - t0);
        { int m = n < 20 ? n : 20;
          t0 = now(); for (it = 0; it < m; it++) ragegl_vwrite(gl, off, tmp, (uint32_t)w * h * BPP, 0); t1 = now(); report("CPU aperture write (= fill)", w, h, m, t1 - t0);
          t0 = now(); for (it = 0; it < m; it++) { ragegl_vread(gl, off, tmp, (uint32_t)w * h * BPP, 0); ragegl_vwrite(gl, off + 512 * 384 * BPP, tmp, (uint32_t)w * h * BPP, 0); } t1 = now(); report("CPU aperture read+write (= copy)", w, h, m, t1 - t0); }
        free(tmp); printf("\n");
    }
    ragegl_free(gl, off); IOServiceClose(c2); ragegl_close(gl);
    return 0;
}

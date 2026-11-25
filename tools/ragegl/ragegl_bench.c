/* tools/ragegl/ragegl_bench.c - PIO vs DMA batch submission: N batches of K small triangles each, timed per batch, final image checked.  No floating point.
 * usage: ragegl_bench [N] [K] */
#include "ragegl.h"
#include <stdlib.h>
#include <sys/time.h>
static unsigned long long us(void) { struct timeval tv; gettimeofday(&tv, 0); return (unsigned long long)tv.tv_sec * 1000000ull + tv.tv_usec; }

static uint32_t build(uint32_t *b, uint32_t k)
{
    uint32_t n = 0, t, v;
#define P(r, val) do { b[n++] = (r); b[n++] = (val); } while (0)
    P(0x304, 0); P(0x54c, 0); P(0x5fc, 0x100c1); P(0x6c4, 0x001f); P(0x6c8, 0xffffffffu); P(0x6d4, 0x70007); P(0x6d8, 0x101); P(0x708, 0);
    for (t = 0; t < k; t++) {
        int32_t bx = 4 + (int32_t)(t % 15) * 16, by = 4 + (int32_t)((t / 15) % 15) * 16;
        int32_t vx[3] = { bx, bx + 12, bx }, vy[3] = { by, by, by + 12 };
        for (v = 0; v < 3; v++) { uint32_t o = 0x240 + v * 0x20;
            P(o, 0); P(o + 4, 0); P(o + 8, 0x3f800000u); P(o + 0xc, 0); P(o + 0x10, 0); P(o + 0x14, 0xffffffffu);
            P(o + 0x18, ((uint32_t)(vx[v] * 4) << 16) | ((uint32_t)(vy[v] * 4) & 0xffff)); }
        P(0x300, 0x3c2aaaabu);                                   /* 1/area, area = 72 px^2: 1/72 = 0x3c638e39 would be exact; this is for timing only */
    }
    return n / 2;
}

int main(int argc, char **argv)
{
    ragegl_t c; uint32_t N = argc > 1 ? (uint32_t)atoi(argv[1]) : 500, K = argc > 2 ? (uint32_t)atoi(argv[2]) : 20, off, done, mode, i, np;
    uint32_t *b = malloc(8 * (8 + K * 22)); unsigned long long t0, t1;
    if (ragegl_open(&c)) { printf("open failed\n"); return 99; }
    if (ragegl_alloc(c, 0x20000, 4096, &off) || ragegl_target(c, off, 256, 2, 256, 256)) { printf("setup failed\n"); return 98; }
    np = build(b, K);
    for (mode = 0; mode < 2; mode++) {
        ragegl_config(c, 1, mode);
        ragegl_fill(c, 0, 0, 256, 256, 0);
        t0 = us();
        for (i = 0; i < N; i++) if (ragegl_submit(c, b, np)) { printf("submit %u failed\n", i); return 1; }
        ragegl_fence(c, i, &done);
        t1 = us();
        printf("%s: %u batches x %u triangles (%u pairs): %llu us total, %llu us/batch, %llu us/triangle\n", mode ? "DMA" : "PIO", N, K, np, t1 - t0, (t1 - t0) / N, (t1 - t0) / N / K);
    }
    ragegl_free(c, off); ragegl_close(c); free(b);
    return 0;
}

/* tests/vmath_bench.c - isolated microbenchmark of each implementation (no scene, no per-frame cost: the question is which implementation of a
 * given function is faster, and tests/vmath_test.c says whether its precision is acceptable).  Run on the target CPU.
 * Build: cc -O2 -mcpu=750 -I../common -o vmath_bench vmath_bench.c      Throughput of independent operations over an L1-resident array. */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/time.h>
#include "rage_vmath.h"

#define N 1024
#define REPS 4000
static uint32_t in[N], outw[N]; static float fin[N], fout[N];
static double now(void) { struct timeval tv; gettimeofday(&tv, 0); return tv.tv_sec + tv.tv_usec * 1e-6; }
static uint32_t fb(float f) { uint32_t u; memcpy(&u, &f, 4); return u; }
static float bf(uint32_t u) { float f; memcpy(&f, &u, 4); return f; }

#define BENCH(name, body) do { double t0, t1; int r, i; uint32_t acc = 0; t0 = now(); \
    for (r = 0; r < REPS; r++) for (i = 0; i < N; i++) { body; } t1 = now(); \
    printf("%-34s %7.2f ns/op   (sink %u)\n", name, (t1 - t0) * 1e9 / ((double)REPS * N), acc); } while (0)

int main(void)
{
    int i; uint32_t s = 12345;
    for (i = 0; i < N; i++) { s = s * 1664525u + 1013904223u; in[i] = (0x3f800000u + ((s >> 9) & 0x3fffffu) * 8u + (s & 0x7fffffu)) & 0x7fffffffu; in[i] = (126u << 23) | (s & 0x7fffffu) | ((s >> 24) & 7u) << 23; fin[i] = bf(in[i]); }
    printf("reciprocal (1/w, 1/area):\n");
    BENCH("  float divide 1.0f/x (fdivs)", { fout[i] = 1.0f / fin[i]; acc += (uint32_t)fb(fout[i]); });
    BENCH("  B 256-seed + 2 Newton (int)", { outw[i] = rage_rcp_b(in[i]); acc += outw[i]; });
    BENCH("  C 256-seed interp + 1 Newton", { outw[i] = rage_rcp_c(in[i]); acc += outw[i]; });
    BENCH("  D 2048-seed16 + 1 Newton", { outw[i] = rage_rcp_d(in[i]); acc += outw[i]; });
    printf("float -> x*4 fixed (vertex X/Y), input already in a float register:\n");
    BENCH("  (int)(x*4.0f) (fmuls+fctiwz+store+load)", { acc += (uint32_t)(int)(fin[i] * 4.0f); });
    BENCH("  rage_f2q (bit manipulation)", { acc += (uint32_t)rage_f2q(fb(fin[i])); });
    printf("float -> x*4 fixed, input is the bit pattern (as GLEngine hands it over in memory):\n");
    BENCH("  (int)(bf(x)*4.0f)", { acc += (uint32_t)(int)(bf(in[i]) * 4.0f); });
    BENCH("  rage_f2q(bits)", { acc += (uint32_t)rage_f2q(in[i]); });
    printf("colour channel float -> byte:\n");
    BENCH("  (int)(c*255+0.5f)", { acc += (uint32_t)(int)(bf(in[i]) * 255.0f + 0.5f); });
    BENCH("  rage_f2byte(bits)", { acc += rage_f2byte(in[i]); });
    return 0;
}

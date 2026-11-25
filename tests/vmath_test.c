/* tests/vmath_test.c - exhaustive error bounds of common/rage_vmath.h against IEEE single arithmetic.
 *   reciprocal variants B/C/D: every one of the 2^23 mantissas (the exponent only shifts the result), error in ulps vs correctly rounded 1/x
 *   rage_f2q: all 2^32 bit patterns vs (int)(x*4.0f)       rage_f2byte: every float in [0,1] vs (int)(c*255+0.5f)
 * Prints the bounds; exits non-zero if one is exceeded. */
#include <stdio.h>
#include <string.h>
#include <math.h>
#include "rage_vmath.h"

static uint32_t fb(float f) { uint32_t u; memcpy(&u, &f, 4); return u; }
static float bf(uint32_t u) { float f; memcpy(&f, &u, 4); return f; }

int main(int argc, char **argv)
{
    uint32_t mant, e = 130; int bad = 0;
    uint32_t maxe[3] = { 0, 0, 0 }; uint64_t exact[3] = { 0, 0, 0 }, n1[3] = { 0, 0, 0 };
    static uint32_t (*const fn[3])(uint32_t) = { rage_rcp_b, rage_rcp_c, rage_rcp_d };
    static const char *nm[3] = { "B 256-seed + 2 Newton", "C 256-seed interp + 1 Newton", "D 2048-seed16 + 1 Newton" };
    int v;
    for (mant = 0; mant < 0x800000u; mant++) {
        uint32_t xb = (e << 23) | mant, ref = fb((float)(1.0 / (double)bf(xb)));
        for (v = 0; v < 3; v++) {
            uint32_t got = fn[v](xb); uint32_t d = got > ref ? got - ref : ref - got;
            if (d == 0) exact[v]++; if (d <= 1) n1[v]++; if (d > maxe[v]) maxe[v] = d;
        }
    }
    for (v = 0; v < 3; v++) printf("rcp %-30s max error %u ulp, exact %.4f%%, within 1 ulp %.4f%%\n", nm[v], maxe[v], 100.0 * exact[v] / 8388608.0, 100.0 * n1[v] / 8388608.0);
    /* exponents and signs: the same mantissa pattern at other exponents / negative must give the matching result */
    for (e = 1; e < 255; e += 7) for (mant = 0; mant < 0x800000u; mant += 4093) for (v = 0; v < 3; v++) {
        uint32_t xb = (e << 23) | mant, ref = fb((float)(1.0 / (double)bf(xb))), got = fn[v](xb), nref = fb((float)(1.0 / (double)bf(xb | 0x80000000u))), ngot = fn[v](xb | 0x80000000u);
        uint32_t d = got > ref ? got - ref : ref - got;
        if (((ref >> 23) & 255) == 0 || ((ref >> 23) & 255) == 255) continue;            /* result not a normal float */
        if (d > maxe[v] + 0) { bad++; printf("exponent test mismatch v%d e=%u mant=%x got %x ref %x\n", v, e, mant, got, ref); }
        if (ngot != (got | 0x80000000u) || nref != (ref | 0x80000000u)) { bad++; printf("sign mismatch v%d\n", v); }
    }
    {   uint64_t x; uint32_t maxd = 0, nbad = 0;
        for (x = 0; x < 0x100000000ull; x++) {
            uint32_t xb = (uint32_t)x; float f = bf(xb); int32_t ref, got;
            if (((xb >> 23) & 255) == 255) continue;
            if (fabsf(f) >= 536870912.0f) { ref = f > 0 ? 0x7fffffff : -0x7fffffff - 1; }       /* x*4 >= 2^31 saturates */
            else ref = (int32_t)(f * 4.0f);
            got = rage_f2q(xb);
            if (ref == -0x7fffffff - 1) ref = 0x80000001;
            if (got != ref) { if (nbad < 5) printf("f2q mismatch %08x got %d ref %d\n", xb, got, ref); nbad++; }
        }
        printf("f2q: all 2^32 patterns, %u mismatches\n", nbad); bad += nbad != 0; (void)maxd;
        if (argc > 1) printf("(argument ignored)\n");
    }
    {   uint32_t xb, d1 = 0, n = 0, maxdiff = 0;
        for (xb = 0; xb <= 0x3f800000u; xb++) {
            float c = bf(xb); int r = (int)(c * 255.0f + 0.5f); uint32_t g = rage_f2byte(xb); uint32_t d = g > (uint32_t)r ? g - r : r - g;
            if (r > 255) r = 255;
            if (d) { n++; if (d > maxdiff) maxdiff = d; }
            (void)d1;
        }
        printf("f2byte: %u of %u floats in [0,1] differ from the float formula, max difference %u (exact evaluation vs float rounding of c*255)\n", n, 0x3f800001u, maxdiff);
        if (maxdiff > 1) bad++;
    }
    for (v = 0; v < 3; v++) if (maxe[v] > 4) bad++;
    printf(bad ? "vmath_test: FAILED\n" : "vmath_test: ok\n");
    return bad != 0;
}

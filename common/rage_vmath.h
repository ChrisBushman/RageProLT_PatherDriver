/* common/rage_vmath.h - the numeric core of vertex/triangle setup, in integer arithmetic on IEEE-single BIT PATTERNS (no float type appears).
 * Portable and header-only (host tests, the GL bundle, PPC).  Design 2.1 / 11.15: every routine has a stated error bound, proven exhaustively in
 * tests/vmath_test.c; which variant the bundle uses was chosen by isolated microbenchmark + that error bound (tests/vmath_bench.c).
 *
 *   rage_rcp_*   bits of 1/x   (W = 1/w per vertex, ONE_OVER_AREA = 1/area)       zero/denormal -> +/-inf, inf/nan -> +/-0
 *   rage_f2q     (int)(x*4)    vertex X/Y in 2 sub-pixel bits, truncating like (int)(x*4.0f); saturates
 *   rage_f2byte  (int)(c*255+0.5), clamped to 0..255     colour channels */
#ifndef RAGE_VMATH_H
#define RAGE_VMATH_H

#include <stdint.h>
#include "rage_rcp_tables.h"

/* one Newton step for r ~ 1/m: m = M/2^31 (M in [2^31,2^32)), r in Q1.31 */
static inline uint32_t rage_newton(uint32_t M, uint32_t r)
{
    uint32_t t; uint64_t p = ((uint64_t)M * r) >> 31;                 /* m*r, ~1.0 */
    t = (uint32_t)((1ull << 32) - p);                                  /* 2 - m*r */
    p = ((uint64_t)r * t) >> 31;
    return p > 0x80000000ull ? 0x80000000u : (uint32_t)p;
}

/* r (Q1.31, in (0.5,1]) for mantissa m with unbiased-exponent word e (biased) and sign -> float bits */
static inline uint32_t rage_rcp_pack(uint32_t sign, uint32_t e, uint32_t r)
{
    uint32_t mant, ex;
    if (r >= 0x80000000u) return sign | ((254u - e) << 23);            /* exactly 1/2^k */
    mant = (((r << 1) - 0x80000000u) + 0x80u) >> 8;                    /* round to nearest at bit 8 */
    ex = 253u - e;
    if (mant >> 23) { mant = 0; ex++; }
    return sign | (ex << 23) | mant;
}
#define RAGE_RCP_SPECIAL(xb) \
    uint32_t sign_ = (xb) & 0x80000000u, e_ = ((xb) >> 23) & 255u; \
    if (e_ == 0) return sign_ | 0x7f800000u; \
    if (e_ == 255) return sign_; \
    if (e_ >= 254) return sign_;  /* 1/2^127 or smaller than the smallest normal after the reciprocal: flush to zero */

/* B: 256-entry seed + two Newton steps */
static inline uint32_t rage_rcp_b(uint32_t xb)
{
    uint32_t mant, M, r;
    RAGE_RCP_SPECIAL(xb)
    mant = xb & 0x7fffffu; M = 0x80000000u | (mant << 8);
    r = rage_rcp8[mant >> 15];
    r = rage_newton(M, r); r = rage_newton(M, r);
    return rage_rcp_pack(sign_, e_, r);
}
/* C: 256-entry seed with linear interpolation + one Newton step */
static inline uint32_t rage_rcp_c(uint32_t xb)
{
    uint32_t mant, M, r, i, f, a, b;
    RAGE_RCP_SPECIAL(xb)
    mant = xb & 0x7fffffu; M = 0x80000000u | (mant << 8);
    i = mant >> 15; f = mant & 0x7fffu; a = rage_rcp8[i]; b = rage_rcp8[i + 1];
    r = a - (uint32_t)(((uint64_t)(a - b) * f) >> 15);
    r = rage_newton(M, r);
    return rage_rcp_pack(sign_, e_, r);
}
/* D: 2048-entry 16-bit seed + one Newton step */
static inline uint32_t rage_rcp_d(uint32_t xb)
{
    uint32_t mant, M, r;
    RAGE_RCP_SPECIAL(xb)
    mant = xb & 0x7fffffu; M = 0x80000000u | (mant << 8);
    r = (uint32_t)rage_rcp11[mant >> 12] << 15;
    if (r > 0x80000000u) r = 0x80000000u;
    r = rage_newton(M, r);
    return rage_rcp_pack(sign_, e_, r);
}

/* (int)(x*4): exact (scaling by 4 is exact in float); truncates toward zero; saturates at +/-2^31-1 */
static inline int32_t rage_f2q(uint32_t xb)
{
    uint32_t e = (xb >> 23) & 255u, m; int32_t sh; uint32_t v;
    if (e < 127u - 2u + 0u) return 0;                                  /* |x*4| < 1 */
    if (e == 255u) return (xb & 0x80000000u) ? (int32_t)0x80000001 : 0x7fffffff;
    sh = (int32_t)e - 148;                                             /* x*4 = m24 * 2^sh */
    m = 0x800000u | (xb & 0x7fffffu);
    if (sh >= 8) v = 0x7fffffffu;                                       /* >= 2^31 */
    else if (sh >= 0) v = m << sh;
    else v = m >> (uint32_t)(-sh);
    if (v > 0x7fffffffu) v = 0x7fffffffu;
    return (xb & 0x80000000u) ? -(int32_t)v : (int32_t)v;
}

/* (int)(c*255 + 0.5) clamped to [0,255], evaluated exactly (the float formula rounds c*255 to 24 bits first: results differ by at most 1, see the test) */
static inline uint32_t rage_f2byte(uint32_t cb)
{
    uint32_t e = (cb >> 23) & 255u, sh, p;
    if ((cb & 0x80000000u) || e == 0) return 0;
    if (e >= 127u) return e == 255u && (cb & 0x7fffffu) ? 0 : 255u;     /* c >= 1 (nan -> 0) */
    sh = 150u - e;                                                     /* c*255 = (m24*255) * 2^-sh, sh in 24..149 */
    if (sh > 32u) return 0;                                            /* below 2^-9: rounds to 0 */
    p = (0x800000u | (cb & 0x7fffffu)) * 255u;                         /* < 2^32 */
    if (sh == 32u) return 0;                                           /* p < 2^32 so c*255 < 1 -> +0.5 < 2 : only >= 0.5 rounds up; p>>31 <=1 */
    return (uint32_t)(((uint64_t)p + (1ull << (sh - 1))) >> sh);
}

#endif

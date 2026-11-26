/* common/rage_f32.h - IEEE single bit patterns from integers, with integer arithmetic only (the kext may not use the FPU).
 * Used by the kernel's textured-quad present (RageLTMode.cpp) for the vertex S/T/W values and ONE_OVER_AREA. */
#ifndef RAGE_F32_H
#define RAGE_F32_H
#include <stdint.h>

/* bits of num * 2^-shift, exactly (num < 2^24 so the value is representable); num == 0 gives +0.0 */
static inline uint32_t rage_f32_pow2(uint32_t num, int shift)
{
    int e = 31;
    if (!num) return 0;
    while (!(num >> e)) e--;                                  /* e = floor(log2 num) */
    return ((uint32_t)(127 + e - shift) << 23) | (((num << (23 - e)) & 0x7fffffu));
}

/* bits of 1.0 / n, rounded to nearest even like the FPU's divide; 1 <= n < 2^24 */
static inline uint32_t rage_f32_recip(uint32_t n)
{
    int e = 23, i; uint32_t q = 0, r = 0;
    if (!n) return 0x7f800000u;                               /* +inf */
    while (!(n >> e)) e--;                                    /* e = floor(log2 n) */
    for (i = 24 + e; i >= 0; i--) {                           /* long division of 2^(24+e) by n: 25+e steps, quotient < 2^25, remainder < 2n < 2^25 */
        r = (r << 1) | (i == 24 + e ? 1u : 0u);
        q <<= 1;
        if (r >= n) { r -= n; q |= 1u; }
    }
    if (2u * r > n || (2u * r == n && (q & 1u))) q++;         /* round to nearest even */
    if (q >> 24) return (uint32_t)(127 - e) << 23;            /* rounded up to 2^24: exactly 2^-e */
    return ((uint32_t)(126 - e) << 23) | (q & 0x7fffffu);
}
#endif

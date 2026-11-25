/* Fixed-point attribute interpolation for the CPU rasteriser (colour, perspective texture coordinates, cube-map directions).
 *
 * Numeric policy (the project owner's rule): integer maths wherever possible, no double, lookup tables with interpolation, then fixed point, single
 * precision float only if the precision loss is too great.  The ONLY float use here is the per-VERTEX conversion of GLEngine's float attributes to
 * fixed point (3 vertices per triangle); every per-pixel operation is 32/64-bit integer.  The reciprocal comes from a 256-entry table with linear
 * interpolation (relative error < 2^-17), so there is no per-pixel divide at all.
 *
 * An attribute F is a plane  F(qx, qy) = F0 + gx * (qx - x0) + gy * (qy - y0)  over quarter-pixel coordinates (the rasteriser's edge units), held as
 * int64 with 16 extra fraction bits so a 640-pixel row loses < 2^-6 of the 30-bit full scale. */
#ifndef GLD_FXMAP_H
#define GLD_FXMAP_H
#include <stdint.h>

typedef struct { int64_t base, gx, gy; } fx_plane;       /* base = value at vertex 0 << 16; gx, gy per quarter pixel, << 16 */

/* 1/m for m in [1,2) as Q31, 257 entries (the last one is only the upper interpolation point).  Built once with integer maths. */
static uint32_t fx_rcp_tab[257];
static int fx_rcp_ready;
static void fx_rcp_init(void)
{
    int i;
    if (fx_rcp_ready) return;
    for (i = 0; i <= 256; i++) fx_rcp_tab[i] = (uint32_t)((((uint64_t)1) << 39) / (uint64_t)(256 + i));
    fx_rcp_ready = 1;
}
static inline int fx_clz(uint32_t v)
{
#if defined(__ppc__) || defined(__POWERPC__)
    int r; __asm__("cntlzw %0,%1" : "=r"(r) : "r"(v)); return r;
#else
    return v ? __builtin_clz(v) : 32;
#endif
}
/* 1 / rn for rn in [2^31, 2^32) (a normalised mantissa), result Q31 in (2^30, 2^31]. */
static inline uint32_t fx_recip_norm(uint32_t rn)
{
    uint32_t idx = (rn >> 23) & 255u, fr = (rn >> 7) & 0xffffu, a = fx_rcp_tab[idx], b = fx_rcp_tab[idx + 1];
    return a - (uint32_t)(((uint64_t)(a - b) * fr) >> 16);
}

/* Power-of-two scale k so that maxabs * 2^k < 2^30 (maxabs given as float bits' exponent; 0 -> 0). */
static inline int fx_scale_for(float maxabs)
{
    uint32_t b; int e;
    __builtin_memcpy(&b, &maxabs, 4);
    e = (int)((b >> 23) & 255u) - 127;
    if (!(b & 0x7fffffffu)) return 0;
    e = 29 - e;
    return e > 100 ? 100 : (e < -100 ? -100 : e);
}
static inline int64_t fx_from_float(float f, int k)       /* f * 2^k, truncated (|result| < 2^30 by construction of k: a plain 32-bit fctiwz, no 64-bit libcall) */
{
    uint32_t b = (uint32_t)(127 + (k > 0 ? k : 0)) << 23; float sc;
    __builtin_memcpy(&sc, &b, 4);
    f *= sc;
    if (k < 0) { b = (uint32_t)(127 - k) << 23; __builtin_memcpy(&sc, &b, 4); f /= sc; }
    return (int64_t)(int32_t)f;
}

/* 1 / area as a normalised mantissa + exponent: 1/area = rc / 2^(31 + e), refined by one Newton step so it carries ~30 bits.  One per triangle; no 64-bit divide. */
typedef struct { uint32_t rc; int e; } fx_inv;
static inline void fx_inv_setup(fx_inv *iv, int64_t area)       /* area > 0, < 2^40 */
{
    int e; uint64_t rn, t; uint32_t rc;
    uint32_t hi = (uint32_t)((uint64_t)area >> 32);
    if (hi) { e = 63 - fx_clz(hi) - 31 + 31; } else e = 31 - fx_clz((uint32_t)area);    /* floor(log2 area) */
    if (e > 31) { rn = (uint64_t)area >> (e - 31); } else { rn = (uint64_t)area << (31 - e); }    /* area = rn * 2^(e-31), rn in [2^31, 2^32) */
    rc = fx_recip_norm((uint32_t)rn);
    t = (rn * (uint64_t)rc) >> 31;                              /* ~ 2^31 */
    rc = (uint32_t)(((uint64_t)rc * ((((uint64_t)1) << 32) - t)) >> 31);
    iv->rc = rc; iv->e = e;
}

/* Plane through three vertices (quarter-pixel positions x,y; fixed values f).  dx1,dy1,dx2,dy2 = vertex 1/2 minus vertex 0, area = dx1*dy2 - dx2*dy1 > 0. */
static inline void fx_plane_setup(fx_plane *p, int64_t f0, int64_t f1, int64_t f2, int64_t dx1, int64_t dy1, int64_t dx2, int64_t dy2, const fx_inv *iv)
{
    int64_t d1 = f1 - f0, d2 = f2 - f0, nx = (d1 * dy2 - d2 * dy1) >> 14, ny = (d2 * dx1 - d1 * dx2) >> 14;    /* num * 2^16 / area = ((num >> 14) * rc) >> (e + 1) */
    p->base = f0 << 16;
    p->gx = (nx * (int64_t)iv->rc) >> (iv->e + 1);
    p->gy = (ny * (int64_t)iv->rc) >> (iv->e + 1);
}
static inline int64_t fx_plane_at(const fx_plane *p, int64_t dqx, int64_t dqy) { return p->base + p->gx * dqx + p->gy * dqy; }

/* Perspective divide P / R in fixed point.  P, R are plane values (<< 16) at scales 2^kp, 2^kr; the result is (P/R) * 2^(kr - kp) ... see fx_div: the caller
 * folds the texture size and the 8 fraction bits into P, so the result is the texel position in Q8.  d = kp - kr. */
static inline int32_t fx_div(int64_t pacc, int64_t racc, int d)
{
    int32_t p = (int32_t)(pacc >> 16), r = (int32_t)(racc >> 16); int e, sh; int64_t prod; int32_t hi; uint32_t lo;
    if (r <= 0) return 0;
    e = 31 - fx_clz((uint32_t)r);
    prod = (int64_t)p * (int64_t)fx_recip_norm((uint32_t)r << (31 - e));
    sh = 31 + e + d;
    if (sh < 1) sh = 1; else if (sh > 62) sh = 62;
    hi = (int32_t)(prod >> 32); lo = (uint32_t)prod;                  /* 32-bit shifts only: a variable 64-bit shift is a library call on 32-bit PowerPC */
    return sh >= 32 ? (hi >> (sh - 32)) : (int32_t)(((uint32_t)hi << (32 - sh)) | (lo >> sh));
}

/* floor(num / den) for num >= 0, den > 0, using the reciprocal of den (iv) and an exact one-step correction: no divide. */
static inline int64_t fx_floordiv(int64_t num, int64_t den, const fx_inv *iv)
{
    int64_t q = (num * (int64_t)iv->rc) >> (iv->e + 31);
    while (q * den > num) q--;
    while ((q + 1) * den <= num) q++;
    return q;
}
/* Covered pixel range [lo, hi] of one row from the three edge equations E_i(px) = E[i] + 4 * A[i] * (px - minx) >= 0.  inv[i] = reciprocal of |4 * A[i]| (when A[i] != 0). */
static inline void fx_row_extent(const int64_t *E, const int64_t *A, const fx_inv *inv, int minx, int maxx, int64_t *plo, int64_t *phi)
{
    int64_t lo = minx, hi = maxx; int i;
    for (i = 0; i < 3; i++) {
        int64_t a4 = 4 * A[i];
        if (a4 > 0) { if (E[i] < 0) { int64_t need = minx + fx_floordiv(-E[i] + a4 - 1, a4, &inv[i]); if (need > lo) lo = need; } }
        else if (a4 < 0) { if (E[i] < 0) hi = lo - 1; else { int64_t room = minx + fx_floordiv(E[i], -a4, &inv[i]); if (room < hi) hi = room; } }
        else if (E[i] < 0) hi = lo - 1;
    }
    *plo = lo; *phi = hi;
}
#endif

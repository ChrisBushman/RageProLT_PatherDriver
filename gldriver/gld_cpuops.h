/* CPU compositing per-pixel operations (blend unit, texture environment, texel fetch, cube-map sampler).  Shared by the rasteriser (gld_stencil.c) and
 * the G4 isolation benchmark (tests/bench/opsbench.c).  All integer; the only float is the per-draw constant colour (cb_load, in gld_stencil.c). */
#ifndef GLD_CPUOPS_H
#define GLD_CPUOPS_H
#include "gld.h"
#include "gld_fxmap.h"
/* COMBINE source/operand selection resolved once per draw: sel = source index * 4 + operand kind (0 colour, 1 one-minus colour, 2 alpha, 3 one-minus alpha); source 0 texture, 1 primary / previous, 2 constant. */
typedef struct { int rsel[3], asel[3]; } env_prep;
/* Blend state: sf/df/eq as GL enums, plus the per-draw resolved forms (factor codes 0 zero, 1 one, 2 cs, 3 1-cs, 4 as, 5 1-as, 6 cd, 7 1-cd, 8.. constant-colour terms). */
typedef struct { uint32_t sf, df, eq; int cc[4]; int blend_on, tex; m64_cpuenv env; env_prep ep; int fmt555, fmt32, sfk, dfk, fastk, ccv[3][4]; int fog, fogc[3]; } cb_state;
static inline int mul255(int a, int b) { int t = a * b + 128; return (t + (t >> 8)) >> 8; }
static int bfac(uint32_t f, int ch, const int *cs, int as, const int *cd, const int *cc)
{
    switch (f) {
    case 0x0000u: return 0;
    case 0x0001u: return 255;
    case 0x0300u: return cs[ch];
    case 0x0301u: return 255 - cs[ch];
    case 0x0302u: return as;
    case 0x0303u: return 255 - as;
    case 0x0304u: return 255;                    /* destination alpha is 1 */
    case 0x0305u: return 0;
    case 0x0306u: return cd[ch];
    case 0x0307u: return 255 - cd[ch];
    case 0x0308u: return 0;                      /* min(As, 1 - Ad) with Ad = 1 */
    case 0x8001u: return cc[ch];
    case 0x8002u: return 255 - cc[ch];
    case 0x8003u: return cc[3];
    case 0x8004u: return 255 - cc[3];
    default:      return 255;
    }
}
static inline int texel_at(const m64_cpuenv *e, int x, int y)
{
    if (!(e->clamp_s | e->clamp_t)) return (int)e->img[((size_t)(y & (e->h - 1)) << e->wsh) + (size_t)(x & (e->w - 1))];     /* repeat both axes: mask and shift (power-of-two images) */
    if (e->clamp_s) { if (x < 0) x = 0; else if (x >= e->w) x = e->w - 1; } else x &= (e->w - 1);
    if (e->clamp_t) { if (y < 0) y = 0; else if (y >= e->h) y = e->h - 1; } else y &= (e->h - 1);
    return (int)e->img[(size_t)y * (size_t)e->w + (size_t)x];
}
/* Bilinear blend of four texels with 8-bit weights, two channels per 32-bit multiply (16-bit lanes: 255 * 256 fits), rounded at each stage; differs from the
 * exact per-channel sum by at most 1 LSB (mean 0.19, tests/bench/opsbench.c). */
static inline uint32_t bil_swar(uint32_t c00, uint32_t c10, uint32_t c01, uint32_t c11, int ax, int ay)
{
    const uint32_t m = 0x00ff00ffu, rd = 0x00800080u; uint32_t tr, ta, br, ba, r, g;
    tr = (((c00 & m) * (uint32_t)(256 - ax) + (c10 & m) * (uint32_t)ax + rd) >> 8) & m;
    ta = ((((c00 >> 8) & m) * (uint32_t)(256 - ax) + ((c10 >> 8) & m) * (uint32_t)ax + rd) >> 8) & m;
    br = (((c01 & m) * (uint32_t)(256 - ax) + (c11 & m) * (uint32_t)ax + rd) >> 8) & m;
    ba = ((((c01 >> 8) & m) * (uint32_t)(256 - ax) + ((c11 >> 8) & m) * (uint32_t)ax + rd) >> 8) & m;
    r = ((tr * (uint32_t)(256 - ay) + br * (uint32_t)ay + rd) >> 8) & m;
    g = ((ta * (uint32_t)(256 - ay) + ba * (uint32_t)ay + rd) >> 8) & m;
    return r | (g << 8);
}
/* Texel fetch at a position given in Q8 texels (8 fraction bits): all integer.  Nearest takes the floor; bilinear weights are the 8 fraction bits of (position - 1/2). */
static uint32_t tex_sample_q8(const m64_cpuenv *e, int32_t fu, int32_t fv)
{
    if (!e->bil) return (uint32_t)texel_at(e, fu >> 8, fv >> 8);
    {   int32_t gu = fu - 128, gv = fv - 128; int x0 = gu >> 8, y0 = gv >> 8;
        return bil_swar((uint32_t)texel_at(e, x0, y0), (uint32_t)texel_at(e, x0 + 1, y0), (uint32_t)texel_at(e, x0, y0 + 1), (uint32_t)texel_at(e, x0 + 1, y0 + 1), gu & 255, gv & 255);
    }
}
/* Cube map: s, t, r are the interpolated direction components (perspective-correct without a divide: the ratios cancel the 1/w).  The major axis picks the
 * face; the face coordinate sc/ma comes from the reciprocal table (error < 2^-17), no divide. */
static uint32_t cube_sample_fx(const m64_cpuenv *e, int32_t s, int32_t t, int32_t r)
{
    int32_t as = s < 0 ? -s : s, at = t < 0 ? -t : t, ar = r < 0 ? -r : r, ma, sc, tc; int f, ex; uint32_t rc; int64_t q; int32_t fu, fv; m64_cpuenv fe = *e;
    if (as >= at && as >= ar) { ma = as; if (s >= 0) { f = 0; sc = -r; tc = -t; } else { f = 1; sc = r; tc = -t; } }
    else if (at >= ar)        { ma = at; if (t >= 0) { f = 2; sc = s; tc = r; } else { f = 3; sc = s; tc = -r; } }
    else                      { ma = ar; if (r >= 0) { f = 4; sc = s; tc = -t; } else { f = 5; sc = -s; tc = -t; } }
    if (ma <= 0) return 0;
    ex = 31 - fx_clz((uint32_t)ma); rc = fx_recip_norm((uint32_t)ma << (31 - ex));                    /* 1 / ma = rc / 2^(31 + ex) */
    q = ((int64_t)sc * (int64_t)rc) >> (ex + 31 - 15);                                               /* sc / ma in Q15 */
    fu = (int32_t)(((q + 32768) * e->w) >> 8);
    q = ((int64_t)tc * (int64_t)rc) >> (ex + 31 - 15);
    fv = (int32_t)(((q + 32768) * e->h) >> 8);
    fe.img = e->face[f];
    return tex_sample_q8(&fe, fu, fv);
}

/* 3D texture (GL_TEXTURE_3D): nearest or trilinear lookup in a w*h*depth ARGB volume; fu/fv/fw are texel coordinates in Q8.  REPEAT wraps (mask for powers of two,
 * otherwise a modulo), CLAMP / CLAMP_TO_EDGE clamp to the outer texels. */
static inline int vol_wrap(int i, int n, int clamp)
{
    if (clamp) return i < 0 ? 0 : i >= n ? n - 1 : i;
    if ((n & (n - 1)) == 0) return i & (n - 1);
    i %= n; return i < 0 ? i + n : i;
}
static inline uint32_t vol_texel(const m64_cpuenv *e, int x, int y, int z)
{
    x = vol_wrap(x, e->w, e->clamp_s); y = vol_wrap(y, e->h, e->clamp_t); z = vol_wrap(z, e->depth, e->clamp_r);
    return e->vimg[((size_t)z * (size_t)e->h + (size_t)y) * (size_t)e->w + (size_t)x];
}
static inline uint32_t vol_lerp(uint32_t a, uint32_t b, int f)           /* per-channel a + (b - a) * f / 256, f 0..255 */
{
    uint32_t o = 0; int sh;
    for (sh = 0; sh < 32; sh += 8) { int ca = (int)((a >> sh) & 255u), cb = (int)((b >> sh) & 255u); o |= (uint32_t)(ca + (((cb - ca) * f) >> 8)) << sh; }
    return o;
}
static uint32_t vol_sample_q8(const m64_cpuenv *e, int32_t fu, int32_t fv, int32_t fw)
{
    int x0, y0, z0, fx, fy, fz; uint32_t a, b;
    if (!e->bil) return vol_texel(e, fu >> 8, fv >> 8, fw >> 8);
    fu -= 128; fv -= 128; fw -= 128; x0 = fu >> 8; y0 = fv >> 8; z0 = fw >> 8; fx = fu & 255; fy = fv & 255; fz = fw & 255;
    a = vol_lerp(vol_lerp(vol_texel(e, x0, y0, z0), vol_texel(e, x0 + 1, y0, z0), fx), vol_lerp(vol_texel(e, x0, y0 + 1, z0), vol_texel(e, x0 + 1, y0 + 1, z0), fx), fy);
    b = vol_lerp(vol_lerp(vol_texel(e, x0, y0, z0 + 1), vol_texel(e, x0 + 1, y0, z0 + 1), fx), vol_lerp(vol_texel(e, x0, y0 + 1, z0 + 1), vol_texel(e, x0 + 1, y0 + 1, z0 + 1), fx), fy);
    return vol_lerp(a, b, fz);
}
static inline float fx_abs(float f) { uint32_t b; __builtin_memcpy(&b, &f, 4); b &= 0x7fffffffu; __builtin_memcpy(&f, &b, 4); return f; }
static inline int sat255(int v) { return v < 0 ? 0 : v > 255 ? 255 : v; }
/* Two-unit CPU compositing (ARB_texture_env_crossbar): while unit 1 is evaluated, the primary colour and both units' texels are reachable by name. */
static struct { int on; uint32_t pri, t0, t1; } g_xb;
/* Mipmapped CPU textures: one level per triangle, from the ratio of texel area to pixel area (nearest level, LOD = 0.5*log2(ratio) rounded).  `st` are the three
 * vertices' true texture coordinates; `pix2` is twice the triangle's area in whole pixels.  Returns the level (0 when the texture has one). */
static int env_pick_level(const m64_cpuenv *e, const float *s, const float *t, float pix2)
{
    float a, r, thr = 2.0f; int k = 0;
    if (e->nlev < 2 || pix2 <= 0.0f) return 0;
    a = (s[1] - s[0]) * (t[2] - t[0]) - (s[2] - s[0]) * (t[1] - t[0]); if (a < 0.0f) a = -a;
    r = a * (float)e->lw[0] * (float)e->lh[0] / pix2;
    while (k < e->nlev - 1 && r >= thr) { k++; thr *= 4.0f; }
    return k;
}
static void env_set_level(m64_cpuenv *e, int k)
{
    int w;
    if (k <= 0 || k >= e->nlev) return;
    e->img = e->limg[k]; e->w = e->lw[k]; e->h = e->lh[k]; for (w = 0; (1 << (w + 1)) <= e->w; w++) {} e->wsh = w;
}
static int env_arg(const m64_cpuenv *e, uint32_t src, uint32_t op, int ch, uint32_t cf, uint32_t ct)    /* ch 0..2 = R,G,B, 3 = A */
{
    uint32_t c = (g_xb.on && src == 0x84c0u) ? g_xb.t0 : (g_xb.on && src == 0x84c1u) ? g_xb.t1 : (g_xb.on && src == 0x8577u) ? g_xb.pri : src == 0x1702u ? ct : src == 0x8576u ? (((uint32_t)e->ec[3] << 24) | ((uint32_t)e->ec[0] << 16) | ((uint32_t)e->ec[1] << 8) | (uint32_t)e->ec[2]) : cf;     /* TEXTURE / CONSTANT / PRIMARY and PREVIOUS (unit 0) */
    int v;
    if (op == 0x0302u || op == 0x0303u) v = (int)(c >> 24); else v = (int)((c >> (ch == 0 ? 16 : ch == 1 ? 8 : ch == 2 ? 0 : 24)) & 255u);
    return (op == 0x0301u || op == 0x0303u) ? 255 - v : v;
}
static int env_fn(uint32_t fn, int a0, int a1, int a2)
{
    switch (fn) {
    case 0x1e01u: return a0;
    case 0x2100u: return mul255(a0, a1);
    case 0x0104u: return a0 + a1;
    case 0x8574u: return a0 + a1 - 128;
    case 0x8575u: return mul255(a0, a2) + mul255(a1, 255 - a2);
    case 0x84e7u: return a0 - a1;
    default:      return a0;
    }
}
static uint32_t env_apply_ref(const m64_cpuenv *e, uint32_t cf, uint32_t ct)
{
    int af = (int)(cf >> 24), at = e->has_a ? (int)(ct >> 24) : 255, ch, r[3], a;
    int fr[3], tr[3], cc[3];
    for (ch = 0; ch < 3; ch++) { int sh = ch == 0 ? 16 : ch == 1 ? 8 : 0; fr[ch] = (int)((cf >> sh) & 255u); tr[ch] = (int)((ct >> sh) & 255u); cc[ch] = e->ec[ch]; }
    switch (e->mode) {
    case 0x1e01u: for (ch = 0; ch < 3; ch++) r[ch] = tr[ch]; a = e->has_a ? at : af; break;
    case 0x2100u: for (ch = 0; ch < 3; ch++) r[ch] = mul255(fr[ch], tr[ch]); a = mul255(af, at); break;
    case 0x2101u: if (!e->has_a) { for (ch = 0; ch < 3; ch++) r[ch] = tr[ch]; } else for (ch = 0; ch < 3; ch++) r[ch] = mul255(fr[ch], 255 - at) + mul255(tr[ch], at); a = af; break;
    case 0x0be2u: for (ch = 0; ch < 3; ch++) r[ch] = mul255(fr[ch], 255 - tr[ch]) + mul255(cc[ch], tr[ch]); a = mul255(af, at); break;
    case 0x0104u: for (ch = 0; ch < 3; ch++) r[ch] = fr[ch] + tr[ch]; a = mul255(af, at); break;
    default: {                                                                     /* GL_COMBINE */
        uint32_t ctt = (ct & 0x00ffffffu) | ((uint32_t)at << 24);
        if (e->rgbf == 0x86aeu || e->rgbf == 0x86afu) {
            int sum = 0; for (ch = 0; ch < 3; ch++) sum += (env_arg(e, e->src[0], e->op[0], ch, cf, ctt) - 128) * (env_arg(e, e->src[1], e->op[1], ch, cf, ctt) - 128);
            { int d = sat255((4 * sum) / 255); r[0] = r[1] = r[2] = d; if (e->rgbf == 0x86afu) a = d; else a = -1; }
        } else {
            for (ch = 0; ch < 3; ch++) r[ch] = env_fn(e->rgbf, env_arg(e, e->src[0], e->op[0], ch, cf, ctt), env_arg(e, e->src[1], e->op[1], ch, cf, ctt), env_arg(e, e->src[2], e->op[2], ch, cf, ctt));
            a = -1;
        }
        for (ch = 0; ch < 3; ch++) r[ch] = sat255(r[ch] * e->rscale);
        if (a < 0) a = env_fn(e->alf, env_arg(e, e->asrc[0], e->aop[0], 3, cf, ctt), env_arg(e, e->asrc[1], e->aop[1], 3, cf, ctt), env_arg(e, e->asrc[2], e->aop[2], 3, cf, ctt));
        a = sat255(a * e->ascale);
        return ((uint32_t)a << 24) | ((uint32_t)r[0] << 16) | ((uint32_t)r[1] << 8) | (uint32_t)r[2];
    }
    }
    return ((uint32_t)sat255(a) << 24) | ((uint32_t)sat255(r[0]) << 16) | ((uint32_t)sat255(r[1]) << 8) | (uint32_t)sat255(r[2]);
}
/* Per-draw resolution of the COMBINE selectors. */
static int env_sel(uint32_t src, uint32_t op) { int idx = src == 0x1702u ? 0 : src == 0x8576u ? 2 : 1; int kind = op == 0x0301u ? 1 : op == 0x0302u ? 2 : op == 0x0303u ? 3 : 0; return idx * 4 + kind; }
static void env_prepare(const m64_cpuenv *e, env_prep *p) { int k; for (k = 0; k < 3; k++) { p->rsel[k] = env_sel(e->src[k], e->op[k]); p->asel[k] = env_sel(e->asrc[k], e->aop[k]); } }
static inline int env_selv(const uint32_t *sc, int sel, int sh)
{ uint32_t c = sc[sel >> 2]; int k = sel & 3, v = k >= 2 ? (int)(c >> 24) : (int)((c >> sh) & 255u); return (k & 1) ? 255 - v : v; }
static inline int env_fn_i(uint32_t fn, int a0, int a1, int a2)
{
    switch (fn) {
    case 0x1e01u: return a0;
    case 0x2100u: return mul255(a0, a1);
    case 0x0104u: return a0 + a1;
    case 0x8574u: return a0 + a1 - 128;
    case 0x8575u: return mul255(a0, a2) + mul255(a1, 255 - a2);
    case 0x84e7u: return a0 - a1;
    default:      return a0;
    }
}
/* Texture environment.  MODULATE / BLEND and the non-dot3 COMBINE are straight-line (selectors resolved once per draw); the rest use the reference code. */
static uint32_t env_apply(const m64_cpuenv *e, const env_prep *ep, uint32_t cf, uint32_t ct)
{
    int af = (int)(cf >> 24), at = e->has_a ? (int)(ct >> 24) : 255, r0, r1, r2, a;
    int fr = (int)((cf >> 16) & 255u), fg = (int)((cf >> 8) & 255u), fb = (int)(cf & 255u), tr = (int)((ct >> 16) & 255u), tg = (int)((ct >> 8) & 255u), tb = (int)(ct & 255u);
    switch (e->mode) {
    case 0x1e01u: r0 = tr; r1 = tg; r2 = tb; a = e->has_a ? at : af; break;
    case 0x2100u: r0 = mul255(fr, tr); r1 = mul255(fg, tg); r2 = mul255(fb, tb); a = mul255(af, at); break;
    case 0x2101u: if (!e->has_a) { r0 = tr; r1 = tg; r2 = tb; } else { r0 = mul255(fr, 255 - at) + mul255(tr, at); r1 = mul255(fg, 255 - at) + mul255(tg, at); r2 = mul255(fb, 255 - at) + mul255(tb, at); } a = af; break;
    case 0x0104u: r0 = fr + tr; r1 = fg + tg; r2 = fb + tb; a = mul255(af, at); break;
    case 0x0be2u: r0 = mul255(fr, 255 - tr) + mul255(e->ec[0], tr); r1 = mul255(fg, 255 - tg) + mul255(e->ec[1], tg); r2 = mul255(fb, 255 - tb) + mul255(e->ec[2], tb); a = mul255(af, at); break;
    case 0x8570u:
        if (e->rgbf == 0x86aeu || e->rgbf == 0x86afu) return env_apply_ref(e, cf, ct);              /* dot3 */
        {   uint32_t sc[3];
            sc[0] = (ct & 0x00ffffffu) | ((uint32_t)at << 24); sc[1] = cf; sc[2] = ((uint32_t)e->ec[3] << 24) | ((uint32_t)e->ec[0] << 16) | ((uint32_t)e->ec[1] << 8) | (uint32_t)e->ec[2];
            r0 = env_fn_i(e->rgbf, env_selv(sc, ep->rsel[0], 16), env_selv(sc, ep->rsel[1], 16), env_selv(sc, ep->rsel[2], 16));
            r1 = env_fn_i(e->rgbf, env_selv(sc, ep->rsel[0], 8), env_selv(sc, ep->rsel[1], 8), env_selv(sc, ep->rsel[2], 8));
            r2 = env_fn_i(e->rgbf, env_selv(sc, ep->rsel[0], 0), env_selv(sc, ep->rsel[1], 0), env_selv(sc, ep->rsel[2], 0));
            a = env_fn_i(e->alf, env_selv(sc, ep->asel[0], 24), env_selv(sc, ep->asel[1], 24), env_selv(sc, ep->asel[2], 24));
            r0 *= e->rscale; r1 *= e->rscale; r2 *= e->rscale; a *= e->ascale;
        }
        break;
    default: return env_apply_ref(e, cf, ct);
    }
    return ((uint32_t)sat255(a) << 24) | ((uint32_t)sat255(r0) << 16) | ((uint32_t)sat255(r1) << 8) | (uint32_t)sat255(r2);
}
static int blend_code(uint32_t f)
{
    switch (f) { case 0x0000u: return 0; case 0x0001u: return 1; case 0x0300u: return 2; case 0x0301u: return 3; case 0x0302u: return 4; case 0x0303u: return 5; case 0x0304u: return 1; case 0x0305u: return 0;
    case 0x0306u: return 6; case 0x0307u: return 7; case 0x0308u: return 0; case 0x8001u: return 8; case 0x8002u: return 9; case 0x8003u: return 10; case 0x8004u: return 11; default: return 1; }
}
/* Resolve the blend state once per draw (cc[] and sf/df/eq must be loaded). fastk: 1 = SRC_ALPHA, ONE_MINUS_SRC_ALPHA; 2 = SRC_ALPHA, ONE; 3 = ONE, ONE (all with ADD). */
static void cb_prepare(cb_state *c, int fmt555)
{
    int ch; c->fmt555 = fmt555; c->sfk = blend_code(c->sf); c->dfk = blend_code(c->df);
    for (ch = 0; ch < 3; ch++) { c->ccv[ch][0] = c->cc[ch]; c->ccv[ch][1] = 255 - c->cc[ch]; c->ccv[ch][2] = c->cc[3]; c->ccv[ch][3] = 255 - c->cc[3]; }
    c->fastk = c->eq != 0x8006u ? 0 : (c->sf == 0x0302u && c->df == 0x0303u) ? 1 : (c->sf == 0x0302u && c->df == 0x0001u) ? 2 : (c->sf == 0x0001u && c->df == 0x0001u) ? 3 : 0;
    if (c->tex) env_prepare(&c->env, &c->ep);
}
static inline uint32_t cb_pack(const cb_state *c, unsigned R, unsigned G, unsigned B)
{ if (c->fmt32) return 0xff000000u | (R << 16) | (G << 8) | B;
  return c->fmt555 ? (uint16_t)(((R >> 3) << 10) | ((G >> 3) << 5) | (B >> 3)) : (uint16_t)(((R >> 3) << 11) | ((G >> 2) << 5) | (B >> 3)); }
static uint32_t cb_blend(const cb_state *c, uint32_t srcargb, uint32_t dst)
{
    int cs[3], cd[3], as = (int)(srcargb >> 24), r[3], ch; unsigned v = dst;
    if (c->fmt32) { cd[0] = (int)((v >> 16) & 255u); cd[1] = (int)((v >> 8) & 255u); cd[2] = (int)(v & 255u); }
    else if (c->fmt555) { unsigned r5 = (v >> 10) & 31, g5 = (v >> 5) & 31, b5 = v & 31; cd[0] = (r5 << 3) | (r5 >> 2); cd[1] = (g5 << 3) | (g5 >> 2); cd[2] = (b5 << 3) | (b5 >> 2); }
    else { unsigned r5 = (v >> 11) & 31, g6 = (v >> 5) & 63, b5 = v & 31; cd[0] = (r5 << 3) | (r5 >> 2); cd[1] = (g6 << 2) | (g6 >> 4); cd[2] = (b5 << 3) | (b5 >> 2); }
    cs[0] = (int)((srcargb >> 16) & 255u); cs[1] = (int)((srcargb >> 8) & 255u); cs[2] = (int)(srcargb & 255u);
    switch (c->fastk) {
    case 1: for (ch = 0; ch < 3; ch++) r[ch] = mul255(cs[ch], as) + mul255(cd[ch], 255 - as); break;
    case 2: for (ch = 0; ch < 3; ch++) r[ch] = mul255(cs[ch], as) + cd[ch]; break;
    case 3: for (ch = 0; ch < 3; ch++) r[ch] = cs[ch] + cd[ch]; break;
    default:
        for (ch = 0; ch < 3; ch++) {
            int vv;
            if (c->eq == 0x8007u) vv = cs[ch] < cd[ch] ? cs[ch] : cd[ch];
            else if (c->eq == 0x8008u) vv = cs[ch] > cd[ch] ? cs[ch] : cd[ch];
            else {
                int sm[8], sf, df, s, d;
                sm[0] = 0; sm[1] = 255; sm[2] = cs[ch]; sm[3] = 255 - cs[ch]; sm[4] = as; sm[5] = 255 - as; sm[6] = cd[ch]; sm[7] = 255 - cd[ch];
                sf = c->sfk < 8 ? sm[c->sfk] : c->ccv[ch][c->sfk - 8]; df = c->dfk < 8 ? sm[c->dfk] : c->ccv[ch][c->dfk - 8];
                s = mul255(cs[ch], sf); d = mul255(cd[ch], df);
                vv = c->eq == 0x800au ? s - d : c->eq == 0x800bu ? d - s : s + d;
            }
            r[ch] = vv < 0 ? 0 : vv > 255 ? 255 : vv;
        }
        break;
    }
    for (ch = 0; ch < 3; ch++) if (r[ch] > 255) r[ch] = 255;
    if (c->fmt32) return 0xff000000u | ((unsigned)r[0] << 16) | ((unsigned)r[1] << 8) | (unsigned)r[2];
    return c->fmt555 ? (uint16_t)(((r[0] >> 3) << 10) | ((r[1] >> 3) << 5) | (r[2] >> 3)) : (uint16_t)(((r[0] >> 3) << 11) | ((r[1] >> 2) << 5) | (r[2] >> 3));
}

#endif

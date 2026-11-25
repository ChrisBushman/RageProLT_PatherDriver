/* G4 isolation benchmark of the CPU compositing per-pixel helpers: the adopted code (gld_cpuops.h) against frozen copies of the code it replaced.
 * Reports ns per call and the difference between the two (exhaustive or random sweeps).
 * cc -O2 -mcpu=7450 -I../../gldriver -I../../common opsbench.c -o opsbench */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/time.h>
#include "gld_cpuops.h"

static int g555 = 0;
__attribute__((noinline)) uint16_t mach64_cb_pack(gld_context *c, unsigned R, unsigned G, unsigned B) { (void)c; return g555 ? (uint16_t)(((R >> 3) << 10) | ((G >> 3) << 5) | (B >> 3)) : (uint16_t)(((R >> 3) << 11) | ((G >> 2) << 5) | (B >> 3)); }
__attribute__((noinline)) void mach64_cb_unpack(gld_context *c, unsigned v, unsigned *R, unsigned *G, unsigned *B)
{
    (void)c;
    if (g555) { unsigned r5 = (v >> 10) & 31, g5 = (v >> 5) & 31, b5 = v & 31; *R = (r5 << 3) | (r5 >> 2); *G = (g5 << 3) | (g5 >> 2); *B = (b5 << 3) | (b5 >> 2); }
    else { unsigned r5 = (v >> 11) & 31, g6 = (v >> 5) & 63, b5 = v & 31; *R = (r5 << 3) | (r5 >> 2); *G = (g6 << 2) | (g6 >> 4); *B = (b5 << 3) | (b5 >> 2); }
}

/* ---- frozen: the code before this optimisation ---- */
static int texel_ref(const m64_cpuenv *e, int x, int y)
{
    if (e->clamp_s) { if (x < 0) x = 0; else if (x >= e->w) x = e->w - 1; } else x &= (e->w - 1);
    if (e->clamp_t) { if (y < 0) y = 0; else if (y >= e->h) y = e->h - 1; } else y &= (e->h - 1);
    return (int)e->img[(size_t)y * (size_t)e->w + (size_t)x];
}
static uint32_t bil_ref(uint32_t c00, uint32_t c10, uint32_t c01, uint32_t c11, int ax, int ay)
{
    int sh; uint32_t out = 0;
    for (sh = 0; sh < 32; sh += 8) {
        int a = (int)((c00 >> sh) & 255u), b = (int)((c10 >> sh) & 255u), c = (int)((c01 >> sh) & 255u), d = (int)((c11 >> sh) & 255u);
        int top = a * (256 - ax) + b * ax, bot = c * (256 - ax) + d * ax;
        out |= (uint32_t)((top * (256 - ay) + bot * ay + 32768) >> 16) << sh;
    }
    return out;
}
static uint32_t tex_sample_ref(const m64_cpuenv *e, int32_t fu, int32_t fv)
{
    if (!e->bil) return (uint32_t)texel_ref(e, fu >> 8, fv >> 8);
    { int32_t gu = fu - 128, gv = fv - 128; int x0 = gu >> 8, y0 = gv >> 8;
      return bil_ref((uint32_t)texel_ref(e, x0, y0), (uint32_t)texel_ref(e, x0 + 1, y0), (uint32_t)texel_ref(e, x0, y0 + 1), (uint32_t)texel_ref(e, x0 + 1, y0 + 1), gu & 255, gv & 255); }
}
static uint16_t blend_ref(const cb_state *c, uint32_t srcargb, uint16_t dst)
{
    int cs[3], cd[3], as = (int)(srcargb >> 24), r[3], ch; unsigned R, G, B;
    mach64_cb_unpack(0, dst, &R, &G, &B);
    cs[0] = (int)((srcargb >> 16) & 255u); cs[1] = (int)((srcargb >> 8) & 255u); cs[2] = (int)(srcargb & 255u); cd[0] = (int)R; cd[1] = (int)G; cd[2] = (int)B;
    for (ch = 0; ch < 3; ch++) {
        int v;
        if (c->eq == 0x8007u) v = cs[ch] < cd[ch] ? cs[ch] : cd[ch];
        else if (c->eq == 0x8008u) v = cs[ch] > cd[ch] ? cs[ch] : cd[ch];
        else { int s = mul255(cs[ch], bfac(c->sf, ch, cs, as, cd, c->cc)), d = mul255(cd[ch], bfac(c->df, ch, cs, as, cd, c->cc)); v = c->eq == 0x800au ? s - d : c->eq == 0x800bu ? d - s : s + d; }
        r[ch] = v < 0 ? 0 : v > 255 ? 255 : v;
    }
    return mach64_cb_pack(0, (unsigned)r[0], (unsigned)r[1], (unsigned)r[2]);
}

#define N 4096
static uint32_t rin[N], rin2[N], tex[256 * 256]; static int ax_[N], ay_[N];
static uint32_t rng = 1u; static uint32_t rnd(void) { rng = rng * 1664525u + 1013904223u; return rng; }
static volatile uint32_t sink;
static double now(void) { struct timeval tv; gettimeofday(&tv, 0); return tv.tv_sec + tv.tv_usec * 1e-6; }
#define TIME(NAME, EXPR) static double NAME(void) { uint32_t a = 0; long it = 0; double t0 = now(), t1; int i; do { for (i = 0; i < N; i++) { uint32_t x = rin[i], y = rin2[i]; int axv = ax_[i], ayv = ay_[i]; (void)axv; (void)ayv; a += (uint32_t)(EXPR); } it += N; t1 = now(); } while (t1 - t0 < 0.4); sink = a; return (t1 - t0) / (double)it * 1e9; }

static m64_cpuenv E0; static cb_state CB;
TIME(t_null, x ^ y)
TIME(t_texel_ref, (uint32_t)texel_ref(&E0, (int)(x & 1023) - 300, (int)(y & 1023) - 300))
TIME(t_texel_new, (uint32_t)texel_at(&E0, (int)(x & 1023) - 300, (int)(y & 1023) - 300))
TIME(t_near_ref, tex_sample_ref(&E0, (int32_t)(x & 0xffffff), (int32_t)(y & 0xffffff)))
TIME(t_near_new, tex_sample_q8(&E0, (int32_t)(x & 0xffffff), (int32_t)(y & 0xffffff)))
TIME(t_bil_ref, tex_sample_ref(&E0, (int32_t)(x & 0xffffff), (int32_t)(y & 0xffffff)))
TIME(t_bil_new, tex_sample_q8(&E0, (int32_t)(x & 0xffffff), (int32_t)(y & 0xffffff)))
TIME(t_env_ref, env_apply_ref(&E0, x, y))
TIME(t_env_new, env_apply(&E0, &CB.ep, x, y))
TIME(t_blend_ref, blend_ref(&CB, x, (uint16_t)y))
TIME(t_blend_new, cb_blend(&CB, x, (uint16_t)y))

static void prep_blend(uint32_t sf, uint32_t df, uint32_t eq) { CB.sf = sf; CB.df = df; CB.eq = eq; CB.tex = 0; cb_prepare(&CB, g555); }
int main(void)
{
    int i; double nul; long d, n, mx;
    static const uint32_t sfs[] = { 0x0302, 0x0001, 0x0000, 0x0306, 0x0300, 0x8001, 0x0303, 0x0308, 0x0304 }, dfs[] = { 0x0303, 0x0001, 0x0300, 0x0301, 0x8002, 0x8004, 0x0000, 0x0307, 0x0302 }, eqs[] = { 0x8006, 0x800a, 0x800b, 0x8007, 0x8008 };
    for (i = 0; i < N; i++) { rin[i] = rnd(); rin2[i] = rnd(); ax_[i] = (int)(rnd() & 255); ay_[i] = (int)(rnd() & 255); }
    for (i = 0; i < 256 * 256; i++) tex[i] = rnd();
    memset(&E0, 0, sizeof E0); E0.img = tex; E0.w = 256; E0.h = 256; E0.wsh = 8; E0.has_a = 1;
    memset(&CB, 0, sizeof CB); CB.cc[0] = 40; CB.cc[1] = 90; CB.cc[2] = 200; CB.cc[3] = 128;
    fx_rcp_init(); nul = t_null();
    printf("%-44s %9s %9s   %s\n", "operation (ns per call, loop overhead removed)", "before", "after", "difference");
    d = 0; for (i = 0; i < 1000000; i++) { int x = (int)(rnd() & 1023) - 300, y = (int)(rnd() & 1023) - 300; if (texel_ref(&E0, x, y) != texel_at(&E0, x, y)) d++; }
    printf("%-44s %9.1f %9.1f   %ld of 1000000 differ\n", "texel fetch, repeat (index by mask+shift)", t_texel_ref() - nul, t_texel_new() - nul, d);
    E0.bil = 0; d = 0; for (i = 0; i < 1000000; i++) { int32_t u = (int32_t)(rnd() & 0xffffff), v = (int32_t)(rnd() & 0xffffff); if (tex_sample_ref(&E0, u, v) != tex_sample_q8(&E0, u, v)) d++; }
    printf("%-44s %9.1f %9.1f   %ld of 1000000 differ\n", "nearest sample", t_near_ref() - nul, t_near_new() - nul, d);
    E0.bil = 1; mx = 0; n = 0; { long sum = 0; for (i = 0; i < 2000000; i++) { int32_t u = (int32_t)(rnd() & 0xffffff), v = (int32_t)(rnd() & 0xffffff); uint32_t p = tex_sample_ref(&E0, u, v), q = tex_sample_q8(&E0, u, v); int sh; for (sh = 0; sh < 32; sh += 8) { long e = labs((long)((p >> sh) & 255) - (long)((q >> sh) & 255)); if (e > mx) mx = e; sum += e; n++; } }
      printf("%-44s %9.1f %9.1f   max %ld LSB, mean %.3f LSB per channel\n", "bilinear sample (4 fetches + 2-lane weights)", t_bil_ref() - nul, t_bil_new() - nul, mx, (double)sum / (double)n); }
    { static const struct { const char *nm; uint32_t mode; } M[] = { { "env MODULATE", 0x2100 }, { "env BLEND", 0x0be2 }, { "env COMBINE ADD_SIGNED (tex, const, prev)", 0x8570 }, { "env COMBINE INTERPOLATE", 0x8570 }, { "env REPLACE / DECAL / ADD (reference path)", 0 } };
      int k; for (k = 0; k < 5; k++) {
        E0.mode = M[k].mode; E0.ec[0] = 10; E0.ec[1] = 200; E0.ec[2] = 30; E0.ec[3] = 77; E0.rscale = 1; E0.ascale = 1;
        if (k == 2) { E0.rgbf = 0x8574u; E0.alf = 0x1e01u; E0.src[0] = 0x1702u; E0.src[1] = 0x8578u; E0.src[2] = 0x8576u; E0.asrc[0] = 0x1702u; E0.asrc[1] = 0x8578u; E0.asrc[2] = 0x8576u; E0.op[0] = 0x0300u; E0.op[1] = 0x0300u; E0.op[2] = 0x0300u; E0.aop[0] = 0x0302u; E0.aop[1] = 0x0302u; E0.aop[2] = 0x0302u; }
        if (k == 3) { E0.rgbf = 0x8575u; E0.alf = 0x8575u; E0.src[0] = 0x1702u; E0.src[1] = 0x8578u; E0.src[2] = 0x8576u; E0.asrc[0] = 0x1702u; E0.asrc[1] = 0x8578u; E0.asrc[2] = 0x8576u; E0.op[0] = 0x0300u; E0.op[1] = 0x0301u; E0.op[2] = 0x0302u; E0.aop[0] = 0x0302u; E0.aop[1] = 0x0303u; E0.aop[2] = 0x0302u; E0.rscale = 2; }
        if (k == 4) E0.mode = 0x1e01u;
        env_prepare(&E0, &CB.ep); d = 0; for (i = 0; i < 1000000; i++) { uint32_t a = rnd(), b = rnd(); if (env_apply_ref(&E0, a, b) != env_apply(&E0, &CB.ep, a, b)) d++; }
        printf("%-44s %9.1f %9.1f   %ld of 1000000 differ\n", M[k].nm, t_env_ref() - nul, t_env_new() - nul, d); } }
    { long cfgs = 0, tot = 0; int a, b, q, f; double tb = 0, ta = 0;
      for (f = 0; f < 2; f++) { g555 = f; d = 0;
        for (a = 0; a < 9; a++) for (b = 0; b < 9; b++) for (q = 0; q < 5; q++) { int k; prep_blend(sfs[a], dfs[b], eqs[q]); cfgs++;
            for (k = 0; k < 4000; k++) { uint32_t sx = rnd(); uint16_t dd = (uint16_t)rnd(); tot++; if (blend_ref(&CB, sx, dd) != cb_blend(&CB, sx, dd)) d++; } }
        printf("blend equality %s: %ld of %ld differ over all %ld source/destination/equation combinations\n", f ? "RGB555" : "RGB565", d, tot, cfgs); }
      g555 = 0;
      prep_blend(0x0302, 0x0303, 0x8006); tb = t_blend_ref() - nul; ta = t_blend_new() - nul; printf("%-44s %9.1f %9.1f   (fast kind 1)\n", "blend SRC_ALPHA, 1-SRC_ALPHA, ADD", tb, ta);
      prep_blend(0x0302, 0x0001, 0x8006); tb = t_blend_ref() - nul; ta = t_blend_new() - nul; printf("%-44s %9.1f %9.1f   (fast kind 2)\n", "blend SRC_ALPHA, ONE, ADD", tb, ta);
      prep_blend(0x0001, 0x0001, 0x8006); tb = t_blend_ref() - nul; ta = t_blend_new() - nul; printf("%-44s %9.1f %9.1f   (fast kind 3)\n", "blend ONE, ONE, ADD", tb, ta);
      prep_blend(0x8001, 0x8002, 0x800a); tb = t_blend_ref() - nul; ta = t_blend_new() - nul; printf("%-44s %9.1f %9.1f   (generic, resolved once)\n", "blend CONSTANT_COLOR, 1-CONSTANT_COLOR, SUBTRACT", tb, ta);
      prep_blend(0x0306, 0x0307, 0x8007); tb = t_blend_ref() - nul; ta = t_blend_new() - nul; printf("%-44s %9.1f %9.1f   (MIN)\n", "blend equation MIN", tb, ta); }
    return 0;
}

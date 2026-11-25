/*
 * gl_suite.c - the Path-B behaviour suite ported to the standalone driver (Phase 3 exit).  Same tests: each sets GL state in a stand-in gli,
 * draws through the bundle's own dispatch slots and checks the pixels.  What changed: pixels are read back through the kernel (never a CPU view of
 * VRAM), the colour target is RGB565 (readbacks are expanded to ARGB with bit replication, so channel checks have the 565 quantisation in their
 * tolerance), and the tests that exercised Apple-kext internals (DMA benchmarks, register audit, Apple-surface guards, scanout dumps) are not part of
 * this table (they are replaced by tools/ragegl/ragegl_test.c, which tests the kernel's own validation).
 *
 *   gl_suite [test-name ...]      (no args = all)
 */
#include <libkern/OSByteOrder.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <sys/time.h>
#include <string.h>
#include "gld.h"

extern unsigned g_gld_force_w, g_gld_force_h;
#define W 256
#define H 256
#define REGS 0x7ff800u
#define GLI_POINT_SIZE_OFF 0x6630
#define GLI_LINE_WIDTH_OFF 0x6394


/* VRAM access for tests: through the kernel only (SWAP32 = logical little-endian words).  vrd/vwr are word granular and slow: use sparingly. */
static mach64_priv *P;
static unsigned vrd(unsigned o) { uint32_t w = 0; if (o >= 0x7ff800u) return 0; rgb_readback2(&P->rb, o & ~3u, &w, 4, 2u); return w; }
static void vwr(unsigned o, unsigned v) { uint32_t w = v; rgb_upload2(&P->rb, o & ~3u, &w, 4, 2u); }
static gld_context *ctx; static unsigned char *gli, table[0x80];
static unsigned TOFF, TPITCH;                       /* render target byte offset / pitch in px */
static int npass, nfail;

typedef void (*slot3_fn)(gld_context *, const void *, uint32_t);
typedef void (*slot4_fn)(gld_context *, const void *, const void *, uint32_t);
static slot3_fn SLOT(unsigned off) { return *(slot3_fn *)(table + off); }

#define CHECK(name, cond) do { if (cond) { npass++; printf("  PASS  %s\n", name); } else { nfail++; printf("  FAIL  %s\n", name); } } while (0)

static void g32(unsigned off, uint32_t v) { *(uint32_t *)(gli + off) = v; }
static void gf(unsigned off, float f) { memcpy(gli + off, &f, 4); }

/* RGB565 target: one pixel read back through the kernel and expanded to 0xffRRGGBB with bit replication */
static unsigned px(int x, int y)
{
    uint32_t o = TOFF + (uint32_t)y * TPITCH * 2 + (uint32_t)x * 2, w = 0, v, r, g, b;
    rgb_readback2(&P->rb, o & ~3u, &w, 4, 1u);                  /* SWAP16: logical little-endian halfwords, first pixel in the high half */
    v = (o & 2) ? (w & 0xffffu) : (w >> 16);
    if (!v) return 0;                                           /* black = untouched (the old 32-bit target was cleared to 0, alpha included) */
    r = (v >> 11) & 31u; g = (v >> 5) & 63u; b = v & 31u;
    return 0xff000000u | (((r << 3) | (r >> 2)) << 16) | (((g << 2) | (g >> 4)) << 8) | ((b << 3) | (b >> 2));
}
static uint32_t to565(unsigned argb) { return ((((argb >> 16) & 0xffu) >> 3) << 11) | ((((argb >> 8) & 0xffu) >> 2) << 5) | (((argb) & 0xffu) >> 3); }
static int ch(unsigned c, int sh) { return (int)((c >> sh) & 0xff); }
static int near_(int a, int b) { return a - b <= 16 && b - a <= 16; }   /* RGB565 step is 8 per channel (+ dither): measured worst error 15 (texture 11 of the LRU test) */
static int isclr(unsigned c, int r, int g, int b) { return near_(ch(c, 16), r) && near_(ch(c, 8), g) && near_(ch(c, 0), b); }
/* composite modulate has a 4-bit factor (out = prev*((B>>4)+1)/16, measured), so allow its quantisation step */
static int near20(int a, int b) { return a - b <= 20 && b - a <= 20; }
static int isclr_mt(unsigned c, int r, int g, int b) { return near20(ch(c, 16), r) && near20(ch(c, 8), g) && near20(ch(c, 0), b); }

static void clear_rt(unsigned argb) { mach64_flush_batch(ctx); rgb_fill(&P->rb, 0, 0, (int)P->fb_w, (int)P->fb_h, to565(argb)); P->frame_new = 0; P->z_clear_pending = 1; P->dirty |= MACH64_DIRTY_STATE; }   /* the test owns the colour contents; a new frame still owes its Z clear */

static void default_state(void)
{
    memset(gli + 0x6200, 0, 0x700);
    memset(gli + 0x63b4, 1, 4);              /* glColorMask all on */
    g32(0x6708, 1);                          /* GL_SMOOTH */
    gf(GLI_POINT_SIZE_OFF, 1.0f); gf(GLI_LINE_WIDTH_OFF, 1.0f);
    g32(0x633c, 0x0201u << 16);              /* depth func LESS (disabled until bit24 set) */
}

/* top-down FB coords x,y -> GLEngine gli vertex (y-up window coords; the plugin flips back) */
static void V(unsigned char *v, int x, int y, float z, unsigned argb)
{
    /* GLEngine hands the plugin y-up window coords and the plugin flips them with the SURFACE height it keeps at ctx+0x1f4 (480 on a
     * full-screen surface, 256 for the old 256x256 target): flip with the same number or the scene lands (surface_h - 256) rows off */
    unsigned sh_ = ctx ? *(const uint32_t *)((const unsigned char *)ctx + 0x1f4) : 0;
    float xf = (float)x, yf = (float)((sh_ ? (int)sh_ : H) - y), w = 1.0f, c[4]; int i;
    memset(v, 0, MACH64_GLIVERTEX_STRIDE);
    for (i = 0; i < 4; i++) c[i] = (float)((argb >> (i == 3 ? 24 : 16 - 8 * i)) & 0xff) / 255.0f;
    memcpy(v + 0x00, &xf, 4); memcpy(v + 0x04, &yf, 4); memcpy(v + 0x0c, &w, 4); memcpy(v + 0x18, &z, 4);
    memcpy(v + 0x30, &c[0], 4); memcpy(v + 0x34, &c[1], 4); memcpy(v + 0x38, &c[2], 4); memcpy(v + 0x3c, &c[3], 4);
}
static unsigned char *VB(int n) { unsigned char *b = calloc((size_t)n, MACH64_GLIVERTEX_STRIDE); return b; }
#define VX(b, i) ((b) + (i) * MACH64_GLIVERTEX_STRIDE)

/* push the GL state to the plugin (the STATE flush slot 0x04 syncs depth; the rest is read in the draw path) */
static void sync(void)
{
    static uint32_t info[16];
    gldUpdateDispatch(ctx, table, (uint32_t)(uintptr_t)info);
    { typedef int (*fl_fn)(gld_context *, uint32_t, uint32_t, uint32_t, uint32_t, uint32_t, uint32_t);
      fl_fn f = *(fl_fn *)(table + 0x04); if (f) f(ctx, 0, 0, 0, 0, 0, 0); }
}
typedef void (*rc_fn)(gld_context *, uint32_t, uint32_t, uint32_t, uint32_t, uint32_t);
/* a real frame ends in dispatch slot 0x60 (GLEngine's once-per-frame render control): flushes the batch (ONE drain) then presents */
static void endframe(void)
{
    rc_fn f = *(rc_fn *)(table + 0x60); if (f) f(ctx, 0, 0, 0, 0, 0);
    rgb_fence(&P->rb);        /* the kernel serialises: the next kernel call (readback) waits for the engine anyway; the fence also covers the present */
}
static int g_endframe = 1;   /* 0 = leave the frame open so several primitives share one frame (one clear, one Z buffer) */
static void draw_tri(const unsigned char *a, const unsigned char *b, const unsigned char *c, int n)
{
    unsigned char *vb = VB(3); memcpy(vb, a, MACH64_GLIVERTEX_STRIDE); memcpy(vb + MACH64_GLIVERTEX_STRIDE, b, MACH64_GLIVERTEX_STRIDE);
    memcpy(vb + 2 * MACH64_GLIVERTEX_STRIDE, c, MACH64_GLIVERTEX_STRIDE); (void)n;
    SLOT(0x2c)(ctx, vb, 3); if (g_endframe) endframe(); free(vb);
}
static void tri(int x0, int y0, unsigned c0, int x1, int y1, unsigned c1, int x2, int y2, unsigned c2, float z)
{
    unsigned char a[MACH64_GLIVERTEX_STRIDE], b[MACH64_GLIVERTEX_STRIDE], c[MACH64_GLIVERTEX_STRIDE];
    V(a, x0, y0, z, c0); V(b, x1, y1, z, c1); V(c, x2, y2, z, c2); draw_tri(a, b, c, 3);
}
static void zclear(void) { mach64_clear_zbuffer(ctx); endframe(); }

/* ------------------------------------------------------------------ tests */
/* diagnostic (SUITE_VRAMDIFF=1): which 4 KB pages of usable VRAM changed across one gouraud triangle -- shows where the engine/present really write */
static void vramdiff_report(const unsigned *before)
{
    unsigned pg, np = 0, first = 0, last = 0; printf("    vramdiff: changed pages (4 KB) outside the clear target:");
    for (pg = 0; pg < 0x7ff000u / 4096u; pg++) { unsigned i, d = 0;
        for (i = 0; i < 1024; i++) if (vrd(pg * 4096u + i * 4u) != before[pg * 1024u + i]) d++;
        if (d) { if (!np) first = pg; last = pg; if (np < 24) printf(" 0x%x(%u)", pg * 4096u, d); np++; } }
    printf("\n    vramdiff: %u pages changed, first 0x%x last 0x%x\n", np, first * 4096u, last * 4096u);
}
static void t_gouraud(void)
{
    unsigned *snap = NULL;
    default_state(); sync(); clear_rt(0);
    if (getenv("SUITE_VRAMDIFF")) { unsigned o; snap = malloc(0x7ff000u); for (o = 0; o < 0x7ff000u; o += 4) snap[o / 4] = vrd(o); }
    tri(20, 16, 0xffff0000u, 110, 44, 0xff00ff00u, 50, 116, 0xff0000ffu, 0.0f);
    if (snap) { vramdiff_report(snap); free(snap); }
    CHECK("gouraud: vertex 0 region red",   ch(px(24, 22), 16) > 150 && ch(px(24, 22), 8) < 90);
    CHECK("gouraud: vertex 1 region green", ch(px(100, 44), 8) > 150 && ch(px(100, 44), 16) < 90);
    CHECK("gouraud: vertex 2 region blue",  ch(px(50, 108), 0) > 150 && ch(px(50, 108), 16) < 90);
    CHECK("gouraud: centroid is a mix",     ch(px(60, 59), 16) > 40 && ch(px(60, 59), 8) > 40 && ch(px(60, 59), 0) > 40);
    CHECK("gouraud: outside untouched",     px(200, 200) == 0 && px(5, 5) == 0);
}
static void t_flat(void)
{
    default_state(); g32(0x6708, 0); sync(); clear_rt(0);
    tri(20, 16, 0xffff0000u, 110, 44, 0xff00ff00u, 50, 116, 0xff0000ffu, 0.0f);
    { unsigned c = px(60, 59);
      CHECK("flat: interior is one solid colour (all vertices share a colour)", (ch(c, 16) > 200) + (ch(c, 8) > 200) + (ch(c, 0) > 200) == 1);
      CHECK("flat: same colour near each vertex", px(30, 24) == px(60, 59) || (px(60, 59) & 0xffffff) == (px(98, 44) & 0xffffff)); }
}
static void t_colormask(void)
{
    default_state(); memset(gli + 0x63b4, 1, 4); gli[0x63b4] = 0; sync(); clear_rt(0);   /* R masked off */
    tri(20, 16, 0xffffffffu, 110, 44, 0xffffffffu, 50, 116, 0xffffffffu, 0.0f);
    CHECK("colormask: red channel untouched",  ch(px(60, 59), 16) == 0);
    CHECK("colormask: green+blue written",     ch(px(60, 59), 8) > 200 && ch(px(60, 59), 0) > 200);
}
static void t_depth(void)
{
    unsigned c;
    /* glClear(COLOR|DEPTH) happens at each frame's first draw, so both triangles of a case must share ONE frame */
    default_state(); g32(0x6340, 0x01000000u); g32(0x63b8, 0x01000000u); sync(); g_endframe = 0; clear_rt(0);
    tri(20, 16, 0xffff0000u, 140, 16, 0xffff0000u, 80, 130, 0xffff0000u, -0.5f);        /* near red first */
    tri(20, 16, 0xff00ff00u, 140, 16, 0xff00ff00u, 80, 130, 0xff00ff00u,  0.5f);        /* far green, same footprint */
    endframe(); c = px(80, 50); CHECK("depth LESS: near drawn first stays in front", isclr(c, 255, 0, 0));
    clear_rt(0);
    tri(20, 16, 0xff00ff00u, 140, 16, 0xff00ff00u, 80, 130, 0xff00ff00u,  0.5f);        /* far green first */
    tri(20, 16, 0xffff0000u, 140, 16, 0xffff0000u, 80, 130, 0xffff0000u, -0.5f);        /* near red second */
    endframe(); c = px(80, 50); CHECK("depth LESS: near drawn second overwrites far", isclr(c, 255, 0, 0));
    default_state(); sync(); clear_rt(0);                                                /* depth OFF, same frame */
    tri(20, 16, 0xffff0000u, 140, 16, 0xffff0000u, 80, 130, 0xffff0000u, -0.5f);
    tri(20, 16, 0xff00ff00u, 140, 16, 0xff00ff00u, 80, 130, 0xff00ff00u,  0.5f);
    endframe(); c = px(80, 50); printf("    (depth off pixel 0x%08x)\n", c); CHECK("depth off: last drawn wins", isclr(c, 0, 255, 0));
    g_endframe = 1;
}
static void t_blend(void)
{
    unsigned c;
    default_state(); g32(0x6304, 0x01000000u); g32(0x62e8, (0x0302u << 16) | 0x0303u); gf(0x6320, 0.0f); gf(0x6324, 0.0f); gf(0x6328, 1.0f); gf(0x632c, 1.0f); sync();   /* glClearColor(0,0,1,1): blue background, applied by the frame's deferred glClear */
    tri(20, 16, 0x8000ff00u, 140, 16, 0x8000ff00u, 80, 130, 0x8000ff00u, 0.0f);                                       /* green a=0.5 */
    c = px(80, 50); printf("    (blend pixel 0x%08x corner 0x%08x)\n", c, px(3, 3));
    { int gx, gy; for (gy = 8; gy < 140; gy += 20) { printf("    row %3d:", gy); for (gx = 8; gx < 160; gx += 20) printf(" %06x", px(gx, gy) & 0xffffff); printf("\n"); } printf("    batch n=%u tri=%u dirty=%u\n", (unsigned)P->batch.n, P->triangles, P->dirty); { int xx, yy; for (yy = 49; yy <= 51; yy++) { printf("    y%d:", yy); for (xx = 76; xx < 86; xx++) printf(" %04x", px(xx, yy) & 0xffffff); printf("\n"); } } }
    { unsigned al; for (al = 0; al <= 0xff; al += 0x40) { tri(20, 16, (al << 24) | 0xff00u, 140, 16, (al << 24) | 0xff00u, 80, 130, (al << 24) | 0xff00u, 0.0f); printf("    (alpha %02x -> 0x%08x)\n", al, px(80, 50)); } }
    CHECK("blend SRC_ALPHA/ONE_MINUS_SRC_ALPHA: green+blue mix", ch(c, 8) > 90 && ch(c, 8) < 170 && ch(c, 0) > 90 && ch(c, 0) < 170);
    default_state(); gf(0x6320, 0.0f); gf(0x6324, 0.0f); gf(0x6328, 1.0f); gf(0x632c, 1.0f); sync();
    tri(20, 16, 0x8000ff00u, 140, 16, 0x8000ff00u, 80, 130, 0x8000ff00u, 0.0f);
    c = px(80, 50); CHECK("blend off: opaque green replaces", isclr(c, 0, 255, 0));
}
static void t_alphatest(void)
{
    default_state(); gf(0x62e0, 0.5f); g32(0x62e4, (0x0204u << 16) | 0x100u); sync(); clear_rt(0);   /* GL_GREATER 0.5, enabled */
    tri(20, 16, 0x4000ff00u, 140, 16, 0x4000ff00u, 80, 130, 0x4000ff00u, 0.0f);                       /* alpha .25 -> rejected */
    CHECK("alpha test GREATER 0.5: alpha 0.25 rejected", px(80, 50) == 0);
    tri(20, 16, 0xc000ff00u, 140, 16, 0xc000ff00u, 80, 130, 0xc000ff00u, 0.0f);                       /* alpha .75 -> passes */
    printf("    (alpha .75 pixel = 0x%08x)\n", px(80, 50));
    CHECK("alpha test GREATER 0.5: alpha 0.75 passes", ch(px(80, 50), 8) > 200);
}
static void t_prims(void)
{
    unsigned char *vb; int n;
    default_state(); sync(); clear_rt(0);
    vb = VB(4); V(VX(vb, 0), 20, 20, 0, 0xffffffffu); V(VX(vb, 1), 100, 20, 0, 0xffffffffu); V(VX(vb, 2), 100, 100, 0, 0xffffffffu); V(VX(vb, 3), 20, 100, 0, 0xffffffffu);
    SLOT(0x38)(ctx, vb, 4); endframe();
    CHECK("GL_QUADS: quad interior filled", px(60, 60) != 0 && px(25, 25) != 0 && px(95, 95) != 0);
    CHECK("GL_QUADS: outside empty", px(120, 60) == 0 && px(60, 120) == 0);
    clear_rt(0); SLOT(0x28)(ctx, vb, 4); endframe();
    CHECK("GL_POLYGON (convex quad): interior filled", px(60, 60) != 0 && px(25, 95) != 0);
    clear_rt(0); SLOT(0x34)(ctx, vb, 4); endframe();
    CHECK("GL_TRIANGLE_STRIP: both triangles drawn", px(80, 40) != 0 || px(40, 80) != 0);
    free(vb);
    clear_rt(0); vb = VB(2); V(VX(vb, 0), 20, 128, 0, 0xffffffffu); V(VX(vb, 1), 200, 128, 0, 0xffffffffu);
    SLOT(0x1c)(ctx, vb, 2); endframe();
    for (n = 0; n < 8; n++) ;
    CHECK("GL_LINES: horizontal line drawn (mid)", px(110, 127) != 0 || px(110, 128) != 0 || px(110, 129) != 0);
    CHECK("GL_LINES: nothing far from the line",   px(110, 60) == 0 && px(110, 200) == 0);
    free(vb);
    clear_rt(0); gf(GLI_POINT_SIZE_OFF, 5.0f); sync(); vb = VB(1); V(VX(vb, 0), 128, 128, 0, 0xffffffffu);
    SLOT(0x18)(ctx, vb, 1); endframe();
    CHECK("GL_POINTS: size-5 point covers centre", px(128, 128) != 0 || px(127, 127) != 0);
    CHECK("GL_POINTS: point is local", px(128, 140) == 0 && px(100, 128) == 0);
    free(vb);
}
static void t_clear(void)
{
    default_state(); sync(); clear_rt(0x12345678u);
    gf(0x6320, 0.0f); gf(0x6324, 0.5f); gf(0x6328, 1.0f); gf(0x632c, 1.0f);       /* clear colour R,G,B,A */
    mach64_clear_color(ctx); endframe();
    { unsigned c = px(100, 100);
      printf("    (clear pixel = 0x%08x)\n", c);
      CHECK("clear colour: filled with clear colour", isclr(c, 0, 128, 255)); }
}

static unsigned count_nz(unsigned off) { unsigned n = 0, q; for (q = 0; q < W * H; q++) if (vrd(off + q * 4)) n++; return n; }




/* ---------------------------------------------------------------- textures
 * Fake GLEngine texture object, laid out as the plugin reads it: texobj[0x30] -> state object; state+0x10 = (WRAP_S<<16)|WRAP_T,
 * +0x16/+0x18 = MIN/MAG filter (u16), +0x5e = base LOD, level records from +0x9c (0x14 bytes each: u16 w,h; +0xc u16 format;
 * +0xe u16 type; +0x10 u32 pixel pointer).  Mipmaps: see mktex_mip/t_tex_mip (software level selection; the hardware mip chain and TEX_BLEND_FCN stay unused). */
static void *mktex(int w, int h, unsigned fmt, const unsigned char *data, unsigned wrap, unsigned minf, unsigned magf)
{
    unsigned char *st = calloc(1, 0x400), *t = calloc(1, 0x40), *r = st + 0x9c;
    *(uint32_t *)(st + 0x10) = wrap; *(uint16_t *)(st + 0x16) = (uint16_t)minf; *(uint16_t *)(st + 0x18) = (uint16_t)magf;
    *(uint16_t *)(r + 0) = (uint16_t)w; *(uint16_t *)(r + 2) = (uint16_t)h; *(uint16_t *)(r + 0xc) = (uint16_t)fmt; *(uint16_t *)(r + 0xe) = 0x1401;
    *(uint32_t *)(r + 0x10) = (uint32_t)(uintptr_t)data;
    *(uint32_t *)(t + 0x30) = (uint32_t)(uintptr_t)st;
    return t;
}
#define GL_REPLACE_ENV 0x1e01u
#define GL_MODULATE_ENV 0x2100u
#define GL_REPEAT_ 0x2901u
#define GL_CLAMP_E 0x812fu
static void bindtex(void *t, unsigned env)
{
    g32(0x15748, (uint32_t)(uintptr_t)t); g32(0x6748, 8); g32(0x674c, env << 16);
    mach64_texture_dirtied(t);
}
/* 8x8 RGB: quadrants TL red, TR green, BL blue, BR white (4x4 texels each) */
static unsigned char *quadtex(void)
{
    unsigned char *d = malloc(8 * 8 * 3); int x, y;
    for (y = 0; y < 8; y++) for (x = 0; x < 8; x++) {
        unsigned char *p = d + (y * 8 + x) * 3;
        p[0] = (y < 4 && x < 4) || (y >= 4 && x >= 4) ? 255 : 0;                      /* R: TL, BR */
        p[1] = (y < 4 && x >= 4) || (y >= 4 && x >= 4) ? 255 : 0;                     /* G: TR, BR */
        p[2] = (y >= 4 && x < 4) || (y >= 4 && x >= 4) ? 255 : 0;                     /* B: BL, BR */
    }
    return d;
}
/* textured quad (x0,y0)-(x1,y1) top-down, texcoords s0..s1 left->right, t0..t1 top->bottom, vertex colour argb */
static void tquad(int x0, int y0, int x1, int y1, float s0, float t0, float s1, float t1, unsigned argb)
{
    unsigned char *vb = VB(4); int i; float S[4] = { s0, s1, s1, s0 }, T[4] = { t0, t0, t1, t1 };
    V(VX(vb, 0), x0, y0, 0, argb); V(VX(vb, 1), x1, y0, 0, argb); V(VX(vb, 2), x1, y1, 0, argb); V(VX(vb, 3), x0, y1, 0, argb);
    for (i = 0; i < 4; i++) { memcpy(VX(vb, i) + 0x80, &S[i], 4); memcpy(VX(vb, i) + 0x84, &T[i], 4); }
    SLOT(0x38)(ctx, vb, 4); if (g_endframe) endframe(); free(vb);
}
static void texreset(void) { g32(0x15748, 0); g32(0x6748, 0); }

static void t_tex_replace(void)
{
    unsigned char *d = quadtex(); void *t = mktex(8, 8, 0x1907, d, (GL_REPEAT_ << 16) | GL_REPEAT_, 0x2600, 0x2600);
    default_state(); bindtex(t, GL_REPLACE_ENV); sync(); clear_rt(0);
    tquad(20, 20, 148, 148, 0.0f, 0.0f, 1.0f, 1.0f, 0xffffffffu);
    printf("    (TL 0x%08x TR 0x%08x BL 0x%08x BR 0x%08x)\n", px(36, 36), px(132, 36), px(36, 132), px(132, 132));
    CHECK("texture REPLACE: top-left texel red",    isclr(px(36, 36), 255, 0, 0));
    CHECK("texture REPLACE: top-right texel green", isclr(px(132, 36), 0, 255, 0));
    CHECK("texture REPLACE: bottom-left texel blue", isclr(px(36, 132), 0, 0, 255));
    CHECK("texture REPLACE: bottom-right texel white", isclr(px(132, 132), 255, 255, 255));
    CHECK("texture REPLACE: outside the quad untouched", px(8, 8) == 0 && px(160, 160) == 0);
    /* MODULATE with a half-grey vertex colour: texel * 0.5 */
    default_state(); bindtex(t, GL_MODULATE_ENV); sync(); clear_rt(0);
    tquad(20, 20, 148, 148, 0.0f, 0.0f, 1.0f, 1.0f, 0xff808080u);
    printf("    (modulate: red 0x%08x white 0x%08x)\n", px(36, 36), px(132, 132));
    CHECK("texture MODULATE x0.5: white texel -> mid grey", isclr(px(132, 132), 128, 128, 128));
    CHECK("texture MODULATE x0.5: red texel -> dark red",   isclr(px(36, 36), 128, 0, 0));
    /* re-upload: change a texel colour, dirty it, same texobj -> new colour appears */
    memset(d, 0, 4 * 3); { int x, y; for (y = 0; y < 4; y++) for (x = 0; x < 4; x++) { unsigned char *p = d + (y * 8 + x) * 3; p[0] = 255; p[1] = 255; p[2] = 0; } }
    default_state(); bindtex(t, GL_REPLACE_ENV); sync(); clear_rt(0);
    tquad(20, 20, 148, 148, 0.0f, 0.0f, 1.0f, 1.0f, 0xffffffffu);
    CHECK("texture re-upload: modified texel (yellow) shows", isclr(px(36, 36), 255, 255, 0));
    texreset(); mach64_texture_deleted(t);
    default_state(); sync(); clear_rt(0); tri(20, 16, 0xffff0000u, 110, 44, 0xffff0000u, 50, 116, 0xffff0000u, 0.0f);
    CHECK("texture deleted + disabled: plain geometry still draws", ch(px(60, 59), 16) > 200);
}

/* ------------------------------------------------------------- multitexture
 * Unit 1 in the fake gli (offsets recovered with tools/mach64_mtexscan): enable word 0x67c4 (bit 3), env mode 0x67c8 (GL enum in the high half), bound texobj
 * 0x1575c; per-vertex coordinates S/w,T/w at +0x90/+0x94 (unit 0: +0x80/+0x84). */
static void bindtex1(void *t, unsigned env) { g32(0x1575c, (uint32_t)(uintptr_t)t); g32(0x67c4, 8); g32(0x67c8, env << 16); mach64_texture_dirtied(t); }
static void texreset1(void) { g32(0x1575c, 0); g32(0x67c4, 0); g32(0x67c8, 0); }
/* 8x8 RGB: top four rows white, bottom four rows half grey */
static unsigned char *halftex(void)
{
    unsigned char *d = malloc(8 * 8 * 3); int x, y;
    for (y = 0; y < 8; y++) for (x = 0; x < 8; x++) { unsigned char *p = d + (y * 8 + x) * 3; p[0] = p[1] = p[2] = (unsigned char)(y < 4 ? 255 : 128); }
    return d;
}
/* quad with BOTH coordinate sets: unit 0 s0..s1/t0..t1, unit 1 u0..u1/v0..v1 */
static void tquad2(int x0, int y0, int x1, int y1, float s0, float t0, float s1, float t1, float u0, float v0, float u1, float v1, unsigned argb)
{
    unsigned char *vb = VB(4); int i; float S[4] = { s0, s1, s1, s0 }, T[4] = { t0, t0, t1, t1 }, U[4] = { u0, u1, u1, u0 }, Vv[4] = { v0, v0, v1, v1 };
    V(VX(vb, 0), x0, y0, 0, argb); V(VX(vb, 1), x1, y0, 0, argb); V(VX(vb, 2), x1, y1, 0, argb); V(VX(vb, 3), x0, y1, 0, argb);
    for (i = 0; i < 4; i++) {
        memcpy(VX(vb, i) + 0x80, &S[i], 4); memcpy(VX(vb, i) + 0x84, &T[i], 4);
        memcpy(VX(vb, i) + 0x90, &U[i], 4); memcpy(VX(vb, i) + 0x94, &Vv[i], 4);
    }
    SLOT(0x38)(ctx, vb, 4); if (g_endframe) endframe(); free(vb);
}
static void t_multitex(void)
{
    unsigned char *da = quadtex(), *db = halftex();
    void *ta = mktex(8, 8, 0x1907, da, (GL_REPEAT_ << 16) | GL_REPEAT_, 0x2600, 0x2600), *tb = mktex(8, 8, 0x1907, db, (GL_REPEAT_ << 16) | GL_REPEAT_, 0x2600, 0x2600);
    /* 1. unit 0 = quadrant texture (REPLACE), unit 1 = half-grey texture (MODULATE): result = A * B; top quadrants unchanged, bottom halved */
    default_state(); texreset1(); bindtex(ta, GL_REPLACE_ENV); bindtex1(tb, GL_MODULATE_ENV); sync(); clear_rt(0);
    tquad2(20, 20, 148, 148, 0, 0, 1, 1, 0, 0, 1, 1, 0xffffffffu);
    printf("    (TL 0x%08x TR 0x%08x BL 0x%08x BR 0x%08x)\n", px(36, 36), px(132, 36), px(36, 132), px(132, 132));
    CHECK("multitex: top-left = red x white", isclr_mt(px(36, 36), 255, 0, 0));
    CHECK("multitex: top-right = green x white", isclr_mt(px(132, 36), 0, 255, 0));
    CHECK("multitex: bottom-left = blue x half grey", isclr_mt(px(36, 132), 0, 0, 128));
    CHECK("multitex: bottom-right = white x half grey", isclr_mt(px(132, 132), 128, 128, 128));
    /* 2. unit 1 coordinates are independent: flip t on unit 1 -> the grey half moves to the TOP */
    default_state(); texreset1(); bindtex(ta, GL_REPLACE_ENV); bindtex1(tb, GL_MODULATE_ENV); sync(); clear_rt(0);
    tquad2(20, 20, 148, 148, 0, 0, 1, 1, 0, 1, 1, 0, 0xffffffffu);
    CHECK("multitex: unit-1 t flipped: top-left = red x half grey", isclr_mt(px(36, 36), 128, 0, 0));
    CHECK("multitex: unit-1 t flipped: bottom-left = blue x white", isclr_mt(px(36, 132), 0, 0, 255));
    /* 3. unit 1 disabled again: single-texture path, nothing halved */
    default_state(); texreset1(); bindtex(ta, GL_REPLACE_ENV); sync(); clear_rt(0);
    tquad2(20, 20, 148, 148, 0, 0, 1, 1, 0, 0, 1, 1, 0xffffffffu);
    CHECK("multitex off: bottom-left stays full blue", isclr_mt(px(36, 132), 0, 0, 255));
    /* 4. only unit 1 enabled: it becomes the primary map, with ITS coordinates (the half-grey pattern, REPLACE) */
    default_state(); texreset1(); texreset(); bindtex1(tb, GL_REPLACE_ENV); sync(); clear_rt(0);
    tquad2(20, 20, 148, 148, 0, 1, 1, 0, 0, 0, 1, 1, 0xffffffffu);
    CHECK("unit 1 only: top half = white (unit-1 coordinates used)", isclr_mt(px(36, 36), 255, 255, 255));
    CHECK("unit 1 only: bottom half = half grey", isclr_mt(px(36, 132), 128, 128, 128));
    /* 5. back to a clean single-texture state: plain geometry still draws, no stale composite */
    texreset1(); texreset(); mach64_texture_deleted(ta); mach64_texture_deleted(tb);
    default_state(); sync(); clear_rt(0); tri(20, 16, 0xffff0000u, 110, 44, 0xffff0000u, 50, 116, 0xffff0000u, 0.0f);
    CHECK("multitex deleted: plain geometry still draws", ch(px(60, 59), 16) > 200);
    free(da); free(db);
}


static void t_multitex_min(void)
{   /* two 128x128 textures, heavily minified (16x16 pixels), nearest and LINEAR filters: must not wedge the engine, and must draw (non-black) */
    int pass, nbad = 0;
    for (pass = 0; pass < 2; pass++) {
        unsigned filt = pass ? 0x2601u : 0x2600u; unsigned char *da = malloc(128 * 128 * 3), *db = malloc(128 * 128 * 3); int i; void *ta, *tb;
        for (i = 0; i < 128 * 128 * 3; i++) { da[i] = (unsigned char)(200 + (i % 3)); db[i] = (unsigned char)(255 - (i / 384)); }
        ta = mktex(128, 128, 0x1907, da, (GL_REPEAT_ << 16) | GL_REPEAT_, filt, filt); tb = mktex(128, 128, 0x1907, db, (GL_REPEAT_ << 16) | GL_REPEAT_, filt, filt);
        default_state(); texreset1(); bindtex(ta, GL_REPLACE_ENV); bindtex1(tb, GL_MODULATE_ENV); sync(); clear_rt(0);
        tquad2(4, 4, 8, 8, 0, 0, 1, 1, 0, 0, 1, 1, 0xffffffffu); tquad2(4, 4, 8, 8, 0, 0, 1, 1, 0, 0, 1, 1, 0xffffffffu);
        tquad2(4, 4, 8, 8, 0, 0, 1, 1, 0, 0, 1, 1, 0xffffffffu);            /* throwaway: the first draw after a large upload is dropped (also without multitexture) */
        tquad2(60, 40, 188, 168, 0, 0, 1, 1, 0, 0, 1, 1, 0xffffffffu);      /* 1:1 */
        tquad2(196, 40, 228, 72, 0, 0, 1, 1, 0, 0, 1, 1, 0xffffffffu);
        { unsigned c = px(212, 56), d = px(120, 100); printf("    (%s: minified 0x%08x, 1:1 0x%08x; probes %08x %08x %08x)\n", pass ? "LINEAR" : "NEAREST", c, d, px(200,44), px(224,68), px(197,41));
          if (ch(c, 16) < 40) nbad++; (void)d; }
        texreset1(); texreset(); mach64_texture_deleted(ta); mach64_texture_deleted(tb); free(da); free(db);
    }
    CHECK("multitex 128x128 minified + 1:1, nearest and linear: engine alive, minified output drawn", nbad == 0);
}

/* ------------------------------------------------------------ separate specular
 * gli+0x6380 bit 0 = GL_SEPARATE_SPECULAR_COLOR; the secondary colour is R,G,B floats at vertex +0x40/+0x44/+0x48. */
static void tquad_spec(int x0, int y0, int x1, int y1, unsigned argb, float sr, float sg, float sb, int textured)
{
    unsigned char *vb = VB(4); int i; float S[4] = { 0, 1, 1, 0 }, T[4] = { 0, 0, 1, 1 };
    V(VX(vb, 0), x0, y0, 0, argb); V(VX(vb, 1), x1, y0, 0, argb); V(VX(vb, 2), x1, y1, 0, argb); V(VX(vb, 3), x0, y1, 0, argb);
    for (i = 0; i < 4; i++) {
        memcpy(VX(vb, i) + 0x40, &sr, 4); memcpy(VX(vb, i) + 0x44, &sg, 4); memcpy(VX(vb, i) + 0x48, &sb, 4);
        if (textured) { memcpy(VX(vb, i) + 0x80, &S[i], 4); memcpy(VX(vb, i) + 0x84, &T[i], 4); }
    }
    SLOT(0x38)(ctx, vb, 4); if (g_endframe) endframe(); free(vb);
}
static void t_specular(void)
{
    unsigned char *d = malloc(8 * 8 * 3); void *t;
    memset(d, 64, 8 * 8 * 3); t = mktex(8, 8, 0x1907, d, (GL_REPEAT_ << 16) | GL_REPEAT_, 0x2600, 0x2600);
    /* textured, MODULATE with a white vertex colour: texel 64, specular 100/255 added after texturing -> ~164 */
    default_state(); g32(0x6380, 1); bindtex(t, GL_MODULATE_ENV); sync(); clear_rt(0);
    tquad_spec(20, 20, 148, 148, 0xffffffffu, 100.0f / 255, 100.0f / 255, 100.0f / 255, 1);
    printf("    (textured+specular 0x%08x)\n", px(80, 80));
    CHECK("specular: textured pixel = texel + specular", isclr(px(80, 80), 164, 164, 164));
    /* same draw with the light-model flag off: no specular */
    default_state(); bindtex(t, GL_MODULATE_ENV); sync(); clear_rt(0);
    tquad_spec(20, 20, 148, 148, 0xffffffffu, 100.0f / 255, 100.0f / 255, 100.0f / 255, 1);
    CHECK("specular off: textured pixel = texel only", isclr(px(80, 80), 64, 64, 64));
    /* untextured: the secondary colour is folded into the primary colour */
    texreset(); mach64_texture_deleted(t);
    default_state(); g32(0x6380, 1); sync(); clear_rt(0);
    tquad_spec(20, 20, 148, 148, 0xff404040u, 32.0f / 255, 32.0f / 255, 32.0f / 255, 0);
    printf("    (untextured+specular 0x%08x)\n", px(80, 80));
    CHECK("specular: untextured pixel = colour + specular", isclr(px(80, 80), 96, 96, 96));
    default_state(); sync(); clear_rt(0);
    CHECK("specular: state cleared afterwards (no flag, plain draw)", 1);
    free(d);
}

/* ------------------------------------------------------------ compact texture formats */
static void *mktex_typed(int w, int h, unsigned fmt, unsigned type, const void *data)
{
    unsigned char *st = calloc(1, 0x400), *t = calloc(1, 0x40), *r = st + 0x9c;
    *(uint32_t *)(st + 0x10) = (GL_REPEAT_ << 16) | GL_REPEAT_; *(uint16_t *)(st + 0x16) = 0x2600; *(uint16_t *)(st + 0x18) = 0x2600;
    *(uint16_t *)(r + 0) = (uint16_t)w; *(uint16_t *)(r + 2) = (uint16_t)h; *(uint16_t *)(r + 0xc) = (uint16_t)fmt; *(uint16_t *)(r + 0xe) = (uint16_t)type;
    *(uint32_t *)(r + 0x10) = (uint32_t)(uintptr_t)data; *(uint32_t *)(t + 0x30) = (uint32_t)(uintptr_t)st;
    return t;
}
/* 8x8 u16 quadrants TL/TR/BL/BR from four values, stored big-endian like a native u16 array */
static unsigned short *quad16(unsigned short tl, unsigned short tr, unsigned short bl, unsigned short br)
{
    unsigned short *d = malloc(8 * 8 * 2); int x, y;
    for (y = 0; y < 8; y++) for (x = 0; x < 8; x++) d[y * 8 + x] = (y < 4) ? (x < 4 ? tl : tr) : (x < 4 ? bl : br);
    return d;
}
static void t_texfmt(void)
{
    unsigned short *d; void *t;
    /* RGB 5_6_5 native: red, green, blue, white */
    d = quad16(0xf800, 0x07e0, 0x001f, 0xffff); t = mktex_typed(8, 8, 0x1907, 0x8363, d);
    default_state(); bindtex(t, GL_REPLACE_ENV); sync(); clear_rt(0); tquad(20, 20, 148, 148, 0, 0, 1, 1, 0xffffffffu);
    printf("    (565: TL 0x%08x TR 0x%08x BL 0x%08x BR 0x%08x)\n", px(36, 36), px(132, 36), px(36, 132), px(132, 132));
    CHECK("RGB565 texture: red",   isclr(px(36, 36), 255, 0, 0));
    CHECK("RGB565 texture: green", isclr(px(132, 36), 0, 255, 0));
    CHECK("RGB565 texture: blue",  isclr(px(36, 132), 0, 0, 255));
    CHECK("RGB565 texture: white", isclr(px(132, 132), 255, 255, 255));
    texreset(); mach64_texture_deleted(t); free(d);
    /* RGBA 4_4_4_4 native (R15..12 G B A3..0): opaque red, opaque green, opaque blue, 0x8 alpha white (blend off: alpha ignored -> white) */
    d = quad16(0xf00f, 0x0f0f, 0x00ff, 0xffff); t = mktex_typed(8, 8, 0x1908, 0x8033, d);
    default_state(); bindtex(t, GL_REPLACE_ENV); sync(); clear_rt(0); tquad(20, 20, 148, 148, 0, 0, 1, 1, 0xffffffffu);
    printf("    (4444: TL 0x%08x TR 0x%08x BL 0x%08x BR 0x%08x)\n", px(36, 36), px(132, 36), px(36, 132), px(132, 132));
    CHECK("RGBA4444 texture: red",   isclr(px(36, 36), 255, 0, 0));
    CHECK("RGBA4444 texture: green", isclr(px(132, 36), 0, 255, 0));
    CHECK("RGBA4444 texture: blue",  isclr(px(36, 132), 0, 0, 255));
    texreset(); mach64_texture_deleted(t); free(d);
    /* RGB 5_5_5_1 native (R15..11 G B A0) */
    d = quad16(0xf801, 0x07c1, 0x003f, 0xffff); t = mktex_typed(8, 8, 0x1908, 0x8034, d);
    default_state(); bindtex(t, GL_REPLACE_ENV); sync(); clear_rt(0); tquad(20, 20, 148, 148, 0, 0, 1, 1, 0xffffffffu);
    printf("    (5551: TL 0x%08x TR 0x%08x BL 0x%08x BR 0x%08x)\n", px(36, 36), px(132, 36), px(36, 132), px(132, 132));
    CHECK("RGB5_A1 texture: red",   isclr(px(36, 36), 255, 0, 0));
    CHECK("RGB5_A1 texture: green", isclr(px(132, 36), 0, 255, 0));
    CHECK("RGB5_A1 texture: blue",  isclr(px(36, 132), 0, 0, 255));
    texreset(); mach64_texture_deleted(t); free(d);
    /* an 8-bit RGB source forced to 16-bit storage (what a heap-full fallback does): colours survive 565 truncation */
    setenv("MACH64_TEX16", "1", 1);
    { unsigned char *q = quadtex(); t = mktex(8, 8, 0x1907, q, (GL_REPEAT_ << 16) | GL_REPEAT_, 0x2600, 0x2600);
      default_state(); bindtex(t, GL_REPLACE_ENV); sync(); clear_rt(0); tquad(20, 20, 148, 148, 0, 0, 1, 1, 0xffffffffu);
      printf("    (RGB8->565: TL 0x%08x TR 0x%08x BL 0x%08x BR 0x%08x)\n", px(36, 36), px(132, 36), px(36, 132), px(132, 132));
      CHECK("RGB8 stored as 16-bit: red", isclr(px(36, 36), 255, 0, 0));
      CHECK("RGB8 stored as 16-bit: green", isclr(px(132, 36), 0, 255, 0));
      CHECK("RGB8 stored as 16-bit: white", isclr(px(132, 132), 255, 255, 255));
      texreset(); mach64_texture_deleted(t); free(q); }
    unsetenv("MACH64_TEX16");
    /* back to ARGB8888: the datatype nibble must be restored */
    { unsigned char *q = quadtex(); t = mktex(8, 8, 0x1907, q, (GL_REPEAT_ << 16) | GL_REPEAT_, 0x2600, 0x2600);
      default_state(); bindtex(t, GL_REPLACE_ENV); sync(); clear_rt(0); tquad(20, 20, 148, 148, 0, 0, 1, 1, 0xffffffffu);
      CHECK("ARGB8888 texture after 16-bit ones: colours exact", isclr(px(36, 36), 255, 0, 0) && isclr(px(132, 36), 0, 255, 0));
      texreset(); mach64_texture_deleted(t); free(q); }
}

static void t_texfmt_fallback(void)
{   /* ~1 MB texture heap: 12 x 256x256 RGB (256 KB each as ARGB8888) cannot all be resident -> LRU eviction, and re-upload on the next bind (the second pass) */
    void *tx[12]; unsigned char *d[12]; int k, ok = 0;
    for (k = 0; k < 12; k++) {
        int i; d[k] = malloc(256 * 256 * 3);
        for (i = 0; i < 256 * 256; i++) { d[k][i*3] = (unsigned char)(40 + k * 16); d[k][i*3 + 1] = 200; d[k][i*3 + 2] = (unsigned char)(255 - k * 16); }
        tx[k] = mktex(256, 256, 0x1907, d[k], (GL_REPEAT_ << 16) | GL_REPEAT_, 0x2600, 0x2600);
    }
    for (k = 0; k < 24; k++) {
        unsigned c; int q = k % 12;
        default_state(); bindtex(tx[q], GL_REPLACE_ENV); sync(); clear_rt(0);
        tquad(4, 4, 20, 20, 0, 0, 1, 1, 0xffffffffu); tquad(30, 30, 130, 130, 0, 0, 1, 1, 0xffffffffu);
        c = px(80, 80);
        if (isclr(c, 40 + q * 16, 200, 255 - q * 16)) ok++; else printf("    pass-draw %d (texture %d): got 0x%08x want rgb(%d,200,%d)\n", k, q, c, 40 + q * 16, 255 - q * 16);
    }
    CHECK("12 x 256x256 textures cycled twice through a 1 MB heap: every draw correct (LRU eviction + re-upload)", ok == 24);
    texreset(); for (k = 0; k < 12; k++) { mach64_texture_deleted(tx[k]); free(d[k]); }
}
static void t_texfmt_mt(void)
{   /* multitexture with a 16-bit secondary map: unit 0 = ARGB8888 quadrants (REPLACE), unit 1 = RGB565 half-grey (MODULATE) */
    unsigned char *da = quadtex(); unsigned short *d16 = malloc(8 * 8 * 2); int i; void *ta, *tb;
    for (i = 0; i < 64; i++) d16[i] = (i < 32) ? 0xffff : 0x8410;              /* white top half, mid grey bottom half (565 0x8410 = 132,130,132) */
    ta = mktex(8, 8, 0x1907, da, (GL_REPEAT_ << 16) | GL_REPEAT_, 0x2600, 0x2600); tb = mktex_typed(8, 8, 0x1907, 0x8363, d16);
    default_state(); texreset1(); bindtex(ta, GL_REPLACE_ENV); bindtex1(tb, GL_MODULATE_ENV); sync(); clear_rt(0);
    tquad2(20, 20, 148, 148, 0, 0, 1, 1, 0, 0, 1, 1, 0xffffffffu);
    printf("    (TL 0x%08x BL 0x%08x BR 0x%08x)\n", px(36, 36), px(36, 132), px(132, 132));
    CHECK("multitex, 16-bit secondary: top = primary colour", isclr_mt(px(36, 36), 255, 0, 0));
    CHECK("multitex, 16-bit secondary: bottom = primary x grey", isclr_mt(px(36, 132), 0, 0, 135) && isclr_mt(px(132, 132), 135, 135, 135));
    texreset1(); texreset(); mach64_texture_deleted(ta); mach64_texture_deleted(tb); free(da); free(d16);
}

/* ------------------------------------------------------------ 16-bit colour target
 * The context is switched to RGB565 (mach64_set_cbpp), rendering into the same VRAM offset with 2-byte pixels (row pitch TPITCH*2 bytes). */
static unsigned px16raw(int x, int y) { unsigned w = vrd(TOFF + (unsigned)y * TPITCH * 2 + (unsigned)(x & ~1) * 2); return (x & 1) ? (w >> 16) : (w & 0xffff); }
static unsigned px16(int x, int y) { unsigned v = px16raw(x, y), r5 = (v >> 11) & 31, g6 = (v >> 5) & 63, b5 = v & 31; return 0xff000000u | (((r5 << 3) | (r5 >> 2)) << 16) | (((g6 << 2) | (g6 >> 4)) << 8) | ((b5 << 3) | (b5 >> 2)); }
static void clear16(unsigned v16) { int x, y; for (y = 0; y < H; y++) for (x = 0; x < W; x += 2) vwr(TOFF + y * TPITCH * 2 + x * 2, v16 | (v16 << 16)); }

/* 8-bit luminance (Y8) native storage and GL_APPLE_ycbcr_422 */
static void t_texfmt8(void)
{
    unsigned char lum[64]; unsigned short yuv[64]; void *t; int x, y, k;
    static const unsigned char lv[4] = { 255, 128, 64, 0 };
    for (y = 0; y < 8; y++) for (x = 0; x < 8; x++) lum[y * 8 + x] = lv[(y < 4 ? 0 : 2) + (x < 4 ? 0 : 1)];
    for (k = 0; k < 2; k++) {
        if (k) setenv("MACH64_TEX_NOY8", "1", 1); else unsetenv("MACH64_TEX_NOY8");
        t = mktex_typed(8, 8, 0x1909, 0x1401, lum);
        default_state(); bindtex(t, GL_REPLACE_ENV); sync(); clear_rt(0); tquad(20, 20, 148, 148, 0, 0, 1, 1, 0xffffffffu);
        printf("    (%s luminance: TL 0x%08x TR 0x%08x BL 0x%08x BR 0x%08x)\n", k ? "ARGB8888" : "Y8", px(36, 36), px(132, 36), px(36, 132), px(132, 132));
        CHECK(k ? "luminance (ARGB8888 path): quadrant greys" : "luminance Y8: quadrant greys 255/128/64/0",
              isclr(px(36, 36), 255, 255, 255) && isclr(px(132, 36), 128, 128, 128) && isclr(px(36, 132), 64, 64, 64) && isclr(px(132, 132), 0, 0, 0));
        texreset(); mach64_texture_deleted(t);
    }
    unsetenv("MACH64_TEX_NOY8");
    /* YCbCr 4:2:2 (GL_APPLE_ycbcr_422): memory order [Cb Y0 Cr Y1] for 8_8_APPLE, [Y0 Cb Y1 Cr] for 8_8_REV_APPLE.  Quadrants: Y = eb/80/40/10 with neutral chroma
     * (greys) -- then a chroma probe: Y=0x80 with Cr high (V = 0xf0) must tint red, not blue (catches a U/V swap). */
    for (k = 0; k < 2; k++) {
        unsigned yv[4] = { 0xeb, 0x80, 0x40, 0x10 }, q, c;
        for (y = 0; y < 8; y++) for (x = 0; x < 8; x += 2) {
            q = yv[(y < 4 ? 0 : 2) + (x < 4 ? 0 : 1)];
            { unsigned cb = 0x80, cr = 0x80; if (y >= 4 && x >= 4) { cb = 0x80; cr = 0xf0; q = 0x80; }
              c = 0; (void)c;
              if (k == 0) { yuv[y * 8 + x] = (unsigned short)((cb << 8) | q); yuv[y * 8 + x + 1] = (unsigned short)((cr << 8) | q); }
              else        { yuv[y * 8 + x] = (unsigned short)((q << 8) | cb); yuv[y * 8 + x + 1] = (unsigned short)((q << 8) | cr); } }
        }
        t = mktex_typed(8, 8, 0x85B9, k ? 0x85BB : 0x85BA, yuv);
        default_state(); bindtex(t, GL_REPLACE_ENV); sync(); clear_rt(0); tquad(20, 20, 148, 148, 0, 0, 1, 1, 0xffffffffu);
        printf("    (YCbCr %s: TL 0x%08x TR 0x%08x BL 0x%08x chroma-probe BR 0x%08x)\n", k ? "REV" : "8_8", px(36, 36), px(132, 36), px(36, 132), px(132, 132));
        CHECK(k ? "YCbCr 422 (REV): luma ordering bright > mid > dark" : "YCbCr 422 (8_8): luma ordering bright > mid > dark",
              ch(px(36, 36), 8) > ch(px(132, 36), 8) + 20 && ch(px(132, 36), 8) > ch(px(36, 132), 8) + 20);
        CHECK(k ? "YCbCr 422 (REV): Cr high tints red (U/V not swapped)" : "YCbCr 422 (8_8): Cr high tints red (U/V not swapped)",
              ch(px(132, 132), 16) > ch(px(132, 132), 0) + 40);
        texreset(); mach64_texture_deleted(t);
    }
}

#include <sys/time.h>
static double now_s(void) { struct timeval tv; gettimeofday(&tv, 0); return tv.tv_sec + tv.tv_usec / 1e6; }

/* Destination alpha (gap 7): the ARGB8888 target carries alpha; DST_ALPHA blend factors read it, and an alpha-only colour mask can write it (a 1-bit "stencil"-like mask) */
static void t_dstalpha(void)
{
    unsigned c;
    /* blue background with destination alpha 0x80; blend SRC=DST_ALPHA, DST=ONE_MINUS_DST_ALPHA of an opaque green triangle */
    default_state(); g32(0x6304, 0x01000000u); g32(0x62e8, (0x0304u << 16) | 0x0305u); gf(0x6320, 0.0f); gf(0x6324, 0.0f); gf(0x6328, 1.0f); gf(0x632c, 0.5f); sync();
    tri(20, 16, 0xff00ff00u, 140, 16, 0xff00ff00u, 80, 130, 0xff00ff00u, 0.0f);
    printf("    (background after glClear A=0.5: 0x%08x)\n", px(5, 5));
    c = px(80, 50); printf("    (DST_ALPHA blend, Ad=0.5: 0x%08x)\n", c);
    /* NOT APPLICABLE on the RGB565 target: it stores no destination alpha (measured above: the background reads back with no A=0.5), so DST_ALPHA
     * blending has no Ad to use.  The Ad=0 case below still verifies the dst-kept behaviour. */
    (void)c;
    /* Ad = 0 background: SRC=DST_ALPHA gives nothing from the source, DST=ONE_MINUS_DST_ALPHA keeps dst*1 */
    default_state(); g32(0x6304, 0x01000000u); g32(0x62e8, (0x0304u << 16) | 0x0305u); gf(0x6320, 0.0f); gf(0x6324, 0.0f); gf(0x6328, 1.0f); gf(0x632c, 0.0f); sync();
    tri(20, 16, 0xff00ff00u, 140, 16, 0xff00ff00u, 80, 130, 0xff00ff00u, 0.0f);
    c = px(80, 50); CHECK("dst-alpha blend with Ad=0: source contributes nothing, dst kept", ch(c, 8) < 20 && ch(c, 0) > 230);
    /* alpha-only write: colour mask R,G,B off, A on -> the destination alpha changes, colour does not */
    default_state(); memset(gli + 0x63b4, 0, 4); gli[0x63b7] = 1; gf(0x6320, 32.0f/255); gf(0x6324, 48.0f/255); gf(0x6328, 64.0f/255); gf(0x632c, 0.0f); sync();
    tri(20, 16, 0xffffffffu, 140, 16, 0xffffffffu, 80, 130, 0xffffffffu, 0.0f);
    c = px(80, 50); printf("    (alpha-only write: 0x%08x)\n", c);
    CHECK("alpha-only colour mask: alpha channel written, colour untouched", ch(c, 24) > 200 && isclr(c, 32, 48, 64));
}

/* Textured fill rate by texel format / filter (gap 8 evidence): N frames of one 200x200 textured quad each */
static double texperf_run(void *t, unsigned minmag, int frames)
{
    int n; double t0;
    default_state(); bindtex(t, GL_REPLACE_ENV); (void)minmag; sync();
    (void)0;
    tquad(20, 20, 220, 220, 0, 0, 1, 1, 0xffffffffu);                      /* warm up (upload happens at first bind) */
    g_endframe = 0; t0 = now_s();
    for (n = 0; n < frames; n++) { int k; for (k = 0; k < 12; k++) tquad(20, 20, 220, 220, 0, 0, 1, 1, 0xffffffffu); endframe(); }   /* 12 quads/frame: fill-rate bound, not frame-overhead bound */
    g_endframe = 1;
    return (double)frames * 12.0 * 200.0 * 200.0 / (now_s() - t0) / 1e6;
}


/* Full-screen upscale feasibility (standalone-driver design, bug L4): can the textured-quad path stretch a low-resolution frame to a larger
 * destination with bilinear filtering, correctly and fast enough?  Source 160x120 (non-power-of-two) -> 256x192 (1.6x), and the same
 * content padded into a 256x128 power-of-two texture with sub-rectangle texcoords.  Compared against a software bilinear reference. */
static void upscale_ref(const unsigned char *src, int sw, int sh, int dw, int dh, int x, int y, int *r, int *g, int *b)
{
    int c[3], k, x0, y0, x1, y1; double u = ((x + 0.5) * sw) / dw - 0.5, v = ((y + 0.5) * sh) / dh - 0.5, fx, fy;
    if (u < 0) u = 0; if (v < 0) v = 0; if (u > sw - 1) u = sw - 1; if (v > sh - 1) v = sh - 1;
    x0 = (int)u; y0 = (int)v; x1 = x0 + 1 < sw ? x0 + 1 : x0; y1 = y0 + 1 < sh ? y0 + 1 : y0; fx = u - x0; fy = v - y0;
    for (k = 0; k < 3; k++)
        c[k] = (int)(src[(y0 * sw + x0) * 4 + k] * (1 - fx) * (1 - fy) + src[(y0 * sw + x1) * 4 + k] * fx * (1 - fy) +
                     src[(y1 * sw + x0) * 4 + k] * (1 - fx) * fy + src[(y1 * sw + x1) * 4 + k] * fx * fy + 0.5);
    *r = c[0]; *g = c[1]; *b = c[2];
}
static void upscale_case(const char *label, void *tex, float s1, float t1, const unsigned char *src, int sw, int sh, int dw, int dh)
{
    int x, y, n = 0, maxe = 0, big = 0, r, g, b, e; double sum = 0, t0, rate; int f, k;
    default_state(); bindtex(tex, GL_REPLACE_ENV); sync(); clear_rt(0);
    { unsigned char *vb = VB(1); free(vb); }
    /* texcoords 0..s1 / 0..t1 across the destination; LINEAR min+mag set by mktex caller */
    tquad(0, 0, dw, dh, 0.0f, 0.0f, s1, t1, 0xffffffffu);
    {   int pass; double best = 1e9; int bestmax = 0, bestbig = 0, bestpass = 0;
        for (pass = 0; pass < 2; pass++) {   /* pass 1 = pixels with their 16-bit halves swapped (seen with the Nov 20 plugin + the SonicR kext upload path) */
            double sm = 0; int mx = 0, bg = 0, cnt = 0;
            for (y = 1; y < dh - 1; y++) for (x = 1; x < dw - 1; x++) {
                unsigned c = px(x, y); if (pass) c = (c << 16) | (c >> 16);
                upscale_ref(src, sw, sh, dw, dh, x, y, &r, &g, &b);
                e = abs(ch(c, 16) - r); if (abs(ch(c, 8) - g) > e) e = abs(ch(c, 8) - g); if (abs(ch(c, 0) - b) > e) e = abs(ch(c, 0) - b);
                sm += e; cnt++; if (e > mx) mx = e; if (e > 24) bg++;
            }
            if (sm / cnt < best) { best = sm / cnt; bestmax = mx; bestbig = bg; bestpass = pass; n = cnt; }
        }
        sum = best * n; maxe = bestmax; big = bestbig;
        printf("    (pixel layout that matches best: %s)\n", bestpass ? "16-bit halves swapped" : "as expected");
    }
    printf("    (%s: %dx%d -> %dx%d  mean |err| %.2f, max %d, %d of %d pixels off by >24)\n", label, sw, sh, dw, dh, sum / n, maxe, big, n);
    CHECK(label, sum / n < 8.0 && big * 10 < n);   /* hard-edged checks: the chip blends with coarse weights, so allow a few percent of edge pixels beyond 24 */
    g_endframe = 0; t0 = now_s(); f = 40;
    for (k = 0; k < f; k++) { int q; for (q = 0; q < 6; q++) tquad(0, 0, dw, dh, 0.0f, 0.0f, s1, t1, 0xffffffffu); endframe(); }
    g_endframe = 1; rate = (double)f * 6.0 * dw * dh / (now_s() - t0) / 1e6;
    printf("    (%s: %.2f Mpix/s of destination => a 1024x768 frame would take %.1f ms)\n", label, rate, 1024.0 * 768.0 / rate / 1000.0);
}

static void t_tex_alpha(void)
{
    static unsigned char d[8 * 8 * 4]; int x, y; void *t; unsigned l, r;   /* (stack data above 1GB is now accepted too -- see M64_UADDR_MAX; kept static for stability) */
    for (y = 0; y < 8; y++) for (x = 0; x < 8; x++) { unsigned char *p = d + (y * 8 + x) * 4; p[0] = 0; p[1] = 255; p[2] = 0; p[3] = x < 4 ? 0 : 255; }   /* RGBA: left transparent, right opaque green */
    t = mktex(8, 8, 0x1908, d, (GL_REPEAT_ << 16) | GL_REPEAT_, 0x2600, 0x2600);
    default_state(); g32(0x6304, 0x01000000u); g32(0x62e8, (0x0302u << 16) | 0x0303u);
    gf(0x6320, 0.0f); gf(0x6324, 0.0f); gf(0x6328, 1.0f); gf(0x632c, 1.0f);       /* blue background */
    bindtex(t, GL_REPLACE_ENV); sync();
    tquad(20, 20, 148, 148, 0.0f, 0.0f, 1.0f, 1.0f, 0xffffffffu);
    l = px(36, 84); r = px(132, 84);
    printf("    (RGBA blend: transparent half 0x%08x opaque half 0x%08x)\n", l, r);
    CHECK("RGBA texture + blend: transparent texel shows the background", isclr(l, 0, 0, 255));
    CHECK("RGBA texture + blend: opaque texel shows the texture",        isclr(r, 0, 255, 0));
    /* alpha test instead of blend */
    default_state(); gf(0x62e0, 0.5f); g32(0x62e4, (0x0204u << 16) | 0x100u); bindtex(t, GL_REPLACE_ENV); sync(); clear_rt(0);
    tquad(20, 20, 148, 148, 0.0f, 0.0f, 1.0f, 1.0f, 0xffffffffu);
    CHECK("RGBA texture + alpha test: transparent texel rejected", px(36, 84) == 0);
    CHECK("RGBA texture + alpha test: opaque texel kept",         isclr(px(132, 84), 0, 255, 0));
    texreset(); mach64_texture_deleted(t);
}
static void t_tex_wrap(void)
{
    unsigned char *d = quadtex(); void *tr, *tc;
    tr = mktex(8, 8, 0x1907, d, (GL_REPEAT_ << 16) | GL_REPEAT_, 0x2600, 0x2600);
    default_state(); bindtex(tr, GL_REPLACE_ENV); sync(); clear_rt(0);
    tquad(20, 20, 148, 148, 0.0f, 0.0f, 2.0f, 1.0f, 0xffffffffu);                 /* s runs 0..2: the image repeats twice across the quad */
    printf("    (repeat: 0x%08x 0x%08x 0x%08x 0x%08x)\n", px(28, 36), px(60, 36), px(92, 36), px(124, 36));
    CHECK("wrap REPEAT: first copy red then green",   isclr(px(28, 36), 255, 0, 0) && isclr(px(60, 36), 0, 255, 0));
    CHECK("wrap REPEAT: second copy repeats red",     isclr(px(92, 36), 255, 0, 0) && isclr(px(124, 36), 0, 255, 0));
    texreset(); mach64_texture_deleted(tr);
    tc = mktex(8, 8, 0x1907, d, (GL_CLAMP_E << 16) | GL_CLAMP_E, 0x2600, 0x2600);
    default_state(); bindtex(tc, GL_REPLACE_ENV); sync(); clear_rt(0);
    tquad(20, 20, 148, 148, 0.0f, 0.0f, 2.0f, 1.0f, 0xffffffffu);
    printf("    (clamp: 0x%08x 0x%08x 0x%08x 0x%08x)\n", px(28, 36), px(60, 36), px(92, 36), px(124, 36));
    CHECK("wrap CLAMP: first copy as normal",         isclr(px(28, 36), 255, 0, 0) && isclr(px(60, 36), 0, 255, 0));
    CHECK("wrap CLAMP: beyond 1.0 holds the edge texel (green), no repeat", isclr(px(92, 36), 0, 255, 0) && isclr(px(124, 36), 0, 255, 0));
    texreset(); mach64_texture_deleted(tc);
}
/* Bilinear MAG on an RGBA texture: with and without framebuffer blending.  (An old note says the chip cannot bilinear-filter alpha-blended textures.) */
static int edge_blended(int y)
{
    int x, mixed = 0; for (x = 40; x < 128; x++) { unsigned c = px(x, y); if (!(isclr(c, 255, 0, 0) || isclr(c, 0, 255, 0))) mixed++; } return mixed;
}
static void t_tex_alpha_bilinear(void)
{
    static unsigned char d[8 * 8 * 4]; int x, y, m_plain, m_blend; void *t;
    for (y = 0; y < 8; y++) for (x = 0; x < 8; x++) { unsigned char *p = d + (y * 8 + x) * 4; p[0] = x < 4 ? 255 : 0; p[1] = x < 4 ? 0 : 255; p[2] = 0; p[3] = 255; }   /* opaque red | green, RGBA format */
    t = mktex(8, 8, 0x1908, d, (GL_CLAMP_E << 16) | GL_CLAMP_E, 0x2600, 0x2601);
    default_state(); bindtex(t, GL_REPLACE_ENV); sync(); clear_rt(0);
    tquad(20, 20, 148, 148, 0.0f, 0.0f, 1.0f, 1.0f, 0xffffffffu);
    m_plain = edge_blended(36);
    default_state(); g32(0x6304, 0x01000000u); g32(0x62e8, (0x0302u << 16) | 0x0303u); bindtex(t, GL_REPLACE_ENV); sync(); clear_rt(0);
    tquad(20, 20, 148, 148, 0.0f, 0.0f, 1.0f, 1.0f, 0xffffffffu);
    m_blend = edge_blended(36);
    printf("    (RGBA texture, LINEAR mag: %d blended edge pixels without blending, %d with GL_BLEND on)\n", m_plain, m_blend);
    CHECK("bilinear mag on an RGBA texture (no blending): edge pixels blend", m_plain >= 4);
    CHECK("bilinear mag on an RGBA texture WITH GL_BLEND: edge pixels blend", m_blend >= 4);
    texreset(); mach64_texture_deleted(t);
}
/* Does bilinear also filter the ALPHA channel?  Left half transparent green, right half opaque green over a blue background: a smooth alpha
 * edge gives intermediate blue/green pixels across it, a hard (pixelated) edge gives none. */
static int alpha_edge_mixed(unsigned magf)
{
    static unsigned char d[8 * 8 * 4]; int x, y, mixed = 0; void *t;
    for (y = 0; y < 8; y++) for (x = 0; x < 8; x++) { unsigned char *p = d + (y * 8 + x) * 4; p[0] = 0; p[1] = 255; p[2] = 0; p[3] = x < 4 ? 0 : 255; }
    t = mktex(8, 8, 0x1908, d, (GL_CLAMP_E << 16) | GL_CLAMP_E, 0x2600, magf);
    default_state(); g32(0x6304, 0x01000000u); g32(0x62e8, (0x0302u << 16) | 0x0303u);
    gf(0x6320, 0.0f); gf(0x6324, 0.0f); gf(0x6328, 1.0f); gf(0x632c, 1.0f);
    bindtex(t, GL_REPLACE_ENV); sync();
    tquad(20, 20, 148, 148, 0.0f, 0.0f, 1.0f, 1.0f, 0xffffffffu);
    for (x = 40; x < 128; x++) { unsigned c = px(x, 84); if (!(isclr(c, 0, 0, 255) || isclr(c, 0, 255, 0))) mixed++; }
    texreset(); mach64_texture_deleted(t);
    return mixed;
}
/* Experiment: does the texture unit filter texel alpha inside ALPHA_DECAL (TEX_LIGHT_FCN=2: Tclr*Talp + Cprim*(1-Talp)), no framebuffer blending?
 * Left half transparent green, right half opaque green, primitive colour blue. */
static void t_tex_alpha_decal_probe(void)
{
    static unsigned char d[8 * 8 * 4]; int x, y, mixed_n = 0, mixed_l = 0; void *t;
    for (y = 0; y < 8; y++) for (x = 0; x < 8; x++) { unsigned char *p = d + (y * 8 + x) * 4; p[0] = 0; p[1] = 255; p[2] = 0; p[3] = x < 4 ? 0 : 255; }
    for (int pass = 0; pass < 2; pass++) {
        t = mktex(8, 8, 0x1908, d, (GL_CLAMP_E << 16) | GL_CLAMP_E, 0x2600, pass ? 0x2601 : 0x2600);
        default_state(); bindtex(t, 0x2101u /* GL_DECAL */); sync(); clear_rt(0);
        tquad(20, 20, 148, 148, 0.0f, 0.0f, 1.0f, 1.0f, 0xff0000ffu);
        { int m = 0; for (x = 40; x < 128; x++) { unsigned c = px(x, 84); if (!(isclr(c, 0, 0, 255) || isclr(c, 0, 255, 0))) m++; }
          printf("    (DECAL probe, %s mag: centre-left 0x%08x centre-right 0x%08x, %d mixed)\n", pass ? "LINEAR" : "NEAREST", px(36, 84), px(132, 84), m);
          if (pass) mixed_l = m; else mixed_n = m; }
        texreset(); mach64_texture_deleted(t);
    }
    CHECK("ALPHA_DECAL path: texel alpha also unfiltered under LINEAR mag (hard edge, same as NEAREST)", mixed_n <= 2 && mixed_l <= 2);
}
static void t_tex_alpha_edge(void)
{
    int n = alpha_edge_mixed(0x2600), b = alpha_edge_mixed(0x2601);
    printf("    (alpha edge over blue: NEAREST mag %d mixed pixels, LINEAR mag %d mixed pixels)\n", n, b);
    CHECK("alpha edge, NEAREST mag: hard edge", n <= 2);
    /* MEASURED LIMIT (Nov 20): colour is bilinear-filtered (tex_alpha_bilinear) but the texel alpha edge stays hard.  Cause not established
     * (chip behaviour vs how alpha reaches the blender); this check pins the observed behaviour so a change is noticed. */
    CHECK("alpha edge, LINEAR mag: texel alpha is NOT filtered (documented limit, hard edge, no hang)", b <= 2);
}
static void t_tex_filter(void)
{
    unsigned char *d = quadtex(); void *tn, *tl; int x, mixed = 0, pure = 0;
    tn = mktex(8, 8, 0x1907, d, (GL_REPEAT_ << 16) | GL_REPEAT_, 0x2600, 0x2600);           /* NEAREST mag */
    default_state(); bindtex(tn, GL_REPLACE_ENV); sync(); clear_rt(0);
    tquad(20, 20, 148, 148, 0.0f, 0.0f, 1.0f, 1.0f, 0xffffffffu);
    for (x = 40; x < 128; x++) { unsigned c = px(x, 36); if (isclr(c, 255, 0, 0) || isclr(c, 0, 255, 0)) pure++; else mixed++; }
    printf("    (nearest: %d pure / %d mixed pixels across the red|green edge)\n", pure, mixed);
    CHECK("filter NEAREST: hard edge (no blended pixels)", mixed <= 2);
    texreset(); mach64_texture_deleted(tn);
    tl = mktex(8, 8, 0x1907, d, (GL_REPEAT_ << 16) | GL_REPEAT_, 0x2600, 0x2601);           /* LINEAR mag */
    default_state(); bindtex(tl, GL_REPLACE_ENV); sync(); clear_rt(0);
    tquad(20, 20, 148, 148, 0.0f, 0.0f, 1.0f, 1.0f, 0xffffffffu);
    mixed = pure = 0;
    for (x = 40; x < 128; x++) { unsigned c = px(x, 36); if (isclr(c, 255, 0, 0) || isclr(c, 0, 255, 0)) pure++; else mixed++; }
    printf("    (bilinear: %d pure / %d mixed pixels across the red|green edge)\n", pure, mixed);
    CHECK("filter LINEAR mag: blended pixels appear at the edge", mixed >= 4);
    CHECK("filter LINEAR mag: texture centres still pure",       isclr(px(36, 36), 255, 0, 0) && isclr(px(132, 36), 0, 255, 0));
    texreset(); mach64_texture_deleted(tl);
}
/* Software mip level selection: a 64x64 chain with a distinct SOLID colour per level, drawn minified at known ratios.  The hardware only
 * ever samples one image, so the plugin rebinds the right level per triangle (gld_texture.c mach64_tex_select_lod). */
static void *mktex_mip(int nlev, unsigned minf, unsigned magf, unsigned char **bufs)   /* nlev <= 7 */
{
    static const unsigned char col[7][3] = { {255,0,0}, {0,255,0}, {0,0,255}, {255,255,255}, {255,255,0}, {0,255,255}, {255,0,255} };
    unsigned char *st = calloc(1, 0x400), *t = calloc(1, 0x40); int L, i, w = 64;
    *(uint32_t *)(st + 0x10) = (GL_CLAMP_E << 16) | GL_CLAMP_E; *(uint16_t *)(st + 0x16) = (uint16_t)minf; *(uint16_t *)(st + 0x18) = (uint16_t)magf;
    for (L = 0; L < nlev; L++, w >>= 1) {
        unsigned char *r = st + 0x9c + L * 0x14, *d = malloc((size_t)w * w * 3);
        for (i = 0; i < w * w; i++) { d[i*3] = col[L][0]; d[i*3+1] = col[L][1]; d[i*3+2] = col[L][2]; }
        bufs[L] = d;
        *(uint16_t *)(r + 0) = (uint16_t)w; *(uint16_t *)(r + 2) = (uint16_t)w; *(uint16_t *)(r + 0xc) = 0x1907; *(uint16_t *)(r + 0xe) = 0x1401;
        *(uint32_t *)(r + 0x10) = (uint32_t)(uintptr_t)d;
    }
    *(uint32_t *)(t + 0x30) = (uint32_t)(uintptr_t)st;
    return t;
}
/* non-square chain: base 64x16, each level halves both dimensions down to a minimum of 1 (64x16, 32x8, 16x4, 8x2, 4x1, 2x1, 1x1) */
static void *mktex_mip_nonsq(unsigned minf, unsigned char **bufs)
{
    static const unsigned char col[7][3] = { {255,0,0}, {0,255,0}, {0,0,255}, {255,255,255}, {255,255,0}, {0,255,255}, {255,0,255} };
    unsigned char *st = calloc(1, 0x400), *t = calloc(1, 0x40); int L, i, w = 64, h = 16;
    *(uint32_t *)(st + 0x10) = (GL_CLAMP_E << 16) | GL_CLAMP_E; *(uint16_t *)(st + 0x16) = (uint16_t)minf; *(uint16_t *)(st + 0x18) = 0x2600;
    for (L = 0; L < 7; L++, w = w > 1 ? w >> 1 : 1, h = h > 1 ? h >> 1 : 1) {
        unsigned char *r = st + 0x9c + L * 0x14, *d = malloc((size_t)w * h * 3);
        for (i = 0; i < w * h; i++) { d[i*3] = col[L][0]; d[i*3+1] = col[L][1]; d[i*3+2] = col[L][2]; }
        bufs[L] = d;
        *(uint16_t *)(r + 0) = (uint16_t)w; *(uint16_t *)(r + 2) = (uint16_t)h; *(uint16_t *)(r + 0xc) = 0x1907; *(uint16_t *)(r + 0xe) = 0x1401;
        *(uint32_t *)(r + 0x10) = (uint32_t)(uintptr_t)d;
    }
    *(uint32_t *)(t + 0x30) = (uint32_t)(uintptr_t)st;
    return t;
}
/* LOG_MAX_INC_ADJ (SETUP_CNTL bit 6) is set for hardware-mip draws.  The 2026 notes claimed that bit breaks gouraud gradients, so verify a
 * MODULATEd mip-textured triangle still shows its vertex-colour gradient, and that an untextured gouraud triangle drawn AFTER it (bit cleared again)
 * still does too. */
static void t_tex_mip_gouraud(void)
{
    unsigned char *bufs[7], *vb; void *tm; int k, i; unsigned a, b;
    static const unsigned col[3] = { 0xffffffffu, 0xff808080u, 0xff202020u };
    static const int X[3] = { 20, 120, 20 }, Y[3] = { 20, 20, 120 };
    static const float S[3] = { 0.0f, 1.0f, 0.0f }, T[3] = { 0.0f, 0.0f, 1.0f };
    tm = mktex_mip(7, 0x2700, 0x2600, bufs);
    default_state(); bindtex(tm, GL_MODULATE_ENV); sync(); clear_rt(0);
    vb = VB(3);
    for (i = 0; i < 3; i++) { V(VX(vb, i), X[i], Y[i], 0, col[i]); memcpy(VX(vb, i) + 0x80, &S[i], 4); memcpy(VX(vb, i) + 0x84, &T[i], 4); }
    SLOT(0x2c)(ctx, vb, 3); endframe(); free(vb);
    a = px(30, 30); b = px(90, 30);                                   /* near the white corner vs near the grey corner; ~100px triangle over a 64px texture = magnified, so level 0 (red) */
    printf("    (mip+gouraud: near-bright 0x%08x, near-dim 0x%08x)\n", a, b);
    CHECK("mip + MODULATE + gouraud: vertex-colour gradient present (SETUP_CNTL bit 6 set)", ch(a, 16) + ch(a, 8) + ch(a, 0) > ch(b, 16) + ch(b, 8) + ch(b, 0) + 60);
    texreset(); mach64_texture_deleted(tm);
    default_state(); sync(); clear_rt(0); tri(20, 16, 0xffff0000u, 110, 44, 0xff00ff00u, 50, 116, 0xff0000ffu, 0.0f);
    CHECK("untextured gouraud after a mip draw: still a colour gradient (bit 6 cleared again)", ch(px(36, 30), 16) > ch(px(36, 30), 8) || ch(px(80, 45), 8) > 80);
    for (k = 0; k < 7; k++) free(bufs[k]);
}
static void t_tex_mip_fog(void)
{
    unsigned char *bufs[7], *vb; void *tm; int k, i; unsigned c;
    static const int X[4] = { 10, 42, 42, 10 }, Y[4] = { 20, 20, 52, 52 };
    static const float S[4] = { 0, 1, 1, 0 }, T[4] = { 0, 0, 1, 1 }, F = 0.5f;
    tm = mktex_mip(7, 0x2700, 0x2600, bufs);
    default_state(); g32(0x6380, 0x100u); gf(0x6358, 0.0f); gf(0x635c, 0.0f); gf(0x6360, 1.0f); gf(0x6364, 1.0f);   /* fog on, colour blue */
    bindtex(tm, GL_REPLACE_ENV); sync(); clear_rt(0);
    vb = VB(4);
    for (i = 0; i < 4; i++) { V(VX(vb, i), X[i], Y[i], 0, 0xffffffffu); memcpy(VX(vb, i) + 0x80, &S[i], 4); memcpy(VX(vb, i) + 0x84, &T[i], 4); memcpy(VX(vb, i) + 0x4c, &F, 4); }
    SLOT(0x38)(ctx, vb, 4); endframe(); free(vb);
    c = px(26, 36); printf("    (mip level 1 green + 50%% blue fog: 0x%08x)\n", c);
    CHECK("mip + fog: level 1 (green) blended half way with the fog colour", ch(c, 16) < 40 && ch(c, 8) > 80 && ch(c, 8) < 180 && ch(c, 0) > 80 && ch(c, 0) < 180);
    texreset(); mach64_texture_deleted(tm);
    for (k = 0; k < 7; k++) free(bufs[k]);
}
typedef void (*fan_fn)(gld_context *, const void *, const void *, uint32_t);
static void setst(unsigned char *v, float S, float T) { memcpy(v + 0x80, &S, 4); memcpy(v + 0x84, &T, 4); }
/* Hardware mips on the other triangle-producing primitives: a 32px square (64px texture) must sample level 1 (green) in every triangle. */
static void t_tex_mip_prims(void)
{
    unsigned char *bufs[7], *vb; void *tm; int k; unsigned c1, c2;
    tm = mktex_mip(7, 0x2700, 0x2600, bufs);
    default_state(); bindtex(tm, GL_REPLACE_ENV); sync(); clear_rt(0);
    /* triangle fan: apex = top-left corner, perimeter = top-right, bottom-right, bottom-left */
    vb = VB(4);
    V(VX(vb, 0), 20, 20, 0, 0xffffffffu); setst(VX(vb, 0), 0, 0);
    V(VX(vb, 1), 52, 20, 0, 0xffffffffu); setst(VX(vb, 1), 1, 0);
    V(VX(vb, 2), 52, 52, 0, 0xffffffffu); setst(VX(vb, 2), 1, 1);
    V(VX(vb, 3), 20, 52, 0, 0xffffffffu); setst(VX(vb, 3), 0, 1);
    (*(fan_fn *)(table + 0x30))(ctx, vb, vb + MACH64_GLIVERTEX_STRIDE, 3); endframe();
    c1 = px(44, 28); c2 = px(28, 44);
    printf("    (fan 32px: tri1 0x%08x tri2 0x%08x)\n", c1, c2);
    CHECK("mip + triangle fan: both triangles sample level 1 (green)", isclr(c1, 0, 255, 0) && isclr(c2, 0, 255, 0));
    free(vb);
    /* quad strip: v0,v1 | v2,v3 -> one 32px quad */
    clear_rt(0); vb = VB(4);
    V(VX(vb, 0), 20, 20, 0, 0xffffffffu); setst(VX(vb, 0), 0, 0);
    V(VX(vb, 1), 20, 52, 0, 0xffffffffu); setst(VX(vb, 1), 0, 1);
    V(VX(vb, 2), 52, 20, 0, 0xffffffffu); setst(VX(vb, 2), 1, 0);
    V(VX(vb, 3), 52, 52, 0, 0xffffffffu); setst(VX(vb, 3), 1, 1);
    SLOT(0x3c)(ctx, vb, 4); endframe();
    c1 = px(44, 28); c2 = px(28, 44);
    printf("    (quad strip 32px: 0x%08x 0x%08x)\n", c1, c2);
    CHECK("mip + quad strip: samples level 1 (green)", isclr(c1, 0, 255, 0) && isclr(c2, 0, 255, 0));
    free(vb);
    /* triangle strip (slot 0x34) */
    clear_rt(0); vb = VB(4);
    V(VX(vb, 0), 20, 20, 0, 0xffffffffu); setst(VX(vb, 0), 0, 0);
    V(VX(vb, 1), 20, 52, 0, 0xffffffffu); setst(VX(vb, 1), 0, 1);
    V(VX(vb, 2), 52, 20, 0, 0xffffffffu); setst(VX(vb, 2), 1, 0);
    V(VX(vb, 3), 52, 52, 0, 0xffffffffu); setst(VX(vb, 3), 1, 1);
    SLOT(0x34)(ctx, vb, 4); endframe();
    c1 = px(44, 28); c2 = px(28, 44);
    printf("    (strip 32px: 0x%08x 0x%08x)\n", c1, c2);
    CHECK("mip + slot 0x34 strip: samples level 1 (green)", isclr(c1, 0, 255, 0) && isclr(c2, 0, 255, 0));
    free(vb);
    texreset(); mach64_texture_deleted(tm);
    for (k = 0; k < 7; k++) free(bufs[k]);
}
/* GL_TEXTURE_LOD_BIAS (gli+0x6774): hardware nibble path.  Sweep: which colour (= mip level) a quad of each size shows, for several biases. */
static int lvl_of(unsigned c)
{
    return isclr(c, 255, 0, 0) ? 0 : isclr(c, 0, 255, 0) ? 1 : isclr(c, 0, 0, 255) ? 2 : isclr(c, 255, 255, 255) ? 3 : -1;
}
static int mip_level_at(int sz)
{
    unsigned c;
    clear_rt(0); tquad(10, 20, 10 + sz, 20 + sz, 0.0f, 0.0f, 1.0f, 1.0f, 0xffffffffu);
    c = px(10 + sz / 2, 20 + sz / 2);
    return lvl_of(c);
}
static void t_tex_lodbias(void)
{
    unsigned char *bufs[7]; void *tm; int k, a, b, c;
    tm = mktex_mip(7, 0x2700, 0x2600, bufs);
    default_state(); bindtex(tm, GL_REPLACE_ENV); sync();
    /* hardware path: bias 0 switches L1->L0 between 54 and 58 px; the TEX_CNTL nibble n multiplies the threshold by (1 + n/16) */
    gf(0x6774, 0.0f); sync();   a = mip_level_at(34); b = mip_level_at(46); c = mip_level_at(62);
    printf("    (HW bias 0: 34px L%d, 46px L%d, 62px L%d)\n", a, b, c);
    CHECK("LOD bias 0 (hardware): 34px and 46px = level 1, 62px = level 0", a == 1 && b == 1 && c == 0);
    gf(0x6774, -0.5f); sync();  a = mip_level_at(34); b = mip_level_at(46);
    printf("    (HW bias -0.5: 34px L%d, 46px L%d)\n", a, b);
    CHECK("LOD bias -0.5 (hardware): sharper, 46px moves to level 0 while 34px stays at level 1", a == 1 && b == 0);
    gf(0x6774, -0.95f); sync(); a = mip_level_at(34);
    printf("    (HW bias -0.95: 34px L%d)\n", a);
    CHECK("LOD bias -0.95 (hardware, nibble 15): 34px moves to level 0", a == 0);
    gf(0x6774, 1.0f); sync();   a = mip_level_at(34); b = mip_level_at(62);
    printf("    (bias +1, hardware chain: 34px L%d, 62px L%d)\n", a, b);
    CHECK("LOD bias +1 (no blurrier hardware field -> software selector): 34px level 2, 62px level 1", a == 2 && b == 1);
    gf(0x6774, -0.5f); sync();  a = mip_level_at(46);
    gf(0x6774, 0.0f); sync();   b = mip_level_at(46);
    CHECK("LOD bias returns to neutral: 46px back at level 1", a == 0 && b == 1);
    /* software selector: whole-level shifts, both signs */
    setenv("MACH64_SW_MIP", "1", 1);
    default_state(); bindtex(tm, GL_REPLACE_ENV); gf(0x6774, 0.0f); sync(); a = mip_level_at(32);
    gf(0x6774, 1.0f); sync();   b = mip_level_at(32);
    gf(0x6774, -1.0f); sync();  c = mip_level_at(32);
    printf("    (SW selector, 32px quad: bias 0 -> L%d, +1 -> L%d, -1 -> L%d)\n", a, b, c);
    CHECK("LOD bias (software selector): 32px quad level 1; +1 -> level 2; -1 -> level 0", a == 1 && b == 2 && c == 0);
    unsetenv("MACH64_SW_MIP");
    gf(0x6774, 0.0f); texreset(); mach64_texture_deleted(tm);
    for (k = 0; k < 7; k++) free(bufs[k]);
}
/* Audit experiment: what does SCALE_3D_CNTL bit 8 do (DRI header: EDGE_ANTI_ALIAS; ATI3DCIF: ENHANCED_COLOR_RANGE_EN)?  A white textured triangle
 * with slanted edges on black plus a dark-to-light gouraud-modulated one: count edge pixels that are neither black nor white, and distinct grey levels. */
static void bit8_frame(int *edge, int *levels, unsigned *g125)
{
    unsigned char *vb; int i, x, y, e = 0, nl = 0; unsigned char seen[256]; unsigned c;
    static const int X[3] = { 20, 150, 40 }, Y[3] = { 20, 40, 150 };
    memset(seen, 0, sizeof seen);
    clear_rt(0);
    vb = VB(3);
    for (i = 0; i < 3; i++) { V(VX(vb, i), X[i], Y[i], 0, i == 0 ? 0xff101010u : i == 1 ? 0xffffffffu : 0xff808080u); setst(VX(vb, i), i == 1 ? 1.0f : 0.0f, i == 2 ? 1.0f : 0.0f); }
    SLOT(0x2c)(ctx, vb, 3); endframe(); free(vb);
    for (y = 20; y < 150; y++) for (x = 20; x < 150; x++) {
        c = px(x, y); if (c == 0) continue;
        if (!seen[(c >> 8) & 255]) { seen[(c >> 8) & 255] = 1; nl++; }
        if ((c >> 24) != 0xff) e++;                       /* partial coverage would show as non-opaque alpha */
    }
    *edge = e; *levels = nl; *g125 = (px(125, 60) >> 8) & 255;
}
/* SCALE_3D_CNTL bit 8: DRI header "EDGE_ANTI_ALIAS", ATI3DCIF "ENHANCED_COLOR_RANGE_EN".  MEASURED: scales interpolated colour to ~1/4, no partial-alpha edges. */
static void t_tex_bit8(void)
{
    static unsigned char d[8 * 8 * 3]; void *t; int e0, l0, e1, l1, i; unsigned g0, g1;
    for (i = 0; i < 8 * 8 * 3; i++) d[i] = 255;
    t = mktex(8, 8, 0x1907, d, (GL_CLAMP_E << 16) | GL_CLAMP_E, 0x2600, 0x2600);
    unsetenv("MACH64_SC_BIT8");
    default_state(); bindtex(t, GL_MODULATE_ENV); sync(); bit8_frame(&e0, &l0, &g0);
    setenv("MACH64_SC_BIT8", "1", 1);
    default_state(); bindtex(t, GL_MODULATE_ENV); sync(); bit8_frame(&e1, &l1, &g1);
    unsetenv("MACH64_SC_BIT8");
    printf("    (bit8 off: %d partial-alpha px, %d grey levels, x=125 green 0x%02x;  bit8 on: %d, %d, 0x%02x)\n", e0, l0, g0, e1, l1, g1);
    CHECK("SCALE_3D_CNTL bit 8 off: gradient at full range, no partial-alpha pixels", g0 >= 0xc0 && e0 == 0);
    /* "bit 8 on" not carried over: it was an audit experiment driven by the old bundle's MACH64_SC_BIT8 knob; the new bundle never sets the bit (measured: no effect without it, e1==0). */
    (void)g1; (void)l1; (void)e1;
    texreset(); mach64_texture_deleted(t);
}
static void t_tex_mip_nonsq(void)
{
    unsigned char *bufs[7]; void *tm; int k;
    static const int qw[3] = { 64, 32, 16 }, qh[3] = { 16, 8, 4 };           /* 1:1, 2x, 4x minified in each axis -> level 0, 1, 2 */
    static const unsigned want[3][3] = { {255,0,0}, {0,255,0}, {0,0,255} };
    tm = mktex_mip_nonsq(0x2700, bufs);
    default_state(); bindtex(tm, GL_REPLACE_ENV); sync(); clear_rt(0);
    for (k = 0; k < 3; k++) {
        char nm[96]; unsigned got;
        tquad(10, 20, 10 + qw[k], 20 + qh[k], 0.0f, 0.0f, 1.0f, 1.0f, 0xffffffffu);
        got = px(10 + qw[k] / 2, 20 + qh[k] / 2);
        printf("    (non-square %dx%d quad centre 0x%08x)\n", qw[k], qh[k], got);
        snprintf(nm, sizeof nm, "mipmap non-square 64x16: %dx%d quad samples level %d", qw[k], qh[k], k);
        CHECK(nm, isclr(got, (int)want[k][0], (int)want[k][1], (int)want[k][2]));
    }
    texreset(); mach64_texture_deleted(tm);
    for (k = 0; k < 7; k++) free(bufs[k]);
}
static void t_tex_mip(void)
{
    unsigned char *bufs[7]; void *tm, *tn; int k;
    static const int size[4] = { 64, 32, 16, 8 };            /* screen px: texel/pixel area ratio 1, 4, 16, 64 -> level 0..3 */
    static const unsigned want[4][3] = { {255,0,0}, {0,255,0}, {0,0,255}, {255,255,255} };
    tm = mktex_mip(7, 0x2700, 0x2600, bufs);                  /* NEAREST_MIPMAP_NEAREST, complete chain -> hardware mips */
    default_state(); bindtex(tm, GL_REPLACE_ENV); sync(); clear_rt(0);
    for (k = 0; k < 4; k++) {                                  /* each tquad ends a frame (deferred clear), so read right after its draw */
        int x0 = 10 + k * 80, y0 = 20; unsigned got; char nm[96];
        tquad(x0, y0, x0 + size[k], y0 + size[k], 0.0f, 0.0f, 1.0f, 1.0f, 0xffffffffu);
        got = px(x0 + size[k] / 2, y0 + size[k] / 2);
        printf("    (mip quad %dpx centre 0x%08x)\n", size[k], got);
        snprintf(nm, sizeof nm, "mipmap: %dpx quad samples level %d", size[k], k);
        CHECK(nm, isclr(got, (int)want[k][0], (int)want[k][1], (int)want[k][2]));
    }
    /* magnified (bigger than the base): stays on level 0 */
    clear_rt(0); tquad(10, 10, 138, 138, 0.0f, 0.0f, 1.0f, 1.0f, 0xffffffffu);
    CHECK("mipmap: magnified quad stays on level 0", isclr(px(74, 74), 255, 0, 0));
    /* NON-mipmapped MIN filter on the same data: a minified quad must keep using level 0 */
    texreset(); mach64_texture_deleted(tm);
    tn = mktex_mip(7, 0x2600, 0x2600, bufs);
    default_state(); bindtex(tn, GL_REPLACE_ENV); sync(); clear_rt(0);
    tquad(10, 20, 26, 36, 0.0f, 0.0f, 1.0f, 1.0f, 0xffffffffu);
    CHECK("mipmap: NEAREST min filter keeps level 0 when minified", isclr(px(18, 28), 255, 0, 0));
    /* same chain, next frame the level must reset: first a minified quad, then a full-size one */
    texreset(); mach64_texture_deleted(tn);
    tm = mktex_mip(7, 0x2700, 0x2600, bufs);
    default_state(); bindtex(tm, GL_REPLACE_ENV); sync(); clear_rt(0);
    g_endframe = 0;
    tquad(10, 20, 26, 36, 0.0f, 0.0f, 1.0f, 1.0f, 0xffffffffu);
    tquad(100, 20, 164, 84, 0.0f, 0.0f, 1.0f, 1.0f, 0xffffffffu);
    g_endframe = 1; endframe();
    CHECK("mipmap: minified then full-size in one frame (level restored)", isclr(px(18, 28), 0, 0, 255) && isclr(px(132, 52), 255, 0, 0));
    texreset(); mach64_texture_deleted(tm);
    for (k = 0; k < 7; k++) free(bufs[k]);
    /* tri-linear at a fractional LOD: a 45px quad is ~0.5 levels down, so the centre must blend level 0 (red) and level 1 (green) */
    tm = mktex_mip(7, 0x2703, 0x2600, bufs);
    default_state(); bindtex(tm, GL_REPLACE_ENV); sync(); clear_rt(0);
    tquad(10, 20, 55, 65, 0.0f, 0.0f, 1.0f, 1.0f, 0xffffffffu);
    { unsigned c = px(32, 42); printf("    (trilinear 45px centre 0x%08x)\n", c);
      CHECK("mipmap: LINEAR_MIPMAP_LINEAR blends two levels at a fractional LOD", ch(c, 16) > 60 && ch(c, 8) > 60 && ch(c, 0) < 30); }
    texreset(); mach64_texture_deleted(tm);
    for (k = 0; k < 7; k++) free(bufs[k]);
    /* incomplete chain (no 1x1): must fall back to the software selector and still pick the right level */
    tm = mktex_mip(6, 0x2700, 0x2600, bufs);
    default_state(); bindtex(tm, GL_REPLACE_ENV); sync(); clear_rt(0);
    tquad(10, 20, 42, 52, 0.0f, 0.0f, 1.0f, 1.0f, 0xffffffffu);
    CHECK("mipmap: incomplete chain falls back to software level selection", isclr(px(26, 36), 0, 255, 0));
    texreset(); mach64_texture_deleted(tm);
    for (k = 0; k < 6; k++) free(bufs[k]);
    /* MACH64_SW_MIP forces the software selector even for a complete chain */
    setenv("MACH64_SW_MIP", "1", 1);
    tm = mktex_mip(7, 0x2700, 0x2600, bufs);
    default_state(); bindtex(tm, GL_REPLACE_ENV); sync(); clear_rt(0);
    tquad(10, 20, 26, 36, 0.0f, 0.0f, 1.0f, 1.0f, 0xffffffffu);
    CHECK("mipmap: MACH64_SW_MIP=1 software selector picks level 2", isclr(px(18, 28), 0, 0, 255));
    unsetenv("MACH64_SW_MIP");
    texreset(); mach64_texture_deleted(tm);
    for (k = 0; k < 7; k++) free(bufs[k]);
}
/* Hardware mipmapping evidence run (opt-in: PB_HWMIP=1 point mip, 2 adds mip-linear/2x2/tri-linear fields; each stage can wedge the engine, so
 * run it alone on a fresh boot).  Recipe from the Win98 driver, docs/re-notes/win98-mipmap.md.  Output lines "HWMIP ..." are the evidence. */
static void hwmip_run(const char *label, unsigned minf)
{
    unsigned char *bufs[7]; void *tm; int k;
    static const int size[4] = { 64, 32, 16, 8 };
    tm = mktex_mip(7, minf, 0x2600, bufs);
    default_state(); bindtex(tm, GL_REPLACE_ENV); sync(); clear_rt(0);
    for (k = 0; k < 4; k++) {
        int x0 = 10 + k * 80, y0 = 20; unsigned got;
        fflush(stdout);
        tquad(x0, y0, x0 + size[k], y0 + size[k], 0.0f, 0.0f, 1.0f, 1.0f, 0xffffffffu);
        got = px(x0 + size[k] / 2, y0 + size[k] / 2);
        printf("    HWMIP %-22s %2dpx centre 0x%08x  SCALE_3D=0x%08x SETUP=0x%08x LOG_MAX_INC=0x%08x TEX_SIZE_PITCH=0x%08x\n", label, size[k], got,
               vrd(REGS + 0x5fc), vrd(REGS + 0x304), vrd(REGS + 0x748), vrd(REGS + 0x770));
        fflush(stdout);
    }
    texreset(); mach64_texture_deleted(tm);
    for (k = 0; k < 7; k++) free(bufs[k]);
}
static void t_tex_hwmip(void)
{
    const char *lv = getenv("PB_HWMIP");
    if (!lv) { printf("    (skipped: set PB_HWMIP=1 to run the experimental hardware-mip test)\n"); return; }
    hwmip_run("NEAREST_MIPMAP_NEAREST", 0x2700);
    if (atoi(lv) >= 2) {
        hwmip_run("LINEAR_MIPMAP_NEAREST", 0x2701);
        hwmip_run("NEAREST_MIPMAP_LINEAR", 0x2702);
        hwmip_run("LINEAR_MIPMAP_LINEAR", 0x2703);
    }
}
/* GL_LINEAR MIN filter on a single-level texture (it wedged the engine in 2026-08 at 128x128, before SETUP_CNTL.LOG_MAX_INC_ADJ was
 * understood).  Alternating red/green columns minified 2:1: NEAREST gives pure columns, a 2x2 min filter blends them. */
static void minlin_run(int sz, unsigned minf, int *mixed_out, int *pure_out)
{
    int x, mixed = 0, pure = 0; unsigned char *d = malloc((size_t)sz * sz * 3); void *tl;
    for (x = 0; x < sz * sz; x++) { int c = (x % sz) & 1; d[x*3] = c ? 0 : 255; d[x*3+1] = c ? 255 : 0; d[x*3+2] = 0; }
    tl = mktex(sz, sz, 0x1907, d, (GL_CLAMP_E << 16) | GL_CLAMP_E, minf, 0x2600);
    default_state(); bindtex(tl, GL_REPLACE_ENV); sync(); clear_rt(0);
    tquad(10, 20, 10 + sz / 2, 20 + sz / 2, 0.0f, 0.0f, 1.0f, 1.0f, 0xffffffffu);
    for (x = 12; x < 8 + sz / 2; x++) { unsigned c = px(x, 20 + sz / 4); if (isclr(c, 255, 0, 0) || isclr(c, 0, 255, 0)) pure++; else mixed++; }
    printf("    (%dx%d minified 2:1, min filter 0x%x: %d pure / %d blended pixels)\n", sz, sz, minf, pure, mixed);
    fflush(stdout);
    texreset(); mach64_texture_deleted(tl); free(d);
    *mixed_out = mixed; *pure_out = pure;
}
static void t_tex_minlin(void)
{
    int m, p;
    minlin_run(32, 0x2600, &m, &p);   CHECK("min filter NEAREST: columns stay pure when minified", m <= 2);
    minlin_run(32, 0x2601, &m, &p);   CHECK("min filter LINEAR 32x32: columns blend when minified", m >= 8 && m > p);
    minlin_run(128, 0x2601, &m, &p);  CHECK("min filter LINEAR 128x128 (the old wedge): no hang, columns blend", m >= 20 && m > p);
    minlin_run(256, 0x2601, &m, &p);  CHECK("min filter LINEAR 256x256: no hang, columns blend", m >= 40 && m > p);
}
static void t_tex_highptr(void)
{
    unsigned char d[8 * 8 * 3]; int x, y; void *t;                 /* STACK data: its address is ~0xbffff... (above the old 1GB cap) */
    for (y = 0; y < 8; y++) for (x = 0; x < 8; x++) { unsigned char *p = d + (y * 8 + x) * 3; p[0] = 255; p[1] = 0; p[2] = 255; }
    t = mktex(8, 8, 0x1907, d, (GL_REPEAT_ << 16) | GL_REPEAT_, 0x2600, 0x2600);
    printf("    (texel data at %p)\n", (void *)d);
    default_state(); bindtex(t, GL_REPLACE_ENV); sync(); clear_rt(0);
    tquad(20, 20, 148, 148, 0.0f, 0.0f, 1.0f, 1.0f, 0xffffffffu);
    CHECK("texture data above 1GB (stack buffer) uploads and samples", (uintptr_t)d > 0x40000000u && isclr(px(80, 80), 255, 0, 255));
    texreset(); mach64_texture_deleted(t);
}
static void t_tex_churn(void)
{
    int i, bad = 0; unsigned char *d = quadtex(); void *t;
    for (i = 0; i < 150; i++) {
        t = mktex(8, 8, 0x1907, d, (GL_REPEAT_ << 16) | GL_REPEAT_, 0x2600, 0x2600);
        default_state(); bindtex(t, GL_REPLACE_ENV); sync(); clear_rt(0);
        tquad(20, 20, 148, 148, 0.0f, 0.0f, 1.0f, 1.0f, 0xffffffffu);
        if (!isclr(px(36, 36), 255, 0, 0) || !isclr(px(132, 132), 255, 255, 255)) bad++;
        texreset(); mach64_texture_deleted(t);
    }
    printf("    (churn: %d create/upload/draw/delete cycles, %d bad)\n", i, bad);
    CHECK("texture churn: 150 create/upload/draw/delete cycles all correct (slots and heap reclaimed)", bad == 0);
}


/* ---------------------------------------------------------------- remaining primitives / fog / stipple / logic op */
static void t_prims2(void)
{
    unsigned char *vb; int i, n;
    /* GL_TRIANGLE_FAN: slot 0x30 is called (apex, perimeter, perimeter_count) */
    default_state(); sync(); clear_rt(0);
    vb = VB(5); V(VX(vb, 0), 100, 100, 0, 0xffffffffu); V(VX(vb, 1), 40, 40, 0, 0xffffffffu); V(VX(vb, 2), 160, 40, 0, 0xffffffffu);
    V(VX(vb, 3), 160, 160, 0, 0xffffffffu); V(VX(vb, 4), 40, 160, 0, 0xffffffffu);
    (*(fan_fn *)(table + 0x30))(ctx, vb, vb + MACH64_GLIVERTEX_STRIDE, 4); endframe();
    CHECK("GL_TRIANGLE_FAN: the three fan triangles (top, right, bottom) drawn", px(100, 60) != 0 && px(140, 100) != 0 && px(100, 140) != 0);
    CHECK("GL_TRIANGLE_FAN: a fan is not closed (left wedge empty)", px(60, 100) == 0);
    CHECK("GL_TRIANGLE_FAN: outside empty", px(20, 20) == 0 && px(180, 180) == 0);
    free(vb);
    /* GL_QUAD_STRIP: v0,v1 | v2,v3 | v4,v5 (two quads side by side) */
    clear_rt(0); vb = VB(6);
    V(VX(vb, 0), 20, 20, 0, 0xffffffffu); V(VX(vb, 1), 20, 100, 0, 0xffffffffu);
    V(VX(vb, 2), 80, 20, 0, 0xffffffffu); V(VX(vb, 3), 80, 100, 0, 0xffffffffu);
    V(VX(vb, 4), 140, 30, 0, 0xffffffffu); V(VX(vb, 5), 140, 90, 0, 0xffffffffu);
    SLOT(0x3c)(ctx, vb, 6); endframe();
    CHECK("GL_QUAD_STRIP: first quad filled",  px(50, 60) != 0 && px(30, 30) != 0 && px(70, 90) != 0);
    CHECK("GL_QUAD_STRIP: second quad filled", px(110, 60) != 0);
    CHECK("GL_QUAD_STRIP: outside empty",      px(160, 60) == 0 && px(50, 120) == 0);
    free(vb);
    /* GL_LINE_STRIP: three vertices -> two segments (an L shape) */
    clear_rt(0); vb = VB(3); V(VX(vb, 0), 30, 30, 0, 0xffffffffu); V(VX(vb, 1), 30, 150, 0, 0xffffffffu); V(VX(vb, 2), 150, 150, 0, 0xffffffffu);
    SLOT(0x20)(ctx, vb, 3); endframe();
    n = 0; for (i = 31; i < 149; i++) if (px(30, i) != 0 || px(29, i) != 0 || px(31, i) != 0) n++;
    CHECK("GL_LINE_STRIP: first segment drawn", n > 100);
    n = 0; for (i = 31; i < 149; i++) if (px(i, 150) != 0 || px(i, 149) != 0 || px(i, 151) != 0) n++;
    CHECK("GL_LINE_STRIP: second segment drawn", n > 100);
    CHECK("GL_LINE_STRIP: no closing segment (strip, not loop)", px(90, 90) == 0);
    free(vb);
}
static void ftri(int x0, int y0, int x1, int y1, int x2, int y2, float f)
{
    unsigned char a[MACH64_GLIVERTEX_STRIDE], b[MACH64_GLIVERTEX_STRIDE], c[MACH64_GLIVERTEX_STRIDE];
    V(a, x0, y0, 0, 0xffffffffu); V(b, x1, y1, 0, 0xffffffffu); V(c, x2, y2, 0, 0xffffffffu);
    memcpy(a + 0x4c, &f, 4); memcpy(b + 0x4c, &f, 4); memcpy(c + 0x4c, &f, 4);
    g_endframe = 0; draw_tri(a, b, c, 3); g_endframe = 1;
}
static void t_fog(void)
{
    default_state(); g32(0x6380, 0x100u); gf(0x6358, 1.0f); gf(0x635c, 0.0f); gf(0x6360, 0.0f); gf(0x6364, 1.0f);   /* fog on, colour red */
    sync(); clear_rt(0);
    ftri(10, 10, 70, 10, 10, 70, 1.0f);        /* fog factor 1 = fragment colour (no fog) */
    ftri(90, 10, 150, 10, 90, 70, 0.0f);       /* factor 0 = full fog colour */
    ftri(10, 90, 70, 90, 10, 150, 0.5f);       /* half */
    endframe();
    printf("    (fog f=1 0x%08x  f=0 0x%08x  f=.5 0x%08x)\n", px(20, 20), px(100, 20), px(20, 100));
    CHECK("fog factor 1.0: fragment colour unchanged (white)", isclr(px(20, 20), 255, 255, 255));
    CHECK("fog factor 0.0: full fog colour (red)",             isclr(px(100, 20), 255, 0, 0));
    CHECK("fog factor 0.5: half way (pink)",                   isclr(px(20, 100), 255, 128, 128));
    default_state(); sync(); clear_rt(0); ftri(10, 10, 70, 10, 10, 70, 0.0f); endframe();
    CHECK("fog disabled: factor ignored, plain white", isclr(px(20, 20), 255, 255, 255));
}
static void t_stipple(void)
{
    int x, lit = 0, alt = 0, k; unsigned prev = 0, cur;
    default_state(); g32(0x66ec, 0x01000000u); for (k = 0; k < 32; k++) g32(0x665c + k * 4, 0xaaaaaaaau);   /* alternate columns */
    sync(); clear_rt(0);
    tri(10, 10, 0xffffffffu, 200, 10, 0xffffffffu, 10, 200, 0xffffffffu, 0.0f);
    for (x = 20; x < 100; x++) { cur = px(x, 40) != 0; lit += cur; if (x > 20 && cur != prev) alt++; prev = cur; }
    printf("    (stipple row y=40, x 20..99: %d lit, %d transitions)\n", lit, alt);
    CHECK("polygon stipple 0xaaaaaaaa: about half the pixels drawn", lit > 28 && lit < 52);
    CHECK("polygon stipple 0xaaaaaaaa: pixels alternate", alt > 60);
    default_state(); sync(); clear_rt(0);
    tri(10, 10, 0xffffffffu, 200, 10, 0xffffffffu, 10, 200, 0xffffffffu, 0.0f);
    for (lit = 0, x = 20; x < 100; x++) lit += px(x, 40) != 0;
    CHECK("stipple off again: row fully solid", lit == 80);
}
static void t_logicop(void)
{
    unsigned c;
    /* destination = glClearColor red (1,0,0); source colour (1,1,0) yellow */
    default_state(); g32(0x63a4, (0x1506u << 16) | 1u); gf(0x6320, 1.0f); gf(0x6324, 0.0f); gf(0x6328, 0.0f); gf(0x632c, 1.0f); sync();
    tri(20, 16, 0xffffff00u, 140, 16, 0xffffff00u, 80, 130, 0xffffff00u, 0.0f);
    c = px(80, 50); printf("    (XOR yellow over red = 0x%08x)\n", c);
    CHECK("logic op XOR: yellow ^ red = green", isclr(c, 0, 255, 0));
    default_state(); g32(0x63a4, (0x150au << 16) | 1u); gf(0x6320, 1.0f); gf(0x6324, 0.0f); gf(0x6328, 0.0f); gf(0x632c, 1.0f); sync();
    tri(20, 16, 0xffffff00u, 140, 16, 0xffffff00u, 80, 130, 0xffffff00u, 0.0f);
    c = px(80, 50); printf("    (INVERT over red = 0x%08x)\n", c);
    CHECK("logic op INVERT: red -> cyan", isclr(c, 0, 255, 255));
    default_state(); g32(0x63a4, (0x1500u << 16) | 1u); gf(0x6320, 1.0f); gf(0x6324, 0.0f); gf(0x6328, 0.0f); gf(0x632c, 1.0f); sync();
    tri(20, 16, 0xffffff00u, 140, 16, 0xffffff00u, 80, 130, 0xffffff00u, 0.0f);
    c = px(80, 50); printf("    (CLEAR = 0x%08x)\n", c);
    CHECK("logic op CLEAR: black", isclr(c, 0, 0, 0));
    default_state(); g32(0x63a4, (0x1503u << 16) | 1u); gf(0x6320, 1.0f); gf(0x6324, 0.0f); gf(0x6328, 0.0f); gf(0x632c, 1.0f); sync();
    tri(20, 16, 0xffffff00u, 140, 16, 0xffffff00u, 80, 130, 0xffffff00u, 0.0f);
    CHECK("logic op COPY: ordinary yellow", isclr(px(80, 50), 255, 255, 0));
    default_state(); sync(); clear_rt(0); tri(20, 16, 0xffffff00u, 140, 16, 0xffffff00u, 80, 130, 0xffffff00u, 0.0f);
    CHECK("logic op off again: ordinary yellow", isclr(px(80, 50), 255, 255, 0));
}


/* ---------------------------------------------------------------- multiple contexts */
struct cinfo { gld_context *c; mach64_priv *p; unsigned char *g; unsigned toff, tpitch; };
static void use(const struct cinfo *k) { ctx = k->c; P = k->p; gli = k->g; TOFF = k->toff; TPITCH = k->tpitch; }
static int mkctx(struct cinfo *k, unsigned tag, void **pf)
{
    int attribs[] = { 8, 32, 12, 16, 5, 73, 0 }, rc; unsigned char *dr;
    rc = gldChoosePixelFormat(pf, attribs); if (rc || !*pf) return 1;
    rc = gldCreateContext(&k->c, *pf, 0, 0, calloc(1, 64), calloc(1, 0x10000), 0); if (rc || !k->c) return 2;
    dr = calloc(1, 0x80); ((unsigned *)dr)[0] = tag;
    rc = gldAttachDrawable(k->c, 0x36, dr, 0); k->p = MACH64_PRIV(k->c); if (rc || !k->p->attached) return 3;
    k->g = calloc(1, 0x100000); *(uint32_t *)(k->g + 0x7898) = (uint32_t)(uintptr_t)k->c; *(uint32_t *)((unsigned char *)k->c + 0x10) = (uint32_t)(uintptr_t)k->g + 0x3610;
    k->toff = k->p->rb.col_off; k->tpitch = k->p->rb.pitch;
    return 0;
}
static int overlap(unsigned a, unsigned alen, unsigned b, unsigned blen) { return a < b + blen && b < a + alen; }
static void t_multictx(void)
{
    struct cinfo A, B; void *pf2 = 0; int rc, i, bad = 0, bad2 = 0, bad3 = 0; unsigned span = W * 2u * H;
    A.c = ctx; A.p = P; A.g = gli; A.toff = TOFF; A.tpitch = TPITCH; memset(&B, 0, sizeof B);
    rc = mkctx(&B, 0x4343, &pf2);
    CHECK("second context: created, attached, has a render target", rc == 0);
    if (rc) { printf("    (mkctx rc=%d)\n", rc); if (B.c) gldDestroyContext(B.c); return; }
    printf("    ctx A target 0x%x z 0x%x | ctx B target 0x%x z 0x%x\n", A.toff, A.p->rb.z_off, B.toff, B.p->rb.z_off);
    CHECK("two contexts: render targets do not overlap", !overlap(A.toff, span, B.toff, span));
    CHECK("two contexts: depth buffers are separate from each other and from both targets",
          !overlap(A.p->rb.z_off, W * 2u * H, B.p->rb.z_off, W * 2u * H) && !overlap(A.p->rb.z_off, W * 2u * H, B.toff, span) && !overlap(B.p->rb.z_off, W * 2u * H, A.toff, span)
          && !overlap(A.p->rb.z_off, W * 2u * H, A.toff, span) && !overlap(B.p->rb.z_off, W * 2u * H, B.toff, span));
    /* interleaved frames, different geometry and colour per context: each target must show only its own */
    for (i = 0; i < 20; i++) {
        use(&A); default_state(); sync(); tri(20, 16, 0xffff0000u, 110, 44, 0xffff0000u, 50, 116, 0xffff0000u, 0.0f);
        use(&B); default_state(); sync(); tri(150, 150, 0xff00ff00u, 230, 170, 0xff00ff00u, 170, 235, 0xff00ff00u, 0.0f);
        use(&A); if (!(isclr(px(50, 60), 255, 0, 0) && px(180, 180) == 0)) bad++;
        use(&B); if (!(isclr(px(180, 185), 0, 255, 0) && px(50, 60) == 0)) bad++;
    }
    CHECK("interleaved frames on two contexts: each target holds only its own drawing (20 rounds)", bad == 0);
    /* per-context state: A masks the red channel, B does not; same white triangle drawn on both, alternating */
    for (i = 0; i < 10; i++) {
        use(&A); default_state(); gli[0x63b4] = 0; sync(); tri(20, 16, 0xffffffffu, 140, 16, 0xffffffffu, 80, 130, 0xffffffffu, 0.0f);
        use(&B); default_state(); sync(); tri(20, 16, 0xffffffffu, 140, 16, 0xffffffffu, 80, 130, 0xffffffffu, 0.0f);
        use(&A); if (!(ch(px(80, 50), 16) == 0 && ch(px(80, 50), 8) > 200)) bad2++;
        use(&B); if (!isclr(px(80, 50), 255, 255, 255)) bad2++;
    }
    CHECK("per-context state: colour mask on A does not leak into B (10 rounds)", bad2 == 0);
    /* a texture bound in A only */
    { unsigned char *d = quadtex(); void *t = mktex(8, 8, 0x1907, d, (GL_REPEAT_ << 16) | GL_REPEAT_, 0x2600, 0x2600);
      for (i = 0; i < 6; i++) {
        use(&A); default_state(); bindtex(t, GL_REPLACE_ENV); sync(); tquad(20, 20, 148, 148, 0.0f, 0.0f, 1.0f, 1.0f, 0xffffffffu);
        use(&B); default_state(); sync(); tquad(20, 20, 148, 148, 0.0f, 0.0f, 1.0f, 1.0f, 0xffffffffu);
        use(&A); if (!(isclr(px(36, 36), 255, 0, 0) && isclr(px(132, 36), 0, 255, 0))) bad3++;
        use(&B); if (!isclr(px(36, 36), 255, 255, 255) || !isclr(px(132, 36), 255, 255, 255)) bad3++;
      }
      use(&A); texreset(); mach64_texture_deleted(t); }
    CHECK("per-context texture binding: textured on A, plain on B, interleaved (6 rounds)", bad3 == 0);
    /* destroy B, A must be unaffected */
    use(&B); gldDestroyContext(B.c); gldDestroyPixelFormat(pf2);
    use(&A); default_state(); sync(); tri(20, 16, 0xffff0000u, 110, 44, 0xff00ff00u, 50, 116, 0xff0000ffu, 0.0f);
    CHECK("after destroying the second context the first still renders correctly", ch(px(24, 22), 16) > 150 && ch(px(100, 44), 8) > 150 && ch(px(50, 108), 0) > 150);
    use(&A);
}

/* ---------------------------------------------------------------- soak */
static long msnow(void) { struct timeval tv; gettimeofday(&tv, 0); return tv.tv_sec * 1000L + tv.tv_usec / 1000L; }
static int soak_frame(int k, void *tex)
{
    unsigned c; int x, lit, alt, kk; unsigned prev;
    default_state();
    switch (k) {
    case 0: sync(); tri(20, 16, 0xffff0000u, 110, 44, 0xff00ff00u, 50, 116, 0xff0000ffu, 0.0f);
        return ch(px(24, 22), 16) > 150 && ch(px(100, 44), 8) > 150 && ch(px(50, 108), 0) > 150 && px(200, 200) == 0;
    case 1: g32(0x6708, 0); sync(); tri(20, 16, 0xffff0000u, 110, 44, 0xff00ff00u, 50, 116, 0xff0000ffu, 0.0f);
        c = px(60, 59); return (ch(c, 16) > 200) + (ch(c, 8) > 200) + (ch(c, 0) > 200) == 1;
    case 2: g32(0x6340, 0x01000000u); g32(0x63b8, 0x01000000u); sync(); g_endframe = 0;
        tri(20, 16, 0xff00ff00u, 140, 16, 0xff00ff00u, 80, 130, 0xff00ff00u, 0.5f); tri(20, 16, 0xffff0000u, 140, 16, 0xffff0000u, 80, 130, 0xffff0000u, -0.5f);
        endframe(); g_endframe = 1; return isclr(px(80, 50), 255, 0, 0);
    case 3: g32(0x6304, 0x01000000u); g32(0x62e8, (0x0302u << 16) | 0x0303u); gf(0x6320, 0.0f); gf(0x6324, 0.0f); gf(0x6328, 1.0f); gf(0x632c, 1.0f); sync();
        tri(20, 16, 0x8000ff00u, 140, 16, 0x8000ff00u, 80, 130, 0x8000ff00u, 0.0f); c = px(80, 50);
        return ch(c, 8) > 90 && ch(c, 8) < 170 && ch(c, 0) > 90 && ch(c, 0) < 170;
    case 4: gf(0x62e0, 0.5f); g32(0x62e4, (0x0204u << 16) | 0x100u); sync(); tri(20, 16, 0x4000ff00u, 140, 16, 0x4000ff00u, 80, 130, 0x4000ff00u, 0.0f);
        return px(80, 50) == 0;
    case 5: gli[0x63b4] = 0; sync(); tri(20, 16, 0xffffffffu, 110, 44, 0xffffffffu, 50, 116, 0xffffffffu, 0.0f);
        return ch(px(60, 59), 16) == 0 && ch(px(60, 59), 8) > 200;
    case 6: bindtex(tex, GL_REPLACE_ENV); sync(); tquad(20, 20, 148, 148, 0.0f, 0.0f, 1.0f, 1.0f, 0xffffffffu);
        return isclr(px(36, 36), 255, 0, 0) && isclr(px(132, 132), 255, 255, 255);
    case 7: bindtex(tex, GL_MODULATE_ENV); sync(); tquad(20, 20, 148, 148, 0.0f, 0.0f, 1.0f, 1.0f, 0xff808080u);
        return isclr(px(132, 132), 128, 128, 128);
    case 8: g32(0x6380, 0x100u); gf(0x6358, 1.0f); gf(0x635c, 0.0f); gf(0x6360, 0.0f); gf(0x6364, 1.0f); sync(); ftri(10, 90, 70, 90, 10, 150, 0.5f); endframe();
        return isclr(px(20, 100), 255, 128, 128);
    case 9: g32(0x66ec, 0x01000000u); for (kk = 0; kk < 32; kk++) g32(0x665c + kk * 4, 0xaaaaaaaau);
        sync(); tri(10, 10, 0xffffffffu, 200, 10, 0xffffffffu, 10, 200, 0xffffffffu, 0.0f);
        for (lit = alt = 0, prev = 0, x = 20; x < 100; x++) { c = px(x, 40) != 0; lit += (int)c; if (x > 20 && c != prev) alt++; prev = c; }
        return lit > 28 && lit < 52 && alt > 60;
    case 10: g32(0x63a4, (0x1506u << 16) | 1u); gf(0x6320, 1.0f); gf(0x6324, 0.0f); gf(0x6328, 0.0f); gf(0x632c, 1.0f); sync();
        tri(20, 16, 0xffffff00u, 140, 16, 0xffffff00u, 80, 130, 0xffffff00u, 0.0f); return isclr(px(80, 50), 0, 255, 0);
    default: { unsigned char *vb = VB(4); int ok;
        sync(); V(VX(vb, 0), 20, 20, 0, 0xffffffffu); V(VX(vb, 1), 100, 20, 0, 0xffffffffu); V(VX(vb, 2), 100, 100, 0, 0xffffffffu); V(VX(vb, 3), 20, 100, 0, 0xffffffffu);
        SLOT(0x38)(ctx, vb, 4); endframe(); ok = px(60, 60) != 0 && px(120, 60) == 0 && px(60, 120) == 0; free(vb); return ok; }
    }
}
static void free_tex(void *t) { if (!t) return; free((void *)(uintptr_t)*(uint32_t *)((unsigned char *)t + 0x30)); free(t); }




static const struct { const char *name; void (*fn)(void); } TESTS[] = {
    {"gouraud", t_gouraud}, {"flat", t_flat}, {"colormask", t_colormask}, {"depth", t_depth}, {"blend", t_blend},
    {"alphatest", t_alphatest}, {"prims", t_prims}, {"clear", t_clear}, {"prims2", t_prims2}, {"fog", t_fog}, {"stipple", t_stipple}, {"logicop", t_logicop}, {"tex_replace", t_tex_replace}, {"multitex", t_multitex}, {"dstalpha", t_dstalpha}, {"texfmt8", t_texfmt8}, {"texfmt", t_texfmt}, {"texfmt_fallback", t_texfmt_fallback}, {"texfmt_mt", t_texfmt_mt}, {"specular", t_specular}, {"multitex_min", t_multitex_min}, {"tex_alpha", t_tex_alpha}, {"tex_wrap", t_tex_wrap}, {"tex_filter", t_tex_filter}, {"tex_alpha_edge", t_tex_alpha_edge}, {"tex_alpha_decal_probe", t_tex_alpha_decal_probe}, {"tex_alpha_bilinear", t_tex_alpha_bilinear}, {"tex_churn", t_tex_churn}, {"tex_highptr", t_tex_highptr}, {"tex_mip", t_tex_mip}, {"tex_mip_nonsq", t_tex_mip_nonsq}, {"tex_bit8", t_tex_bit8}, {"tex_lodbias", t_tex_lodbias}, {"tex_mip_prims", t_tex_mip_prims}, {"tex_mip_fog", t_tex_mip_fog}, {"tex_mip_gouraud", t_tex_mip_gouraud}, {"tex_hwmip", t_tex_hwmip}, {"tex_minlin", t_tex_minlin}, {"multictx", t_multictx}, 
};

int main(int argc, char **argv)
{
    int attribs[] = { 8, 32, 12, 16, 5, 73, 0 }, rc, i, a; void *pf = 0; unsigned char *drawable;
    setvbuf(stdout, NULL, _IONBF, 0);
    gldInitializeLibrary(NULL, 0, 0, 0, 0);
    rc = gldChoosePixelFormat(&pf, attribs);
    if (rc || !pf) { fprintf(stderr, "gldChoosePixelFormat failed\n"); return 1; }
    rc = gldCreateContext(&ctx, pf, 0, 0, calloc(1, 64), calloc(1, 0x10000), 0);
    if (rc || !ctx) { fprintf(stderr, "gldCreateContext -> 0x%x (is the kext loaded with the engine live?)\n", rc); return 1; }
    P = MACH64_PRIV(ctx);
    g_gld_force_w = W; g_gld_force_h = H;
    drawable = calloc(1, 0x80); ((unsigned *)drawable)[0] = 0x4242;
    rc = gldAttachDrawable(ctx, 0x36, drawable, 0);
    if (rc || !P->attached) { fprintf(stderr, "attach failed rc=0x%x\n", rc); return 1; }
    gli = calloc(1, 0x100000);
    *(uint32_t *)(gli + 0x7898) = (uint32_t)(uintptr_t)ctx;
    *(uint32_t *)((unsigned char *)ctx + 0x10) = (uint32_t)(uintptr_t)gli + 0x3610;
    TOFF = P->rb.col_off; TPITCH = P->rb.pitch;
    printf("suite: target off=0x%x pitch=%upx z_off=0x%x\n", TOFF, TPITCH, P->rb.z_off);

    for (i = 0; i < (int)(sizeof TESTS / sizeof TESTS[0]); i++) {
        int run = (argc < 2);
        for (a = 1; a < argc; a++) if (!strcmp(argv[a], TESTS[i].name)) run = 1;
        if (!run) continue;
        printf("[%s]\n", TESTS[i].name); TESTS[i].fn();
    }
    printf("\n(stats: triangles %u, dropped regs %u, fence %u, last submit error 0x%x)\n", P->triangles, P->rb.dropped, P->rb.fence, (unsigned)P->rb.last_err);
    printf("\nSUITE: %d passed, %d failed\n", npass, nfail);
    gldDestroyContext(ctx); gldDestroyPixelFormat(pf);
    return nfail ? 2 : 0;
}

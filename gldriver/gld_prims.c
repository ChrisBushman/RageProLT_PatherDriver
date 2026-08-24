/*
 * gld_prims.c - render-slot bodies: decompose GL primitives into triangles and
 * push them through the proven mach64_draw_triangle path.
 *
 * GLEngine calls these through the dispatch table (installed by gldUpdateDispatch)
 * as f(ctx, verts, count), where `verts` is the first of `count` incoming gli
 * vertices at stride 0x100.  The incoming vertex already carries X_Y/ARGB/Z
 * pre-packed to hardware format (recovered from ATIRage128GLDriver, slot 0x28 --
 * see docs/re-notes/gld-layer.md), so mach64_read_vertex is mostly a field copy.
 *
 * The mach64 setup engine is triangle-oriented: the triangle family is the
 * validated path; points/lines need sprite/quad expansion and are TODO.
 */
#include "gld.h"

/* incoming gli-vertex field offsets (recovered) */
#define VOFF_S     0x00
#define VOFF_T     0x04
#define VOFF_W     0x08
#define VOFF_SPEC  0x0c
#define VOFF_XY    0x30
#define VOFF_ARGB  0x34
#define VOFF_Z     0x38

/* Recovered gli-vertex layout (dumped live from GLEngine, 0x100 stride):
 *   +0x00 float screen X   +0x04 float screen Y (window coords, y-up)
 *   +0x08 float screen Z   +0x0c float W
 *   +0x10..0x1c clip/NDC coords   +0x30/34/38 float R/G/B   +0x3c float A
 * Convert to our hardware vertex: pack screen X/Y (flip Y to top-down FB) and
 * fold the float color to 0x00RRGGBB. */
#define GVOFF_X    0x00
#define GVOFF_Y    0x04
#define GVOFF_Z    0x08
#define GVOFF_W    0x0c
#define GVOFF_R    0x30
#define GVOFF_G    0x34
#define GVOFF_B    0x38

static uint32_t chan(float f){ int c = (int)(f*255.0f + 0.5f); return c<0?0:(c>255?255:(uint32_t)c); }

void mach64_read_vertex(const void *vp, m64_vertex *o, float fb_h)
{
    const unsigned char *v = (const unsigned char *)vp;
    float x = *(const float *)(v + GVOFF_X);
    float y = *(const float *)(v + GVOFF_Y);
    float w = *(const float *)(v + GVOFF_W);
    /* Depth: the screen-Z slot (+0x08) is 0 (GLEngine's depth range is degenerate
     * for our surface), so derive it from the NDC z at +0x18: [-1,1] -> [0,1]. */
    float ndcz  = *(const float *)(v + 0x18);
    float depth = ndcz * 0.5f + 0.5f;
    uint32_t argb = (chan(*(const float *)(v + GVOFF_R)) << 16)
                  | (chan(*(const float *)(v + GVOFF_G)) <<  8)
                  |  chan(*(const float *)(v + GVOFF_B));
    mach64_pack_vertex(o, x, fb_h - y, depth, w, argb);  /* GL y-up -> FB y-down */
}

#define VTX(base, i)  ((const void *)((const unsigned char *)(base) + (i) * MACH64_GLIVERTEX_STRIDE))

/* Pack an m64_vertex at gli-screen (gx,gy), taking colour/depth/w from vp.
 * Used to expand points/lines into quads (the mach64 setup engine only does
 * triangles), same y-flip + colour fold as mach64_read_vertex. */
static void pack_at(const void *vp, float gx, float gy, float fb_h, m64_vertex *o)
{
    const unsigned char *v = (const unsigned char *)vp;
    float depth = *(const float *)(v + 0x18) * 0.5f + 0.5f;   /* NDC z -> [0,1] */
    float w     = *(const float *)(v + GVOFF_W);
    uint32_t argb = (chan(*(const float *)(v + GVOFF_R)) << 16)
                  | (chan(*(const float *)(v + GVOFF_G)) <<  8)
                  |  chan(*(const float *)(v + GVOFF_B));
    mach64_pack_vertex(o, gx, fb_h - gy, depth, w, argb);
}

/* Read a gli float field (glPointSize@0x6630 / glLineWidth@0x6394), defaulting
 * to 1.0.  Returns the GL size clamped to a sane minimum. */
static float gli_size(gld_context *ctx, uint32_t off)
{
    uint32_t bits = mach64_gli_u32(ctx, off);
    float f;
    if (!bits) return 1.0f;
    __builtin_memcpy(&f, &bits, 4);
    return (f >= 1.0f) ? f : 1.0f;
}

/* One point -> a size x size quad (two same-winding triangles) centred on it.
 * s is the half-extent (GL point size is the diameter). */
static void draw_point(gld_context *ctx, const void *vp, float fb_h, float s)
{
    const unsigned char *v = (const unsigned char *)vp;
    float x = *(const float *)(v + GVOFF_X), y = *(const float *)(v + GVOFF_Y);
    m64_vertex a, b, c, d;
    pack_at(vp, x - s, y - s, fb_h, &a);
    pack_at(vp, x + s, y - s, fb_h, &b);
    pack_at(vp, x - s, y + s, fb_h, &c);
    pack_at(vp, x + s, y + s, fb_h, &d);
    mach64_draw_triangle(ctx, &a, &b, &c);
    mach64_draw_triangle(ctx, &d, &c, &b);
}

/* One line v0->v1 -> a width-2 quad, offset perpendicular to the major axis
 * (offset in Y for x-major lines, in X for y-major), per Mesa mach64_draw_line. */
static void draw_line(gld_context *ctx, const void *v0, const void *v1,
                      float fb_h, float wdt)
{
    float x0 = *(const float *)((const unsigned char *)v0 + GVOFF_X);
    float y0 = *(const float *)((const unsigned char *)v0 + GVOFF_Y);
    float x1 = *(const float *)((const unsigned char *)v1 + GVOFF_X);
    float y1 = *(const float *)((const unsigned char *)v1 + GVOFF_Y);
    float adx = x1 - x0, ady = y1 - y0, ix, iy;
    m64_vertex A, B, C, D;
    if (adx < 0) adx = -adx;
    if (ady < 0) ady = -ady;
    if (adx > ady) { ix = 0.f; iy = wdt; } else { ix = wdt; iy = 0.f; }
    pack_at(v0, x0 - ix, y0 - iy, fb_h, &A);
    pack_at(v1, x1 - ix, y1 - iy, fb_h, &B);
    pack_at(v0, x0 + ix, y0 + iy, fb_h, &C);
    pack_at(v1, x1 + ix, y1 + iy, fb_h, &D);
    mach64_draw_triangle(ctx, &A, &B, &C);
    mach64_draw_triangle(ctx, &D, &C, &B);
}

static void emit_tri(gld_context *ctx, const void *base,
                     uint32_t a, uint32_t b, uint32_t c)
{
    m64_vertex v0, v1, v2;
    float fb_h = (float)*(const uint32_t *)((const unsigned char *)ctx + 0x1f4);
    mach64_read_vertex(VTX(base, a), &v0, fb_h);
    mach64_read_vertex(VTX(base, b), &v1, fb_h);
    mach64_read_vertex(VTX(base, c), &v2, fb_h);
    mach64_draw_triangle(ctx, &v0, &v1, &v2);
}

/* ---- triangle family (the proven hardware path) --------------------- */
void mach64_render_triangles(gld_context *ctx, const void *v, uint32_t n)
{
    uint32_t i;
    gld_log("mach64_render_triangles(n=%u v=%p)\n", n, v);
    if (v && n > 0) {
        const unsigned char *p = (const unsigned char *)v;
        gld_log("  vtx0: XY=0x%08x ARGB=0x%08x Z=0x%08x\n",
                *(const uint32_t *)(p + VOFF_XY),
                *(const uint32_t *)(p + VOFF_ARGB),
                *(const uint32_t *)(p + VOFF_Z));
    }
    for (i = 0; i + 3 <= n; i += 3) emit_tri(ctx, v, i, i+1, i+2);
}

void mach64_render_tri_strip(gld_context *ctx, const void *v, uint32_t n)
{
    uint32_t i;
    mach64_sync_depth_from_gli(ctx);   /* Z_CNTL follows the app's GL depth state */
    for (i = 2; i < n; i++)                      /* flip winding on odd tris */
        if (i & 1) emit_tri(ctx, v, i-1, i-2, i);
        else       emit_tri(ctx, v, i-2, i-1, i);
}

void mach64_render_tri_fan(gld_context *ctx, const void *v, uint32_t n)
{
    uint32_t i;
    for (i = 2; i < n; i++) emit_tri(ctx, v, 0, i-1, i);
}

void mach64_render_polygon(gld_context *ctx, const void *v, uint32_t n)
{
    mach64_render_tri_fan(ctx, v, n);            /* convex polygon = fan */
}

void mach64_render_quads(gld_context *ctx, const void *v, uint32_t n)
{
    uint32_t i;
    for (i = 0; i + 4 <= n; i += 4) {            /* quad -> 2 tris */
        emit_tri(ctx, v, i,   i+1, i+2);
        emit_tri(ctx, v, i,   i+2, i+3);
    }
}

void mach64_render_quad_strip(gld_context *ctx, const void *v, uint32_t n)
{
    uint32_t i;
    for (i = 0; i + 4 <= n; i += 2) {            /* strip quad -> 2 tris */
        emit_tri(ctx, v, i,   i+1, i+2);
        emit_tri(ctx, v, i+2, i+1, i+3);
    }
}

/* ---- point/line family: setup engine is triangle-only; TODO expand to
 * point-sprite / line quads.  No-op for now so a stream of them can't corrupt
 * the ring; the validated triangle family above is complete. ------------ */
#define FB_H(ctx) ((float)*(const uint32_t *)((const unsigned char *)(ctx) + 0x1f4))

/* gli offsets recovered by diffing point-size/line-width configs (tools/
 * mach64_ptlnstate): glPointSize float @0x6630, glLineWidth float @0x6394. */
#define GLI_POINT_SIZE  0x6630
#define GLI_LINE_WIDTH  0x6394

void mach64_render_points(gld_context *ctx, const void *v, uint32_t n)
{
    float fb_h = FB_H(ctx), s = gli_size(ctx, GLI_POINT_SIZE) * 0.5f; uint32_t i;
    mach64_sync_depth_from_gli(ctx);
    for (i = 0; i < n; i++) draw_point(ctx, VTX(v, i), fb_h, s);
}
void mach64_render_lines(gld_context *ctx, const void *v, uint32_t n)
{
    float fb_h = FB_H(ctx), w = gli_size(ctx, GLI_LINE_WIDTH) * 0.5f; uint32_t i;
    mach64_sync_depth_from_gli(ctx);
    for (i = 0; i + 2 <= n; i += 2) draw_line(ctx, VTX(v, i), VTX(v, i+1), fb_h, w);
}
void mach64_render_line_strip(gld_context *ctx, const void *v, uint32_t n)
{
    float fb_h = FB_H(ctx), w = gli_size(ctx, GLI_LINE_WIDTH) * 0.5f; uint32_t i;
    mach64_sync_depth_from_gli(ctx);
    for (i = 1; i < n; i++) draw_line(ctx, VTX(v, i-1), VTX(v, i), fb_h, w);
}
void mach64_render_line_loop(gld_context *ctx, const void *v, uint32_t n)
{
    float fb_h = FB_H(ctx), w = gli_size(ctx, GLI_LINE_WIDTH) * 0.5f; uint32_t i;
    mach64_sync_depth_from_gli(ctx);
    for (i = 1; i < n; i++) draw_line(ctx, VTX(v, i-1), VTX(v, i), fb_h, w);
    if (n > 2) draw_line(ctx, VTX(v, n-1), VTX(v, 0), fb_h, w);   /* close the loop */
}

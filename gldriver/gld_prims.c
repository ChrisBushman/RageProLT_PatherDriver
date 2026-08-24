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
    float z = *(const float *)(v + GVOFF_Z);
    float w = *(const float *)(v + GVOFF_W);
    uint32_t argb = (chan(*(const float *)(v + GVOFF_R)) << 16)
                  | (chan(*(const float *)(v + GVOFF_G)) <<  8)
                  |  chan(*(const float *)(v + GVOFF_B));
    mach64_pack_vertex(o, x, fb_h - y, z, w, argb);   /* GL y-up -> FB y-down */
}

#define VTX(base, i)  ((const void *)((const unsigned char *)(base) + (i) * MACH64_GLIVERTEX_STRIDE))

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
    /* GEOMETRY IS FLOWING: GLEngine calls this (tbl+0x2c) with post-T&L vertices.
     * Dump vertex[0..2] at candidate offsets to pin the real screen-coord/color
     * layout (0x30/0x34/0x38 packed vs 0xf0/0xf4/0xf8 float from the T&L xform). */
    gld_log("mach64_render_tri_strip(n=%u v=%p)\n", n, v);
    if (v && n >= 2) { const unsigned char *p = (const unsigned char *)v; uint32_t k, j;
        /* Full 0x100-byte dump of v[0] and v[1] -- find the offset whose values
         * differ per-vertex as NDC (bf000000/3f000000) or screen (256/512/768). */
        for (k = 0; k < 2; k++) { const unsigned char *q = p + k*0x100;
            for (j = 0; j < 0x100; j += 0x20)
                gld_log("  v[%u]+0x%02x: %08x %08x %08x %08x %08x %08x %08x %08x\n", k, j,
                    *(const uint32_t*)(q+j+0x00),*(const uint32_t*)(q+j+0x04),
                    *(const uint32_t*)(q+j+0x08),*(const uint32_t*)(q+j+0x0c),
                    *(const uint32_t*)(q+j+0x10),*(const uint32_t*)(q+j+0x14),
                    *(const uint32_t*)(q+j+0x18),*(const uint32_t*)(q+j+0x1c)); } }
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
void mach64_render_points     (gld_context *c, const void *v, uint32_t n){ (void)c;(void)v;(void)n; }
void mach64_render_lines      (gld_context *c, const void *v, uint32_t n){ (void)c;(void)v;(void)n; }
void mach64_render_line_strip (gld_context *c, const void *v, uint32_t n){ (void)c;(void)v;(void)n; }
void mach64_render_line_loop  (gld_context *c, const void *v, uint32_t n){ (void)c;(void)v;(void)n; }

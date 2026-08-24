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

/* read one incoming gli vertex into our hardware m64_vertex.  X_Y/ARGB/Z arrive
 * already packed, so they copy straight across; S/T/W are floats for texturing. */
void mach64_read_vertex(const void *vp, m64_vertex *o)
{
    const unsigned char *v = (const unsigned char *)vp;
    o->x_y       = *(const int32_t  *)(v + VOFF_XY);
    o->argb      = *(const uint32_t *)(v + VOFF_ARGB);
    o->z         = *(const int32_t  *)(v + VOFF_Z);
    o->w         = *(const int32_t  *)(v + VOFF_W);
    o->s         = *(const int32_t  *)(v + VOFF_S);
    o->t         = *(const int32_t  *)(v + VOFF_T);
    o->spec_argb = *(const uint32_t *)(v + VOFF_SPEC);
}

#define VTX(base, i)  ((const void *)((const unsigned char *)(base) + (i) * MACH64_GLIVERTEX_STRIDE))

static void emit_tri(gld_context *ctx, const void *base,
                     uint32_t a, uint32_t b, uint32_t c)
{
    m64_vertex v0, v1, v2;
    mach64_read_vertex(VTX(base, a), &v0);
    mach64_read_vertex(VTX(base, b), &v1);
    mach64_read_vertex(VTX(base, c), &v2);
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
    if (v && n) { const unsigned char *p = (const unsigned char *)v; uint32_t k;
        for (k = 0; k < (n < 3 ? n : 3); k++) { const unsigned char *q = p + k*0x100;
            gld_log("  v[%u] @0x30:%08x %08x %08x  @0xf0:%08x %08x %08x  @0x60:%08x %08x\n",
                k,
                *(const uint32_t*)(q+0x30),*(const uint32_t*)(q+0x34),*(const uint32_t*)(q+0x38),
                *(const uint32_t*)(q+0xf0),*(const uint32_t*)(q+0xf4),*(const uint32_t*)(q+0xf8),
                *(const uint32_t*)(q+0x60),*(const uint32_t*)(q+0x64)); } }
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

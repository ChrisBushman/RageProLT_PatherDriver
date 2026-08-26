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
    uint32_t argb = (chan(*(const float *)(v + 0x3c)) << 24)   /* vertex colour alpha */
                  | (chan(*(const float *)(v + GVOFF_R)) << 16)
                  | (chan(*(const float *)(v + GVOFF_G)) <<  8)
                  |  chan(*(const float *)(v + GVOFF_B));
#ifdef GLD_FOGSCAN
    /* Learn the fog factor (+0x4c) vs colour alpha (+0x3c): print only when +0x4c
     * changes.  Tells us +0x4c's fog-OFF default (if a fixed 1.0, fog could run
     * unconditionally without the enable flag) and confirms it varies with depth. */
    {   static uint32_t fs_last = 0xdeadbeefu;
        uint32_t f4c = *(const uint32_t *)(v + 0x4c);
        if (f4c != fs_last) {
            fs_last = f4c;
            gld_log("FOGSCAN vtx +0x4c=0x%08x (%.3f)  +0x3c=0x%08x (%.3f)\n",
                    f4c, *(const float *)(v + 0x4c),
                    *(const uint32_t *)(v + 0x3c), *(const float *)(v + 0x3c));
        }
    }
#endif
    mach64_pack_vertex(o, x, fb_h - y, depth, w, argb);  /* GL y-up -> FB y-down */
    /* Per-vertex texcoords.  GLEngine hands us PERSPECTIVE-PREMULTIPLIED coords: +0x80/+0x84
     * hold S/w and T/w, and +0x0c holds 1/w (HW-proven via the cube test).  But the mach64
     * setup engine (with TEX_ST_DIRECT clear, our config) expects RAW S,T and does the
     * perspective divide itself -- address = interp(S*W)/interp(W) with W = 1/w -- so feeding
     * it the already-divided S/w makes it divide TWICE (address = S/w) and the texture warps
     * with perspective.  Un-premultiply here: raw S = (S/w)/(1/w).  (2D was immune: w=1.)
     * pack_vertex already put W = 1/w (from +0x0c) into o->w for the engine's divide. */
    {   float sw = *(const float *)(v + 0x80);   /* S/w */
        float tw = *(const float *)(v + 0x84);   /* T/w */
        float rs = (w != 0.0f) ? sw / w : sw;    /* w == 1/w_clip (from +0x0c) -> raw S */
        float rt = (w != 0.0f) ? tw / w : tw;
        __builtin_memcpy(&o->s, &rs, 4);
        __builtin_memcpy(&o->t, &rt, 4);
    }
    /* GL_FOG: the LT Pro sources the interpolated fog factor from the SPECULAR alpha
     * (VERTEX_SPEC_ARGB[31:24]) -- NOT the primary vertex alpha the Rage128 reference
     * driver assumes (HW-proven inert here).  The per-vertex fog factor is at gli-vtx
     * +0x4c; the fog blend (fragment vs DP_FOG_CLR) is armed by SCALE_3D_CNTL
     * ALPHA_FOG_EN=FOG (bit12) in mach64_apply_fog.  mach64_fog_on is set per-draw by
     * mach64_bind_texture. */
    if (mach64_fog_on) o->spec_argb = chan(*(const float *)(v + 0x4c)) << 24;
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

/* Half-texel inset: remap the texcoord so u in [0,1] samples texel CENTRES
 * [0.5/w .. (w-0.5)/w] instead of the boundaries (u' = u*(w-1)/w + 0.5/w).  The
 * mach64's REPEAT floor-addresses the edge S slightly below 0 to the last column
 * (a 1px wrong-colour line down the left/top edge); insetting pins the edge sample
 * to texel 0 / w-1.  Applied ONLY when the triangle's texcoords are within [0,1]
 * (a texture drawn once) -- a genuinely TILING texture (u>1) is left untouched so
 * its tiling stays exact and isn't compressed by the inset. */
static float vst(int32_t bits) { float f; __builtin_memcpy(&f, &bits, 4); return f; }
static void  set_s(m64_vertex *v, float f) { __builtin_memcpy(&v->s, &f, 4); }
static void  set_t(m64_vertex *v, float f) { __builtin_memcpy(&v->t, &f, 4); }

static void emit_tri(gld_context *ctx, const void *base,
                     uint32_t a, uint32_t b, uint32_t c)
{
    mach64_priv *p = MACH64_PRIV(ctx);
    m64_vertex v0, v1, v2;
    float fb_h = (float)*(const uint32_t *)((const unsigned char *)ctx + 0x1f4);
    mach64_read_vertex(VTX(base, a), &v0, fb_h);
    mach64_read_vertex(VTX(base, b), &v1, fb_h);
    mach64_read_vertex(VTX(base, c), &v2, fb_h);
    if (p->hw.textured && p->tex_w > 0 && p->tex_h > 0) {
        float s0=vst(v0.s), s1=vst(v1.s), s2=vst(v2.s);
        float t0=vst(v0.t), t1=vst(v1.t), t2=vst(v2.t);
        int within = (s0>=-0.001f && s0<=1.001f && s1>=-0.001f && s1<=1.001f &&
                      s2>=-0.001f && s2<=1.001f && t0>=-0.001f && t0<=1.001f &&
                      t1>=-0.001f && t1<=1.001f && t2>=-0.001f && t2<=1.001f);
        if (within) {                     /* non-tiling: inset to texel centres */
            float ss=(float)(p->tex_w-1)/(float)p->tex_w, bs=0.5f/(float)p->tex_w;
            float ts=(float)(p->tex_h-1)/(float)p->tex_h, bt=0.5f/(float)p->tex_h;
            set_s(&v0,s0*ss+bs); set_s(&v1,s1*ss+bs); set_s(&v2,s2*ss+bs);
            set_t(&v0,t0*ts+bt); set_t(&v1,t1*ts+bt); set_t(&v2,t2*ts+bt);
        }
    }
    mach64_draw_triangle(ctx, &v0, &v1, &v2);
}

/* Reject an absurd vertex count: the STRIP/FAN/QUAD dispatch slots are mis-mapped
 * (labels off by one -- see gld_dispatch.c) and a mis-dispatched slot can hand us
 * a garbage count, which without this guard runs a loop over unmapped memory and
 * crashes the app.  GL_TRIANGLES is correctly dispatched and unaffected. */
#define PRIM_SANE(n) do { if ((n) > 0x40000u) { \
        gld_log("mach64: bogus vertex count 0x%x -- skipping primitive\n", (n)); \
        return; } } while (0)

/* Elements-tris vector (dispatch slot 0x48).  Apple's GLEngine does T&L + clipping in
 * software: when a primitive is clipped (GL_SCISSOR_TEST, or a triangle crossing the view
 * frustum), it re-dispatches the resulting clipped CONVEX POLYGON here as an ARRAY OF
 * VERTEX POINTERS (v[0..n-1] are uint32 addresses), NOT the flat contiguous array the
 * DrawArrays slots receive.  Each pointer targets a vertex in the SAME layout as the flat
 * path (window x,y @ +0x00/+0x04, colour @ +0x30..+0x3c, NDC z @ +0x18, s/t @ +0x80/+0x84),
 * so mach64_read_vertex works on the dereferenced vertex.  The polygon is convex, so
 * tessellate it as a triangle FAN from vertex 0 -- the same winding the source primitive
 * had (the clip preserves it, matching the flat path's setup-engine face test).
 * Recovered 2026-08-26 via the scissor RE (docs/re-notes/scissor.md).  Note: carries no
 * half-texel inset (emit_tri's texture path), so a *textured* clipped polygon samples the
 * raw texcoords -- fine for the common untextured/colour case. */
void mach64_render_elt_tris(gld_context *ctx, const void *v, uint32_t n)
{
    const uint32_t *pa = (const uint32_t *)v;
    float fb_h = (float)*(const uint32_t *)((const unsigned char *)ctx + 0x1f4);
    m64_vertex v0, vp, vc;
    uint32_t i;
    PRIM_SANE(n);
    if (!v || n < 3) return;
    mach64_sync_depth_from_gli(ctx);
    mach64_bind_texture(ctx);
    mach64_read_vertex((const void *)(uintptr_t)pa[0], &v0, fb_h);   /* fan apex */
    for (i = 2; i < n; i++) {
        mach64_read_vertex((const void *)(uintptr_t)pa[i-1], &vp, fb_h);
        mach64_read_vertex((const void *)(uintptr_t)pa[i],   &vc, fb_h);
        mach64_draw_triangle(ctx, &v0, &vp, &vc);
    }
}

/* ---- triangle family (the proven hardware path) --------------------- */
void mach64_render_triangles(gld_context *ctx, const void *v, uint32_t n)
{
    uint32_t i;
    PRIM_SANE(n);
    gld_log("mach64_render_triangles(n=%u v=%p)\n", n, v);
    if (v && n > 0) {
        const unsigned char *p = (const unsigned char *)v;
        gld_log("  vtx0: XY=0x%08x ARGB=0x%08x Z=0x%08x\n",
                *(const uint32_t *)(p + VOFF_XY),
                *(const uint32_t *)(p + VOFF_ARGB),
                *(const uint32_t *)(p + VOFF_Z));
    }
    mach64_bind_texture(ctx);            /* upload+program texture if bound */
    for (i = 0; i + 3 <= n; i += 3) emit_tri(ctx, v, i, i+1, i+2);
}

void mach64_render_tri_strip(gld_context *ctx, const void *v, uint32_t n)
{
    uint32_t i;
    PRIM_SANE(n);
    mach64_sync_depth_from_gli(ctx);   /* Z_CNTL follows the app's GL depth state */
    mach64_bind_texture(ctx);          /* upload+program texture if bound */
    for (i = 2; i < n; i++)                      /* flip winding on odd tris */
        if (i & 1) emit_tri(ctx, v, i-1, i-2, i);
        else       emit_tri(ctx, v, i-2, i-1, i);
}

void mach64_render_tri_fan(gld_context *ctx, const void *v, uint32_t n)
{
    uint32_t i;
    PRIM_SANE(n);
    mach64_sync_depth_from_gli(ctx);
    mach64_bind_texture(ctx);          /* upload+program texture if bound */
    for (i = 2; i < n; i++) emit_tri(ctx, v, 0, i-1, i);
}

/* GL_TRIANGLE_FAN dispatch slot (table+0x30) uses a DIFFERENT calling convention
 * than every other primitive slot: GLEngine calls it as
 *   f(ctx, apex_vertex, perimeter_verts, perimeter_count)
 * with the fan apex in r4, the perimeter array (contiguous immediately after the
 * apex) in r5, and the perimeter vertex count in r6 -- NOT f(ctx, verts, count).
 * The generic render_tri_fan reads the count from r5, which here is the perimeter
 * POINTER -> a garbage count -> a runaway loop over unmapped memory and a crash
 * (recovered via the GLD_DISPATCH_PROBE run, tools/mach64_dispatchprobe).  The
 * apex is verts[0] and the perimeter is verts[1..], so the full contiguous array
 * is (perimeter_count + 1) vertices and the generic fan body over `apex` with that
 * total is correct. */
void mach64_render_tri_fan_slot(gld_context *ctx, const void *apex,
                                const void *perim, uint32_t m)
{
    (void)perim;   /* == apex + one vertex stride; array is contiguous from apex */
    mach64_render_tri_fan(ctx, apex, m + 1);
}

void mach64_render_polygon(gld_context *ctx, const void *v, uint32_t n)
{
    PRIM_SANE(n);
    mach64_render_tri_fan(ctx, v, n);            /* convex polygon = fan */
}

void mach64_render_quads(gld_context *ctx, const void *v, uint32_t n)
{
    uint32_t i;
    PRIM_SANE(n);
    mach64_sync_depth_from_gli(ctx);
    mach64_bind_texture(ctx);          /* upload+program texture if bound */
    for (i = 0; i + 4 <= n; i += 4) {            /* quad -> 2 tris */
        emit_tri(ctx, v, i,   i+1, i+2);
        emit_tri(ctx, v, i,   i+2, i+3);
    }
}

void mach64_render_quad_strip(gld_context *ctx, const void *v, uint32_t n)
{
    uint32_t i;
    PRIM_SANE(n);
    mach64_sync_depth_from_gli(ctx);
    mach64_bind_texture(ctx);          /* upload+program texture if bound */
    for (i = 0; i + 4 <= n; i += 2) {            /* strip quad -> 2 tris */
        emit_tri(ctx, v, i,   i+1, i+2);
        emit_tri(ctx, v, i+2, i+1, i+3);
    }
}

/* ---- point/line family: the mach64 setup engine is triangle-only, so each
 * point expands to a size x size quad and each line to a width-2 quad (draw_point/
 * draw_line above), both fed through the proven triangle path.  Dispatch slots are
 * 0x18 (POINTS), 0x1c (LINES), 0x20 (LINE_STRIP + LINE_LOOP) -- recovered by the
 * GLD_DISPATCH_PROBE run.  Each path calls mach64_bind_texture so it honours the
 * CURRENT GL texture state instead of inheriting whatever the last triangle draw
 * programmed (an untextured point stream after a textured quad would otherwise
 * sample stale texture bits).  Note: draw_point/draw_line carry no per-vertex
 * texcoords (s/t = 0), so a *textured* point/line samples texel (0,0) uniformly
 * rather than interpolating -- acceptable for the common untextured/colour case;
 * per-vertex-texcoord expansion is a TODO if a textured-line app needs it. -- */
#define FB_H(ctx) ((float)*(const uint32_t *)((const unsigned char *)(ctx) + 0x1f4))

/* gli offsets recovered by diffing point-size/line-width configs (tools/
 * mach64_ptlnstate): glPointSize float @0x6630, glLineWidth float @0x6394. */
#define GLI_POINT_SIZE  0x6630
#define GLI_LINE_WIDTH  0x6394

void mach64_render_points(gld_context *ctx, const void *v, uint32_t n)
{
    float fb_h = FB_H(ctx), s = gli_size(ctx, GLI_POINT_SIZE) * 0.5f; uint32_t i;
    PRIM_SANE(n);
    mach64_sync_depth_from_gli(ctx);
    mach64_bind_texture(ctx);          /* honour current texture state (usu. off) */
    for (i = 0; i < n; i++) draw_point(ctx, VTX(v, i), fb_h, s);
}
void mach64_render_lines(gld_context *ctx, const void *v, uint32_t n)
{
    float fb_h = FB_H(ctx), w = gli_size(ctx, GLI_LINE_WIDTH) * 0.5f; uint32_t i;
    PRIM_SANE(n);
    mach64_sync_depth_from_gli(ctx);
    mach64_bind_texture(ctx);
    for (i = 0; i + 2 <= n; i += 2) draw_line(ctx, VTX(v, i), VTX(v, i+1), fb_h, w);
}
void mach64_render_line_strip(gld_context *ctx, const void *v, uint32_t n)
{
    float fb_h = FB_H(ctx), w = gli_size(ctx, GLI_LINE_WIDTH) * 0.5f; uint32_t i;
    PRIM_SANE(n);
    mach64_sync_depth_from_gli(ctx);
    mach64_bind_texture(ctx);
    for (i = 1; i < n; i++) draw_line(ctx, VTX(v, i-1), VTX(v, i), fb_h, w);
}
void mach64_render_line_loop(gld_context *ctx, const void *v, uint32_t n)
{
    float fb_h = FB_H(ctx), w = gli_size(ctx, GLI_LINE_WIDTH) * 0.5f; uint32_t i;
    PRIM_SANE(n);
    mach64_sync_depth_from_gli(ctx);
    mach64_bind_texture(ctx);
    for (i = 1; i < n; i++) draw_line(ctx, VTX(v, i-1), VTX(v, i), fb_h, w);
    if (n > 2) draw_line(ctx, VTX(v, n-1), VTX(v, 0), fb_h, w);   /* close the loop */
}

/* ---- element (clipped) point/line vectors, slots 0x40 / 0x44 -------------
 * Like the tris case (mach64_render_elt_tris / slot 0x48), Apple's GLEngine software-clips
 * points and lines (scissor / frustum) and re-dispatches the survivors HERE as an ARRAY OF
 * VERTEX POINTERS (v[0..n-1] are heap addresses), not the flat contiguous array the
 * DrawArrays slots (0x18/0x1c) receive -- HW-verified via tools/mach64_clipptlntest.  A
 * clipped point survives unchanged (n = the surviving points); a clipped line keeps its 2
 * endpoints (moved onto the box edge).  Each pointer targets a vertex in the usual layout,
 * so draw_point/draw_line (which read window x,y @ +0x00/+0x04) work on the deref.  The
 * elt_is_ptr guard skips any non-pointer entry defensively. */
static int elt_is_ptr(uint32_t p) { return p > 0x10000u && p < 0x40000000u; }

void mach64_render_elt_points(gld_context *ctx, const void *v, uint32_t n)
{
    const uint32_t *pa = (const uint32_t *)v;
    float fb_h = FB_H(ctx), s = gli_size(ctx, GLI_POINT_SIZE) * 0.5f; uint32_t i;
    PRIM_SANE(n);
    if (!v) return;
    mach64_sync_depth_from_gli(ctx);
    mach64_bind_texture(ctx);
    for (i = 0; i < n; i++)
        if (elt_is_ptr(pa[i])) draw_point(ctx, (const void *)(uintptr_t)pa[i], fb_h, s);
}
void mach64_render_elt_lines(gld_context *ctx, const void *v, uint32_t n)
{
    const uint32_t *pa = (const uint32_t *)v;
    float fb_h = FB_H(ctx), w = gli_size(ctx, GLI_LINE_WIDTH) * 0.5f; uint32_t i;
    PRIM_SANE(n);
    if (!v) return;
    mach64_sync_depth_from_gli(ctx);
    mach64_bind_texture(ctx);
    for (i = 0; i + 2 <= n; i += 2)                 /* pointer pairs = clipped segments */
        if (elt_is_ptr(pa[i]) && elt_is_ptr(pa[i+1]))
            draw_line(ctx, (const void *)(uintptr_t)pa[i],
                           (const void *)(uintptr_t)pa[i+1], fb_h, w);
}

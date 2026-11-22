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
 * The mach64 setup engine is triangle-oriented, so every primitive is decomposed
 * to triangles: strips/fans/quads/polygons tessellate directly, and the point/line
 * family expands to quads (draw_point -> size x size quad, draw_line -> width-2
 * quad) fed through the same mach64_draw_triangle path.  ALL GL primitive modes are
 * implemented and wired (gld_dispatch.c): POINTS/LINES/LINE_STRIP/LINE_LOOP and the
 * clipped elt_points/elt_lines/elt_tris vectors, in addition to the triangle family.
 * (Point size / line width come from the gli -- GLI_POINT_SIZE / GLI_LINE_WIDTH.)
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

static uint32_t chan(float f){ return m64_f2u8(f); }

void mach64_read_vertex(const void *vp, m64_vertex *o, float fb_h)
{
    const unsigned char *v = (const unsigned char *)vp;
    float x = *(const float *)(v + GVOFF_X);
    float y = *(const float *)(v + GVOFF_Y);
    float w = *(const float *)(v + GVOFF_W);
    /* Depth for the Z buffer.  The screen-Z slot (+0x08) is 0 (GLEngine's window depth range
     * is degenerate for our surface), so derive it from +0x18.  +0x18 is CLIP-space z
     * (pre-perspective-divide); the true NDC z is clip_z / w = clip_z * (1/w), and +0x0c
     * holds 1/w.  (This used to treat +0x18 as NDC z directly -- correct for 2D/ortho where
     * w=1, but in perspective it left z well outside [-1,1] so depth clamped to the far plane
     * across much of the scene -> everything at max Z -> Z-fighting that flickered once a
     * textured cube rotated.)  window z = ndc_z*0.5 + 0.5 (default depth range). */
    float rw = (w != 0.0f) ? 1.0f / w : 1.0f;       /* shared by every texcoord pair below (fdivs is ~17 cycles on the G3) */
    float ndcz  = *(const float *)(v + 0x18) * w;   /* w == 1/w_clip (from +0x0c) */
    float depth = ndcz * 0.5f + 0.5f;
    uint32_t argb = (chan(*(const float *)(v + 0x3c)) << 24)   /* vertex colour alpha */
                  | (chan(*(const float *)(v + GVOFF_R)) << 16)
                  | (chan(*(const float *)(v + GVOFF_G)) <<  8)
                  |  chan(*(const float *)(v + GVOFF_B));
    mach64_pack_vertex(o, x, fb_h - y, depth, w, argb);  /* GL y-up -> FB y-down */
    /* Per-vertex texcoords.  GLEngine hands us PERSPECTIVE-PREMULTIPLIED coords: +0x80/+0x84
     * hold S/w and T/w, and +0x0c holds 1/w (HW-proven via the cube test).  But the mach64
     * setup engine (with TEX_ST_DIRECT clear, our config) expects RAW S,T and does the
     * perspective divide itself -- address = interp(S*W)/interp(W) with W = 1/w -- so feeding
     * it the already-divided S/w makes it divide TWICE (address = S/w) and the texture warps
     * with perspective.  Un-premultiply here: raw S = (S/w)/(1/w).  (2D was immune: w=1.)
     * pack_vertex already put W = 1/w (from +0x0c) into o->w for the engine's divide. */
    {   float sw = *(const float *)(v + mach64_tex_vofs[0]);        /* S/w (unit 0: +0x80) */
        float tw = *(const float *)(v + mach64_tex_vofs[0] + 4);    /* T/w */
        float rs = sw * rw;    /* w == 1/w_clip (from +0x0c) -> raw S = (S/w)/w; one reciprocal per vertex instead of a divide per coordinate */
        float rt = tw * rw;
        __builtin_memcpy(&o->s, &rs, 4);
        __builtin_memcpy(&o->t, &rt, 4);
    }
    /* Secondary texture unit (ARB_multitexture): same gli vertex, coordinates one unit stride (0x10) further on (+0x90/+0x94; the Rage128 driver
     * copies exactly these fields), un-premultiplied by the same 1/w.  The hardware has ONE W per vertex plus SECONDARY_W, both = 1/w. */
    if (mach64_tex_vofs[1]) {
        float sw = *(const float *)(v + mach64_tex_vofs[1]), tw = *(const float *)(v + mach64_tex_vofs[1] + 4);
        float rs = sw * rw, rt = tw * rw;
        __builtin_memcpy(&o->s2, &rs, 4); __builtin_memcpy(&o->t2, &rt, 4); o->w2 = o->w;
    }
    /* GL_FOG: the LT Pro sources the interpolated fog factor from the SPECULAR alpha
     * (VERTEX_SPEC_ARGB[31:24]) -- NOT the primary vertex alpha the Rage128 reference
     * driver assumes (HW-proven inert here).  The per-vertex fog factor is at gli-vtx
     * +0x4c; the fog blend (fragment vs DP_FOG_CLR) is armed by SCALE_3D_CNTL
     * ALPHA_FOG_EN=FOG (bit12) in mach64_apply_fog.  mach64_fog_on is set per-draw by
     * mach64_bind_texture. */
    if (mach64_fog_on) o->spec_argb = chan(*(const float *)(v + 0x4c)) << 24;
    if (mach64_spec_mode) {   /* separate specular colour: R,G,B floats at +0x40/+0x44/+0x48 */
        uint32_t sr = chan(*(const float *)(v + 0x40)), sg = chan(*(const float *)(v + 0x44)), sb = chan(*(const float *)(v + 0x48));
        if (mach64_spec_mode == 1) o->spec_argb |= (sr << 16) | (sg << 8) | sb;          /* hardware adds it after texturing */
        else {                                                                         /* untextured: the hardware only adds it to textured pixels -> fold into the colour */
            uint32_t r = ((o->argb >> 16) & 0xff) + sr, gg = ((o->argb >> 8) & 0xff) + sg, b = (o->argb & 0xff) + sb;
            if (r > 255) r = 255; if (gg > 255) gg = 255; if (b > 255) b = 255;
            o->argb = (o->argb & 0xff000000u) | (r << 16) | (gg << 8) | b;
        }
    }
}

#define VTX(base, i)  ((const void *)((const unsigned char *)(base) + (i) * MACH64_GLIVERTEX_STRIDE))

/* Pack an m64_vertex at gli-screen (gx,gy), taking colour/depth/w from vp.
 * Used to expand points/lines into quads (the mach64 setup engine only does
 * triangles), same y-flip + colour fold as mach64_read_vertex. */
static void pack_at(const void *vp, float gx, float gy, float fb_h, m64_vertex *o,
                    int tex_w, int tex_h)
{
    const unsigned char *v = (const unsigned char *)vp;
    float w     = *(const float *)(v + GVOFF_W);              /* 1/w_clip */
    float depth = *(const float *)(v + 0x18) * w * 0.5f + 0.5f; /* clip_z*(1/w) -> NDC -> [0,1] */
    uint32_t argb = (chan(*(const float *)(v + GVOFF_R)) << 16)
                  | (chan(*(const float *)(v + GVOFF_G)) <<  8)
                  |  chan(*(const float *)(v + GVOFF_B));
    mach64_pack_vertex(o, gx, fb_h - gy, depth, w, argb);   /* zeros o->s/o->t */
    /* Per-vertex texcoords, so a TEXTURED point/line samples the vertex's texcoord rather
     * than texel (0,0).  GLEngine stores S/w,T/w at +0x80/+0x84 and 1/w at +0x0c (= w here),
     * so un-premultiply to raw S,T -- same as mach64_read_vertex.  draw_point passes the same
     * vertex for all 4 quad corners (uniform texcoord across the point); draw_line passes each
     * endpoint (the scaler interpolates the texture along the line). */
    {   float sw = *(const float *)(v + mach64_tex_vofs[0]), tw = *(const float *)(v + mach64_tex_vofs[0] + 4);
        float rs = (w != 0.0f) ? sw / w : sw, rt = (w != 0.0f) ? tw / w : tw;
        /* Half-texel inset for a texcoord in [0,1] (same fix emit_tri does for triangles):
         * remap so the edge samples texel CENTRES [0.5/w..(w-0.5)/w] instead of the boundary
         * -- kills the mach64 REPEAT wrap-around line at s/t=0 (the last column bleeding onto
         * the first pixel).  Skipped for tiling coords (>1) so tiling stays exact. */
        if (tex_w > 0 && rs >= -0.001f && rs <= 1.001f && rt >= -0.001f && rt <= 1.001f) {
            rs = rs * (float)(tex_w - 1) / (float)tex_w + 0.5f / (float)tex_w;
            rt = rt * (float)(tex_h - 1) / (float)tex_h + 0.5f / (float)tex_h;
        }
        __builtin_memcpy(&o->s, &rs, 4); __builtin_memcpy(&o->t, &rt, 4);
    }
    if (mach64_tex_vofs[1]) {      /* secondary unit: same un-premultiply, no inset (points/lines sample the vertex coordinate directly) */
        float sw = *(const float *)(v + mach64_tex_vofs[1]), tw = *(const float *)(v + mach64_tex_vofs[1] + 4);
        float rs = (w != 0.0f) ? sw / w : sw, rt = (w != 0.0f) ? tw / w : tw;
        __builtin_memcpy(&o->s2, &rs, 4); __builtin_memcpy(&o->t2, &rt, 4); o->w2 = o->w;
    }
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
    mach64_priv *p = MACH64_PRIV(ctx);
    const unsigned char *v = (const unsigned char *)vp;
    float x = *(const float *)(v + GVOFF_X), y = *(const float *)(v + GVOFF_Y);
    int tw = (p->hw.textured && p->tex_w > 0 && p->tex_h > 0) ? p->tex_w : 0, th = p->tex_h;
    m64_vertex a, b, c, d;
    pack_at(vp, x - s, y - s, fb_h, &a, tw, th);
    pack_at(vp, x + s, y - s, fb_h, &b, tw, th);
    pack_at(vp, x - s, y + s, fb_h, &c, tw, th);
    pack_at(vp, x + s, y + s, fb_h, &d, tw, th);
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
    mach64_priv *p = MACH64_PRIV(ctx);
    float adx = x1 - x0, ady = y1 - y0, ix, iy;
    int tw = (p->hw.textured && p->tex_w > 0 && p->tex_h > 0) ? p->tex_w : 0, th = p->tex_h;
    m64_vertex A, B, C, D;
    if (adx < 0) adx = -adx;
    if (ady < 0) ady = -ady;
    if (adx > ady) { ix = 0.f; iy = wdt; } else { ix = wdt; iy = 0.f; }
    pack_at(v0, x0 - ix, y0 - iy, fb_h, &A, tw, th);
    pack_at(v1, x1 - ix, y1 - iy, fb_h, &B, tw, th);
    pack_at(v0, x0 + ix, y0 + iy, fb_h, &C, tw, th);
    pack_at(v1, x1 + ix, y1 + iy, fb_h, &D, tw, th);
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

/* Draw one triangle from three vertices that mach64_read_vertex has already converted (r0..r2; gv0..gv2 are the matching GLEngine vertices, for the
 * LOD area).  Fans, strips and quads convert each vertex ONCE and call this per triangle instead of re-reading 3 vertices per triangle. */
/* G3 maths: no FPU compares / divides per triangle.
 * m64_uv_within: all six texcoords in [-0.001, 1.001], tested on the float BIT PATTERNS (for IEEE singles the magnitude order equals the order of the bits with the sign cleared;
 * NaN/Inf fail both tests exactly as the float compare did).  m64_inset: s' = s*(w-1)/w + 0.5/w constants; for the power-of-two sizes the hardware needs they are exact from a table
 * (no fdivs); other sizes fall back to the divide. */
static inline int m64_bits_in_range(int32_t b)
{
    uint32_t mag = (uint32_t)b & 0x7fffffffu;
    return b < 0 ? mag <= 0x3a83126fu : mag <= 0x3f8020c5u;         /* 0.001f , 1.001f */
}
static inline int m64_uv_within(int32_t s0, int32_t s1, int32_t s2, int32_t t0, int32_t t1, int32_t t2)
{
    return m64_bits_in_range(s0) & m64_bits_in_range(s1) & m64_bits_in_range(s2) & m64_bits_in_range(t0) & m64_bits_in_range(t1) & m64_bits_in_range(t2);
}
static inline void m64_inset(int n, float *scale, float *bias)
{
    static const float inv[13] = { 1.0f, 0.5f, 0.25f, 0.125f, 0.0625f, 0.03125f, 0.015625f, 0.0078125f, 0.00390625f, 0.001953125f, 0.0009765625f, 0.00048828125f, 0.000244140625f };
    unsigned u = (unsigned)n;
    if (u && (u & (u - 1)) == 0 && u <= 4096u) { unsigned z; int lg; __asm__("cntlzw %0,%1" : "=r"(z) : "r"(u)); lg = 31 - (int)z; *scale = 1.0f - inv[lg]; *bias = 0.5f * inv[lg]; }
    else { *scale = (float)(n - 1) / (float)n; *bias = 0.5f / (float)n; }
}
/* m64_vertex is 10 words: copy them inline (the struct assignment compiled to a memcpy() call per vertex, ~2% of the main thread) */
static inline __attribute__((always_inline)) void m64_vcopy(m64_vertex *d, const m64_vertex *s)
{
    d->x_y = s->x_y; d->z = s->z; d->w = s->w; d->s = s->s; d->t = s->t; d->spec_argb = s->spec_argb; d->argb = s->argb; d->s2 = s->s2; d->t2 = s->t2; d->w2 = s->w2;
}
static void emit_tri_rv(gld_context *ctx, const m64_vertex *r0, const m64_vertex *r1, const m64_vertex *r2,
                        const void *gv0, const void *gv1, const void *gv2)
{
    mach64_priv *p = MACH64_PRIV(ctx);
    /* Work on the caller's vertices through const pointers; copy them (OWN) only when this triangle has to modify one (flat colour / texel inset): most triangles need no copy. */
    const m64_vertex *q0 = r0, *q1 = r1, *q2 = r2;
    m64_vertex v0, v1, v2; int own = 0;
#define OWN() do { if (!own) { m64_vcopy(&v0, r0); m64_vcopy(&v1, r1); m64_vcopy(&v2, r2); q0 = &v0; q1 = &v1; q2 = &v2; own = 1; } } while (0)
    /* glShadeModel(GL_FLAT): flatten to the provoking (GL default = LAST) vertex's colour so
     * the 3D scaler interpolates a constant.  For GL_TRIANGLES v2 is the provoking vertex. */
    if (mach64_flat_on) { OWN(); v0.argb = v2.argb; v1.argb = v2.argb; }
    if (p->hw.textured && p->tex_slot) {        /* software mip level / min filter for this triangle */
        const unsigned char *g0 = (const unsigned char *)gv0, *g1 = (const unsigned char *)gv1, *g2 = (const unsigned char *)gv2;
        float x0 = *(const float *)(g0 + GVOFF_X), y0 = *(const float *)(g0 + GVOFF_Y);
        float x1 = *(const float *)(g1 + GVOFF_X), y1 = *(const float *)(g1 + GVOFF_Y);
        float x2 = *(const float *)(g2 + GVOFF_X), y2 = *(const float *)(g2 + GVOFF_Y);
        float ds0 = vst(q0->s), ds1 = vst(q1->s), ds2 = vst(q2->s), dt0 = vst(q0->t), dt1 = vst(q1->t), dt2 = vst(q2->t);
        mach64_tex_select_lod(ctx, 0.5f * ((x1 - x0) * (y2 - y0) - (x2 - x0) * (y1 - y0)),
                                   0.5f * ((ds1 - ds0) * (dt2 - dt0) - (ds2 - ds0) * (dt1 - dt0)));
    }
    if (p->hw.textured && p->tex_w > 0 && p->tex_h > 0) {
        if (m64_uv_within(q0->s, q1->s, q2->s, q0->t, q1->t, q2->t)) {      /* non-tiling: inset to texel centres */
            float ss, bs, ts, bt; m64_inset(p->tex_w, &ss, &bs); m64_inset(p->tex_h, &ts, &bt);
            OWN();
            set_s(&v0, vst(v0.s)*ss+bs); set_s(&v1, vst(v1.s)*ss+bs); set_s(&v2, vst(v2.s)*ss+bs);
            set_t(&v0, vst(v0.t)*ts+bt); set_t(&v1, vst(v1.t)*ts+bt); set_t(&v2, vst(v2.t)*ts+bt);
        }
    }
    if (p->hw.textured && p->hw.multitex && p->tex2_w > 0 && p->tex2_h > 0) {      /* same half-texel inset for the secondary map */
        if (m64_uv_within(q0->s2, q1->s2, q2->s2, q0->t2, q1->t2, q2->t2)) {
            float ss, bs, ts, bt; m64_inset(p->tex2_w, &ss, &bs); m64_inset(p->tex2_h, &ts, &bt);
            OWN();
            { float f; f=vst(v0.s2)*ss+bs; __builtin_memcpy(&v0.s2,&f,4); f=vst(v1.s2)*ss+bs; __builtin_memcpy(&v1.s2,&f,4); f=vst(v2.s2)*ss+bs; __builtin_memcpy(&v2.s2,&f,4);
                       f=vst(v0.t2)*ts+bt; __builtin_memcpy(&v0.t2,&f,4); f=vst(v1.t2)*ts+bt; __builtin_memcpy(&v1.t2,&f,4); f=vst(v2.t2)*ts+bt; __builtin_memcpy(&v2.t2,&f,4); }
        }
    }
#undef OWN
    mach64_draw_triangle(ctx, q0, q1, q2);
}

#define FBH_OF(ctx) ((float)*(const uint32_t *)((const unsigned char *)(ctx) + 0x1f4))
static void emit_tri(gld_context *ctx, const void *base, uint32_t a, uint32_t b, uint32_t c)
{
    m64_vertex v0, v1, v2; float fb_h = FBH_OF(ctx);
    mach64_read_vertex(VTX(base, a), &v0, fb_h);
    mach64_read_vertex(VTX(base, b), &v1, fb_h);
    mach64_read_vertex(VTX(base, c), &v2, fb_h);
    emit_tri_rv(ctx, &v0, &v1, &v2, VTX(base, a), VTX(base, b), VTX(base, c));
}

/* Reject an absurd vertex count: the STRIP/FAN/QUAD dispatch slots are mis-mapped
 * (labels off by one -- see gld_dispatch.c) and a mis-dispatched slot can hand us
 * a garbage count, which without this guard runs a loop over unmapped memory and
 * crashes the app.  GL_TRIANGLES is correctly dispatched and unaffected. */
#define PRIM_SANE(n) do { if ((n) > 0x40000u) return; } while (0)

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
    m64_vertex v0, vq[2];
    uint32_t i;
    PRIM_SANE(n);
    if (!v || n < 3) return;
    mach64_sync_depth_from_gli(ctx);
    mach64_bind_texture(ctx);
    mach64_read_vertex((const void *)(uintptr_t)pa[0], &v0, fb_h);   /* fan apex */
    mach64_read_vertex((const void *)(uintptr_t)pa[1], &vq[1], fb_h);
    for (i = 2; i < n; i++) {                                         /* each perimeter vertex is converted once and reused by the next triangle */
        m64_vertex *vp = &vq[(i - 1) & 1], *vc = &vq[i & 1];
        mach64_read_vertex((const void *)(uintptr_t)pa[i], vc, fb_h);
        mach64_draw_triangle(ctx, &v0, vp, vc);
    }
}

/* ---- triangle family (the proven hardware path) --------------------- */
void mach64_render_triangles(gld_context *ctx, const void *v, uint32_t n)
{
    uint32_t i;
    PRIM_SANE(n);
    mach64_sync_depth_from_gli(ctx);     /* Z_CNTL follows the app's GL depth state (GL_TRIANGLES
                                          * was missing this -- strip/fan/quad have it -- so a
                                          * depth-tested glBegin(GL_TRIANGLES) never enabled Z). */
    mach64_bind_texture(ctx);            /* upload+program texture if bound */
    for (i = 0; i + 3 <= n; i += 3) emit_tri(ctx, v, i, i+1, i+2);
}

void mach64_render_tri_strip(gld_context *ctx, const void *v, uint32_t n)
{
    uint32_t i;
    PRIM_SANE(n);
    mach64_sync_depth_from_gli(ctx);   /* Z_CNTL follows the app's GL depth state */
    mach64_bind_texture(ctx);          /* upload+program texture if bound */
    if (n < 3) return;
    {   m64_vertex q[3]; float fb_h = FBH_OF(ctx);        /* rolling window of 3 converted vertices: each vertex is read once */
        mach64_read_vertex(VTX(v, 0), &q[0], fb_h);
        mach64_read_vertex(VTX(v, 1), &q[1], fb_h);
        for (i = 2; i < n; i++) {
            m64_vertex *a = &q[(i - 2) % 3], *b = &q[(i - 1) % 3], *c = &q[i % 3];
            mach64_read_vertex(VTX(v, i), c, fb_h);
            if (i & 1) emit_tri_rv(ctx, b, a, c, VTX(v, i-1), VTX(v, i-2), VTX(v, i));   /* flip winding on odd tris */
            else       emit_tri_rv(ctx, a, b, c, VTX(v, i-2), VTX(v, i-1), VTX(v, i));
        } }
}

void mach64_render_tri_fan(gld_context *ctx, const void *v, uint32_t n)
{
    uint32_t i;
    PRIM_SANE(n);
    mach64_sync_depth_from_gli(ctx);
    mach64_bind_texture(ctx);          /* upload+program texture if bound */
    if (n < 3) return;
    {   m64_vertex a, q[2]; float fb_h = FBH_OF(ctx);      /* apex converted once, each perimeter vertex once */
        mach64_read_vertex(VTX(v, 0), &a, fb_h);
        mach64_read_vertex(VTX(v, 1), &q[1], fb_h);
        for (i = 2; i < n; i++) {
            m64_vertex *b = &q[(i - 1) & 1], *c = &q[i & 1];
            mach64_read_vertex(VTX(v, i), c, fb_h);
            emit_tri_rv(ctx, &a, b, c, VTX(v, 0), VTX(v, i-1), VTX(v, i));
        } }
}

/* GL_TRIANGLE_FAN dispatch slot (table+0x30) uses a DIFFERENT calling convention
 * than every other primitive slot: GLEngine calls it as
 *   f(ctx, apex_vertex, perimeter_verts, perimeter_count)
 * with the fan apex in r4, the perimeter array (contiguous immediately after the
 * apex) in r5, and the perimeter vertex count in r6 -- NOT f(ctx, verts, count).
 * The generic render_tri_fan reads the count from r5, which here is the perimeter
 * POINTER -> a garbage count -> a runaway loop over unmapped memory and a crash
 *.  The
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
    for (i = 0; i + 4 <= n; i += 4) {            /* quad -> 2 tris (4 vertex conversions, not 6) */
        m64_vertex q[4]; float fb_h = FBH_OF(ctx); int k;
        for (k = 0; k < 4; k++) mach64_read_vertex(VTX(v, i + k), &q[k], fb_h);
        emit_tri_rv(ctx, &q[0], &q[1], &q[2], VTX(v, i), VTX(v, i+1), VTX(v, i+2));
        emit_tri_rv(ctx, &q[0], &q[2], &q[3], VTX(v, i), VTX(v, i+2), VTX(v, i+3));
    }
}

void mach64_render_quad_strip(gld_context *ctx, const void *v, uint32_t n)
{
    uint32_t i;
    PRIM_SANE(n);
    mach64_sync_depth_from_gli(ctx);
    mach64_bind_texture(ctx);          /* upload+program texture if bound */
    for (i = 0; i + 4 <= n; i += 2) {            /* strip quad -> 2 tris */
        m64_vertex q[4]; float fb_h = FBH_OF(ctx); int k;
        for (k = 0; k < 4; k++) mach64_read_vertex(VTX(v, i + k), &q[k], fb_h);
        emit_tri_rv(ctx, &q[0], &q[1], &q[2], VTX(v, i), VTX(v, i+1), VTX(v, i+2));
        emit_tri_rv(ctx, &q[2], &q[1], &q[3], VTX(v, i+2), VTX(v, i+1), VTX(v, i+3));
    }
}

/* ---- point/line family: the mach64 setup engine is triangle-only, so each
 * point expands to a size x size quad and each line to a width-2 quad (draw_point/
 * draw_line above), both fed through the proven triangle path.  Dispatch slots are
 * 0x18 (POINTS), 0x1c (LINES), 0x20 (LINE_STRIP + LINE_LOOP) -- recovered empirically.
 * Each path calls mach64_bind_texture so it honours the
 * CURRENT GL texture state instead of inheriting whatever the last triangle draw
 * programmed (an untextured point stream after a textured quad would otherwise
 * sample stale texture bits).  pack_at now carries per-vertex texcoords, so a
 * TEXTURED point samples its vertex's texel (uniform across the point) and a
 * TEXTURED line interpolates the texture along its length. -- */
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

/* ---- element (clipped) point/line vectors, slots 0x40 / 0x44 -------------
 * Like the tris case (mach64_render_elt_tris / slot 0x48), Apple's GLEngine software-clips
 * points and lines (scissor / frustum) and re-dispatches the survivors HERE as an ARRAY OF
 * VERTEX POINTERS (v[0..n-1] are heap addresses), not the flat contiguous array the
 * DrawArrays slots (0x18/0x1c) receive -- HW-verified via tools/mach64_clipptlntest.  A
 * clipped point survives unchanged (n = the surviving points); a clipped line keeps its 2
 * endpoints (moved onto the box edge).  Each pointer targets a vertex in the usual layout,
 * so draw_point/draw_line (which read window x,y @ +0x00/+0x04) work on the deref.  The
 * elt_is_ptr guard skips any non-pointer entry defensively. */
static int elt_is_ptr(uint32_t p) { return p > 0x10000u && p <= M64_UADDR_MAX; }

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

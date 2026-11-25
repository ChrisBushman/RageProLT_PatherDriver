/*
 * gld_stencil.c - stencil buffer emulation (the chip has no stencil planes) and the depth shadow it needs.  Integer only.
 *
 * GLEngine's stencil state (static analysis of GLEngine: glStencilFunc 0x38528, glStencilOp, glGetIntegerv 0x76cfc/0x7fd0c, glEnable 0x805d8/0x81634):
 *   face = byte gli[0x14612] (the active face, glActiveStencilFaceEXT / glStencil*Separate: 0 front, 1 back); per face at face*0x10:
 *   +0x670c value mask (word)  +0x6710 reference (word, clamped to (1 << desc[0x38]) - 1)  +0x6714 func  +0x6716 sfail  +0x6718 dpfail  +0x671a dppass (halfwords)
 *   write mask gli[0x63ac + face*4], clear value gli[0x6334], gli[0x6734]: bit0/bit1 = stencil test enabled for face 0/1, bit2 = GL_STENCIL_TEST_TWO_SIDE_EXT,
 *   front face halfword gli[0x66e8] (0x900 CW, 0x901 CCW).
 *
 * Three ways to execute it, chosen per state change (mach64_zs_sync):
 *   mode 0  stencil off.
 *   mode 1  HARDWARE Z-AS-STENCIL: depth test off, one face, masks 0xff, ops KEEP/REPLACE (ZERO with ALWAYS or ref 0).  The Z buffer holds (s * 257) >> 1 (the chip stores the 16-bit depth shifted down one bit) and a stencil-tested
 *           draw gives every vertex the constant depth ref * 257; the chip's own Z compare is the stencil compare and the Z write is the pass op.  Full speed.
 *   mode 2  CPU: everything else (INCR/DECR/INVERT/wraps, any mask, sfail/dpfail ops, depth test together with stencil, two-sided).  A CPU stencil buffer (and a CPU depth
 *           buffer when the depth test is on) is run by an integer rasteriser with the chip's own quarter-pixel coordinates and a top-left fill rule; the pixels that pass
 *           are drawn by the hardware as scissor rectangles (the hardware depth test is off in this mode: the CPU depth buffer is the truth).
 * The two planes (stencil, depth) each have their truth in one place: LOC_UNI (every value equal: a fresh clear, free), LOC_CPU (the buffer in memory) or LOC_HW (the chip's
 * Z buffer, which can hold only ONE plane at a time; moving a plane in or out costs a readback/upload, skipped when the chip's copy is known to equal the CPU copy or to be
 * uniform).  Known limit: fragments the alpha test or a texture's alpha discards still update the stencil buffer (the CPU does not see texels).
 */
#include <stdio.h>
#include "gld.h"
#include "gld_fxmap.h"
#include "gld_cpuops.h"

#define OP_KEEP      0x1e00u
#define OP_REPLACE   0x1e01u
#define OP_INCR      0x1e02u
#define OP_DECR      0x1e03u
#define OP_INVERT    0x150au
#define OP_INCR_WRAP 0x8507u
#define OP_DECR_WRAP 0x8508u
#define OP_ZERO      0x0000u
#define F_NEVER 0x200u
#define F_ALWAYS 0x207u

typedef struct {
    uint16_t func, sfail, dpfail, dppass; uint8_t ref, vmask, wmask; uint8_t built;      /* built: the tables below match the fields above */
    uint8_t spass[256], nsf[256], ndf[256], ndp[256];      /* per old stencil value: test passes / value after sfail, dpfail, dppass (write mask applied) */
} zs_face;
enum { LOC_UNI, LOC_CPU, LOC_HW };
typedef struct { int loc; uint32_t uni; int hw_uni, hw_cpu; void *buf; } zs_plane;
#define PL_S 0
#define PL_D 1
#define MAXRECT 512
typedef struct { int x0, x1, y0, y1; } zrect;
typedef struct {
    zs_plane pl[2];
    uint32_t stride, rows;
    zs_face face[2];
    int fen[2], two, front_ccw, depth_en, dfunc, dmask, m1_ref, m1_func, m1_op;
    int envcpu;                   /* a texture environment the chip cannot do (GL_BLEND, GL_COMBINE): textured triangles are shaded on the CPU */
    int cb;                       /* blending the chip cannot do (subtract / min / max equations, constant-colour or saturate factors): untextured triangles are composited on the CPU */
    int rn; zrect r[MAXRECT];
} zs_state;

static zs_state *zs_get(mach64_priv *p)
{
    zs_state *z = (zs_state *)p->zs;
    if (!z) { z = (zs_state *)calloc(1, sizeof *z); p->zs = z; }
    return z;
}
void mach64_zs_free(mach64_priv *p)
{
    zs_state *z = (zs_state *)p->zs;
    if (!z) return;
    free(z->pl[0].buf); free(z->pl[1].buf); free(z); p->zs = 0;
}
/* a new surface: everything is undefined -> stencil 0, depth far, nothing in the chip */
void mach64_zs_reset(mach64_priv *p)
{
    zs_state *z = zs_get(p);
    if (!z) return;
    free(z->pl[0].buf); free(z->pl[1].buf);
    memset(z->pl, 0, sizeof z->pl);
    z->pl[PL_D].uni = 0xffffu;
    z->stride = p->rb.pitch; z->rows = p->rb.h;
    p->zs_mode = 0;
}

static uint32_t apply_op(uint32_t op, uint32_t s, uint32_t ref)
{
    switch (op) {
    case OP_KEEP: return s;
    case OP_ZERO: return 0;
    case OP_REPLACE: return ref;
    case OP_INCR: return s < 255u ? s + 1u : 255u;
    case OP_DECR: return s ? s - 1u : 0u;
    case OP_INVERT: return ~s & 255u;
    case OP_INCR_WRAP: return (s + 1u) & 255u;
    case OP_DECR_WRAP: return (s - 1u) & 255u;
    default: return s;
    }
}
static int cmpf(uint32_t func, uint32_t a, uint32_t b)       /* a FUNC b, GL enumerants 0x200..0x207 */
{
    switch (func) {
    case 0x200: return 0;
    case 0x201: return a < b;
    case 0x202: return a == b;
    case 0x203: return a <= b;
    case 0x204: return a > b;
    case 0x205: return a != b;
    case 0x206: return a >= b;
    default: return 1;
    }
}

/* ---- planes: moving truth between the CPU copy and the chip's Z buffer ---- */
static int ensure_buf(zs_state *z, int which)
{
    zs_plane *pl = &z->pl[which];
    if (!pl->buf) pl->buf = malloc((size_t)z->stride * z->rows * (which == PL_S ? 1u : 2u));
    return pl->buf != 0;
}
#define XCHUNK_ROWS(z) ((32768u / (z->stride * 2u)) ? (32768u / (z->stride * 2u)) : 1u)
static void hw_read_plane(gld_context *ctx, zs_state *z, int which)      /* chip Z -> CPU buffer */
{
    mach64_priv *p = MACH64_PRIV(ctx);
    uint16_t *tmp; uint32_t row = 0, nr = XCHUNK_ROWS(z), i;
    if (!ensure_buf(z, which)) return;
    tmp = (uint16_t *)malloc((size_t)nr * z->stride * 2u);
    if (!tmp) return;
    mach64_flush_batch(ctx);
    while (row < z->rows) {
        uint32_t n = z->rows - row < nr ? z->rows - row : nr, cnt = n * z->stride;
        rgb_readback2(&p->rb, p->rb.z_off + row * z->stride * 2u, tmp, cnt * 2u, 1u /* SWAP16 */);
        if (which == PL_S) { uint8_t *d = (uint8_t *)z->pl[PL_S].buf + (size_t)row * z->stride; for (i = 0; i < cnt; i++) d[i] = (uint8_t)((tmp[i] * 2u + 129u) / 257u); }
        else memcpy((uint16_t *)z->pl[PL_D].buf + (size_t)row * z->stride, tmp, (size_t)cnt * 2u);
        row += n;
    }
    free(tmp);
}
static void hw_write_plane(gld_context *ctx, zs_state *z, int which)     /* CPU buffer -> chip Z */
{
    mach64_priv *p = MACH64_PRIV(ctx);
    uint16_t *tmp; uint32_t row = 0, nr = XCHUNK_ROWS(z), i;
    tmp = (uint16_t *)malloc((size_t)nr * z->stride * 2u);
    if (!tmp) return;
    mach64_flush_batch(ctx);
    while (row < z->rows) {
        uint32_t n = z->rows - row < nr ? z->rows - row : nr, cnt = n * z->stride;
        if (which == PL_S) { const uint8_t *s = (const uint8_t *)z->pl[PL_S].buf + (size_t)row * z->stride; for (i = 0; i < cnt; i++) tmp[i] = (uint16_t)((s[i] * 257u) >> 1); }
        else memcpy(tmp, (const uint16_t *)z->pl[PL_D].buf + (size_t)row * z->stride, (size_t)cnt * 2u);
        rgb_upload(&p->rb, p->rb.z_off + row * z->stride * 2u, tmp, cnt * 2u, 1);
        row += n;
    }
    free(tmp);
}
static void fill_plane(zs_state *z, int which)                            /* materialise a uniform plane */
{
    zs_plane *pl = &z->pl[which]; size_t n = (size_t)z->stride * z->rows, i;
    if (!ensure_buf(z, which)) return;
    if (which == PL_S) memset(pl->buf, (int)pl->uni, n);
    else { uint16_t *d = (uint16_t *)pl->buf; uint16_t v = (uint16_t)pl->uni; for (i = 0; i < n; i++) d[i] = v; }
}
static uint32_t hw_encode(int which, uint32_t uni) { return which == PL_S ? (uni * 257u) >> 1 : uni; }
static void plane_leave_hw(gld_context *ctx, zs_state *z, int which)
{
    zs_plane *pl = &z->pl[which];
    if (pl->loc != LOC_HW) return;
    if (pl->hw_uni) pl->loc = LOC_UNI;
    else if (pl->hw_cpu) pl->loc = LOC_CPU;
    else { hw_read_plane(ctx, z, which); pl->loc = LOC_CPU; }
}
static void plane_to_hw(gld_context *ctx, zs_state *z, int which)
{
    mach64_priv *p = MACH64_PRIV(ctx);
    zs_plane *pl = &z->pl[which];
    if (pl->loc == LOC_HW || !p->rb.z_off) return;
    plane_leave_hw(ctx, z, 1 - which);
    if (pl->loc == LOC_UNI) { mach64_flush_batch(ctx); rgb_clear_z(&p->rb, hw_encode(which, pl->uni)); pl->hw_uni = 1; pl->hw_cpu = 0; }
    else { hw_write_plane(ctx, z, which); pl->hw_cpu = 1; pl->hw_uni = 0; }
    pl->loc = LOC_HW;
    p->dirty |= MACH64_DIRTY_STATE;                 /* the kernel Z fill/upload reprogrammed the engine */
}
static void plane_to_cpu(gld_context *ctx, zs_state *z, int which)
{
    zs_plane *pl = &z->pl[which];
    if (pl->loc == LOC_HW) plane_leave_hw(ctx, z, which);
    if (pl->loc == LOC_UNI) { fill_plane(z, which); pl->loc = LOC_CPU; }
}

void mach64_zs_clear_depth(gld_context *ctx)
{
    mach64_priv *p = MACH64_PRIV(ctx); zs_state *z = zs_get(p); zs_plane *pl;
    if (!z) return;
    pl = &z->pl[PL_D]; pl->uni = 0xffffu;
    if (pl->loc == LOC_HW) { mach64_flush_batch(ctx); rgb_clear_z(&p->rb, 0xffffu); pl->hw_uni = 1; pl->hw_cpu = 0; p->dirty |= MACH64_DIRTY_STATE; }
    else pl->loc = LOC_UNI;
}
void mach64_zs_clear_stencil(gld_context *ctx, uint32_t v)
{
    mach64_priv *p = MACH64_PRIV(ctx); zs_state *z = zs_get(p); zs_plane *pl;
    if (!z) return;
    pl = &z->pl[PL_S]; pl->uni = v & 0xffu;
    if (pl->loc == LOC_HW) { mach64_flush_batch(ctx); rgb_clear_z(&p->rb, (pl->uni * 257u) >> 1); pl->hw_uni = 1; pl->hw_cpu = 0; p->dirty |= MACH64_DIRTY_STATE; }
    else pl->loc = LOC_UNI;
}
/* glReadPixels(GL_DEPTH_COMPONENT) reads the chip's Z buffer: make sure it holds the depth plane */
void mach64_zs_depth_to_hw(gld_context *ctx)
{
    mach64_priv *p = MACH64_PRIV(ctx); zs_state *z = (zs_state *)p->zs;
    if (z) plane_to_hw(ctx, z, PL_D);
}
/* test access: the truth of one pixel (moves the plane to the CPU) */
uint32_t mach64_zs_peek(gld_context *ctx, int which, int x, int y)
{
    mach64_priv *p = MACH64_PRIV(ctx); zs_state *z = zs_get(p);
    if (!z) return which == PL_S ? 0 : 0xffffu;
    plane_to_cpu(ctx, z, which);
    if (!z->pl[which].buf || x < 0 || y < 0 || (uint32_t)x >= z->stride || (uint32_t)y >= z->rows) return 0;
    return which == PL_S ? ((uint8_t *)z->pl[PL_S].buf)[(size_t)y * z->stride + (uint32_t)x] : ((uint16_t *)z->pl[PL_D].buf)[(size_t)y * z->stride + (uint32_t)x];
}

/* ---- state decode (called from the STATE flush) ------------------------- */
static void face_build(zs_face *f)
{
    unsigned s;
    for (s = 0; s < 256; s++) {
        uint32_t keep = s & ~(uint32_t)f->wmask;
        f->spass[s] = (uint8_t)cmpf(f->func, (uint32_t)f->ref & f->vmask, s & f->vmask);
        f->nsf[s] = (uint8_t)(keep | (apply_op(f->sfail, s, f->ref) & f->wmask));
        f->ndf[s] = (uint8_t)(keep | (apply_op(f->dpfail, s, f->ref) & f->wmask));
        f->ndp[s] = (uint8_t)(keep | (apply_op(f->dppass, s, f->ref) & f->wmask));
    }
}
static void face_read(zs_face *f, uint32_t gli, uint32_t face)
{
    uint32_t fb = gli + 0x6700u + face * 0x10u;
    uint16_t func = *(const uint16_t *)(uintptr_t)(fb + 0x14), sfail = *(const uint16_t *)(uintptr_t)(fb + 0x16), dpfail = *(const uint16_t *)(uintptr_t)(fb + 0x18), dppass = *(const uint16_t *)(uintptr_t)(fb + 0x1a);
    uint8_t ref = (uint8_t)*(const uint32_t *)(uintptr_t)(fb + 0x10), vmask = (uint8_t)*(const uint32_t *)(uintptr_t)(fb + 0x0c), wmask = (uint8_t)*(const uint32_t *)(uintptr_t)(gli + 0x63acu + face * 4u);
    if (func < F_NEVER || func > F_ALWAYS) func = F_ALWAYS;
    if (f->built && f->func == func && f->sfail == sfail && f->dpfail == dpfail && f->dppass == dppass && f->ref == ref && f->vmask == vmask && f->wmask == wmask) return;     /* unchanged: no table rebuild (this ran on every draw) */
    f->func = func; f->sfail = sfail; f->dpfail = dpfail; f->dppass = dppass; f->ref = ref; f->vmask = vmask; f->wmask = wmask;
    face_build(f); f->built = 1;
}
/* can the chip's Z buffer run this exactly?  -> reference, Z function, Z write op */
static int mode1_ok(const zs_face *f, uint32_t *ref, uint32_t *zfunc, uint32_t *wop)
{
    if (f->vmask != 0xffu || f->wmask != 0xffu) return 0;
    if (f->func != F_ALWAYS && f->sfail != OP_KEEP) return 0;               /* a failing fragment would have to write */
    if (f->dppass != OP_KEEP && f->dppass != OP_REPLACE && f->dppass != OP_ZERO) return 0;
    if (f->dppass == OP_ZERO && f->func != F_ALWAYS && f->ref != 0) return 0;
    *ref = f->ref; *zfunc = f->func; *wop = f->dppass;
    return 1;
}

void mach64_zs_sync(gld_context *ctx, uint32_t gli)
{
    mach64_priv *p = MACH64_PRIV(ctx); zs_state *z = zs_get(p);
    uint32_t bits, mode = 0, ref = 0, zf = 0, wop = 0, zc = 0;
    static const uint32_t zt[8] = { MACH64_Z_TEST_NEVER, MACH64_Z_TEST_LESS, MACH64_Z_TEST_EQUAL, MACH64_Z_TEST_LEQUAL, MACH64_Z_TEST_GREATER, MACH64_Z_TEST_NOTEQUAL,
                                    MACH64_Z_TEST_GEQUAL, MACH64_Z_TEST_ALWAYS };
    if (!z) return;
    bits = *(const uint32_t *)(uintptr_t)(gli + 0x6734);
    z->fen[0] = (int)(bits & 1u); z->two = (int)((bits >> 2) & 1u); z->fen[1] = z->two ? (int)(((bits >> 1) | bits) & 1u) : z->fen[0];
    z->front_ccw = (*(const uint16_t *)(uintptr_t)(gli + 0x66e8) != 0x900u);
    z->depth_en = (*(const uint32_t *)(uintptr_t)(gli + 0x6340) & 0x01000000u) != 0;
    z->dfunc = (int)(*(const uint32_t *)(uintptr_t)(gli + 0x633c) >> 16); if (z->dfunc < 0x200 || z->dfunc > 0x207) z->dfunc = 0x201;
    z->dmask = (*(const uint32_t *)(uintptr_t)(gli + 0x63b8) & 0x01000000u) != 0;
    face_read(&z->face[0], gli, 0); face_read(&z->face[1], gli, 1);
    {   uint32_t bl = *(const uint32_t *)(uintptr_t)(gli + 0x6304), eq = *(const uint32_t *)(uintptr_t)(gli + 0x6300) >> 16;
        uint32_t fs = *(const uint32_t *)(uintptr_t)(gli + 0x62e8), sf = fs >> 16, df = fs & 0xffffu;
        z->cb = (bl & 0x01000000u) && (eq == 0x800au || eq == 0x800bu || eq == 0x8007u || eq == 0x8008u || sf == 0x308u || (sf >= 0x8001u && sf <= 0x8004u) || (df >= 0x8001u && df <= 0x8004u) || df == 0x308u);
    }
    z->envcpu = mach64_env_needs_cpu(gli);
    if (z->fen[0] || (z->two && z->fen[1]) || p->query || z->cb || z->envcpu) {
        mode = 2;
        if (!p->query && !z->cb && !z->envcpu && !z->depth_en && !z->two && z->fen[0] && p->rb.z_off && mode1_ok(&z->face[0], &ref, &zf, &wop)) mode = 1;
    }
    p->zs_mode = mode;
    if (mode == 1) {
        float zfl = (wop == OP_ZERO && zf == F_ALWAYS) ? 0.0f : (float)ref / 255.0f;
        zc = MACH64_Z_EN | zt[zf - F_NEVER] | (wop == OP_KEEP ? 0u : MACH64_Z_MASK_EN);
        mach64_zforce = zfl; z->m1_ref = (int)ref; z->m1_func = (int)zf; z->m1_op = (int)wop;
    } else if (mode == 2) {
        mach64_zforce = -1.0f; zc = 0;                                       /* the CPU depth/stencil buffers decide; the chip draws plain */
    } else {
        mach64_zforce = -1.0f;
        if (z->depth_en) zc = MACH64_Z_EN | zt[z->dfunc - 0x200] | (z->dmask ? MACH64_Z_MASK_EN : 0u);
    }
    if (p->hw.z_cntl != zc) { p->hw.z_cntl = zc; p->dirty |= MACH64_DIRTY_STATE; }
}

/* ---- per draw ------------------------------------------------------------ */
void mach64_zs_prepare(gld_context *ctx)
{
    mach64_priv *p = MACH64_PRIV(ctx); zs_state *z = (zs_state *)p->zs;
    if (!z || !p->rb.z_off) return;
    if (p->zs_mode == 1) { plane_to_hw(ctx, z, PL_S); return; }
    if (p->zs_mode == 2) { plane_to_cpu(ctx, z, PL_S); if (z->depth_en) plane_to_cpu(ctx, z, PL_D); return; }
    if (z->depth_en) plane_to_hw(ctx, z, PL_D);
}
/* a hardware draw is about to run: the chip's copy of whatever it holds changes if the Z write is on */
void mach64_zs_note_hw(gld_context *ctx)
{
    mach64_priv *p = MACH64_PRIV(ctx); zs_state *z = (zs_state *)p->zs;
    if (!z) return;
    if ((p->hw.z_cntl & (MACH64_Z_EN | MACH64_Z_MASK_EN)) == (MACH64_Z_EN | MACH64_Z_MASK_EN)) {
        zs_plane *pl = &z->pl[p->zs_mode == 1 ? PL_S : PL_D];
        if (pl->loc == LOC_HW) { pl->hw_uni = 0; pl->hw_cpu = 0; }
    }
}

static void rects_draw(gld_context *ctx, zs_state *z, const m64_vertex *a, const m64_vertex *b, const m64_vertex *c)
{
    mach64_priv *p = MACH64_PRIV(ctx); int i;
    /* The scissor is a kernel-owned register (a stream write of it is dropped): set it with its own call, one submit per rectangle, then restore the full target. */
    for (i = 0; i < z->rn; i++) {
        mach64_flush_batch(ctx);
        rgb_scissor(&p->rb, z->r[i].x0, z->r[i].x1, z->r[i].y0, z->r[i].y1);
        mach64_hw_tri(ctx, a, b, c);
        mach64_flush_batch(ctx);
    }
    if (z->rn) { rgb_scissor(&p->rb, 0, (int)p->fb_w - 1, 0, (int)p->fb_h - 1); p->dirty |= MACH64_DIRTY_STATE; }
    z->rn = 0;
}
static void rect_run(gld_context *ctx, zs_state *z, int y, int x0, int x1, const m64_vertex *a, const m64_vertex *b, const m64_vertex *c, int *flushed)
{
    int i;
    for (i = z->rn - 1; i >= 0; i--) {                     /* extend a rectangle that ended on the previous row with the same extent */
        if (z->r[i].y1 == y - 1 && z->r[i].x0 == x0 && z->r[i].x1 == x1) { z->r[i].y1 = y; return; }
    }
    if (z->rn == MAXRECT) { rects_draw(ctx, z, a, b, c); *flushed = 1; }
    z->r[z->rn].x0 = x0; z->r[z->rn].x1 = x1; z->r[z->rn].y0 = y; z->r[z->rn].y1 = y; z->rn++;
}


/* ---- CPU compositing: the blend unit for the equations/factors the chip lacks.  8-bit integer maths; the destination has no alpha (Ad = 1). ---- */
static void cb_load(gld_context *ctx, cb_state *c)
{
    uint32_t fs = mach64_gli_u32(ctx, 0x62e8); int i;
    c->sf = fs >> 16; c->df = fs & 0xffffu; c->eq = mach64_gli_u32(ctx, 0x6300) >> 16;
    c->blend_on = (mach64_gli_u32(ctx, 0x6304) & 0x01000000u) != 0;
    for (i = 0; i < 4; i++) {
        uint32_t u = mach64_gli_u32(ctx, 0x62f0u + (uint32_t)i * 4u); float f; __builtin_memcpy(&f, &u, 4);
        c->cc[i] = f <= 0.0f ? 0 : f >= 1.0f ? 255 : (int)(f * 255.0f + 0.5f);
    }
    c->fog = mach64_fog_cpu;
    for (i = 0; i < 3; i++) {                                                  /* GL_FOG_COLOR (gli+0x6358) */
        uint32_t u = mach64_gli_u32(ctx, 0x6358u + (uint32_t)i * 4u); float f; __builtin_memcpy(&f, &u, 4);
        c->fogc[i] = f <= 0.0f ? 0 : f >= 1.0f ? 255 : (int)(f * 255.0f + 0.5f);
    }
}
/* One pixel's colour: vertex colour planes, texture environment (one unit, two units, cube, volume), fog.  A macro because it works on the rasteriser's locals; used twice (before the stencil/depth tests when the alpha test needs the fragment alpha, otherwise after them). */
#define SHADE_PIXEL \
                            for (c = 0; c < 4; c++) { int64_t v = (fca[c] + ((int64_t)1 << 37)) >> 38; if (v < 0) v = 0; else if (v > 255) v = 255; out |= (uint32_t)v << (8 * c); }      /* vertex colour: incremental plane, no divide */ \
                            if (cbs.tex) { \
                                if (cbs.env.e2) {                                    /* two-unit composite: unit 0 then unit 1, both texels and the primary colour nameable (crossbar) */ \
                                    const m64_cpuenv *e2 = cbs.env.e2; uint32_t t0, t1, o0; \
                                    if (faff) { t0 = tex_sample_q8(&cbs.env, ((int32_t)(fpa >> 16)) >> fkp, ((int32_t)(fqa >> 16)) >> fkp); t1 = tex_sample_q8(e2, ((int32_t)(fpa2 >> 16)) >> fkp, ((int32_t)(fqa2 >> 16)) >> fkp); } \
                                    else { t0 = tex_sample_q8(&cbs.env, fx_div(fpa, fra, fkp - fkr), fx_div(fqa, fra, fkp - fkr)); t1 = tex_sample_q8(e2, fx_div(fpa2, fra, fkp - fkr), fx_div(fqa2, fra, fkp - fkr)); } \
                                    g_xb.on = 1; g_xb.pri = out; g_xb.t0 = t0; g_xb.t1 = t1; \
                                    o0 = env_apply_ref(&cbs.env, out, t0); out = env_apply_ref(e2, o0, t1); \
                                    g_xb.on = 0; \
                                } else if (cbs.env.cube) out = env_apply(&cbs.env, &cbs.ep, out, cube_sample_fx(&cbs.env, (int32_t)(fpa >> 16), (int32_t)(fqa >> 16), (int32_t)(fra >> 16))); \
                                else if (cbs.env.vol) out = env_apply(&cbs.env, &cbs.ep, out, faff ? vol_sample_q8(&cbs.env, ((int32_t)(fpa >> 16)) >> fkp, ((int32_t)(fqa >> 16)) >> fkp, ((int32_t)(fza >> 16)) >> fkp) \
                                                                                          : vol_sample_q8(&cbs.env, fx_div(fpa, fra, fkp - fkr), fx_div(fqa, fra, fkp - fkr), fx_div(fza, fra, fkp - fkr))); \
                                else if (faff) out = env_apply(&cbs.env, &cbs.ep, out, tex_sample_q8(&cbs.env, ((int32_t)(fpa >> 16)) >> fkp, ((int32_t)(fqa >> 16)) >> fkp)); \
                                else out = env_apply(&cbs.env, &cbs.ep, out, tex_sample_q8(&cbs.env, fx_div(fpa, fra, fkp - fkr), fx_div(fqa, fra, fkp - fkr)));      /* the chip: address = interp(S*W) / interp(W) */ \
                            } \
                            if (cbs.fog) { int64_t fq = (fga + ((int64_t)1 << 37)) >> 38; int fv, ch; if (fq < 0) fq = 0; else if (fq > 255) fq = 255; fv = (int)fq; \
                                for (ch = 0; ch < 3; ch++) { int v = (int)((out >> (8 * ch)) & 255u); v = (v * fv + cbs.fogc[2 - ch] * (255 - fv) + 127) / 255; out = (out & ~(255u << (8 * ch))) | ((uint32_t)v << (8 * ch)); } }      /* GL_FOG: factor interpolated from the vertex fog value (GLEngine evaluates the mode), blended against the fog colour */
int mach64_zs_cpu_draw(gld_context *ctx, const m64_vertex *va, const m64_vertex *vb, const m64_vertex *vc)
{
    mach64_priv *p = MACH64_PRIV(ctx); zs_state *z = (zs_state *)p->zs;
    int64_t x[3], y[3], zq[3], area, A[3], B[3], E[3], gx, gy, zrow, zacc, dz1, dz2, dx1, dx2, dy1, dy2;
    int notinc[3], i, minx, maxx, miny, maxy, W, Hh, py, px, front, fi, flushed = 0, all_draw;
    uint32_t cov = 0, pass = 0; const zs_face *f; int sten_on, colour_on;
    uint8_t *sb; uint16_t *db; int depth_en; void *cbw = 0; cb_state cbs; const m64_vertex *vq[3]; int swapped = 0, cb_any = 0;
    fx_plane fpc[4], fpp, fpq, fpr, fpz, fpp2, fpq2, fpf; int at_on = 0; uint32_t afunc = 0x207, aref = 0; int64_t fga = 0; int fkp = 0, fkr = 0, faff = 0; fx_inv fiv, einv[3];                  /* fixed-point colour / texture-coordinate / cube-direction planes (gld_fxmap.h); fpp, fpq, fpr double as the cube X, Y, Z */
    if (!z || !z->pl[PL_S].buf) return 0;
    W = (int)p->fb_w; Hh = (int)p->fb_h;
    x[0] = (int16_t)((uint32_t)va->x_y >> 16); y[0] = (int16_t)((uint32_t)va->x_y & 0xffffu); zq[0] = (uint32_t)va->z >> 15;
    x[1] = (int16_t)((uint32_t)vb->x_y >> 16); y[1] = (int16_t)((uint32_t)vb->x_y & 0xffffu); zq[1] = (uint32_t)vb->z >> 15;
    x[2] = (int16_t)((uint32_t)vc->x_y >> 16); y[2] = (int16_t)((uint32_t)vc->x_y & 0xffffu); zq[2] = (uint32_t)vc->z >> 15;
    area = (x[1] - x[0]) * (y[2] - y[0]) - (x[2] - x[0]) * (y[1] - y[0]);
    if (!area) return 0;
    front = p->nonpoly ? 1 : (((area < 0) != 0) == (z->front_ccw != 0));      /* y is flipped: a y-up CCW triangle has negative area here */
    fi = (z->two && !front) ? 1 : 0; f = &z->face[fi]; sten_on = z->fen[fi];
    colour_on = (p->hw.dp_write_mask & (p->rb.fmt555 ? 0x7fffu : 0xffffu)) != 0;      /* any colour channel writable (1555: the alpha bit does not count) */
    depth_en = z->depth_en; sb = (uint8_t *)z->pl[PL_S].buf; db = depth_en ? (uint16_t *)z->pl[PL_D].buf : 0;
    if (depth_en && !db) return 0;
    if (area < 0) {                                                            /* orient so the area is positive */
        int64_t tx = x[1], ty = y[1], tz = zq[1]; swapped = 1; x[1] = x[2]; y[1] = y[2]; zq[1] = zq[2]; x[2] = tx; y[2] = ty; zq[2] = tz; area = -area;
                                                                                /* (the emitter normalises the hardware winding itself: it gets the original vertices) */
    }
    for (i = 0; i < 3; i++) {
        int j = (i + 1) % 3; int64_t dx = x[j] - x[i], dy = y[j] - y[i];
        A[i] = -dy; B[i] = dx; notinc[i] = !(dy < 0 || (dy == 0 && dx > 0));     /* top-left rule (y down, positive area: the interior is on the left of a->b): an edge going up is a LEFT edge, a horizontal edge going right a TOP edge; measured to be the chip's own tie rule */
    }
    dx1 = x[1] - x[0]; dy1 = y[1] - y[0]; dx2 = x[2] - x[0]; dy2 = y[2] - y[0]; dz1 = zq[1] - zq[0]; dz2 = zq[2] - zq[0];
    gx = (((dz1 * dy2 - dz2 * dy1)) * 65536) / area;          /* depth change per quarter pixel in x/y, 16.16 */
    gy = (((dz2 * dx1 - dz1 * dx2)) * 65536) / area;
    {   int64_t mnx = x[0], mxx = x[0], mny = y[0], mxy = y[0];
        for (i = 1; i < 3; i++) { if (x[i] < mnx) mnx = x[i]; if (x[i] > mxx) mxx = x[i]; if (y[i] < mny) mny = y[i]; if (y[i] > mxy) mxy = y[i]; }
        minx = (int)(mnx >> 2); maxx = (int)(mxx >> 2); miny = (int)(mny >> 2); maxy = (int)(mxy >> 2);
        if (minx < 0) minx = 0; if (miny < 0) miny = 0; if (maxx > W - 1) maxx = W - 1; if (maxy > Hh - 1) maxy = Hh - 1; }
    z->rn = 0;
    cbs.tex = 0;
    fx_rcp_init();
    for (i = 0; i < 3; i++) { int64_t a4 = A[i] < 0 ? -4 * A[i] : 4 * A[i]; if (a4) fx_inv_setup(&einv[i], a4); }      /* covered range of each row from the edge equations: reciprocal per edge, no divide */
    if ((z->cb || z->envcpu) && maxy >= miny && ((!p->hw.textured && !mach64_tex_cube) ? z->cb != 0 : (mach64_cpu_env(ctx, &cbs.env) != 0 && (cbs.tex = 1)))) {
        mach64_flush_batch(ctx); cbw = mach64_cb_open(ctx, miny, maxy - miny + 1); cb_load(ctx, &cbs); { uint32_t af = mach64_gli_u32(ctx, 0x62e4u), ru = mach64_gli_u32(ctx, 0x62e0u); float rf; __builtin_memcpy(&rf, &ru, 4); at_on = (af & 0x100u) != 0; afunc = af >> 16; aref = rf <= 0.0f ? 0u : rf >= 1.0f ? 255u : (uint32_t)(rf * 255.0f + 0.5f); if (afunc < 0x200 || afunc > 0x207) at_on = 0; } cb_prepare(&cbs, mach64_cb_is555(ctx));        /* blend factors and COMBINE selectors resolved once per draw */
        { const m64_vertex *t = vb; vq[0] = va; vq[1] = (swapped ? vc : vb); vq[2] = (swapped ? t : vc); }
        if (cbs.tex && !cbs.env.cube && !cbs.env.vol && cbs.env.nlev > 1) {                          /* mipmapped texture: pick this triangle's level */
            float s0[3], t0[3]; int q; float pix2 = (float)area / 16.0f;
            for (q = 0; q < 3; q++) { s0[q] = gld_flt((uint32_t)vq[q]->s); t0[q] = gld_flt((uint32_t)vq[q]->t); }
            env_set_level(&cbs.env, env_pick_level(&cbs.env, s0, t0, pix2));
            if (cbs.env.e2 && cbs.env.e2->nlev > 1) {
                float s1[3], t1[3]; m64_cpuenv *e2w = (m64_cpuenv *)cbs.env.e2;
                for (q = 0; q < 3; q++) { s1[q] = gld_flt((uint32_t)vq[q]->s2); t1[q] = gld_flt((uint32_t)vq[q]->t2); }
                env_set_level(e2w, env_pick_level(e2w, s1, t1, pix2));
            }
        }
        fx_inv_setup(&fiv, area);
        {   int c, i2; int64_t dxa = x[1] - x[0], dya = y[1] - y[0], dxb = x[2] - x[0], dyb = y[2] - y[0];
            for (c = 0; c < 4; c++) fx_plane_setup(&fpc[c], (int64_t)((vq[0]->argb >> (8 * c)) & 255u) << 22, (int64_t)((vq[1]->argb >> (8 * c)) & 255u) << 22, (int64_t)((vq[2]->argb >> (8 * c)) & 255u) << 22, dxa, dya, dxb, dyb, &fiv);
            if (cbs.fog) fx_plane_setup(&fpf, (int64_t)(vq[0]->spec_argb >> 24) << 22, (int64_t)(vq[1]->spec_argb >> 24) << 22, (int64_t)(vq[2]->spec_argb >> 24) << 22, dxa, dya, dxb, dyb, &fiv);
            if (cbs.tex) {
                float P[3], Q[3], R[3], Z[3] = { 0, 0, 0 }, mp = 0.0f, mr = 0.0f;
                faff = !cbs.env.cube && vq[0]->w == vq[1]->w && vq[1]->w == vq[2]->w;          /* constant w: the texel position is itself a plane (w cancels), no divide */
                for (i2 = 0; i2 < 3; i2++) {
                    float sv = gld_flt((uint32_t)vq[i2]->s), tv = gld_flt((uint32_t)vq[i2]->t), wv = gld_flt((uint32_t)vq[i2]->w);
                    if (cbs.env.cube) { P[i2] = sv * wv; Q[i2] = tv * wv; R[i2] = gld_flt((uint32_t)vq[i2]->r) * wv; if (fx_abs(R[i2]) > mp) mp = fx_abs(R[i2]); }
                    else { float wm = faff ? 1.0f : wv; P[i2] = sv * wm * (float)cbs.env.w * 256.0f; Q[i2] = tv * wm * (float)cbs.env.h * 256.0f; R[i2] = wv; if (fx_abs(wv) > mr) mr = fx_abs(wv);
                          if (cbs.env.vol) { Z[i2] = gld_flt((uint32_t)vq[i2]->r) * wm * (float)cbs.env.depth * 256.0f; if (fx_abs(Z[i2]) > mp) mp = fx_abs(Z[i2]); } }
                    if (fx_abs(P[i2]) > mp) mp = fx_abs(P[i2]); if (fx_abs(Q[i2]) > mp) mp = fx_abs(Q[i2]);
                }
                fkp = fx_scale_for(mp); fkr = cbs.env.cube ? fkp : fx_scale_for(mr);
                fx_plane_setup(&fpp, fx_from_float(P[0], fkp), fx_from_float(P[1], fkp), fx_from_float(P[2], fkp), dxa, dya, dxb, dyb, &fiv);
                fx_plane_setup(&fpq, fx_from_float(Q[0], fkp), fx_from_float(Q[1], fkp), fx_from_float(Q[2], fkp), dxa, dya, dxb, dyb, &fiv);
                if (cbs.env.e2) {                                              /* unit 1 of a two-unit composite: its own S,T planes (same W) */
                    float P2[3], Q2[3]; const m64_cpuenv *e2 = cbs.env.e2; float mp2 = mp;
                    for (i2 = 0; i2 < 3; i2++) {
                        float wv = gld_flt((uint32_t)vq[i2]->w), wm = faff ? 1.0f : wv;
                        P2[i2] = gld_flt((uint32_t)vq[i2]->s2) * wm * (float)e2->w * 256.0f; Q2[i2] = gld_flt((uint32_t)vq[i2]->t2) * wm * (float)e2->h * 256.0f;
                        if (fx_abs(P2[i2]) > mp2) mp2 = fx_abs(P2[i2]); if (fx_abs(Q2[i2]) > mp2) mp2 = fx_abs(Q2[i2]);
                    }
                    if (mp2 > mp) { mp = mp2; fkp = fx_scale_for(mp); fx_plane_setup(&fpp, fx_from_float(P[0], fkp), fx_from_float(P[1], fkp), fx_from_float(P[2], fkp), dxa, dya, dxb, dyb, &fiv); fx_plane_setup(&fpq, fx_from_float(Q[0], fkp), fx_from_float(Q[1], fkp), fx_from_float(Q[2], fkp), dxa, dya, dxb, dyb, &fiv); }
                    fx_plane_setup(&fpp2, fx_from_float(P2[0], fkp), fx_from_float(P2[1], fkp), fx_from_float(P2[2], fkp), dxa, dya, dxb, dyb, &fiv);
                    fx_plane_setup(&fpq2, fx_from_float(Q2[0], fkp), fx_from_float(Q2[1], fkp), fx_from_float(Q2[2], fkp), dxa, dya, dxb, dyb, &fiv);
                }
                if (cbs.env.vol) fx_plane_setup(&fpz, fx_from_float(Z[0], fkp), fx_from_float(Z[1], fkp), fx_from_float(Z[2], fkp), dxa, dya, dxb, dyb, &fiv);
                fx_plane_setup(&fpr, fx_from_float(R[0], fkr), fx_from_float(R[1], fkr), fx_from_float(R[2], fkr), dxa, dya, dxb, dyb, &fiv);
            }
        }
    }
    for (py = miny; py <= maxy; py++) {
        int64_t qy = 4 * (int64_t)py + 2, qx0 = 4 * (int64_t)minx + 2, fca[4], fpa = 0, fqa = 0, fra = 0, fza = 0, fpa2 = 0, fqa2 = 0, lo, hi; int run = -1;
        for (i = 0; i < 3; i++) E[i] = A[i] * (qx0 - x[(i)]) + B[i] * (qy - y[i]) - notinc[i];
        fx_row_extent(E, A, einv, minx, maxx, &lo, &hi);                       /* the covered pixels of this row: no per-pixel edge test, no scan of the empty bounding box */
        if (lo > hi) continue;
        qx0 = 4 * lo + 2;
        zrow = (zq[0] * 65536) + gx * (qx0 - x[0]) + gy * (qy - y[0]);
        zacc = zrow;
        if (cbw) {
            int64_t ddx = qx0 - x[0], ddy = qy - y[0]; int c;
            for (c = 0; c < 4; c++) fca[c] = fx_plane_at(&fpc[c], ddx, ddy);
            if (cbs.fog) fga = fx_plane_at(&fpf, ddx, ddy);
            if (cbs.tex) { fpa = fx_plane_at(&fpp, ddx, ddy); fqa = fx_plane_at(&fpq, ddx, ddy); fra = fx_plane_at(&fpr, ddx, ddy); if (cbs.env.vol) fza = fx_plane_at(&fpz, ddx, ddy); if (cbs.env.e2) { fpa2 = fx_plane_at(&fpp2, ddx, ddy); fqa2 = fx_plane_at(&fpq2, ddx, ddy); } }
        }
        for (px = (int)lo; px <= (int)hi; px++) {
            {
                size_t idx = (size_t)py * z->stride + (size_t)px; int sp = 1, zp = 1, c, shaded = 0; uint32_t zi = 0, s = 0, out = 0;
                cov++;
                if (cbw && colour_on && at_on) { SHADE_PIXEL shaded = 1; if (!cmpf(afunc, out >> 24, aref)) goto skip_px; }      /* alpha test first: a discarded fragment touches neither stencil nor depth */
                if (sten_on) { s = sb[idx]; sp = f->spass[s]; }
                if (depth_en && sp) {
                    int64_t zz = (zacc + 0x8000) >> 16; if (zz < 0) zz = 0; else if (zz > 65535) zz = 65535; zi = (uint32_t)zz >> 1;                         /* the chip stores and compares the 16-bit depth shifted down one bit (measured) */
                    zp = cmpf((uint32_t)z->dfunc, zi, db[idx]);
                }
                if (sten_on) sb[idx] = !sp ? f->nsf[s] : (zp ? f->ndp[s] : f->ndf[s]);
                if (sp && zp) {
                    if (depth_en && z->dmask) db[idx] = (uint16_t)zi;
                    pass++;
                    if (cbw && colour_on) {                                        /* composite this pixel */
                        uint16_t *dp = mach64_cb_px(cbw, py, px);
                        if (dp) {
                            if (!shaded) { SHADE_PIXEL }
                            *dp = cbs.blend_on ? cb_blend(&cbs, out, *dp) : cb_pack(&cbs, (out >> 16) & 255u, (out >> 8) & 255u, out & 255u); cb_any = 1;
                        }
                    } else if (colour_on && run < 0) run = px;
                } else if (run >= 0) { rect_run(ctx, z, py, run, px - 1, va, vb, vc, &flushed); run = -1; }
            }
            skip_px: ;
            zacc += 4 * gx;
            if (cbw) { fca[0] += 4 * fpc[0].gx; fca[1] += 4 * fpc[1].gx; fca[2] += 4 * fpc[2].gx; fca[3] += 4 * fpc[3].gx; if (cbs.fog) fga += 4 * fpf.gx; if (cbs.tex) { fpa += 4 * fpp.gx; fqa += 4 * fpq.gx; fra += 4 * fpr.gx; if (cbs.env.vol) fza += 4 * fpz.gx; if (cbs.env.e2) { fpa2 += 4 * fpp2.gx; fqa2 += 4 * fpq2.gx; } } }
        }
        if (run >= 0) rect_run(ctx, z, py, run, px - 1, va, vb, vc, &flushed);
    }
    if (cbw) { mach64_cb_close(ctx, cbw, cb_any); z->rn = 0; if (depth_en && z->dmask && pass) { z->pl[PL_D].hw_uni = 0; z->pl[PL_D].hw_cpu = 0; } if (p->query) mach64_query_add(ctx, pass); return 1; }
    if (depth_en && z->dmask && pass) { z->pl[PL_D].hw_uni = 0; z->pl[PL_D].hw_cpu = 0; }
    if (p->query) mach64_query_add(ctx, pass);
    all_draw = (!flushed && cov && pass == cov);
    if (!colour_on || !pass) { z->rn = 0; return 1; }
    if (all_draw) { z->rn = 0; return mach64_hw_tri(ctx, va, vb, vc); }
    rects_draw(ctx, z, va, vb, vc);
    return 1;
}

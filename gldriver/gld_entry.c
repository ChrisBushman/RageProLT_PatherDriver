/*
 * gld_entry.c - the remaining gld* entry points GLEngine looks up (~53 total).
 *
 * Implements the CORE set needed to drive a context to a drawable, and STUBS
 * every optional feature entry point so none of GLEngine's symbol lookups come
 * back NULL (a NULL it then calls would crash the GL app).  Stubs return 0
 * (NULL/success) and are only reached if an app uses that feature.
 *
 * gldAttachDrawable is reversed from the reference (ctx, type, drawable, flags):
 * it derives a surface struct from Apple's CGS drawable descriptor and calls a
 * kext selector (set_surface) that binds the render target.  That descriptor
 * format carries no window geometry, which comes from CGS (gld_cgs.c).
 */
#include "gld.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* ---- gldAttachDrawable: bind the app's drawable as the render target -
 * type: 0x50 window / 0x5a fullscreen / 0x36 (pbuffer?) -- same tokens as the
 * pixel-format parser.  drawable: Apple CGS descriptor; the reference reads
 * drawable[0/4/8] and calls a kext set_surface selector (ctx+0x04 connection).
 * The drawable carries no geometry: the window bounds come from CGS (mach64_cgs_resolve_bounds, gld_cgs.c; docs/re-notes/windowed.md)
 * and the surface offset/pitch from Apple's bound surface (Path B) or our own buffers (legacy path).  Known gap: with several GL windows
 * open at once the CGS resolver picks the first window that has a surface. */
/* Test hook: a headless harness has no window system, so it names the drawable size here before attaching (0 = ask the display / CGS). */
unsigned g_gld_force_w, g_gld_force_h;

/* GLEngine's capability descriptor for our (hardware) renderer (gli[0x789c]) arrives all zero: viewport clamp, GL_MAX_TEXTURE_SIZE, GL_MAX_TEXTURE_UNITS...
 * An app that creates textures BEFORE its drawable is attached (every NSOpenGLView screen saver: Flurry) calls glTexImage2D while the max texture size is
 * still 0, so GLEngine silently drops the image (the driver's level records stay empty -> the texture draws as flat colour).  So fill it as soon as the
 * driver sees the context: gldCreateTexture / gldModifyTexture run before any image, and the back-pointer gli[0x7898] == ctx is valid by then.
 * gli base = ctx[0x10] - 0x3610 (measured; checked by the back-pointer). */
static void rage_fill_descriptor(gld_context *ctx)
{
    unsigned char *c = (unsigned char *)ctx; uint32_t a8, g, descp;
    if (!c) return;
    a8 = *(uint32_t *)(c + 0x10);
    if (a8 < 0x13610u || a8 > M64_UADDR_MAX) return;
    g = a8 - 0x3610u;
    if (*(uint32_t *)(uintptr_t)(g + 0x7898) != (uint32_t)(uintptr_t)ctx) return;
    descp = *(uint32_t *)(uintptr_t)(g + 0x789c);
    if (descp > 0x10000 && descp <= M64_UADDR_MAX) {
        uint32_t *dd = (uint32_t *)(uintptr_t)descp;
        if (*(uint16_t *)(uintptr_t)(descp + 0xb2) == 1024) return;           /* already done */
        dd[0x00/4] = 0x00000004; dd[0x04/4] = 0x4e7fffff; dd[0x08/4] = 0x00000003;
        dd[0x0c/4] = 0x00000800; dd[0x10/4] = 0x00000800; dd[0x1c/4] = 0x01000000;
        dd[0x24/4] = 0x00000008; dd[0x28/4] = 0x00000008; dd[0x2c/4] = 0x00000008; dd[0x34/4] = 0x00000020;
        dd[0x38/4] = 0x00000008;                  /* stencil bits: GLEngine glStencilFunc clamps the reference to (1 << dd[0x38]) - 1 (static analysis of GLEngine 0x38528): with 0 every reference was stored as 0 */
        {   /* GL_EXTENSIONS: GLEngine appends one extension per set bit of the 72-bit mask at descriptor +0x104 (GLEngine 0x39188; the bit -> name table is in
             * docs/standalone-driver-design.md).  Only features this driver really delivers: */
            static const unsigned char bits[] = { 5 /* ARB_texture_env_add */, 23 /* EXT_texture_env_add */, 16 /* ARB_occlusion_query */, 27 /* EXT_texture_lod_bias */,
                                                  30 /* EXT_stencil_wrap */, 34 /* EXT_separate_specular_color */, 36 /* EXT_blend_func_separate */, 38 /* EXT_stencil_two_side */,
                                                  69 /* SGIS_texture_edge_clamp */ };
            unsigned i; for (i = 0; i < sizeof bits; i++) dd[0x104 / 4 + bits[i] / 32] |= 1u << (bits[i] & 31);
        }
        *(uint16_t *)(uintptr_t)(descp + 0xb2) = 1024;
        *(uint16_t *)(uintptr_t)(descp + 0xac) = 2;
    }
}
int gldAttachDrawable(gld_context *ctx, int type, void *drawable, uint32_t flags)
{
    mach64_priv *pv;
    if (!ctx) return 0x271e;
    pv = MACH64_PRIV(ctx);
    if ((type == 0x50 || type == 0x5a || type == 0x36) && !drawable) return 0x2715;
    if (type == 0 || !drawable) {                         /* detach: drain, give the VRAM back */
        mach64_repaint_stop(pv);
        mach64_flush_batch(ctx);
        rgb_fence(&pv->rb);
        rgb_release(&pv->rb);
        pv->attached = 0; pv->win_mode = 0;
        return 0;
    }
    {
        const uint32_t *d = (const uint32_t *)drawable;
        uint8_t *c = (uint8_t *)ctx;
        uint32_t width = 0, height = 0, dw = 0, dh = 0, dbpp = 0;
        int wx = 0, wy = 0, ww = 0, wh = 0;
        mach64_flush_batch(ctx);
        rgb_fence(&pv->rb);
        rgb_release(&pv->rb);
        pv->attached = 0; pv->win_mode = 0; pv->win_x = pv->win_y = 0;
        if (g_gld_force_w && g_gld_force_h) { width = g_gld_force_w; height = g_gld_force_h; }
        else if (mach64_cgs_resolve_bounds(d[1], d[2], pv->dbuf, &wx, &wy, &ww, &wh) && ww > 0 && wh > 0 && ww <= 2048 && wh <= 2048) {
            /* a window: the descriptor carries CGS ids, the true content rectangle comes from CGS (resolved once; the position drifts per call) */
            width = (uint32_t)ww; height = (uint32_t)wh; pv->win_mode = 1; pv->win_x = (uint32_t)wx; pv->win_y = (uint32_t)wy;
            pv->cgs_win = d[1]; pv->cgs_surf = d[2];
        } else if (mach64_cgs_off_display()) {
            /* The window exists but lies on ANOTHER display (a screen saver / app on the GeForce's screen whose context CGL gave us).  Drawing it into our
             * scanout interleaves two animations (flicker) at the wrong scale; refuse so GLEngine uses another renderer for it. */
            return 0x2715;
        } else if (!rgb_display(&pv->rb, &dw, &dh, &dbpp) && dw && dh) { width = dw; height = dh; }   /* full screen */
        if (!width || !height) return 0x2715;
        pv->fb_w = width; pv->fb_h = height;
        if (mach64_build_surface(ctx)) return 0x2720;
        mach64_repaint_start(pv);                                  /* windowed contexts only */
        /* Surface fields GLEngine reads to consider the drawable renderable (offsets from the reference gldAttachDrawable). */
        *(uint32_t *)(c + 0x20)  = (uint32_t)(uintptr_t)drawable;
        *(uint32_t *)(c + 0x24)  = (flags >> 8) & 0xff;
        *(uint32_t *)(c + 0x28)  = flags & 0xff;
        *(uint32_t *)(c + 0x64)  = 1;
        *(uint32_t *)(c + 0x68)  = pv->rb.pitch * 2u * height;
        *(uint32_t *)(c + 0x94)  = pv->rb.col_off;
        *(uint32_t *)(c + 0x98)  = pv->rb.pitch * 2u;
        *(uint32_t *)(c + 0x9c)  = 0;
        *(uint32_t *)(c + 0xa0)  = 0;
        *(uint32_t *)(c + 0xa4)  = 0x32;
        *(uint32_t *)(c + 0xa8)  = 0;
        *(uint32_t *)(c + 0x1f0) = width;
        *(uint32_t *)(c + 0x1f4) = height;
        /* Double-buffer dispatch gate (reference gldAttachDrawable): flag in the GLEngine object at *(ctx+0x14), caps 0x20 clear. */
        if (pv->dbuf) {
            uint32_t a7p = *(uint32_t *)(c + 0x14);
            *(uint32_t *)(c + 0x2c) &= ~0x20u;
            *(uint32_t *)(c + 0x1e4) = 1;
            if (a7p > 0x10000 && a7p <= M64_UADDR_MAX) { uint32_t w0 = *(uint32_t *)(uintptr_t)a7p; *(uint32_t *)(uintptr_t)a7p = w0 | 2u; }
        }

        /* GLEngine leaves its drawable descriptor gli[0x789c] ALL-ZEROS for our HW
         * surface, so glViewport clamps the viewport to desc[0xc]/desc[0x10] = 0 ->
         * 0x0 viewport -> ALL geometry culled before any primitive dispatch.  The
         * software renderer's descriptor is fully populated (desc = {4, 4e7fffff, 3,
         * 0x800, 0x800, ...}); populate it ourselves.  ctx+0x10 = the gli
         * context (a8 from gldCreateContext); desc = gli[0x789c]. */
        {   uint32_t p10 = *(uint32_t *)(c + 0x10);   /* points into gli (+0x3610) */
            uint32_t gli = 0, off;
            /* Find the gli base: the object whose [+0x7898] back-points to ctx. */
            for (off = 0; off <= 0x8000; off += 4) {
                uint32_t g = p10 - off;
                if (g < 0x10000 || g > M64_UADDR_MAX) break;
                if (*(uint32_t *)(uintptr_t)(g + 0x7898) == (uint32_t)(uintptr_t)ctx) {
                    gli = g; break;
                }
            }
            if (gli) {
                uint32_t descp = *(uint32_t *)(uintptr_t)(gli + 0x789c);
                if (descp > 0x10000 && descp <= M64_UADDR_MAX) {
                    uint32_t *dd = (uint32_t *)(uintptr_t)descp;
                    dd[0x00/4] = 0x00000004;
                    dd[0x04/4] = 0x4e7fffff;
                    dd[0x08/4] = 0x00000003;
                    dd[0x0c/4] = 0x00000800;   /* width  (viewport clamp max) */
                    dd[0x10/4] = 0x00000800;   /* height (viewport clamp max) */
                    dd[0x1c/4] = 0x01000000;
                    dd[0x24/4] = 0x00000008;
                    dd[0x28/4] = 0x00000008;
                    dd[0x2c/4] = 0x00000008;
                    dd[0x34/4] = 0x00000020;
                    /* GL_MAX_TEXTURE_SIZE: GLEngine's glGetIntegerv worker reads
                     * it as a HALFWORD from descriptor+0xb2 (traced: GLEngine
                     * 0xb6e6c -> lhz r0,0xb2(gli[0x789c])).  Zero here made every
                     * glTexImage2D fail GL_INVALID_VALUE.  mach64 texture unit
                     * maxes at 1024x1024 -> advertise 1024 (power-of-two). */
                    *(uint16_t *)(uintptr_t)(descp + 0xb2) = 1024;
                    /* GL_MAX_TEXTURE_UNITS: read as a halfword from descriptor+0xac
                     * (traced: GLEngine 0xb8204 -> lhz r0,0xac(gli[0x789c])).  We
                     * support one texture unit; advertise 1 (was 0). */
                    *(uint16_t *)(uintptr_t)(descp + 0xac) = 2;
                }
            }
        }
    }
    return 0;
}

/* ---- renderer strings / queries ------------------------------------- */
const char *gldGetString(gld_context *ctx, uint32_t name)
{
    (void)ctx;
    switch (name) {
    case 0x1f00: return "RageProLT Project";              /* GL_VENDOR    */
    case 0x1f01: return "ATI Rage LT Pro (mach64)";       /* GL_RENDERER  */
    case 0x1f02: return "1.1 Mach64-0.1";                 /* GL_VERSION   */
    case 0x1f03: return "";                               /* GL_EXTENSIONS*/
    default:     return "";
    }
}

/* gldGetVersion: GLEngine looks this up by name IMMEDIATELY after
 * gldInitializeLibrary and BAILS (never enumerating the renderer) if it is
 * absent -- this was THE discovery gate.  Fills 4 version-component outputs;
 * values taken from the reference bundle (2, 4, 0xb, 0x1000). */
int gldGetVersion(int *v0, int *v1, int *v2, int *v3)
{
    if (v0) *v0 = 2;
    if (v1) *v1 = 4;
    if (v2) *v2 = 0xb;
    if (v3) *v3 = 0x1000;
    return 1;
}

int gldGetError(gld_context *ctx) { (void)ctx; return 0; }

int gldGetInteger(gld_context *ctx, uint32_t pname, int *params)
{
    /* GLEngine never calls this for the texture caps (GL_MAX_TEXTURE_SIZE comes
     * from the drawable descriptor +0xb2, set in gldAttachDrawable). */
    (void)ctx;
    if (params) *params = 0;
    return 0;
}

int gldSetInteger(gld_context *ctx, uint32_t pname, int value)
{ (void)ctx; return 0; }

/* gldInitDispatch: initial dispatch-table setup (pairs with gldUpdateDispatch).
 * Install our render slots so the table is usable from the start. */
void gldInitDispatch(gld_context *ctx, void *table, void *outinfo)
{
    /* CONTRACT (ref gldInitDispatch 0x60a60): arg3(r5) is an OUTPUT struct that
     * GLEngine reads back for the drawable geometry.  The reference copies
     * ctx[0x94..0xa8] into outinfo[0..0x14]; we ignored it, leaving GLEngine a
     * garbage drawable -> geometry culled before any primitive dispatch.  Port
     * it verbatim. */
    if (outinfo) {
        const uint32_t *c = (const uint32_t *)((const unsigned char *)ctx);
        uint32_t *o = (uint32_t *)outinfo;
        o[0] = c[0x94/4];   /* target base   */
        o[1] = c[0x98/4];   /* target pitch  */
        o[2] = c[0x9c/4];
        o[3] = c[0xa0/4];
        o[4] = c[0xa4/4];
        o[5] = c[0xa8/4];
    }
    /* Reference gldInitDispatch installs the driver's control/flush slots that
     * GLEngine calls to render.  table[0x04] is THE flush entry.  Store the table
     * ptr in ctx+0x1c (reference: stw r4, 0x1c(ctx)) and install our flush. */
    *(void **)((unsigned char *)ctx + 0x1c)  = table;
    /* table[0x08] = GLEngine's glReadPixels readback hook; it's called with GLEngine's rb_ctx, not
     * our gld_context, so stash ours for mach64_ctl08 to read the back-buffer VRAM.  The GLEngine gc
     * holds GL_PACK_* state; our table sits at gc+0x78a8 (table[0x08]=gc+0x78b0), so gc=table-0x78a8. */
    if (ctx->backend)                                 /* per-context gc = the hardened path pixel ops use */
        MACH64_PRIV(ctx)->gc = (unsigned char *)table - 0x78a8;
    *(void **)((unsigned char *)table + 0x00) = (void *)mach64_ctl00;
    *(void **)((unsigned char *)table + 0x04) = (void *)mach64_flush;
    *(void **)((unsigned char *)table + 0x08) = (void *)mach64_ctl08;
    *(void **)((unsigned char *)table + 0x0c) = (void *)mach64_ctl0c;
    *(void **)((unsigned char *)table + 0x10) = (void *)mach64_ctl10;
    *(void **)((unsigned char *)table + 0x14) = (void *)mach64_ctl14;
    gldUpdateDispatch(ctx, table, (uint32_t)(uintptr_t)outinfo);
}

/* gldFlushBuffer: finish the frame and present it. */
int gldFlushBuffer(gld_context *ctx, void *drawable)
{
    (void)drawable;
    return gldFinish(ctx);          /* flush, fence, present (single path: the kernel blit); arms the next frame's clears */
}

/* ---- feature stubs: present so GLEngine's lookups are non-NULL; only called
 * if the app uses the feature.  Return 0 (NULL/success). --------------- */
#define GLD_STUB(name) int name(void) { return 0; }
GLD_STUB(gldCreateBuffer)          GLD_STUB(gldDestroyBuffer)
GLD_STUB(gldCreatePipelineProgram) GLD_STUB(gldDestroyPipelineProgram)
GLD_STUB(gldModifyPipelineProgram) GLD_STUB(gldRelatePipelineProgram)
GLD_STUB(gldGetPipelineProgramInfo)


/* ---- APPLE_fence / ARB_occlusion_query (ABI from the X1900 reference: gldCreateFence/Query(ctx, void **out) store a malloc'd object, gldTestObject/
 * gldFinishObject(ctx, kind, obj) with kind 0 = fence, dispatch slot 0x64 = signal fence(ctx, fence), 0x68 = begin query(ctx, query), 0x6c = end query(ctx),
 * gldGetQueryInfo(ctx, query, pname, int *out) with 0x8867 = result available, 0x8866 = result).  The engine is serialised through rgb_fence, so a signalled
 * fence is complete as soon as the batch has drained, and a query's count is final when the query ends. ---------------------------------------------------- */
typedef struct { uint32_t id; uint8_t done; uint8_t pad[3]; } gld_fence;
typedef struct { uint32_t id; uint32_t count; } gld_query;
int gldCreateFence(gld_context *ctx, void **out)
{
    gld_fence *f = (gld_fence *)calloc(1, sizeof *f); (void)ctx;
    if (!f) { if (out) *out = 0; return 0x2720; }
    f->done = 1; if (out) *out = f; return 0;
}
int gldDestroyFence(gld_context *ctx, void *f) { (void)ctx; free(f); return 0; }
void mach64_signal_fence(gld_context *ctx, void *fence)
{
    mach64_priv *p = MACH64_PRIV(ctx); gld_fence *f = (gld_fence *)fence;
    if (p && p->attached) { mach64_flush_batch(ctx); rgb_fence(&p->rb); }
    if (f) f->done = 1;
}
int gldTestObject(gld_context *ctx, int kind, void *obj)
{
    mach64_priv *p = MACH64_PRIV(ctx);
    if (kind == 0 && p && p->attached) { mach64_flush_batch(ctx); rgb_fence(&p->rb); }
    (void)obj; return 1;
}
int gldFinishObject(gld_context *ctx, int kind, void *obj)
{
    mach64_priv *p = MACH64_PRIV(ctx);
    if (p && p->attached) { mach64_flush_batch(ctx); rgb_fence(&p->rb); }
    (void)kind; (void)obj; return 0;
}
int gldCreateQuery(gld_context *ctx, void **out)
{
    gld_query *q = (gld_query *)calloc(1, sizeof *q); (void)ctx;
    if (!q) { if (out) *out = 0; return 0x2720; }
    if (out) *out = q; return 0;
}
int gldDestroyQuery(gld_context *ctx, void *q)
{
    mach64_priv *p = MACH64_PRIV(ctx);
    if (p && p->query == q) { p->query = 0; mach64_sync_depth_from_gli(ctx); }
    free(q); return 0;
}
int gldGetQueryInfo(gld_context *ctx, void *query, uint32_t pname, int *out)
{
    mach64_priv *p = MACH64_PRIV(ctx); gld_query *q = (gld_query *)query;
    if (p && p->attached) { mach64_flush_batch(ctx); rgb_fence(&p->rb); }
    if (!q || !out) return 0;
    if (pname == 0x8867u) *out = 1; else if (pname == 0x8866u) *out = (int)q->count;
    return 0;
}
void mach64_begin_query(gld_context *ctx, void *query)
{
    mach64_priv *p = MACH64_PRIV(ctx); gld_query *q = (gld_query *)query;
    if (!p || !q) return;
    q->count = 0; p->query = q; mach64_sync_depth_from_gli(ctx);          /* the CPU rasteriser counts the passing samples */
}
void mach64_end_query(gld_context *ctx)
{
    mach64_priv *p = MACH64_PRIV(ctx);
    if (!p) return;
    mach64_flush_batch(ctx); p->query = 0; mach64_sync_depth_from_gli(ctx);
}
void mach64_query_add(gld_context *ctx, uint32_t n) { mach64_priv *p = MACH64_PRIV(ctx); if (p && p->query) ((gld_query *)p->query)->count += n; }

/* gldCreateShared / gldDestroyShared -- the context-level SHARED state object (the shared texture-name /
 * display-list namespace that sibling GL contexts inherit).  These are REAL in the reference (ati128gld
 * @0x4d00 / @0x4d60), not stubs: CreateShared allocs a 44-byte struct, zero-inits it, and writes the
 * pointer to *out; DestroyShared frees it.  Our old GLD_STUB took void and NEVER wrote *out, so GLEngine
 * kept an UNINITIALIZED driver shared-handle (garbage it may later dereference) -- a latent corruption
 * source.  We don't share texture storage at the driver level (our VRAM heap is already process-global),
 * so the struct is opaque bookkeeping GLEngine holds and hands back to us; matching the reference's
 * alloc/free contract is what matters.  Signature per the reference: r3 = void **out (no ctx arg). */
int gldCreateShared(void **out)
{
    void *s;
    if (!out) { return 0x271e; }  /* ref: error on NULL out */
    *out = NULL;
    s = calloc(1, 0x2c);                                    /* 44-byte shared struct, zero-inited (ref) */
    if (!s) { return 0x2720; }
    *out = s;
    return 0;
}
int gldDestroyShared(void *shared)
{
    if (!shared) { return 0x2714; }  /* ref: error on NULL */
    free(shared);
    return 0;
}
/* ---- Texture path -------------------------------------------------------
 * Return a valid 0x40-byte texture-object (faithful to the reference
 * gldCreateTexture, ati128gld 0x6838) so GLEngine sets up texturing. */
void gldCreateTexture(void *ctx, void **out, void *shared)
{
    rage_fill_descriptor((gld_context *)ctx);
    unsigned char *t = (unsigned char *)calloc(1, 0x40);
    (void)ctx;
    if (t) {
        *(uint32_t *)(t + 0x04) = 3;
        *(uint32_t *)(t + 0x30) = (uint32_t)(uintptr_t)shared;   /* format/shared */
        *(uint32_t *)(t + 0x18) = (uint32_t)(uintptr_t)(t + 0x1c);  /* list head */
        *(uint32_t *)(t + 0x20) = (uint32_t)(uintptr_t)(t + 0x18);
        t[0x38] = 0xff; t[0x39] = 0x23;
    }
    if (out) *out = t;
}
int gldDeleteTexture(void *ctx, void *t)
{ (void)ctx; if (t) { mach64_texture_deleted(t); free(t); } return 0; }
int gldCreateTextureLevel(void *ctx, void *t, uint32_t a3, uint32_t lvl, uint32_t a5, uint32_t a6, uint32_t a7)
{ (void)ctx; return 0; }
int gldDeleteTextureLevel(void *ctx, void *t, uint32_t lvl)
{ (void)ctx; return 0; }
int gldModifyTexture(void *ctx, void *t, uint32_t a3)
{
    rage_fill_descriptor((gld_context *)ctx);
    mach64_texture_bound(t);                /* this texture is now the current one */
    if (a3 & 0x80)                          /* image dirty: upload at next draw    */
        mach64_texture_dirtied(t);
    return 0;
}
int gldModifyTextureLevel(void *ctx, void *t, uint32_t a3, uint32_t lvl, uint32_t a5, uint32_t a6, uint32_t a7)
{ (void)ctx; return 0; }
int gldGetTextureLevel(void *ctx, void *t, uint32_t a3, uint32_t a4, uint32_t a5, uint32_t a6, uint32_t a7)
{ (void)ctx; return 0; }
/* glGetTexLevelParameter for the driver.  a3=level, a5=pname (GL enum), a6=out (write the value here),
 * a7=the candidate dimension for a proxy level.
 *
 * ROOT CAUSE of the Chess hang (HW-confirmed): the caller (gluBuild2DMipmaps' proxy probe) seeds *out=0
 * and spins `while(width==0)` until we write a NONZERO width.  Unlike the reference @0x69f4 -- where
 * GLEngine answers WIDTH/HEIGHT from its own bookkeeping and only forwards format/size pnames to the
 * driver -- our GLEngine path forwards the WIDTH query to us AND does not route the proxy glTexImage2D
 * through gldModifyTexture, so the level record (state+0x9c) reads w=h=0.  The candidate size is instead
 * passed in a7 (observed a7=0x100=256 for Chess's 256x256 board texture; the state object holds no dims).
 *   The earlier stub wrote *out=0 (width 0), so every proxy size looked "rejected" and the shrink-loop
 * never terminated (1.07M identical GL_TEXTURE_WIDTH polls, app hung).  Fix: answer WIDTH/HEIGHT with the
 * level record when populated, else the a7 candidate; reject (write 0) only a proxy level that exceeds the
 * Rage Pro's 1024x1024 limit; answer INTERNAL_FORMAT / component sizes explicitly.
 * Level record: t+0x30 -> state; rec at state+0x9c+(baseLOD[s+0x5e]+level)*0x14 (w@0x00 h@0x02 fmt@0x0c);
 * proxy flag = bit 0x100 of *(u16*)state. */
int gldGetTextureLevelInfo(void *ctx, void *t, uint32_t a3, uint32_t a4, uint32_t a5, uint32_t a6, uint32_t a7)
{
    uint32_t state, w = 0, h = 0; uint16_t fmt = 0, flags = 0;
    (void)ctx; (void)a4; (void)a7;
    state = t ? *(const uint32_t *)((const unsigned char *)t + 0x30) : 0;
    if (state >= 0x10000u && state <= M64_UADDR_MAX) {
        const unsigned char *s = (const unsigned char *)(uintptr_t)state;
        const unsigned char *r = s + 0x9c + ((uint32_t)s[0x5e] + a3) * 0x14;
        flags = *(const uint16_t *)s;                       /* proxy flag lives in bit 0x100 */
        w     = *(const uint16_t *)(r + 0x00);
        h     = *(const uint16_t *)(r + 0x02);
        fmt   = *(const uint16_t *)(r + 0x0c);
    }
    if (a6 < 0x10000u) return 0;                            /* no out pointer -> nothing to do */

    /* GLEngine seeds *out=0 and the caller (gluBuild2DMipmaps' proxy check) spins `while(width==0)`, so we
     * MUST write a nonzero width.  For a PROXY query GLEngine does NOT route the glTexImage2D through our
     * gldModifyTexture, so the level record is empty (w=h=0) and the candidate dimension is passed in a7. */
    { uint32_t cand = a7 ? a7 : 1u;                          /* candidate size for a proxy level (lives in a7) */
      uint32_t rw = w ? w : cand, rh = h ? h : cand;

      /* Proxy fit reject: too big for the Rage Pro's 1024x1024 limit -> write 0 so the caller shrinks. */
      if ((flags & 0x100u) && (rw > 1024u || rh > 1024u)) {
          *(uint32_t *)(uintptr_t)a6 = 0;
          return 0;
      }
      switch (a5) {
      case 0x1000u: *(uint32_t *)(uintptr_t)a6 = rw; break;                   /* GL_TEXTURE_WIDTH  */
      case 0x1001u: *(uint32_t *)(uintptr_t)a6 = rh; break;                   /* GL_TEXTURE_HEIGHT */
      case 0x8071u: *(uint32_t *)(uintptr_t)a6 = 1u; break;                   /* GL_TEXTURE_DEPTH  */
      case 0x1005u: *(uint32_t *)(uintptr_t)a6 = 0u; break;                   /* GL_TEXTURE_BORDER */
      case 0x1003u: *(uint32_t *)(uintptr_t)a6 = fmt ? fmt : 0x1908u; break;  /* INTERNAL_FORMAT (dflt GL_RGBA) */
      case 0x805cu: case 0x805du: case 0x805eu: case 0x805fu:                 /* R/G/B/A_SIZE = 8 bpp */
          *(uint32_t *)(uintptr_t)a6 = 8u; break;
      case 0x8060u: case 0x8061u:                                            /* LUMINANCE/INTENSITY_SIZE */
          *(uint32_t *)(uintptr_t)a6 = 0u; break;
      default: *(uint32_t *)(uintptr_t)a6 = 0u; break;
      }
    }
    return 0;
}
int gldIsTextureResident(void *ctx, void *t)
{ (void)ctx; return 1; }
GLD_STUB(gldCreateVertexArray)     GLD_STUB(gldModifyVertexArray)
GLD_STUB(gldFlushVertexArray)      GLD_STUB(gldDestroyVertexArray)
GLD_STUB(gldDestroyMemoryPluginData) GLD_STUB(gldGetMemoryPluginData)
GLD_STUB(gldSetMemoryPluginData)   GLD_STUB(gldFinishMemoryPluginData)
GLD_STUB(gldTestMemoryPluginData)

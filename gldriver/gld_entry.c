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
 * format is the remaining unknown -> we log what the framework passes and use a
 * placeholder target for now, to be refined once we see real drawables.
 */
#include "gld.h"
#include "../common/mach64_abi.h"   /* kMach64Sel_* selector indices */
#include <stdio.h>
#include <stdarg.h>
#include <stdlib.h>

/* ---- diagnostic log ------------------------------------------------- */
void gld_log(const char *fmt, ...)
{
    va_list ap;
    FILE *f = fopen("/tmp/mach64gld.log", "a");
    if (!f) return;
    va_start(ap, fmt);
    vfprintf(f, fmt, ap);
    va_end(ap);
    fclose(f);
}

/* ---- gldAttachDrawable: bind the app's drawable as the render target -
 * type: 0x50 window / 0x5a fullscreen / 0x36 (pbuffer?) -- same tokens as the
 * pixel-format parser.  drawable: Apple CGS descriptor; the reference reads
 * drawable[0/4/8] and calls a kext set_surface selector (ctx+0x04 connection).
 * TODO: reverse the CGS format -> real VRAM offset/pitch + our set_surface. */
int gldAttachDrawable(gld_context *ctx, int type, void *drawable, uint32_t flags)
{
    gld_log("gldAttachDrawable(ctx=%p type=0x%x drawable=%p flags=0x%x)\n",
            (void*)ctx, type, drawable, flags);
    if (!ctx) return 0x271e;
    if ((type == 0x50 || type == 0x5a || type == 0x36) && !drawable)
        return 0x2715;
    /* detach: restore the CRTC scanout to the front (original desktop) buffer, else a
     * page-flipped DB context leaves the display scanning out of our back buffer and
     * the desktop stays invisible after the app exits. */
    if (type == 0 || !drawable) {
        mach64_priv *pv = MACH64_PRIV(ctx);
        if (pv && pv->dbuf && pv->front_off) mach64_flip(ctx, pv->front_off);
        return 0;
    }

    {
        const uint32_t *d = (const uint32_t *)drawable;
        uint8_t *c = (uint8_t *)ctx;
        uint32_t width  = d[1];        /* windowed descriptor: [1]=width      */
        uint32_t height = d[2];        /*                      [2]=height     */
        uint32_t base_bytes  = 0x600000u;   /* fallback: off-screen scratch  */
        uint32_t pitch_bytes;
        uint32_t fb_pitch_px;

        /* Bind the LIVE display framebuffer as the render target (full-screen): read the
         * mach64 CRTC via the kext -> DMA lands on screen.  Take the render dims from the
         * CRTC HARDWARE (pitch = width, V_DISP+1 = height), NOT the drawable descriptor:
         * GLEngine's fullscreen descriptor has been observed pointing at reused heap (d[2]
         * = ASCII "path", d[1] a growing garbage value) which would leave ctx+0x1f4 junk
         * and render everything offscreen.  The CRTC is the authoritative fullscreen size. */
        if ((type == 0x36 || type == 0x5a) && ctx->connect) {
            uint32_t crtc = 0, base = 0, pitch = 0, hi = 0;
            IOConnectMethodScalarIScalarO(ctx->connect, kMach64Sel_get_surface_size,
                                          0, 4, &crtc, &base, &pitch, &hi);
            gld_log("  framebuffer CRTC_OFF_PITCH=0x%x -> base=0x%x pitch=%upx v_disp=%u\n",
                    crtc, base, pitch, hi);
            if (pitch) { base_bytes = base; width = pitch;
                         if (hi >= 16 && hi <= 4096) height = hi; }
        }
        pitch_bytes = width * 4;
        fb_pitch_px = width;

        gld_log("  drawable[0..2] = 0x%x 0x%x 0x%x  -> surface %ux%u target base=0x%x\n",
                d[0], d[1], d[2], width, height, base_bytes);

        /* Populate the ctx surface fields GLEngine reads to consider the drawable
         * renderable -- WITHOUT these it culls all geometry (no dispatch).
         * Offsets from the reference gldAttachDrawable (ati128gld 0x4f38):
         * 0x20=drawable, 0x24/0x28=flags, 0x94=target base, 0x98=pitch,
         * 0x1f0/0x1f4=width/height, 0x64=surface handle, 0x68=byte size.
         * These land in unused/_tail/extra ctx space (we don't map registers). */
        *(uint32_t *)(c + 0x20)  = (uint32_t)drawable;
        *(uint32_t *)(c + 0x24)  = (flags >> 8) & 0xff;
        *(uint32_t *)(c + 0x28)  = flags & 0xff;
        *(uint32_t *)(c + 0x64)  = 1;                      /* non-null handle  */
        *(uint32_t *)(c + 0x68)  = pitch_bytes * height;   /* surface size     */
        *(uint32_t *)(c + 0x94)  = base_bytes;             /* target base      */
        *(uint32_t *)(c + 0x98)  = pitch_bytes;            /* target pitch     */
        /* 0x9c..0xa8: ref context-init defaults (ati128gld 0x4b30) that
         * gldInitDispatch reports to GLEngine's out-struct.  Without these they
         * are malloc garbage. */
        *(uint32_t *)(c + 0x9c)  = 0;
        *(uint32_t *)(c + 0xa0)  = 0;
        *(uint32_t *)(c + 0xa4)  = 0x32;
        *(uint32_t *)(c + 0xa8)  = 0;
        *(uint32_t *)(c + 0x1f0) = width;
        *(uint32_t *)(c + 0x1f4) = height;

        /* DOUBLE-BUFFER DISPATCH GATE (reference RE, ATIRage128GLDriver gldAttach-
         * Drawable type-0x36 path -> sub_a524(ctx,&caps,r5=0)).  The reference does
         * NOT route DB geometry through a special dispatch slot -- slot 0x60 is a
         * no-op.  Instead it sets a FLAG in the GLEngine object at *(ctx+0x14) (= a7
         * from gldCreateContext): word0 |= 2.  GLEngine reads that flag to decide the
         * context has a bound back-buffer surface and dispatches primitives normally
         * instead of falling back to slot 0x60.  It also sets ctx+0x1e4=1 and requires
         * caps bit 0x20 (ctx+0x2c) CLEAR.  Our DB context attaches as type 0x36 but
         * never set this -> GLEngine routed it to 0x60 -> black.  Set it now. */
        if (type == 0x36) {
            uint32_t a7p = *(uint32_t *)(c + 0x14);
            *(uint32_t *)(c + 0x2c) &= ~0x20u;      /* caps 0x20 clear (0x36 gate) */
            *(uint32_t *)(c + 0x1e4) = 1;
            if (a7p > 0x10000 && a7p < 0x40000000) {
                uint32_t w0 = *(uint32_t *)(uintptr_t)a7p;
                *(uint32_t *)(uintptr_t)a7p = w0 | 2u;
                gld_log("  [DB-gate] a7=0x%x word0 0x%x -> 0x%x, ctx+0x2c=0x%x ctx+0x1e4=1\n",
                        a7p, w0, w0 | 2u, *(uint32_t *)(c + 0x2c));
            } else {
                gld_log("  [DB-gate] a7=0x%x NOT a pointer -- flag NOT set\n", a7p);
            }
        }

        mach64_set_render_target(ctx, base_bytes, fb_pitch_px);

        {   mach64_priv *pv = MACH64_PRIV(ctx);
            uint32_t surf  = fb_pitch_px * 4u * height;   /* bytes per colour surface */
            uint32_t z_off = 0x400000u;   /* single-buffered: Z past the front buffer */

            /* Double-buffered (dbuf armed by the DB pixel format in gldCreateContext):
             * render off-screen to a back buffer and blit it to the front on present.
             * 8MB VRAM map: front | back | Z | texture heap, packed to the top --
             *   front 0x1000..            scanout, 3MB @ 1024x768x32
             *   back  front+surf..        off-screen render target (3MB)
             *   Z     back+surf..         16-bit depth (1.5MB)
             *   heap  Z+zsize..0x800000   ~0.5MB (shrunk from 1.5MB to make room).
             * GLEngine's glClear hits the FRONT, so the driver clears the back. */
            if (pv->dbuf) {
                uint32_t back  = base_bytes + surf;
                uint32_t zsize = fb_pitch_px * 2u * height;
                z_off = back + surf;
                pv->front_off = base_bytes;
                pv->back_off  = back;
                pv->render_off = back;   /* page-flip: front is displayed, render into back */
                pv->fb_w = width; pv->fb_h = height; pv->fb_pitch = fb_pitch_px;
                mach64_set_texheap(z_off + zsize, 0x800000u);
                mach64_set_render_target(ctx, back, fb_pitch_px);   /* draw to back */
                gld_log("  [dbuf] front=0x%x back=0x%x z=0x%x %ux%u pitch=%upx -> render->back\n",
                        base_bytes, back, z_off, width, height, fb_pitch_px);
            }

            /* DEPTH: bind the 16-bit Z buffer (single-buf: 0x400000 past the front;
             * double-buf: above the back buffer).  Real per-vertex Z comes from the
             * NDC z in mach64_read_vertex; Z_CNTL follows the app's GL depth state,
             * synced per-draw in mach64_flush / render_tri_strip -- not forced here. */
            mach64_set_zbuffer(ctx, z_off, fb_pitch_px);
        }
        MACH64_PRIV(ctx)->frame_new = 1;   /* clear Z (+ back) before the first frame */

        /* EXPERIMENT (root-cause validation): GLEngine leaves its drawable
         * descriptor gli[0x789c] ALL-ZEROS for our HW surface (surface-binding
         * contract incomplete), so glViewport clamps the viewport to
         * desc[0xc]/desc[0x10] = 0 -> 0x0 viewport -> ALL geometry culled before
         * any primitive dispatch.  The software renderer's descriptor is fully
         * populated (desc = {4, 4e7fffff, 3, 0x800, 0x800, ...}).  Populate it
         * ourselves to test whether that is the ONLY gate.  ctx+0x10 = the gli
         * context (a8 from gldCreateContext); desc = gli[0x789c]. */
        {   uint32_t p10 = *(uint32_t *)(c + 0x10);   /* points into gli (+0x3610) */
            uint32_t gli = 0, off;
            /* Find the gli base: the object whose [+0x7898] back-points to ctx. */
            for (off = 0; off <= 0x8000; off += 4) {
                uint32_t g = p10 - off;
                if (g < 0x10000 || g >= 0x40000000) break;
                if (*(uint32_t *)(uintptr_t)(g + 0x7898) == (uint32_t)(uintptr_t)ctx) {
                    gli = g; break;
                }
            }
            gld_log("  [desc-exp] ctx+0x10=0x%x -> gli base=0x%x (off 0x%x)\n", p10, gli, off);
            if (gli) {
                uint32_t descp = *(uint32_t *)(uintptr_t)(gli + 0x789c);
                gld_log("  [desc-exp] gli[0x789c]=desc=0x%x  desc[0xc]=0x%x desc[0x10]=0x%x\n",
                        descp,
                        descp>0x10000?*(uint32_t*)(uintptr_t)(descp+0xc):0,
                        descp>0x10000?*(uint32_t*)(uintptr_t)(descp+0x10):0);
                if (descp > 0x10000 && descp < 0x40000000) {
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
                    *(uint16_t *)(uintptr_t)(descp + 0xac) = 1;
                    gld_log("  [desc-exp] populated descriptor (maxtex=1024, texunits=1)\n");
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
    gld_log("gldGetString(name=0x%x)\n", name);
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
    gld_log("gldGetVersion()\n");
    if (v0) *v0 = 2;
    if (v1) *v1 = 4;
    if (v2) *v2 = 0xb;
    if (v3) *v3 = 0x1000;
    return 1;
}

int gldGetError(gld_context *ctx) { (void)ctx; gld_log("gldGetError()\n"); return 0; }

int gldGetInteger(gld_context *ctx, uint32_t pname, int *params)
{
    /* GLEngine never calls this for the texture caps (GL_MAX_TEXTURE_SIZE comes
     * from the drawable descriptor +0xb2, set in gldAttachDrawable). */
    (void)ctx;
    gld_log("gldGetInteger(pname=0x%x)\n", pname);
    if (params) *params = 0;
    return 0;
}

int gldSetInteger(gld_context *ctx, uint32_t pname, int value)
{ (void)ctx; gld_log("gldSetInteger(pname=0x%x val=%d)\n", pname, value); return 0; }

/* gldInitDispatch: initial dispatch-table setup (pairs with gldUpdateDispatch).
 * Install our render slots so the table is usable from the start. */
void gldInitDispatch(gld_context *ctx, void *table, void *outinfo)
{
    gld_log("gldInitDispatch(table=%p outinfo=%p)\n", table, outinfo);
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
        gld_log("  outinfo <= ctx[0x94..0xa8]: %08x %08x %08x %08x %08x %08x\n",
                o[0],o[1],o[2],o[3],o[4],o[5]);
    }
    /* Reference gldInitDispatch installs the driver's control/flush slots that
     * GLEngine calls to render.  table[0x04] is THE flush entry.  Store the table
     * ptr in ctx+0x1c (reference: stw r4, 0x1c(ctx)) and install our flush. */
    *(void **)((unsigned char *)ctx + 0x1c)  = table;
    *(void **)((unsigned char *)table + 0x00) = (void *)mach64_ctl00;
    *(void **)((unsigned char *)table + 0x04) = (void *)mach64_flush;
    *(void **)((unsigned char *)table + 0x08) = (void *)mach64_ctl08;
    *(void **)((unsigned char *)table + 0x0c) = (void *)mach64_ctl0c;
    *(void **)((unsigned char *)table + 0x10) = (void *)mach64_ctl10;
    *(void **)((unsigned char *)table + 0x14) = (void *)mach64_ctl14;
    gldUpdateDispatch(ctx, table, (uint32_t)(uintptr_t)outinfo);
}

/* gldFlushBuffer: swap/present the drawable.  Submits are synchronous today, so
 * fence.  TODO: real front/back flip once double-buffered surfaces are bound. */
int gldFlushBuffer(gld_context *ctx, void *drawable)
{
    uint8_t *c = (uint8_t *)ctx;
    gld_log("gldFlushBuffer(ctx=%p drawable=%p) cbpp(ctx+0x5f)=%u target=0x%x pitch=0x%x\n",
            (void*)ctx, drawable, (unsigned)c[0x5f],
            *(uint32_t *)(c + 0x94), *(uint32_t *)(c + 0x98));
    /* FINISH FIRST, then present.  gldFinish flushes the triangle batch, submits the
     * command buffer and waits for idle -> the back buffer is fully rendered.  Only
     * then is it safe to blit back->front.  (Presenting first blits an un-rendered
     * back buffer, so a swap without a preceding glFinish showed black -- the
     * animation swap-chain bug.  A single glFinish-before-swap test hid it.) */
    {   int r = gldFinish(ctx);
        if (MACH64_PRIV(ctx)->dbuf) mach64_present(ctx);   /* swap: back -> front */
        MACH64_PRIV(ctx)->frame_new = 1;                   /* next frame: clear Z + back */
        return r;
    }
}

/* ---- feature stubs: present so GLEngine's lookups are non-NULL; only called
 * if the app uses the feature.  Return 0 (NULL/success). --------------- */
#define GLD_STUB(name) int name(void) { gld_log(#name "()\n"); return 0; }
GLD_STUB(gldCreateBuffer)          GLD_STUB(gldDestroyBuffer)
GLD_STUB(gldCreateFence)           GLD_STUB(gldDestroyFence)
GLD_STUB(gldCreatePipelineProgram) GLD_STUB(gldDestroyPipelineProgram)
GLD_STUB(gldModifyPipelineProgram) GLD_STUB(gldRelatePipelineProgram)
GLD_STUB(gldGetPipelineProgramInfo)
GLD_STUB(gldCreateQuery)           GLD_STUB(gldDestroyQuery)
GLD_STUB(gldGetQueryInfo)
GLD_STUB(gldCreateShared)          GLD_STUB(gldDestroyShared)
/* ---- Texture path (INVESTIGATION) --------------------------------------
 * Return a valid 0x40-byte texture-object (faithful to the reference
 * gldCreateTexture, ati128gld 0x6838) so GLEngine sets up texturing and emits
 * per-vertex texcoords we can locate; log every texture call + its args to map
 * how GLEngine delivers pixels.  Not a real upload yet. */
void gldCreateTexture(void *ctx, void **out, void *shared)
{
    unsigned char *t = (unsigned char *)calloc(1, 0x40);
    (void)ctx;
    if (t) {
        *(uint32_t *)(t + 0x04) = 3;
        *(uint32_t *)(t + 0x30) = (uint32_t)(uintptr_t)shared;   /* format/shared */
        *(uint32_t *)(t + 0x18) = (uint32_t)(uintptr_t)(t + 0x1c);  /* list head */
        *(uint32_t *)(t + 0x20) = (uint32_t)(uintptr_t)(t + 0x18);
        t[0x38] = 0xff; t[0x39] = 0x23;
    }
    gld_log("gldCreateTexture(out=%p shared=%p) -> texobj=%p\n", (void*)out, shared, (void*)t);
    if (out) *out = t;
}
int gldDeleteTexture(void *ctx, void *t)
{ (void)ctx; gld_log("gldDeleteTexture(%p)\n", t); if (t) { mach64_texture_deleted(t); free(t); } return 0; }
int gldCreateTextureLevel(void *ctx, void *t, uint32_t a3, uint32_t lvl, uint32_t a5, uint32_t a6, uint32_t a7)
{ (void)ctx; gld_log("gldCreateTextureLevel(t=%p a3=0x%x lvl=%u a5=0x%x a6=0x%x a7=0x%x)\n", t,a3,lvl,a5,a6,a7); return 0; }
int gldDeleteTextureLevel(void *ctx, void *t, uint32_t lvl)
{ (void)ctx; gld_log("gldDeleteTextureLevel(t=%p lvl=%u)\n", t, lvl); return 0; }
int gldModifyTexture(void *ctx, void *t, uint32_t a3)
{
    (void)ctx;
    gld_log("gldModifyTexture(t=%p a3=0x%x)\n", t, a3);
    mach64_texture_bound(t);                /* this texture is now the current one */
    if (a3 & 0x80)                          /* image dirty: upload at next draw    */
        mach64_texture_dirtied(t);
    return 0;
}
int gldModifyTextureLevel(void *ctx, void *t, uint32_t a3, uint32_t lvl, uint32_t a5, uint32_t a6, uint32_t a7)
{ (void)ctx; gld_log("gldModifyTextureLevel(t=%p a3=0x%x lvl=%u a5=0x%x a6=0x%x a7=0x%x)\n", t,a3,lvl,a5,a6,a7); return 0; }
int gldGetTextureLevel(void *ctx, void *t, uint32_t a3, uint32_t a4, uint32_t a5, uint32_t a6, uint32_t a7)
{ (void)ctx; gld_log("gldGetTextureLevel(t=%p a3=0x%x a4=0x%x a5=0x%x a6=0x%x a7=0x%x)\n", t,a3,a4,a5,a6,a7); return 0; }
int gldGetTextureLevelInfo(void *ctx, void *t, uint32_t a3, uint32_t a4, uint32_t a5, uint32_t a6, uint32_t a7)
{ (void)ctx; gld_log("gldGetTextureLevelInfo(t=%p a3=0x%x a4=0x%x a5=0x%x a6=0x%x a7=0x%x)\n", t,a3,a4,a5,a6,a7); return 0; }
int gldIsTextureResident(void *ctx, void *t)
{ (void)ctx; gld_log("gldIsTextureResident(%p)\n", t); return 1; }
GLD_STUB(gldCreateVertexArray)     GLD_STUB(gldModifyVertexArray)
GLD_STUB(gldFlushVertexArray)      GLD_STUB(gldDestroyVertexArray)
GLD_STUB(gldDestroyMemoryPluginData) GLD_STUB(gldGetMemoryPluginData)
GLD_STUB(gldSetMemoryPluginData)   GLD_STUB(gldFinishMemoryPluginData)
GLD_STUB(gldTestMemoryPluginData)
GLD_STUB(gldFinishObject)          GLD_STUB(gldTestObject)

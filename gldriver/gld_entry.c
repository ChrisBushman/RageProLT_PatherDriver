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
    /* detach */
    if (type == 0 || !drawable) return 0;

    {
        const uint32_t *d = (const uint32_t *)drawable;
        uint8_t *c = (uint8_t *)ctx;
        uint32_t width  = d[1];        /* full-screen descriptor: [1]=1024   */
        uint32_t height = d[2];        /*                         [2]=768    */
        uint32_t base_bytes  = 0x600000u;   /* fallback: off-screen scratch  */
        uint32_t pitch_bytes = width * 4;
        uint32_t fb_pitch_px = width;

        /* Bind the LIVE display framebuffer as the render target (full-screen):
         * read the mach64 CRTC_OFF_PITCH via the kext -> DMA lands on screen. */
        if ((type == 0x36 || type == 0x5a) && ctx->connect) {
            uint32_t crtc = 0, base = 0, pitch = 0, hi = 0;
            IOConnectMethodScalarIScalarO(ctx->connect, kMach64Sel_get_surface_size,
                                          0, 4, &crtc, &base, &pitch, &hi);
            gld_log("  framebuffer CRTC_OFF_PITCH=0x%x -> base=0x%x pitch=%upx\n",
                    crtc, base, pitch);
            if (pitch) { base_bytes = base; fb_pitch_px = pitch;
                         pitch_bytes = pitch * 4; }
        }

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

        mach64_set_render_target(ctx, base_bytes, fb_pitch_px);

        /* DEPTH: bind a 16-bit Z buffer past the visible framebuffer (fb is base
         * 0x1000 + 1024x768x4 ~ 0x301000, so 0x400000 doesn't overlap), enable
         * GL_LESS + Z write, and clear it to far.  Real per-vertex Z now comes
         * from the NDC z in mach64_read_vertex, so overlapping geometry occludes
         * correctly (Z_CNTL/vertex-Z/Z_OFF_PITCH all verified vs the RRG spec). */
        mach64_set_zbuffer(ctx, 0x400000u, fb_pitch_px);
        mach64_set_depth_test(ctx, 1, 0x0201 /* GL_LESS */);
        mach64_clear_zbuffer(ctx);

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
                    gld_log("  [desc-exp] populated descriptor (dims 0x800x0x800)\n");
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
{ (void)drawable; return gldFinish(ctx); }

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
GLD_STUB(gldCreateTexture)         GLD_STUB(gldDeleteTexture)
GLD_STUB(gldCreateTextureLevel)    GLD_STUB(gldDeleteTextureLevel)
GLD_STUB(gldModifyTexture)         GLD_STUB(gldModifyTextureLevel)
GLD_STUB(gldGetTextureLevel)       GLD_STUB(gldGetTextureLevelInfo)
GLD_STUB(gldIsTextureResident)
GLD_STUB(gldCreateVertexArray)     GLD_STUB(gldModifyVertexArray)
GLD_STUB(gldFlushVertexArray)      GLD_STUB(gldDestroyVertexArray)
GLD_STUB(gldDestroyMemoryPluginData) GLD_STUB(gldGetMemoryPluginData)
GLD_STUB(gldSetMemoryPluginData)   GLD_STUB(gldFinishMemoryPluginData)
GLD_STUB(gldTestMemoryPluginData)
GLD_STUB(gldFinishObject)          GLD_STUB(gldTestObject)

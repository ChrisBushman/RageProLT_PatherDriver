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
        /* DISPATCH TEST: keep the render target OFF-SCREEN (scratch VRAM) so we
         * can confirm GLEngine now flushes geometry to our slot without touching
         * the visible framebuffer.  base/pitch below are the scratch values. */
        uint32_t base_bytes  = 0x600000u;
        uint32_t pitch_bytes = width * 4;

        gld_log("  drawable[0..2] = 0x%x 0x%x 0x%x  -> surface %ux%u\n",
                d[0], d[1], d[2], width, height);

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
        *(uint32_t *)(c + 0x1f0) = width;
        *(uint32_t *)(c + 0x1f4) = height;

        mach64_set_render_target(ctx, base_bytes, width);
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
void gldInitDispatch(gld_context *ctx, void *table, uint32_t flags)
{
    gld_log("gldInitDispatch(table=%p flags=0x%x)\n", table, flags);
    gldUpdateDispatch(ctx, table, flags);
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

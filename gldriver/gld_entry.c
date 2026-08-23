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
    if (drawable) {
        const uint32_t *d = (const uint32_t *)drawable;
        gld_log("  drawable[0..3] = 0x%08x 0x%08x 0x%08x 0x%08x\n",
                d[0], d[1], d[2], d[3]);
    }
    /* Placeholder: bind our proven off-screen target so the render pipeline runs
     * end-to-end under the framework.  Real windowed binding comes once the CGS
     * descriptor -> VRAM offset/pitch mapping is recovered from live drawables. */
    mach64_set_render_target(ctx, 0x600000u, 128);
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

int gldGetError(gld_context *ctx) { (void)ctx; return 0; }   /* GL_NO_ERROR */

int gldGetInteger(gld_context *ctx, uint32_t pname, int *params)
{
    (void)ctx; (void)pname;
    if (params) *params = 0;
    return 0;
}

int gldSetInteger(gld_context *ctx, uint32_t pname, int value)
{ (void)ctx; (void)pname; (void)value; return 0; }

/* gldInitDispatch: initial dispatch-table setup (pairs with gldUpdateDispatch).
 * Install our render slots so the table is usable from the start. */
void gldInitDispatch(gld_context *ctx, void *table, uint32_t flags)
{
    gldUpdateDispatch(ctx, table, flags);
}

/* gldFlushBuffer: swap/present the drawable.  Submits are synchronous today, so
 * fence.  TODO: real front/back flip once double-buffered surfaces are bound. */
int gldFlushBuffer(gld_context *ctx, void *drawable)
{ (void)drawable; return gldFinish(ctx); }

/* ---- feature stubs: present so GLEngine's lookups are non-NULL; only called
 * if the app uses the feature.  Return 0 (NULL/success). --------------- */
#define GLD_STUB(name) int name(void) { return 0; }
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

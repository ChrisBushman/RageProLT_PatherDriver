/*
 * gld_glue.c - bundle glue: the service matcher gld_context.c needs, plus the
 * library lifecycle entry points OpenGL.framework calls at load/unload.
 */
#include "gld.h"
#include <IOKit/IOKitLib.h>
#include <mach/mach.h>

/* Resolve our accelerator so gldGetRendererInfo/gldCreateContext can open it.
 * Matches the Mach64Accelerator personality our kext publishes. */
io_service_t mach64_match_service(uint32_t index)
{
    (void)index;
    return IOServiceGetMatchingService(kIOMasterPortDefault,
                                       IOServiceMatching("Mach64Accelerator"));
}

#ifdef GLD_APPLE_PROBE
/* Path B -- from INSIDE our GLEngine-loaded bundle, open Apple's own ATIRagePro GL user client
 * (IOATIRProGLContext = accelerator user-client TYPE 1, reversed from the kext) and RETURN the live connection so gldAttachDrawable can drive the surface bind.
 * ADDITIVE -- separate connection, does NOT disturb our existing kext path. */
unsigned int mach64_apple_glctx_open(void)
{
    io_service_t svc = IOServiceGetMatchingService(kIOMasterPortDefault,
                                                   IOServiceMatching("ATIRagePro"));
    if (!svc) return 0;
    io_connect_t cn = 0;
    kern_return_t kr = IOServiceOpen(svc, mach_task_self(), 1 /* GL context type */, &cn);
    IOObjectRelease(svc);
    if (kr != KERN_SUCCESS) return 0;
    return (unsigned int)cn;
}

/* Apple's public IOAccelDeviceRegion (IOAccelBounds = SInt16 x,y,w,h), for set_shape. */
typedef struct { short x, y, w, h; }              apple_bounds;
typedef struct { unsigned int num_rects; apple_bounds bounds; apple_bounds rect[1]; } apple_region;

/* Phase 3 -- CREATE + SHAPE a surface on Apple's accelerator so the GL set_surface can bind it.
 * The VALIDATED sequence (tools/mach64_surfscan.c, proven live):
 *   open type-0 IOATIRProSurface
 *   -> set_id_mode(idx, 0x24)  [ScalarIScalarO, sel 7]  registers it in find_surface_for_id's list
 *   -> set_shape(region w x h)  [ScalarIStructureI, sel 9]  allocates the VRAM backing that
 *      set_surface's setCompatibleSurfaceMode/bind step requires.  Without set_shape, set_surface
 *      finds the surface but returns 0x2c2 (no backing).
 * set_shape_backing frees the (NULL on a fresh surface = no-op) old backing then IOMallocPageable's
 * ~w*h*bpp -- safe as long as w/h are the real window dims.  Returns the surface connection (kept
 * open = surface stays alive), 0 on failure.  mode 0x24 = 8888 | windowed (windowed path stores
 * idx unchanged at surf+0x98, so it accepts the full CGS-sized handle). */
unsigned int mach64_apple_make_surface(uint32_t surface_id, uint32_t mode, uint32_t x, uint32_t y,
                                       uint32_t w, uint32_t h)
{
    io_service_t svc = IOServiceGetMatchingService(kIOMasterPortDefault,
                                                   IOServiceMatching("ATIRagePro"));
    if (!svc) return 0;
    io_connect_t sc = 0;
    /* User-client types (HARDWARE-VERIFIED): the surface client is **TYPE 0**, GL is type 1,
     * 2D context is type 2.  Proven by the get_state probe (tools/mach64_surftype.c: get_state
     * = surface sel 2 succeeds only on the SURFACE client, and that's type 0) AND every
     * live-confirmed present tool (mach64_writetest/fspresent/flushtest.c open SURF_TYPE=0 and
     * put real pixels on screen).  NB: an earlier static-RE pass wrongly concluded type 2 from
     * newUserClient's vtable slots -- but those factory slots are inherited IOAccelerator
     * virtuals patched at load, unresolvable from this kext alone; the live tools are ground
     * truth.  Opening type 2 (the 2D context) made set_id_mode return 0x2c2. */
    kern_return_t kr = IOServiceOpen(svc, mach_task_self(), 0 /* IOATIRProSurface client */, &sc);
    IOObjectRelease(svc);
    if (kr != KERN_SUCCESS) return 0;
    { uint32_t idx = surface_id & 0x7fffu;
      kern_return_t ir = IOConnectMethodScalarIScalarO(sc, 7 /* kIOAccelSurfaceSetIDMode */,
                                                       2, 0, idx, mode);
      if (ir != KERN_SUCCESS) return (unsigned int)sc; }
    /* set_shape -- allocate the surface backing (ScalarIStructureI: 2 scalars + region struct). */
    { apple_region rgn;
      short sw = (short)(w ? w : 1), sh = (short)(h ? h : 1);
      short sx = (short)x, sy = (short)y;
      /* The shape is the surface's clip region IN DISPLAY COORDINATES: build_swap blits the backing to the scanout at the shape's
       * rectangles.  With the origin left at 0,0 every windowed frame was presented at the display's top-left corner (measured:
       * a 400x300 GL window at 100,80 left its frame at x0..399,y0..299 after the window closed).  The backing size is w x h either way. */
      rgn.num_rects = 1;
      rgn.bounds.x  = sx; rgn.bounds.y  = sy; rgn.bounds.w  = sw; rgn.bounds.h  = sh;
      rgn.rect[0].x = sx; rgn.rect[0].y = sy; rgn.rect[0].w = sw; rgn.rect[0].h = sh;
      IOConnectMethodScalarIStructureI(sc, 9 /* set_shape */,
                              2 /*scalarIn*/, (IOByteCount)sizeof(rgn),
                              0u /*shapeBits*/, 0u /*options*/, &rgn); }
    return (unsigned int)sc;
}

/* Re-send the surface shape clipped to the visible rects (n rects, display coordinates; bounds = the whole window).  Used when other windows
 * overlap this one: build_swap blits the backing only to the shape's rectangles, so a full-window shape paints over windows in front. */
void mach64_apple_reshape(unsigned int cn, const short (*r)[4], int n, int x, int y, int w, int h)
{
    struct { unsigned int num_rects; apple_bounds bounds; apple_bounds rect[32]; } rgn;
    int i;
    if (!cn || n < 0 || n > 32) return;
    rgn.num_rects = (unsigned int)n;
    rgn.bounds.x = (short)x; rgn.bounds.y = (short)y; rgn.bounds.w = (short)w; rgn.bounds.h = (short)h;
    for (i = 0; i < n; i++) { rgn.rect[i].x = (short)r[i][0]; rgn.rect[i].y = (short)r[i][1]; rgn.rect[i].w = (short)r[i][2]; rgn.rect[i].h = (short)r[i][3]; }
    IOConnectMethodScalarIStructureI((io_connect_t)cn, 9, 2, (IOByteCount)(12 + 8 * n), 0u, 0u, &rgn);
}

/* Phase 2 -- bind the registered+shaped surface to the GL context.  set_surface's methodDescs
 * convention is ScalarIStructureI (flags=4: 4 scalars + 0-byte struct), NOT ScalarIScalarO --
 * calling it as ScalarIScalarO makes IOKit reject it 0x2c2 BEFORE set_surface runs (that was the
 * whole "0x2c2" saga).  idx = surface_id & 0x7fff (what set_id_mode's windowed path stored at
 * surf+0x98); mode=0 binds (validated: setCompatibleSurfaceMode passes when the surface's format
 * masks to 0, which 8888 does).  Then set_swap_rect for the window geometry. */
void mach64_apple_bind_surface(unsigned int cn, uint32_t surface_id,
                               uint32_t x, uint32_t y, uint32_t w, uint32_t h)
{
    if (!cn) return;
    { uint32_t idx = surface_id & 0x7fffu, dummy = 0;
      /* mode BIT 0x20 = present-by-2D-BLIT (the "windowed" present).  set_surface is the LAST writer
       * of surf[0x154] (after set_id_mode's 0x24), and we used to pass 0 -> bit 0x20 CLEAR.  For a
       * WINDOWED surface that was harmless (it never owns a display, so is_flip_allowed()=0 forces the
       * blit anyway).  But a FULLSCREEN surface OWNS its display (set_id_mode), so with bit 0x20 clear
       * the kext present takes the submit_flip_buffer CRTC PAGE-FLIP path (kext 0x5c28 tests
       * surf[0x154]&0x20), which drives the mach64 CRTC scanout register and RACES Apple's IONDRV
       * framebuffer driver -> HARD KERNEL PANIC (the fullscreen crash).  Setting bit 0x20 forces the
       * safe 2D-blit present for ALL surfaces -- windowed unchanged, fullscreen no longer flips. */
      kern_return_t sr = IOConnectMethodScalarIStructureI((io_connect_t)cn, 0 /* set_surface */,
                             4 /*scalarIn*/, 0 /*structInSize*/,
                             idx, 0x24u /*mode: 0x20=blit-present (no CRTC flip), 0x04=8888*/, 0u, 0u, &dummy);
      if (sr != KERN_SUCCESS) return; }
    /* set_swap_rect is ScalarIStructureI (4 scalars, 0-byte struct) -- common/atiragepro_abi.h, docs/re-notes/ABI.md,
     * tools/mach64_submittest.c.  Issued as ScalarIScalarO it was rejected by IOKit with 0xe00002c2 BEFORE the method ran,
     * so ctx+0x90..0x96 (the window rect Apple's build_swap blits to) was never set and surface_flush presented at the surface's
     * default origin (window-local 0,22) -- the stale GL pixels left in the top-left corner of the display after a window closed. */
    { uint32_t dummy = 0;
      IOConnectMethodScalarIStructureI((io_connect_t)cn, 1 /* set_swap_rect */,
                                       4 /*scalarIn*/, 0 /*structInSize*/, x, y, w, h, &dummy); }
}
#endif

/* The framework's interface pointers, saved for later gld* use. */
void *g_gld_iface = 0;

/* gldInitializeLibrary: the framework passes its interface pointers (r3..r7) and
 * expects the first one RETURNED as the success signal (the reference saves them
 * and returns arg1).  Our earlier `return 0` made the framework treat init as
 * failed and never call gldGetRendererInfo.  Passthrough. */
void *gldInitializeLibrary(void *iface, void *a2, void *a3, void *a4, void *a5)
{
    g_gld_iface = iface;
    return iface;   /* non-null = success, matching the reference */
}

void gldTerminateLibrary(void) { }

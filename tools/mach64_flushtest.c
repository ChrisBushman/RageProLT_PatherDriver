/* mach64_flushtest.c -- LIVE validation that Apple's present path runs engine-INDEPENDENTLY.
 *
 * Static crack (docs/re-notes/apple-glcontext-protocol.md "DECISIVE 2026-08-27"): Apple's
 * "swap" is a PIO 2D BLIT, not a CRTC flip, and it never touches the stubbed start_engine:
 *   surface_flush (surface sel 10) -> flush_surface -> (virtual) submit_swap_buffer
 *     -> build_swap  (emits mach64 2D-blit {reg,val} pairs, per-clip-rect)
 *     -> ATIRagePro::submit_buffer  (PIO: stwbrx val -> MMIO[reg], FIFO-throttled; NO DMA/engine)
 * start_engine @13210 (li r3,1;blr) has ZERO callers -> the stub is never on any path.
 *
 * THIS PROBE drives the VALIDATED surface handshake (set_id_mode + set_shape + set_surface, all
 * proven live in surfscan) and then fires surface_flush to confirm the present path EXECUTES and
 * RETURNS without needing the engine.  It does NOT yet aim to paint the real scanout (that needs a
 * fullscreen surface whose front == the display buffer -- the wire-up step); the question here is
 * narrow and decisive: does Apple's PIO-blit present run to completion engine-independently?
 *
 * surface_flush convention (from methodDescs @0x18510): flags=0 => ScalarIScalarO, 2 scalar in,
 * 0 out.  surface_flush(surf, arg0, arg1): arg0 forwarded to flush_surface (do-swap/buffer sel),
 * arg1 IGNORED.
 *
 * PANIC-AWARE: every step prints + fflush + a one-line breadcrumb BEFORE the risky call, so a
 * panic still tells us exactly how far we got.  Talks only to Apple's already-loaded kext; does
 * NOT load our kext/bundle.  Build: cc -O2 -o mach64_flushtest mach64_flushtest.c -framework IOKit
 * Run from ssh (panic-prone -- expected at this stage):  sudo ./mach64_flushtest [idx]
 */
#include <IOKit/IOKitLib.h>
#include <mach/mach.h>
#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>

#define GL_TYPE               1u   /* IOATIRProGLContext */
#define SURF_TYPE             0u   /* IOATIRProSurface   */
#define SEL_SET_SURFACE       0u   /* GL sel 0: ScalarIStructureI, 4 scalarIn                 */
#define SEL_GET_SURFACE_SIZE  5u   /* GL sel 5: ScalarIScalarO, 0 in / 4 out (base,pitch,w,h) */
#define SEL_SET_ID_MODE       7u   /* surface sel 7: ScalarIScalarO, 2 in (idx,mode)          */
#define SEL_SET_SHAPE         9u   /* surface sel 9: ScalarIStructureI, 2 scalar + region     */
#define SEL_SURFACE_FLUSH    10u   /* surface sel 10: ScalarIScalarO, 2 in (doSwap, ignored)  */

#pragma pack(push, 2)
typedef struct { int16_t x, y, w, h; } IOAccelBounds_t;
typedef struct {
    uint32_t        num_rects;
    IOAccelBounds_t bounds;
    IOAccelBounds_t rect[1];
} IOAccelDeviceRegion_t;
#pragma pack(pop)

static io_connect_t open_client(unsigned type)
{
    io_service_t svc = IOServiceGetMatchingService(kIOMasterPortDefault,
                                                   IOServiceMatching("ATIRagePro"));
    if (!svc) { printf("no ATIRagePro service\n"); return 0; }
    io_connect_t c = 0;
    kern_return_t kr = IOServiceOpen(svc, mach_task_self(), type, &c);
    IOObjectRelease(svc);
    if (kr != KERN_SUCCESS) { printf("open type %u FAILED 0x%08x\n", type, kr); return 0; }
    return c;
}

#define STEP(msg) do { printf(">>> %s\n", msg); fflush(stdout); } while (0)

int main(int argc, char **argv)
{
    uint32_t idx  = (argc > 1) ? (uint32_t)strtoul(argv[1], 0, 0) : 0x13u;
    uint32_t mode = 0x24u;              /* 8888 | windowed */
    int16_t  W = 256, H = 256;

    io_connect_t sf = open_client(SURF_TYPE);
    io_connect_t gl = open_client(GL_TYPE);
    if (!sf || !gl) return 1;
    printf("=== opened surface(type0) + GL(type1) clients ===\n");

    /* --- validated handshake --- */
    STEP("set_id_mode");
    kern_return_t ir = IOConnectMethodScalarIScalarO(sf, SEL_SET_ID_MODE, 2, 0, idx, mode);
    printf("    set_id_mode(0x%x,0x%x) -> 0x%08x\n", idx, mode, ir); fflush(stdout);

    STEP("set_shape (allocs backing)");
    IOAccelDeviceRegion_t rgn;
    rgn.num_rects = 1;
    rgn.bounds.x = 0; rgn.bounds.y = 0; rgn.bounds.w = W; rgn.bounds.h = H;
    rgn.rect[0]  = rgn.bounds;
    kern_return_t hr = IOConnectMethodScalarIStructureI(sf, SEL_SET_SHAPE, 2,
                          (IOByteCount)sizeof(rgn), 0u, 0u, &rgn);
    printf("    set_shape(%dx%d) -> 0x%08x\n", W, H, hr); fflush(stdout);

    STEP("set_surface (bind)");
    uint32_t dummy = 0;
    kern_return_t sr = IOConnectMethodScalarIStructureI(gl, SEL_SET_SURFACE, 4, 0,
                          idx, 0u, 0u, 0u, &dummy);
    printf("    set_surface(0x%x,0) -> 0x%08x  %s\n", idx, sr,
           sr == KERN_SUCCESS ? "*** BOUND ***" : "FAILED"); fflush(stdout);
    if (sr != KERN_SUCCESS) { printf("=== not bound; abort before flush ===\n"); return 1; }

    /* --- learn WHERE the surface lives (cold-safe getter) --- */
    STEP("get_surface_size");
    uint32_t a=0,b=0,c=0,d=0;
    kern_return_t gz = IOConnectMethodScalarIScalarO(gl, SEL_GET_SURFACE_SIZE, 0, 4, &a,&b,&c,&d);
    printf("    get_surface_size -> 0x%08x  [%08x %08x %08x %08x]\n", gz, a,b,c,d); fflush(stdout);

    /* --- THE TEST: does Apple's PIO-blit present execute engine-independently? --- */
    STEP("surface_flush(doSwap=1)  <-- if this returns, present runs WITHOUT start_engine");
    kern_return_t f1 = IOConnectMethodScalarIScalarO(sf, SEL_SURFACE_FLUSH, 2, 0, 1u, 0u);
    printf("    surface_flush(1,0) -> 0x%08x  %s\n", f1,
           f1 == KERN_SUCCESS ? "*** PRESENT PATH RAN ***" : "(returned error, but no panic)");
    fflush(stdout);

    STEP("surface_flush(doSwap=0)  (second variant)");
    kern_return_t f0 = IOConnectMethodScalarIScalarO(sf, SEL_SURFACE_FLUSH, 2, 0, 0u, 0u);
    printf("    surface_flush(0,0) -> 0x%08x\n", f0); fflush(stdout);

    IOServiceClose(gl);
    IOServiceClose(sf);
    printf("=== done -- surface_flush ran to completion, engine-independent present CONFIRMED ===\n");
    return 0;
}

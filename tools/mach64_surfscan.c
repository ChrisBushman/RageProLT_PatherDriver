/* mach64_surfscan.c -- "wider find": enumerate what surfaces Apple's ATIRagePro accelerator
 * actually has registered, by scanning get_surface_info(id) (IOATIRProGLContext selector 6)
 * across an id range.  This answers the question our set_surface failure couldn't: WHAT is
 * find_surface_for_id running against?  Is the list empty, does it hold other surfaces but not
 * ours, or does it hold ours under a different id?
 *
 * get_surface_info is COLD-SAFE for any id (verified in disasm 0x6b3c): it mutex_locks, calls
 * find_surface_for_id(id), and on a miss (result==NULL) returns 0xe00002c2 with NO deref; on a
 * hit it only READS the surface's geometry fields (surf+0x154/+0x140/+0x12c).  No bind, no alloc,
 * no NULL-deref path -- so scanning a whole id range is safe, no panic risk.
 *
 * Talks DIRECTLY to Apple's already-loaded ATIRagePro.kext -- does NOT load our kext/bundle and
 * does NOT disturb anything.  Build: cc -O2 -o mach64_surfscan mach64_surfscan.c -framework IOKit
 * Run (safe from ssh):  ./mach64_surfscan [idx]      (idx defaults to 0x13)
 */
#include <IOKit/IOKitLib.h>
#include <mach/mach.h>
#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>

#define GL_TYPE               1u   /* IOATIRProGLContext */
#define SURF_TYPE             0u   /* IOATIRProSurface   */
#define SEL_SET_SURFACE       0u   /* GL sel 0: in=4(id,mode,m,m)  */
#define SEL_GET_SURFACE_INFO  6u   /* GL sel 6: in=1(id) out=3     */
#define SEL_SET_ID_MODE       7u   /* surface sel 7: in=2(idx,mode) */
#define SEL_SET_SHAPE         9u   /* surface sel 9: ScalarIStructI, 2 scalars + IOAccelDeviceRegion
                                    * (methodDescs: flags=4, count0=2, count1=0xffffffff) */

/* Apple's public IOAccelTypes.h layout (IOAccelBounds = SInt16 x,y,w,h). */
#pragma pack(push, 2)
typedef struct { int16_t x, y, w, h; } IOAccelBounds_t;
typedef struct {
    uint32_t        num_rects;
    IOAccelBounds_t bounds;
    IOAccelBounds_t rect[1];        /* num_rects entries; 1 here = whole surface */
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

/* scan get_surface_info over [lo,hi]; print every id that resolves.  Returns #hits. */
static int scan(io_connect_t gl, uint32_t lo, uint32_t hi, const char *tag)
{
    int hits = 0;
    uint32_t id;
    for (id = lo; ; id++) {
        uint32_t a = 0, b = 0, c = 0;
        kern_return_t kr = IOConnectMethodScalarIScalarO(gl, SEL_GET_SURFACE_INFO,
                                                         1, 3, id, &a, &b, &c);
        if (kr == KERN_SUCCESS) {
            printf("  [%s] id=0x%-6x REGISTERED  info=%08x %08x %08x\n", tag, id, a, b, c);
            fflush(stdout);
            if (++hits > 64) { printf("  [%s] >64 hits, stopping scan\n", tag); break; }
        }
        if (id == hi) break;
    }
    printf("  [%s] scanned 0x%x..0x%x -> %d hit(s)\n", tag, lo, hi, hits);
    fflush(stdout);
    return hits;
}

/* probe one specific id verbosely (shows the error code on a miss, not just silence) */
static void probe1(io_connect_t gl, uint32_t id, const char *tag)
{
    uint32_t a = 0, b = 0, c = 0;
    kern_return_t kr = IOConnectMethodScalarIScalarO(gl, SEL_GET_SURFACE_INFO,
                                                     1, 3, id, &a, &b, &c);
    printf("  [%s] get_surface_info(0x%x) -> 0x%08x  %s %08x %08x %08x\n",
           tag, id, kr, kr == KERN_SUCCESS ? "FOUND" : "(miss)", a, b, c);
    fflush(stdout);
}

int main(int argc, char **argv)
{
    uint32_t idx  = (argc > 1) ? (uint32_t)strtoul(argv[1], 0, 0) : 0x13u;
    uint32_t mode = 0x24u;   /* 8888 | windowed -> stores idx unchanged at surf+0x98 */

    io_connect_t gl = open_client(GL_TYPE);
    if (!gl) return 1;
    printf("=== opened GL client (type 1) ===\n");

    printf("--- BEFORE: what does the GL context's find already see? ---\n");
    scan(gl, 0x0u,    0x1fffu, "lo");     /* small ids / indices / low handles */
    scan(gl, 0x8000u, 0x9fffu, "cgs");    /* CGS-handle range (0x80xx we've seen) */
    probe1(gl, idx, "target-before");

    /* Now register OUR OWN surface and see if it becomes findable. */
    io_connect_t sf = open_client(SURF_TYPE);
    if (sf) {
        kern_return_t ir = IOConnectMethodScalarIScalarO(sf, SEL_SET_ID_MODE, 2, 0, idx, mode);
        printf("=== set_id_mode(idx=0x%x mode=0x%x) -> 0x%08x  %s ===\n", idx, mode, ir,
               ir == KERN_SUCCESS ? "REGISTERED" : "FAILED");

        /* THE missing step: give the surface a VRAM backing via set_shape, so set_surface's
         * bind (the 0x5ac backing-alloc vtable) has something to return.  Well-formed region:
         * one rect covering a modest surface (256x256) so the IOMallocPageable size stays small
         * and safe.  set_shape_backing frees the (NULL) old backing first -> no-op on a fresh
         * surface -> safe.  shapeBits=0, options=0 to start. */
        IOAccelDeviceRegion_t rgn;
        int16_t W = 256, H = 256;
        rgn.num_rects  = 1;
        rgn.bounds.x   = 0;  rgn.bounds.y = 0;  rgn.bounds.w = W;  rgn.bounds.h = H;
        rgn.rect[0].x  = 0;  rgn.rect[0].y = 0; rgn.rect[0].w = W; rgn.rect[0].h = H;
        kern_return_t hr = IOConnectMethodScalarIStructureI(sf, SEL_SET_SHAPE,
                              2 /*scalars*/, (IOByteCount)sizeof(rgn),
                              0u /*shapeBits*/, 0u /*options*/, &rgn);
        printf("=== set_shape(%dx%d, shapeBits=0) -> 0x%08x  %s ===\n", W, H, hr,
               hr == KERN_SUCCESS ? "SHAPED (backing allocated)" : "set_shape FAILED");
    } else {
        printf("=== could not open surface client; skipping registration ===\n");
    }

    printf("--- AFTER: does our just-registered surface now appear? ---\n");
    probe1(gl, idx, "target-after");
    scan(gl, 0x0u,    0x1fffu, "lo2");
    scan(gl, 0x8000u, 0x9fffu, "cgs2");

    /* Bind the shaped surface (validated: ScalarIStructureI convention, mode=0). */
    int bound = 0;
    {   uint32_t dummy = 0;
        kern_return_t sr = IOConnectMethodScalarIStructureI(gl, SEL_SET_SURFACE,
                                4 /*scalarIn*/, 0 /*structInSize*/, idx, 0u, 0u, 0u, &dummy);
        printf("=== set_surface(idx=0x%x, mode=0) [ScalarIStructI] -> 0x%08x  %s ===\n", idx, sr,
               sr == KERN_SUCCESS ? "*** BOUND ***" : "FAILED");
        fflush(stdout);
        bound = (sr == KERN_SUCCESS);
    }

    /* NEXT MILESTONE: map the command (type 1) + context (type 2) buffers.  This is the SAFE order
     * -- mapping these BEFORE set_surface NULL-derefs -> panic; AFTER the bind the buffers are
     * allocated (set_surface's map handler does alloc + init_command_buffer_header).  Standalone,
     * so no contention with our kext.  Dump the VendorCommandBufferHeader (first 8 words). */
    if (bound) {
        /* documented sequence is set_surface -> set_swap_rect -> map.  set_swap_rect (GL sel 1) is
         * ALSO flags=4/ScalarIStructureI (like set_surface), not ScalarIScalarO. */
        {   uint32_t dummy = 0;
            kern_return_t rr = IOConnectMethodScalarIStructureI(gl, 1 /*set_swap_rect*/,
                                    4, 0, 0u, 0u, 256u, 256u, &dummy);
            printf("=== set_swap_rect(0,0,256,256) [ScalarIStructI] -> 0x%08x ===\n", rr);
            fflush(stdout);
        }
        unsigned int mtypes[2];  const char *mnames[2];  int m;
        mtypes[0] = 1; mnames[0] = "command(type1, ctx+0xb8)";
        mtypes[1] = 2; mnames[1] = "context(type2, ctx+0xd0)";
        for (m = 0; m < 2; m++) {
            vm_address_t addr = 0;  IOByteCount size = 0;
            kern_return_t mr = IOConnectMapMemory(gl, mtypes[m], mach_task_self(),
                                                  &addr, &size, kIOMapAnywhere);
            printf("=== map %s -> kr=0x%08x  addr=0x%lx size=%lu ===\n",
                   mnames[m], mr, (unsigned long)addr, (unsigned long)size);
            if (mr == KERN_SUCCESS && addr && size >= 0x20) {
                uint32_t *h = (uint32_t *)addr;
                printf("    header[0..7]: %08x %08x %08x %08x %08x %08x %08x %08x\n",
                       h[0], h[1], h[2], h[3], h[4], h[5], h[6], h[7]);
            }
            fflush(stdout);
        }
    } else {
        printf("=== not bound; skipping buffer map ===\n");
    }

    if (sf) IOServiceClose(sf);
    IOServiceClose(gl);
    printf("=== done ===\n");
    return 0;
}

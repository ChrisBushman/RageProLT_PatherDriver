/* mach64_writetest.c -- STEP 1 of the Apple-present wire-up: put KNOWN pixels into the surface and
 * confirm Apple's surface_flush presents THEM (turn the (0,0) garbage square a solid colour).
 *
 * Path (reversed from ragepro.asm): the surface backing (surf+0x294, IOMallocPageable) is reached
 * for CPU write via surface_write_lock (surface sel 14).  methodDescs @0x18570: flags=2 =>
 * ScalarIStructureO, 0 scalar-in, variable struct-out.  surface_write_lock -> surface_lock_options
 * -> fills an IOAccelSurfaceData: out[0] = surface address ([surfbuf+0x8]), + a pitch ([+0x18]).
 * surface_write_unlock = sel 15 (ScalarIScalarO, 0 in / 0 out).
 *
 * The memory model is NOT assumed: this probe DUMPS the IOAccelSurfaceData first (address, pitch,
 * etc.) to learn where the pixels live and whether we get a usable userspace address.  Only if
 * out[0] looks like a plausible userspace pointer does it write a solid colour + flush.  So even if
 * the write model is different than expected, we LEARN the layout without a blind bad write.
 *
 * Build: cc -O2 -o mach64_writetest mach64_writetest.c -framework IOKit
 * Run (panic-prone, from ssh):  sudo ./mach64_writetest [idx]
 */
#include <IOKit/IOKitLib.h>
#include <mach/mach.h>
#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <string.h>

#define GL_TYPE            1u
#define SURF_TYPE          0u
#define SEL_SET_SURFACE    0u
#define SEL_GET_SURF_SIZE  5u
#define SEL_SET_ID_MODE    7u
#define SEL_SET_SHAPE      9u
#define SEL_SURFACE_FLUSH 10u
#define SEL_WRITE_LOCK    14u   /* ScalarIStructureO, 0 in, var struct out (IOAccelSurfaceData) */
#define SEL_WRITE_UNLOCK  15u   /* ScalarIScalarO, 0 in / 0 out                                 */

#pragma pack(push, 2)
typedef struct { int16_t x, y, w, h; } IOAccelBounds_t;
typedef struct { uint32_t num_rects; IOAccelBounds_t bounds; IOAccelBounds_t rect[1]; } IOAccelDeviceRegion_t;
#pragma pack(pop)

static io_connect_t open_client(unsigned type)
{
    io_service_t svc = IOServiceGetMatchingService(kIOMasterPortDefault, IOServiceMatching("ATIRagePro"));
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
    int16_t  W = 256, H = 256;

    io_connect_t sf = open_client(SURF_TYPE);
    io_connect_t gl = open_client(GL_TYPE);
    if (!sf || !gl) return 1;
    printf("=== opened surface(type0) + GL(type1) ===\n");

    /* --- validated handshake --- */
    STEP("set_id_mode");
    printf("    -> 0x%08x\n", IOConnectMethodScalarIScalarO(sf, SEL_SET_ID_MODE, 2, 0, idx, 0x24u));
    STEP("set_shape 256x256");
    IOAccelDeviceRegion_t rgn; rgn.num_rects = 1;
    rgn.bounds.x=0; rgn.bounds.y=0; rgn.bounds.w=W; rgn.bounds.h=H; rgn.rect[0]=rgn.bounds;
    printf("    -> 0x%08x\n", IOConnectMethodScalarIStructureI(sf, SEL_SET_SHAPE, 2,
                                (IOByteCount)sizeof(rgn), 0u, 0u, &rgn));
    STEP("set_surface (bind)");
    uint32_t dummy=0;
    kern_return_t sr = IOConnectMethodScalarIStructureI(gl, SEL_SET_SURFACE, 4, 0, idx,0u,0u,0u,&dummy);
    printf("    -> 0x%08x %s\n", sr, sr==KERN_SUCCESS?"BOUND":"FAILED");
    if (sr != KERN_SUCCESS) return 1;

    /* --- surface_write_lock: DUMP the IOAccelSurfaceData to learn the memory model --- */
    STEP("surface_write_lock (sel 14, ScalarIStructureO)  <-- learn address/pitch");
    uint32_t sd[32];  memset(sd, 0, sizeof(sd));
    IOByteCount sdsz = sizeof(sd);
    kern_return_t wl = IOConnectMethodScalarIStructureO(sf, SEL_WRITE_LOCK, 0, &sdsz, sd);
    printf("    write_lock -> 0x%08x  outsize=%lu\n", wl, (unsigned long)sdsz);
    if (wl == KERN_SUCCESS) {
        int i, x, y, nw;
        uintptr_t addr;
        uint32_t  pitch;
        volatile uint32_t *p;
        nw = (int)(sdsz/4); if (nw > 24) nw = 24;
        printf("    IOAccelSurfaceData[0..%d]:\n", nw-1);
        for (i = 0; i < nw; i += 4)
            printf("      +%02x: %08x %08x %08x %08x\n", i*4,
                   sd[i], i+1<nw?sd[i+1]:0, i+2<nw?sd[i+2]:0, i+3<nw?sd[i+3]:0);
        fflush(stdout);

        /* out[0] = surface address; if it looks like a usable userspace pointer, paint it RED. */
        addr  = (uintptr_t)sd[0];
        pitch = sd[1] ? sd[1] : (uint32_t)(W*4);
        printf("    -> address=0x%lx pitch=%u\n", (unsigned long)addr, pitch); fflush(stdout);
        if (addr >= 0x1000 && addr < 0xf0000000u) {
            STEP("write SOLID RED into the mapped surface (ARGB 0xFFFF0000)");
            p = (volatile uint32_t *)addr;
            for (y = 0; y < H; y++)
                for (x = 0; x < W; x++)
                    p[y*(pitch/4) + x] = 0xFFFF0000u;
            printf("    wrote %dx%d red\n", W, H); fflush(stdout);
        } else {
            printf("    address not a plausible userspace pointer -- NOT writing (learned the model)\n");
        }
        STEP("surface_write_unlock (sel 15)");
        printf("    -> 0x%08x\n", IOConnectMethodScalarIScalarO(sf, SEL_WRITE_UNLOCK, 0, 0));
    }

    /* --- present: the (0,0) square should now be RED if the write reached the presented buffer --- */
    STEP("surface_flush(1,0)  <-- expect a RED square at (0,0) if our pixels reached it");
    printf("    -> 0x%08x\n", IOConnectMethodScalarIScalarO(sf, SEL_SURFACE_FLUSH, 2, 0, 1u, 0u));

    IOServiceClose(gl); IOServiceClose(sf);
    printf("=== done -- look at screen (0,0): RED = our pixels via Apple's present; garbage = write missed ===\n");
    return 0;
}

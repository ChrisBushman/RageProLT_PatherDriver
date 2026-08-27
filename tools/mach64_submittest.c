/* mach64_submittest.c - MINIMAL standalone submit test for Path B.
 *
 * Drives ONLY Apple's ATIRagePro kext.  Runs the validated sequence:
 *   open surface -> set_id_mode(idx,0x24) -> set_shape(256x256)
 *   open GL -> set_surface(idx,0) [ScalarIStructureI] -> set_swap_rect [ScalarIStructureI]
 *   map command ring (type 1) + map register region (type 0, for readback)
 * then writes ONE harmless command into the ring -- SCRATCH_REG0 (our 0x480 = Apple engine-relative
 * 0x080, a pure scratchpad) -- sets the ring COUNT (ring+0x14 = 1), and probes SAFE triggers only
 * (get_status query + a sleep).  Does NOT call finish/wait_for_stamp (those can block on a stamp).
 * Reads SCRATCH_REG0 back before/after: if it changes to our pattern, a drain fired.
 *
 * Build knobs: -DSKIP_REGMAP (skip the type-0 map/readback), -DSKIP_TRIGGER (skip the drain probe).
 * Build: cc -O2 -o mach64_submittest mach64_submittest.c -framework IOKit
 */
#include <IOKit/IOKitLib.h>
#include <mach/mach.h>
#include <unistd.h>
#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>

#define GL_TYPE   1u
#define SURF_TYPE 0u
#define SEL_SET_SURFACE   0u   /* ScalarIStructureI: 4 scalars + 0 struct */
#define SEL_SET_SWAP_RECT 1u   /* ScalarIStructureI: 4 scalars + 0 struct */
#define SEL_GET_STATUS    4u   /* ScalarIScalarO: 0 in, 1 out (safe query) */
#define SEL_SET_ID_MODE   7u   /* surface: ScalarIScalarO 2 in */
#define SEL_SET_SHAPE     9u   /* surface: ScalarIStructureI 2 scalars + region */

#define SCRATCH_APERTURE  0x480u   /* SCRATCH_REG0 full-aperture byte offset (DRM: 0x0480) */
#define SCRATCH_ENGINE    0x080u   /* engine-relative (aperture - 0x400) = what submit_buffer wants */
#define PATTERN           0xC0DE0001u

#pragma pack(push, 2)
typedef struct { int16_t x, y, w, h; } bounds_t;
typedef struct { uint32_t num_rects; bounds_t bounds; bounds_t rect[1]; } region_t;
#pragma pack(pop)

static uint32_t bswap32(uint32_t x)
{
    return ((x & 0x000000ffu) << 24) | ((x & 0x0000ff00u) << 8) |
           ((x & 0x00ff0000u) >> 8)  | ((x & 0xff000000u) >> 24);
}

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

static void show_scratch(vm_address_t reg, IOByteCount rsz, const char *when)
{
    if (!reg) { printf("  [%s] (no register map)\n", when); return; }
    uint32_t v_ap = 0, v_en = 0;
    if (rsz > SCRATCH_APERTURE + 4) v_ap = *(volatile uint32_t *)(reg + SCRATCH_APERTURE);
    if (rsz > SCRATCH_ENGINE   + 4) v_en = *(volatile uint32_t *)(reg + SCRATCH_ENGINE);
    printf("  [%s] SCRATCH@0x480 = %08x (bswap %08x) ; @0x080 = %08x (bswap %08x)\n",
           when, v_ap, bswap32(v_ap), v_en, bswap32(v_en));
    fflush(stdout);
}

int main(int argc, char **argv)
{
    uint32_t idx = (argc > 1) ? (uint32_t)strtoul(argv[1], 0, 0) : 0x13u;

    io_connect_t sf = open_client(SURF_TYPE);
    if (!sf) return 1;
    kern_return_t ir = IOConnectMethodScalarIScalarO(sf, SEL_SET_ID_MODE, 2, 0, idx, 0x24u);
    printf("set_id_mode(0x%x,0x24) -> 0x%08x\n", idx, ir);
    {   region_t rgn;
        rgn.num_rects = 1;
        rgn.bounds.x=0; rgn.bounds.y=0; rgn.bounds.w=256; rgn.bounds.h=256;
        rgn.rect[0]=rgn.bounds;
        kern_return_t hr = IOConnectMethodScalarIStructureI(sf, SEL_SET_SHAPE, 2,
                               (IOByteCount)sizeof(rgn), 0u, 0u, &rgn);
        printf("set_shape(256x256) -> 0x%08x\n", hr);
    }

    io_connect_t gl = open_client(GL_TYPE);
    if (!gl) { IOServiceClose(sf); return 1; }
    {   uint32_t dummy = 0;
        kern_return_t sr = IOConnectMethodScalarIStructureI(gl, SEL_SET_SURFACE, 4, 0,
                               idx, 0u, 0u, 0u, &dummy);
        printf("set_surface(0x%x) -> 0x%08x  %s\n", idx, sr, sr?"":"BOUND");
        if (sr != KERN_SUCCESS) { printf("not bound; abort\n"); goto done; }
        IOConnectMethodScalarIStructureI(gl, SEL_SET_SWAP_RECT, 4, 0, 0u,0u,256u,256u, &dummy);
    }

    vm_address_t cmd = 0, reg = 0;  IOByteCount csz = 0, rsz = 0;
    kern_return_t mc = IOConnectMapMemory(gl, 1, mach_task_self(), &cmd, &csz, kIOMapAnywhere);
    printf("map cmd(type1) -> 0x%08x addr=0x%lx size=%lu\n", mc, (unsigned long)cmd, (unsigned long)csz);
    if (mc != KERN_SUCCESS || !cmd) { printf("no command ring; abort\n"); goto done; }

#ifndef SKIP_REGMAP
    kern_return_t mr = IOConnectMapMemory(gl, 0, mach_task_self(), &reg, &rsz,
                                          kIOMapAnywhere | kIOMapInhibitCache);
    printf("map reg(type0) -> 0x%08x addr=0x%lx size=%lu\n", mr, (unsigned long)reg, (unsigned long)rsz);
    show_scratch(reg, rsz, "baseline");
#endif

    printf("--- ring header before: ");
    { uint32_t *h = (uint32_t*)cmd; int i; for (i=0;i<8;i++) printf("%08x ", h[i]); printf("---\n"); }

    {   uint32_t *body = (uint32_t *)(cmd + 0x20);
        body[0] = SCRATCH_ENGINE; body[1] = PATTERN;
        *(uint32_t *)(cmd + 0x14) = 1;   /* COUNT = 1 pair */
        printf("wrote 1 cmd {reg=0x%x, val=0x%08x}; set count(ring+0x14)=1\n", SCRATCH_ENGINE, PATTERN);
        fflush(stdout);
    }

#ifndef SKIP_TRIGGER
    {   uint32_t st = 0xdeadbeef;
        kern_return_t gr = IOConnectMethodScalarIScalarO(gl, SEL_GET_STATUS, 0, 1, &st);
        printf("probe get_status -> 0x%08x status=0x%08x\n", gr, st);
        show_scratch(reg, rsz, "after get_status");
    }
    usleep(200000);
    show_scratch(reg, rsz, "after 200ms");
#endif
    printf("--- ring header after:  ");
    { uint32_t *h = (uint32_t*)cmd; int i; for (i=0;i<8;i++) printf("%08x ", h[i]); printf("---\n"); }
    printf("(if SCRATCH shows %08x or its bswap, the drain FIRED)\n", PATTERN);

done:
    IOServiceClose(gl);
    IOServiceClose(sf);
    printf("=== done ===\n");
    return 0;
}

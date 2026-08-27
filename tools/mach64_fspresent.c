/* mach64_fspresent.c -- STEP 2a: confirm Apple's present covers the FULL SCREEN.
 *
 * Step 1 (mach64_writetest) proved a 256x256 surface at (0,0) presents our pixels via surface_flush.
 * This shapes a FULL-SCREEN surface (whole scanout) and paints a recognizable pattern (4 vertical
 * colour bands R|G|B|White + a 2px white border), so we can verify: (a) the blit covers the entire
 * display, (b) geometry/pitch are correct (bands are vertical & straight, border frames the screen).
 *
 * Same validated path: set_id_mode -> set_shape(full screen) -> set_surface -> write_lock ->
 * paint -> write_unlock -> surface_flush.  Standalone (Apple kext only; no our kext/bundle).
 * Panic-prone (bigger backing + full-screen blit).  Run from ssh:
 *   sudo ./mach64_fspresent [W H]     (defaults to 1024x768, the Lombard panel)
 * NOTE: this paints the WHOLE screen with the test pattern; it clears on the next desktop repaint.
 */
#include <IOKit/IOKitLib.h>
#include <mach/mach.h>
#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <string.h>

#define GL_TYPE 1u
#define SURF_TYPE 0u
#define SEL_SET_SURFACE 0u
#define SEL_SET_ID_MODE 7u
#define SEL_SET_SHAPE 9u
#define SEL_SURFACE_FLUSH 10u
#define SEL_WRITE_LOCK 14u
#define SEL_WRITE_UNLOCK 15u

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
#define STEP(m) do { printf(">>> %s\n", m); fflush(stdout); } while (0)

int main(int argc, char **argv)
{
    uint32_t idx = 0x14u;
    int16_t W = (argc > 2) ? (int16_t)atoi(argv[1]) : 1024;
    int16_t H = (argc > 2) ? (int16_t)atoi(argv[2]) : 768;
    io_connect_t sf, gl;
    IOAccelDeviceRegion_t rgn;
    uint32_t dummy = 0, sd[32];
    IOByteCount sdsz = sizeof(sd);
    kern_return_t sr, wl;

    sf = open_client(SURF_TYPE); gl = open_client(GL_TYPE);
    if (!sf || !gl) return 1;
    printf("=== opened surface+GL; target %dx%d ===\n", W, H);

    STEP("set_id_mode");
    printf("    -> 0x%08x\n", IOConnectMethodScalarIScalarO(sf, SEL_SET_ID_MODE, 2, 0, idx, 0x24u));
    STEP("set_shape FULL SCREEN");
    rgn.num_rects = 1; rgn.bounds.x = 0; rgn.bounds.y = 0; rgn.bounds.w = W; rgn.bounds.h = H;
    rgn.rect[0] = rgn.bounds;
    printf("    -> 0x%08x\n", IOConnectMethodScalarIStructureI(sf, SEL_SET_SHAPE, 2,
                                (IOByteCount)sizeof(rgn), 0u, 0u, &rgn));
    STEP("set_surface (bind)");
    sr = IOConnectMethodScalarIStructureI(gl, SEL_SET_SURFACE, 4, 0, idx, 0u, 0u, 0u, &dummy);
    printf("    -> 0x%08x %s\n", sr, sr == KERN_SUCCESS ? "BOUND" : "FAILED");
    if (sr != KERN_SUCCESS) return 1;

    STEP("write_lock");
    memset(sd, 0, sizeof(sd));
    wl = IOConnectMethodScalarIStructureO(sf, SEL_WRITE_LOCK, 0, &sdsz, sd);
    printf("    -> 0x%08x  address=0x%08x pitch=%u w=%u h=%u bpp=%u\n",
           wl, sd[0], sd[4], sd[5], sd[6], sd[7]);
    if (wl == KERN_SUCCESS) {
        uintptr_t addr = (uintptr_t)sd[0];
        uint32_t  pitch = sd[4] ? sd[4] : (uint32_t)(W * 4);
        if (addr >= 0x1000 && addr < 0xf0000000u) {
            volatile uint32_t *p = (volatile uint32_t *)addr;
            uint32_t stride = pitch / 4, band, col;
            int x, y;
            /* 4 vertical bands R|G|B|White (ARGB), + 2px white border. */
            STEP("paint full-screen pattern (R|G|B|White bands + border)");
            for (y = 0; y < H; y++) {
                for (x = 0; x < W; x++) {
                    band = (uint32_t)((x * 4) / W);
                    col = band == 0 ? 0xFFFF0000u : band == 1 ? 0xFF00FF00u
                        : band == 2 ? 0xFF0000FFu : 0xFFFFFFFFu;
                    if (x < 2 || x >= W - 2 || y < 2 || y >= H - 2) col = 0xFFFFFFFFu;
                    p[y * stride + x] = col;
                }
            }
            printf("    painted %dx%d\n", W, H); fflush(stdout);
        } else {
            printf("    address not usable -- skipping paint\n");
        }
        STEP("write_unlock");
        printf("    -> 0x%08x\n", IOConnectMethodScalarIScalarO(sf, SEL_WRITE_UNLOCK, 0, 0));
    }

    STEP("surface_flush(1,0)  <-- expect the pattern across the WHOLE screen");
    printf("    -> 0x%08x\n", IOConnectMethodScalarIScalarO(sf, SEL_SURFACE_FLUSH, 2, 0, 1u, 0u));

    IOServiceClose(gl); IOServiceClose(sf);
    printf("=== done -- full screen should show R|G|B|White bands framed by a white border ===\n");
    return 0;
}

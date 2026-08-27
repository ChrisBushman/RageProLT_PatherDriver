/* mach64_surftype.c - identify which ATIRagePro user-client type (0/1/2) is the SURFACE client.
 * type 1 = GL (already confirmed).  Calls get_state (surface selector index 2, 0-in/1-out) on
 * each -- cold-safe (queries accelerator state, no buffer/VRAM touch).  The surface client
 * succeeds; the GL/2D clients have a different method/arg-shape at index 2 so IOKit rejects
 * them (0x2c2) without running anything.  Safe: no memory mapping. */
#include <IOKit/IOKitLib.h>
#include <mach/mach.h>
#include <stdio.h>
#include <stdint.h>

int main(void)
{
    io_service_t svc = IOServiceGetMatchingService(kIOMasterPortDefault,
                                                   IOServiceMatching("ATIRagePro"));
    if (!svc) { printf("no ATIRagePro service\n"); return 1; }
    int t;
    for (t = 0; t <= 2; t++) {
        io_connect_t c = 0;
        kern_return_t kr = IOServiceOpen(svc, mach_task_self(), (unsigned)t, &c);
        if (kr != KERN_SUCCESS) { printf("type %d: open fail 0x%08x\n", t, kr); continue; }
        uint32_t st = 0xdeadbeef;
        kern_return_t gr = IOConnectMethodScalarIScalarO(c, 2 /* get_state (surface) */, 0, 1, &st);
        printf("type %d: get_state(idx2,0in/1out) -> kr=0x%08x state=0x%08x  %s\n", t, gr, st,
               (gr == KERN_SUCCESS) ? "<-- SURFACE client" : "");
        IOServiceClose(c);
    }
    IOObjectRelease(svc);
    return 0;
}

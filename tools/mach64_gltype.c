/* mach64_gltype.c - identify which ATIRagePro user-client type (0/1/2) is the GL context.
 * Opens each type and calls get_config (GL selector index 3, 0-in/3-out) -- cold-safe (returns
 * accelerator caps, skips the surface part when no surface is bound; verified in disasm).  The
 * GL context returns config; IOKit rejects the call on a client whose index-3 arg counts differ.
 * Safe: no memory mapping (that panics), no surface bind. */
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
        uint32_t a = 0, b = 0, d = 0;
        kern_return_t gr = IOConnectMethodScalarIScalarO(c, 3, 0, 3, &a, &b, &d);
        printf("type %d: get_config(idx3,0in/3out) -> kr=0x%08x  vals=%08x %08x %08x  %s\n",
               t, gr, a, b, d, (gr == KERN_SUCCESS) ? "<-- looks like GL" : "");
        IOServiceClose(c);
    }
    IOObjectRelease(svc);
    return 0;
}

/* mach64_glcheck.c - is Apple's ATIRagePro GL user client instantiable?
 * IOATIRProAccelerator::newUserClient accepts types 0,1,2 (GL context / 2D context / surface).
 * Try to IOServiceOpen each and report which succeed -- if the GL context type opens, Apple's
 * never-shipped GL kernel path is LIVE and we can drop our bundle onto it. */
#include <IOKit/IOKitLib.h>
#include <mach/mach.h>
#include <stdio.h>

int main(void)
{
    io_service_t svc = IOServiceGetMatchingService(kIOMasterPortDefault,
                                                   IOServiceMatching("IOATIRProAccelerator"));
    if (!svc) {
        printf("IOATIRProAccelerator not found; trying ATIRagePro...\n");
        svc = IOServiceGetMatchingService(kIOMasterPortDefault, IOServiceMatching("ATIRagePro"));
    }
    if (!svc) { printf("no ATIRagePro accelerator service in the registry\n"); return 1; }
    { io_name_t nm; if (IORegistryEntryGetName(svc, nm) == KERN_SUCCESS) printf("service: %s\n", nm); }

    int t;
    for (t = 0; t <= 3; t++) {
        io_connect_t conn = 0;
        kern_return_t kr = IOServiceOpen(svc, mach_task_self(), (unsigned)t, &conn);
        printf("IOServiceOpen(type=%d) -> kr=0x%08x  %s%s\n", t, kr,
               (kr == KERN_SUCCESS) ? "OPEN OK" : "fail",
               (kr == KERN_SUCCESS) ? "" : "");
        if (kr == KERN_SUCCESS) { IOServiceClose(conn); }
    }
    IOObjectRelease(svc);
    return 0;
}

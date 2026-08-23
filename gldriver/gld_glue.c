/*
 * gld_glue.c - bundle glue: the service matcher gld_context.c needs, plus the
 * library lifecycle entry points OpenGL.framework calls at load/unload.
 */
#include "gld.h"
#include <IOKit/IOKitLib.h>

/* Resolve our accelerator so gldGetRendererInfo/gldCreateContext can open it.
 * Matches the Mach64Accelerator personality our kext publishes. */
io_service_t mach64_match_service(uint32_t index)
{
    (void)index;
    return IOServiceGetMatchingService(kIOMasterPortDefault,
                                       IOServiceMatching("Mach64Accelerator"));
}

/* Called once when the framework loads the bundle; return 0 for success. */
int gldInitializeLibrary(void *framework_glue)
{
    (void)framework_glue;
    gld_log("\n=== gldInitializeLibrary: OpenGL.framework loaded our bundle ===\n");
    return 0;
}

void gldTerminateLibrary(void) { }

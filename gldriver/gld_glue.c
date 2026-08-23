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

/* The framework's interface pointers, saved for later gld* use. */
void *g_gld_iface = 0;

/* gldInitializeLibrary: the framework passes its interface pointers (r3..r7) and
 * expects the first one RETURNED as the success signal (the reference saves them
 * and returns arg1).  Our earlier `return 0` made the framework treat init as
 * failed and never call gldGetRendererInfo.  Passthrough + log the args. */
void *gldInitializeLibrary(void *iface, void *a2, void *a3, void *a4, void *a5)
{
    gld_log("\n=== gldInitializeLibrary iface=%p a2=%p a3=%p a4=%p a5=%p ===\n",
            iface, a2, a3, a4, a5);
    /* Report the GLEngine caller so we can disassemble the loader/post-Init path
     * exactly (static PIC xref-ing GLEngine was a dead end). */
    gld_log("  caller(ret0)=%p\n", __builtin_return_address(0));
    g_gld_iface = iface;
    return iface;   /* non-null = success, matching the reference */
}

void gldTerminateLibrary(void) { }

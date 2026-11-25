/* gld_glue.c - library lifecycle entry points OpenGL.framework calls at load/unload. */
#include "gld.h"

/* The framework's interface pointers, saved for later gld* use. */
void *g_gld_iface = 0;

/* gldInitializeLibrary: the framework passes its interface pointers (r3..r7) and
 * expects the first one RETURNED as the success signal (the reference saves them
 * and returns arg1).  Our earlier `return 0` made the framework treat init as
 * failed and never call gldGetRendererInfo.  Passthrough. */
void *gldInitializeLibrary(void *iface, void *a2, void *a3, void *a4, void *a5)
{
    g_gld_iface = iface;
    return iface;   /* non-null = success, matching the reference */
}

void gldTerminateLibrary(void) { }

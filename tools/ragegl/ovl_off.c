/* tools/ragegl/ovl_off.c - hide and disable the overlay (debug kext).  Only the owning client may do it; a dead owner's overlay is disabled by the kernel when its client closes. */
#include "ragegl.h"
int main(void) { ragegl_t c; kern_return_t kr; if (ragegl_open(&c)) return 99; kr = IOConnectMethodScalarIScalarO(c, RAGE_GL_OVL_OFF, 0, 0); printf("OVL_OFF 0x%x\n", (unsigned)kr); ragegl_close(c); return 0; }

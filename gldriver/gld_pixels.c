/* gld_pixels.c - pixel-op hooks (glReadPixels readback and the three control slots).  Readback goes through the kernel (rgb_readback);
 * the bundle never holds a CPU view of VRAM (design 4).  First cut: control slots only; readback follows. */
#include "gld.h"

int mach64_ctl0c(gld_context *c, uint32_t a, uint32_t b, uint32_t d, uint32_t e, uint32_t f, uint32_t g) { (void)c; (void)a; (void)b; (void)d; (void)e; (void)f; (void)g; return 0; }
int mach64_ctl10(gld_context *c, uint32_t a, uint32_t b, uint32_t d, uint32_t e, uint32_t f, uint32_t g) { (void)c; (void)a; (void)b; (void)d; (void)e; (void)f; (void)g; return 0; }
int mach64_ctl14(gld_context *c, uint32_t a, uint32_t b, uint32_t d, uint32_t e, uint32_t f, uint32_t g) { (void)c; (void)a; (void)b; (void)d; (void)e; (void)f; (void)g; return 0; }
int mach64_ctl08(void *rb_ctx, int x, int y, int w, int h, unsigned format, unsigned type, void *pixels)
{ (void)rb_ctx; (void)x; (void)y; (void)w; (void)h; (void)format; (void)type; (void)pixels; return 0; }

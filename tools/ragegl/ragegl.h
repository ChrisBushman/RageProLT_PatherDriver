/* tools/ragegl/ragegl.h - thin user-space wrapper over the GL client (common/rage_abi.h).  Header-only; no floating point. */
#ifndef RAGEGL_H
#define RAGEGL_H
#include <stdio.h>
#include <stdint.h>
#include <string.h>
#include <mach/mach.h>
#include <IOKit/IOKitLib.h>
#include "rage_abi.h"

typedef io_connect_t ragegl_t;

static inline int ragegl_open(ragegl_t *c)
{
    mach_port_t master; io_service_t svc; kern_return_t kr;
    IOMasterPort(MACH_PORT_NULL, &master);
    svc = IOServiceGetMatchingService(master, IOServiceMatching("RageLTDevice"));
    if (!svc) return -1;
    kr = IOServiceOpen(svc, mach_task_self(), RAGE_CLIENT_GL, c);
    IOObjectRelease(svc);
    return kr == KERN_SUCCESS ? 0 : (int)kr;
}
static inline void ragegl_close(ragegl_t c) { IOServiceClose(c); }

#define RGL_CALL(sel, nin, nout, ...) IOConnectMethodScalarIScalarO(c, sel, nin, nout, __VA_ARGS__)
static inline kern_return_t ragegl_info(ragegl_t c, uint32_t *gen, uint32_t *feat, uint32_t *end, uint32_t *flags)
{ return RGL_CALL(RAGE_GL_INFO, 0, 4, gen, feat, end, flags); }
static inline kern_return_t ragegl_alloc(ragegl_t c, uint32_t len, uint32_t align, uint32_t *off)
{ return RGL_CALL(RAGE_GL_ALLOC, 2, 1, len, align, off); }
static inline kern_return_t ragegl_free(ragegl_t c, uint32_t off) { return RGL_CALL(RAGE_GL_FREE, 1, 0, off); }
static inline kern_return_t ragegl_target(ragegl_t c, uint32_t off, uint32_t pitch, uint32_t bpp, uint32_t w, uint32_t h)
{ return RGL_CALL(RAGE_GL_SET_TARGET, 5, 0, off, pitch, bpp, w, h); }
static inline kern_return_t ragegl_scissor(ragegl_t c, int32_t l, int32_t r, int32_t t, int32_t b)
{ return RGL_CALL(RAGE_GL_SET_SCISSOR, 4, 0, (uint32_t)l, (uint32_t)r, (uint32_t)t, (uint32_t)b); }
static inline kern_return_t ragegl_setz(ragegl_t c, uint32_t off, uint32_t pitch) { return RGL_CALL(RAGE_GL_SET_Z, 2, 0, off, pitch); }
static inline kern_return_t ragegl_fill(ragegl_t c, int32_t x, int32_t y, int32_t w, int32_t h, uint32_t col)
{ return RGL_CALL(RAGE_GL_FILL, 5, 0, (uint32_t)x, (uint32_t)y, (uint32_t)w, (uint32_t)h, col); }
static inline kern_return_t ragegl_submit(ragegl_t c, const uint32_t *pairs, uint32_t n)
{ return RGL_CALL(RAGE_GL_SUBMIT, 2, 0, (uint32_t)(uintptr_t)pairs, n); }
static inline kern_return_t ragegl_fence(ragegl_t c, uint32_t seq, uint32_t *done) { return RGL_CALL(RAGE_GL_FENCE, 1, 1, seq, done); }
static inline kern_return_t ragegl_present(ragegl_t c, uint32_t sx, uint32_t sy, uint32_t w, uint32_t h, uint32_t dx, uint32_t dy)
{ return RGL_CALL(RAGE_GL_PRESENT, 6, 0, sx, sy, w, h, dx, dy); }
static inline kern_return_t ragegl_vwrite(ragegl_t c, uint32_t off, const void *p, uint32_t len, uint32_t fl)
{ return RGL_CALL(RAGE_GL_VRAM_WRITE, 4, 0, off, (uint32_t)(uintptr_t)p, len, fl); }
static inline kern_return_t ragegl_vread(ragegl_t c, uint32_t off, void *p, uint32_t len, uint32_t fl)
{ return RGL_CALL(RAGE_GL_VRAM_READ, 4, 0, off, (uint32_t)(uintptr_t)p, len, fl); }
static inline kern_return_t ragegl_display(ragegl_t c, uint32_t *gen, uint32_t *w, uint32_t *h, uint32_t *pitch, uint32_t *bpp, uint32_t *base)
{ return RGL_CALL(RAGE_GL_DISPLAY, 0, 6, gen, w, h, pitch, bpp, base); }
static inline kern_return_t ragegl_scanout_read(ragegl_t c, uint32_t boff, void *p, uint32_t len)
{ return RGL_CALL(RAGE_GL_SCANOUT_READ, 3, 0, boff, (uint32_t)(uintptr_t)p, len); }
#endif
static inline kern_return_t ragegl_config(ragegl_t c, uint32_t key, uint32_t v) { return RGL_CALL(RAGE_GL_CONFIG, 2, 0, key, v); }
static inline kern_return_t ragegl_texture(ragegl_t c, uint32_t index, uint32_t off, uint32_t fmt) { return RGL_CALL(RAGE_GL_SET_TEXTURE, 3, 0, index, off, fmt); }

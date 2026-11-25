/* gldriver/rage_backend.h - the new GL bundle's only door to the hardware: the kernel's GL client (common/rage_abi.h), nothing else.
 * No Apple kext, no kernel-pointer walking, no register or VRAM mapping in this process (design section 4).  Single-threaded, serial model:
 * rgb_flush() returns without waiting when nothing is pending; the kernel blocks the NEXT submit only if the previous batch is still in flight.
 * Integer / single-float only (design 2.1). */
#ifndef RAGE_BACKEND_H
#define RAGE_BACKEND_H

#include <stdint.h>
#include <stddef.h>
#include <mach/mach.h>
#include <IOKit/IOKitLib.h>
#include "mach64_emit.h"

typedef struct {
    io_connect_t conn;
    uint32_t gen;                 /* display generation the surface was built for; a change means "rebuild" */
    uint32_t w, h, pitch, bpp;    /* current render target */
    uint32_t col_off, z_off;      /* kernel-allocated blocks (0 = none); z_off != 0 means a Z buffer is bound */
    uint32_t features, flags;
    uint32_t dropped;             /* register writes of a stream the kernel owns and the bundle therefore did not forward (statistics) */
    uint32_t fence;               /* last submitted sequence */
} rgb_t;

int  rgb_open(rgb_t *b);                                           /* 0 or an IOKit error */
void rgb_close(rgb_t *b);
int  rgb_surface(rgb_t *b, uint32_t w, uint32_t h, uint32_t bpp, int with_z);   /* allocate colour (+Z), make it the target; frees the previous one */
int  rgb_scissor(rgb_t *b, int l, int r, int t, int bt);
/* Forward a host-order packet stream (mach64_emit.h) as a validated batch.  Kernel-owned registers (target/Z/scissor/texture offsets/pixel
 * width) are routed to the matching kernel calls instead of being written; anything else not on the allow-list is counted in b->dropped. */
int  rgb_flush(rgb_t *b, const m64_cmdbuf *cb);
int  rgb_fill(rgb_t *b, int x, int y, int w, int h, uint32_t rgb565);
int  rgb_fence(rgb_t *b);                                          /* wait for everything submitted */
int  rgb_present(rgb_t *b, int sx, int sy, int w, int h, int dx, int dy);
int  rgb_tex_alloc(rgb_t *b, uint32_t bytes, uint32_t *off);
int  rgb_tex_free(rgb_t *b, uint32_t off);
int  rgb_upload(rgb_t *b, uint32_t off, const void *data, uint32_t len, int swap16);   /* copies through the kernel (validated span, drains the engine first) */
int  rgb_readback(rgb_t *b, uint32_t off, void *data, uint32_t len);

#endif

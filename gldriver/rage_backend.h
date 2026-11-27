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
    int      pot_pitch;           /* set before rgb_surface: allocate the target with a power-of-two pitch (needed to present it through the 3D engine: conversion / scaling) */
    int      fmt555;              /* colour target is ARGB1555 (the scanout is 15 bit), else RGB565; set by rgb_surface from the kernel */
    int      last_err;            /* last non-zero return of a submit (diagnostic) */
    int      dbl;                 /* set before rgb_surface: also allocate a second colour block (hardware-overlay double buffering) */
    uint32_t col_alt;             /* the other colour block (the one NOT currently the render target); 0 = single buffered */
    int      ovl_on;              /* the kernel overlay is enabled and shows one of the two blocks */
    int      ovl_fail;            /* the kernel refused the overlay (busy, unsupported ...): stay on the quad present */
} rgb_t;

int  rgb_open(rgb_t *b);                                           /* 0 or an IOKit error */
void rgb_close(rgb_t *b);
int  rgb_surface(rgb_t *b, uint32_t w, uint32_t h, uint32_t bpp, int with_z);   /* allocate colour (+Z), make it the target; frees the previous one */
int  rgb_scissor(rgb_t *b, int l, int r, int t, int bt);
/* Forward a host-order packet stream (mach64_emit.h) as a validated batch.  Kernel-owned registers (target/Z/scissor/texture offsets/pixel
 * width) are routed to the matching kernel calls instead of being written; anything else not on the allow-list is counted in b->dropped. */
int  rgb_flush(rgb_t *b, const m64_cmdbuf *cb);
int  rgb_fill(rgb_t *b, int x, int y, int w, int h, uint32_t rgb565);
uint32_t rgb_gen(rgb_t *b);                                         /* current display generation (odd = mode change in progress) */
int  rgb_present_scaled(rgb_t *b, int sx, int sy, int sw, int sh, int dx, int dy, int dw, int dh, int bilinear);   /* target rect -> scanout rect through the 3D engine */
int  rgb_display(rgb_t *b, uint32_t *w, uint32_t *h, uint32_t *bpp);   /* live scanout size and depth */
void rgb_release(rgb_t *b);                                         /* free the colour and Z blocks */
int  rgb_clear_z(rgb_t *b, uint32_t value);                        /* fill the bound Z buffer */
int  rgb_fence(rgb_t *b);                                          /* wait for everything submitted */
int  rgb_present(rgb_t *b, int sx, int sy, int w, int h, int dx, int dy);
int  rgb_overlay_wanted(const rgb_t *b, uint32_t src_w);                   /* 1 = use the hardware overlay scaler for a smaller full-screen drawable (kernel feature, width <= 720, not disabled by RAGE_GL_OVERLAY=0) */
int  rgb_overlay_present(rgb_t *b, uint32_t dw, uint32_t dh);              /* show the finished target through the overlay and make the other block the target; 0 = done, else fall back to the quad */
void rgb_overlay_off(rgb_t *b);                                            /* hide and disable the overlay (before blocks are freed or the surface rebuilt) */
int  rgb_tex_alloc(rgb_t *b, uint32_t bytes, uint32_t *off);
int  rgb_tex_free(rgb_t *b, uint32_t off);
int  rgb_upload(rgb_t *b, uint32_t off, const void *data, uint32_t len, int swap16);   /* copies through the kernel (validated span, drains the engine first) */
int  rgb_upload2(rgb_t *b, uint32_t off, const void *data, uint32_t len, uint32_t xfer_flags);   /* RAGE_XFER_* flags: 1 swap16, 2 swap32 */
int  rgb_upload_rows(rgb_t *b, uint32_t off, const void *data, uint32_t rowbytes, uint32_t nrows, uint32_t src_pitch, uint32_t dst_pitch, uint32_t flags);   /* RAGE_GL_VRAM_WRITE_ROWS: strided copy; flags: bits 0-1 swap, 8-12 pad bytes per row, 16-17 extra rows */
int  rgb_readback2(rgb_t *b, uint32_t off, void *data, uint32_t len, uint32_t xfer_flags);
int  rgb_readback(rgb_t *b, uint32_t off, void *data, uint32_t len);

#endif

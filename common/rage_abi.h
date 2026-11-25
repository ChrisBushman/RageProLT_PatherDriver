/* common/rage_abi.h - the user-space <-> kext contract of the NEW driver (kernel and tools include the same file).
 * User-client types and selectors are numbered here and nowhere else. */
#ifndef RAGE_ABI_H
#define RAGE_ABI_H

#include "rage_log.h"

#define RAGE_CLIENT_LOG   0x4c4f4700u            /* 'LOG': diagnostics client (Phase 2) */
#define RAGE_CLIENT_GL    0x474c0100u            /* 'GL' v1: the new GL bundle's client (Phase 3) */

/* GL client selectors (kIOUCScalarIScalarO; "in"/"out" = scalar counts).  Every call that needs a render target returns kIOReturnNotReady while the
 * display mode is changing or after it changed (the target was invalidated): re-read DISPLAY, set the target again.  All offsets are VRAM byte offsets
 * the client owns; the kernel owns scissor, addresses and pixel width.  User memory is passed as an address + length and copied with an
 * IOMemoryDescriptor of the calling task (never dereferenced raw). */
enum {
    RAGE_GL_INFO = 0,        /* in 0, out 4: mode_gen, feature bits, usable VRAM end, flags (bit0 engine live) */
    RAGE_GL_ALLOC = 1,       /* in 2 {length, align}, out 1 {offset} */
    RAGE_GL_FREE = 2,        /* in 1 {offset} */
    RAGE_GL_SET_TARGET = 3,  /* in 5 {offset, pitch_px, bpp(2|4), width, height} */
    RAGE_GL_SET_SCISSOR = 4, /* in 4 {left, right, top, bottom} signed, inclusive; clamped to the target */
    RAGE_GL_SET_Z = 5,       /* in 2 {offset, pitch_px} (16-bit Z, height of the target) */
    RAGE_GL_FILL = 6,        /* in 5 {x, y, w, h, rgb565}; 16 bpp targets */
    RAGE_GL_SUBMIT = 7,      /* in 2 {user address of {reg,val} pairs, number of pairs} */
    RAGE_GL_FENCE = 8,       /* in 1 {sequence}, out 1 {completed}; waits (bounded) */
    RAGE_GL_PRESENT = 9,     /* in 6 {sx, sy, w, h, dx, dy}: target -> scanout, clipped on both sides */
    RAGE_GL_VRAM_WRITE = 10, /* in 4 {offset, user address, length, flags}; offset/length multiples of 4, <= 1 MB */
    RAGE_GL_VRAM_READ = 11,  /* in 4 {offset, user address, length, flags} */
    RAGE_GL_DISPLAY = 12,    /* in 0, out 6: mode_gen, width, height, pitch_px, bpp, scanout base */
    RAGE_GL_SCANOUT_READ = 13, /* debug builds only (RAGE_DEBUG_ENGTEST): in 3 {byte offset into the scanout block, user address, length} */
    RAGE_GL_CONFIG = 14,       /* in 2 {key, value}: key 1 = use DMA submission (1) or PIO (0); a global switch, harmless to flip */
    RAGE_GL_SET_TEXTURE = 15,  /* in 3 {index 0..10 = TEX_n_OFF / 11 = SECONDARY_TEX_OFF, VRAM offset (0xffffffff unbinds, 0xfffffffe = set the format only), texel format 0/3/4/6/8/11/12/15 (0 = unchanged; 8 = Y8, 11/12 = YCbCr 4:2:2)} */
    RAGE_GL_CLEAR_Z = 16,      /* in 1 {value}: fill the whole bound 16-bit Z buffer (clipped to the scissor) with value */
    RAGE_GL_SEL_COUNT = 17
};
#define RAGE_XFER_SWAP16 1u      /* VRAM_WRITE/READ flag: swap bytes inside every 16-bit unit */
#define RAGE_XFER_SWAP32 2u      /* ... inside every 32-bit unit */

enum { RAGE_LOG_SEL_INFO = 0, RAGE_LOG_SEL_READ = 1, RAGE_LOG_SEL_MARK = 2, RAGE_LOG_SEL_COUNT = 3, RAGE_LOG_SEL_PANIC = 3 /* only with RAGE_DEBUG_PANIC */, RAGE_LOG_SEL_ENGTEST = 4 /* only with RAGE_DEBUG_ENGTEST */ };

/* SEL_INFO: scalarI 0, scalarO 4 = { head, capacity, refused batches, feature bits } */
/* SEL_READ: scalarI 1 = from_seq, structO RAGE_LOG_READ_BYTES = struct rage_log_read_out */
/* SEL_PANIC (debug builds only): scalarI 1 = token; logs an ERROR event then calls panic().  Refused (bad index) in normal builds. */
/* SEL_ENGTEST (debug builds only): scalarI 1 = test mask, scalarO 4 = { pass mask, outside-changed words, inside-correct pixels, status }; details go to the event ring. */
/* SEL_MARK: scalarI 2 = { a0, a1 }: logs a RAGE_EV_MARK so test phases can be lined up with the driver's events */

#define RAGE_LOG_READ_EVENTS 31u
struct rage_log_read_out {                       /* exactly 1024 bytes */
    uint32_t count;                              /* events returned */
    uint32_t lost;                               /* events overwritten before they could be read */
    uint32_t head;                               /* ring head at the time of the read */
    uint32_t capacity;
    uint32_t pad[4];
    struct rage_log_ev ev[RAGE_LOG_READ_EVENTS];
};
#define RAGE_LOG_READ_BYTES ((uint32_t)sizeof(struct rage_log_read_out))

/* feature bits reported by SEL_INFO and published as the kext property "RageLTFeatures" */
#define RAGE_FBIT_PANIC_LOG 0x01u
#define RAGE_FBIT_2D_GA     0x02u
#define RAGE_FBIT_SLEEPWAKE 0x04u
#define RAGE_FBIT_DMA       0x08u
#define RAGE_FBIT_OVERLAY   0x10u
#define RAGE_FBIT_SCALER    0x20u
#define RAGE_FBIT_DVD       0x40u
#define RAGE_FBIT_FS_SCALE  0x80u

#endif

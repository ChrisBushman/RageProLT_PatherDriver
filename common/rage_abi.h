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
    RAGE_GL_FILL = 6,        /* in 5 {x, y, w, h, colour}; colour = rgb565 (16 bpp target) or 0xAARRGGBB (32 bpp target) */
    RAGE_GL_SUBMIT = 7,      /* in 2 {user address of {reg,val} pairs, number of pairs} */
    RAGE_GL_FENCE = 8,       /* in 1 {sequence}, out 1 {completed}; waits (bounded) */
    RAGE_GL_PRESENT = 9,     /* in 6 {sx, sy, w, h, dx, dy}: target -> scanout, clipped on both sides; a different pixel size (16 bpp target, 32 bpp scanout) goes through the textured-quad path (power-of-two target pitch) */
    RAGE_GL_VRAM_WRITE = 10, /* in 4 {offset, user address, length, flags}; offset/length multiples of 4, <= 1 MB */
    RAGE_GL_VRAM_READ = 11,  /* in 4 {offset, user address, length, flags} */
    RAGE_GL_DISPLAY = 12,    /* in 0, out 6: mode_gen, width, height, pitch_px, bpp, scanout base */
    RAGE_GL_SCANOUT_READ = 13, /* debug builds only (RAGE_DEBUG_ENGTEST): in 3 {byte offset into the scanout block, user address, length} */
    RAGE_GL_CONFIG = 14,       /* in 2 {key, value}: key 1 = use DMA submission (1) or PIO (0); a global switch, harmless to flip */
    RAGE_GL_SET_TEXTURE = 15,  /* in 3 {index 0..10 = TEX_n_OFF / 11 = SECONDARY_TEX_OFF, VRAM offset (0xffffffff unbinds, 0xfffffffe = set the format only), texel format 0/3/4/6/8/11/12/15 (0 = unchanged; 8 = Y8, 11/12 = YCbCr 4:2:2)} */
    RAGE_GL_CLEAR_Z = 16,      /* in 1 {value}: fill the whole bound 16-bit Z buffer (clipped to the scissor) with value */
    RAGE_GL_SUBMIT_EX = 17,    /* in 6 {user address of {reg,val} pairs, number of pairs (0 = textures only), user address of {index,offset} texture pairs, number of texture pairs (<= 12),
                                * texel format 0/3/4/6/8/11/12/15 for the primary unit (0 or 6 = unchanged), format for the secondary unit (0 = unchanged)}: every SET_TEXTURE of a flush plus
                                * the submit in ONE kernel call (a flush used to cost 13+ round trips); the texture records apply exactly as SET_TEXTURE would, failures ignored */
    RAGE_GL_PRESENT_SCALED = 18, /* in 5 {sx | sy << 16, sw | sh << 16, dx | dy << 16, dw | dh << 16, filter (0 nearest, 1 bilinear)}: target rectangle -> scanout rectangle through the 3D engine (any pixel formats; needs a power-of-two target pitch) */
    RAGE_GL_REG_DUMP = 19,     /* debug builds only (RAGE_DEBUG_ENGTEST), READ-ONLY: in 2 {user address, byte length}; fills {tag, value} word pairs for a fixed register list: tag 0x100|i = block-1 index i (overlay/scaler), tag 0x00..0xfc = block-0 byte offset, tag 0xf00 = PCI revision/class word (docs/re-notes/overlay-scaler-plan.md step A) */
    RAGE_GL_OVL_B0 = 20,       /* debug builds only (RAGE_DEBUG_ENGTEST): overlay plan step B0, the ONLY write: OVERLAY_VIDEO_KEY_CLR (block 1 off 0x008) <- 0x005A5A5A, read back, restore 0; out 3 {value before, value read back, value after restore} */
    RAGE_GL_OVL_ON = 21,       /* RAGE_FEAT_OVERLAY: show a client-owned VRAM buffer through the hardware scaler: in 6 {source VRAM offset, source pitch in pixels, srcW | srcH << 16, dstX | dstY << 16, dstW | dstH << 16, source bytes per pixel (2 = the target format of the 15/16 bpp scanout, 4 = ARGB8888)}; the block must belong to the caller; source width <= 720; one overlay, one owner. Disabled again on OVL_OFF, client close, mode change (docs/re-notes/overlay-scaler-plan.md) */
    RAGE_GL_OVL_OFF = 22,      /* in 0: hide and disable the overlay (owner only) */
    RAGE_GL_OVL_FLIP = 23,     /* in 1 {offset}: point the scaler at another buffer of the same size (owner only); applied by the chip at its next fetch (tearing possible) */
    RAGE_GL_SWAP_TARGET = 24,  /* in 1 {dstOffset}: copy the whole render target into another block of the caller's of the same size/pitch/format (2D engine blit) and make that block the render target; scissor, Z binding and every other state are kept (the overlay present: the target still holds the frame after the flip) */
    RAGE_GL_APERTURE_BENCH = 25, /* debug builds only (RAGE_DEBUG_ENGTEST), B1g-4: in 3 {offset, length, cache mode 0 inhibit/1 write-combine/2 write-through/3 copyback/4 default}, owner's own VRAM block; out 2 {ns per pass of 32-bit stores, ns per pass of 32-bit loads} (8 passes averaged) */
    RAGE_GL_VRAM_WRITE_ROWS = 26, /* in 6 {offset, user address, rowbytes | nrows << 16, source pitch (bytes), destination pitch (bytes), flags}: copy nrows rows of rowbytes from the user buffer to VRAM at the destination pitch
                                   * (rows need not be contiguous in either place).  rowbytes a multiple of 4, <= 4096; nrows 1..1024; flags bits 0-1 = swap as VRAM_WRITE, bits 8-12 = bytes (0, 4, 8, 12, 16) to replicate
                                   * after every row from its own last bytes, bits 16-17 = extra rows (0..3) replicated from the last row.  The whole destination span must belong to the caller. */
    RAGE_GL_MC_EXP = 27,       /* debug builds only (RAGE_DEBUG_ENGTEST): LT Pro motion-compensation experiment, stage 1 (docs/re-notes/mc-experiment-plan.md): ONE 16x16 luma prediction through the scaler pipe, kernel-built register sequence (Win98 values), caller's own 8-bit planes.
                                * in 5 {source offset, destination offset, pitch | height << 16, X | Y << 16 (destination block), mvx & 0xffff | mvy << 16 (half-pel, signed)}; out 1 {GUI_STAT afterwards with the low byte replaced by the status: 0 ok / 1 engine did not settle (reset) / 2 refused}.
                                * (IOConnectMethodScalarIScalarO takes at most 6 arguments IN TOTAL on Panther: the first version declared 6 + 2 and the kernel read garbage output pointers: panic Nov 28 22:35.) */
    RAGE_GL_MC_BATCH = 28,     /* RAGE_FEAT_DVD: MPEG-2 motion compensation through the LT Pro scaler pipe (docs/re-notes/mc-experiment-plan.md): in 2 {user address of RageMcCmd[], count (1..RAGE_MC_MAX_CMDS)}.
                                * Every command adds (ONE/ONE blend, signed destination clamp) a bilinear half-pel prediction block to the bytes already in the destination, so the caller first writes the
                                * residual there (signed bytes, two's complement, -128..127) or zero.  All blocks of a batch are validated before the first register is written; planes must belong to the caller.
                                * Measured on hardware: integer, h and v half-pel are bit-exact MPEG-2; hv half-pel rounds twice (+1 on ~35% of samples); two-reference blocks average with (a+b+1)>>1. */
    RAGE_GL_OVL_ON_YUV = 29,   /* RAGE_FEAT_OVERLAY (+ RAGE_FEAT_DVD): show planar YUV 4:2:0 (YV12 layout, three separate 8-bit planes) through the hardware scaler, no conversion by the CPU or the 3D engine:
                                * in 1 {user address of one RageOvlYuv}.  The Y, U and V blocks must belong to the caller; one overlay, one owner (the same one as OVL_ON); OVL_OFF turns it off. */
    RAGE_GL_OVL_FLIP_YUV = 30, /* in 3 {Y offset, U offset, V offset}: point the YUV overlay at another set of planes of the same geometry (owner only) */
    RAGE_GL_SEL_COUNT = 31
};
#define RAGE_XFER_SWAP16 1u      /* VRAM_WRITE/READ flag: swap bytes inside every 16-bit unit */
#define RAGE_XFER_SWAP32 2u      /* ... inside every 32-bit unit */

/* one block of RAGE_GL_MC_BATCH (32 bytes) */
#define RAGE_MC_MAX_CMDS 4096
#define RAGE_MC_TWOWAY   1u              /* flags: second reference srcOff2/mv2 averaged with the first */
typedef struct {
    uint32_t srcOff;        /* VRAM byte offset of line 0 of the reference plane in the coordinate system used (a field: first line of that parity) */
    uint32_t srcOff2;       /* second reference (RAGE_MC_TWOWAY), same stride/rows */
    uint32_t dstOff;        /* VRAM byte offset of destination line 0 of the system, multiple of 8 */
    uint16_t stride;        /* bytes between lines of the system (frame: plane pitch; field: 2 x pitch), multiple of 8 */
    uint16_t width;         /* valid pixels per line (<= stride) */
    uint16_t rows;          /* valid lines of the system */
    uint8_t  size;          /* 0 = 16x16, 1 = 16x8, 2 = 8x8, 3 = 8x4 (width x height) */
    uint8_t  flags;
    int16_t  x, y;          /* destination block origin in the system */
    int16_t  mvx, mvy;      /* half-sample vector of the first reference */
    int16_t  mv2x, mv2y;    /* second reference */
} RageMcCmd;

/* RAGE_GL_INFO flags (4th output) */
#define RAGE_INFO_ENGINE  1u     /* engine live */
#define RAGE_INFO_DMA     2u     /* DMA submission active */
#define RAGE_INFO_OVL     8u     /* the hardware overlay scaler is currently enabled for some client (read-only status, e.g. for tests) */
#define RAGE_INFO_TGT555  4u     /* the scanout is ARGB1555 (Thousands of colours): colour targets are created as 1555, not 565 (read at SET_TARGET time; re-read after a mode change) */
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

/* RAGE_GL_OVL_ON_YUV argument (48 bytes).  Chroma planes are half the width and height of luma with a chroma pitch of exactly pitchY / 2 (what the scaler assumes: Win98 layout). */
typedef struct {
    uint32_t offY, offU, offV;   /* VRAM byte offsets of the planes, multiples of 8 */
    uint32_t pitchY;             /* luma line pitch in bytes, multiple of 16 */
    uint16_t srcW, srcH;         /* source size in luma pixels: even, srcW <= 720 */
    uint16_t dstX, dstY, dstW, dstH;   /* destination rectangle on the scanout */
    uint32_t flags, reserved;    /* 0 */
} RageOvlYuv;

#endif

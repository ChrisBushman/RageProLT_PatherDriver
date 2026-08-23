/*
 * gld.h - Apple OpenGL "GLD" plugin interface (Panther/10.3), as reversed
 * from ATIRage128GLDriver.bundle.  Our bundle (ATIRageProGLDriver.bundle)
 * exports these gld* symbols; Apple's OpenGL.framework + GLEngine.bundle
 * call them.  55 entry points total (see docs/re-notes/ABI.md); the ones
 * that carry real work for a fixed-function mach64 are declared here.
 *
 * NOTE: this is a PRIVATE, undocumented, version-specific Apple interface.
 * Signatures below are reconstructed from disassembly + observed call sites
 * and must be validated against a real 10.3 bundle before shipping.
 */
#ifndef GLD_H
#define GLD_H

#include <stdint.h>
#include <mach/mach.h>   /* io_connect_t etc. (IOKit user side)            */

/* ---- Renderer capability struct filled by gldGetRendererInfo ---------
 * Offsets recovered from ATIRage128GLDriver`gldGetRendererInfo.  The
 * framework reads these to decide HW-vs-software and to publish caps.
 */
typedef struct {
    uint32_t handle;        /* 0x00 renderer handle                       */
    uint32_t device_id;     /* 0x04 (idx<<24)|devmask                     */
    uint32_t renderer_id;   /* 0x08 e.g. 0x2513 (Rage128); pick a new one */
    uint32_t buffer_modes;  /* 0x0c                                       */
    uint32_t caps_accel;    /* 0x10 accelerated + buffer-mode bits        */
    uint32_t caps_color;    /* 0x14 color/depth-mode bits                 */
    uint32_t caps_misc;     /* 0x18 texture/misc                          */
    uint32_t max_something;  /* 0x1c                                       */
    uint8_t  _pad[0x14];    /* 0x20..0x2f (shorts + a byte, see disasm)    */
    uint32_t video_memory;  /* 0x30 VRAM bytes                            */
    uint32_t texture_memory; /* 0x34 texture memory bytes                 */
} gld_renderer_info;

/* ---- Our per-context object (mirrors the 0x200-byte struct malloc'd by
 * gldCreateContext).  Only the fields the handshake touches are named. */
typedef struct {
    uint32_t     renderer_index;   /* 0x00                                */
    io_connect_t connect;          /* 0x04 IOServiceOpen result           */
    uint32_t     chip_flag;        /* 0x08                                */
    /* ... shared ctx + create args at 0x0c/0x10/0x14/0x18 ...            */
    uint32_t     fmt_caps;         /* 0x2c decoded pixel format           */
    uint32_t     mode_word;        /* 0x30                                */
    uint32_t     device_info[3];   /* 0x34..0x3c  <- get_config (sel 3)   */
    /* mapped regions (IOConnectMapMemory):                               */
    void        *regs;             /* 0xb8 type 1 surface? see abi note   */
    uint32_t     regs_size;        /* 0xbc                                */
    void        *surface;          /* 0xc0 type 2                         */
    uint32_t     surface_size;     /* 0xc4                                */
    void        *fb;               /* 0xd0 type 4 command buffer          */
    uint32_t     fb_size;          /* 0xd4                                */
    void        *cmd_regs;         /* 0xdc type 0 register aperture       */
    uint8_t      _tail[0x200 - 0xe0];
} gld_context;

/* ---- The gld* entry points (subset; full list in ABI.md) ------------ */
int  gldInitializeLibrary(void *);
void gldTerminateLibrary(void);
int  gldGetRendererInfo(gld_renderer_info *out, uint32_t renderer_mask);
int  gldChoosePixelFormat(void **pf, const int *attribs);
int  gldCreateContext(gld_context **out, void *pixfmt, void *shared,
                      void *share_io, void *a7, void *a8, void *a9);
int  gldDestroyContext(gld_context *ctx);
void gldUpdateDispatch(gld_context *ctx, void *dispatch_table);
/* vertex path -> new_vertex_buffer/delete_vertex_buffer selectors        */
void *gldAllocVertexBuffer(gld_context *ctx, uint32_t size);
int   gldCompleteVertexBuffer(gld_context *ctx, void *vb);
void  gldFreeVertexBuffer(gld_context *ctx, void *vb);
int   gldFlush(gld_context *ctx);
int   gldFinish(gld_context *ctx);

#endif /* GLD_H */

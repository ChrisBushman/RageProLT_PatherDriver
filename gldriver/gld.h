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
#include <IOKit/IOKitLib.h>   /* io_connect_t + IOConnectMethod* (user side) */
#include "../common/mach64_emit.h"

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
    void        *backend;          /* 0xe0 our mach64_priv (see below)    */
    uint8_t      _tail[0x200 - 0xe0 - sizeof(void *)];
} gld_context;

/* ---- Our per-context backend state (hung off gld_context.backend) -----
 * Not part of Apple's 0x200 struct -- allocated separately so we can grow it. */
#define MACH64_DIRTY_STATE   0x1     /* re-emit the state block next draw   */

typedef struct {
    m64_state  hw;                 /* shadow of the engine's raster state  */
    uint32_t   dirty;              /* MACH64_DIRTY_* flags                 */
    uint32_t   last_stamp;         /* newest new_vertex_buffer stamp       */
    uint32_t   target_off;         /* current render target byte offset    */
    uint32_t   target_pitch;       /* current render target pitch (pixels) */
    uint32_t   scratch[1024];      /* host-order command-buffer staging    */
} mach64_priv;

static __inline__ mach64_priv *MACH64_PRIV(gld_context *c)
{ return (mach64_priv *)c->backend; }

/* ---- state translation (gld_state.c): GL state -> m64_state ---------- */
void mach64_init_state(mach64_priv *p);
void mach64_set_render_target(gld_context *ctx, uint32_t off, uint32_t pitch_px);
void mach64_set_shade_model(gld_context *ctx, int smooth);  /* GL_SMOOTH? */
void mach64_set_depth_test(gld_context *ctx, int enable, uint32_t gl_func);
void mach64_set_scissor(gld_context *ctx, int x, int y, int w, int h);

/* ---- geometry translation + submission (gld_render.c) --------------- */
void mach64_pack_vertex(m64_vertex *out, float x, float y, float z,
                        float w, uint32_t argb);
int  mach64_draw_triangle(gld_context *ctx, const m64_vertex *v0,
                          const m64_vertex *v1, const m64_vertex *v2);
uint32_t mach64_submit(gld_context *ctx, m64_cmdbuf *cb);

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

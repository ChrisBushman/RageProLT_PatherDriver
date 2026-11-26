/*
 * gld.h - the GL bundle's internal interface (standalone driver, design section 4).  The bundle talks to the hardware ONLY through
 * rage_backend.h (the kernel's GL client): no Apple kext, no kernel-pointer peeks, no CPU view of VRAM or registers.
 * The gld* entry points and the gli/dispatch-table layouts are Apple's private GLEngine interface (docs/re-notes/ABI.md, gld-layer.md).
 */
#ifndef GLD_H
#define GLD_H

#include <stdlib.h>
#include <stdint.h>
#include <string.h>
#include "mach64_emit.h"
#include "mach64_regs.h"
#include "rage_backend.h"
#include "rage_vmath.h"

/* float <-> bits without a float type in the arithmetic */
static inline uint32_t gld_bits(float f) { uint32_t u; __builtin_memcpy(&u, &f, 4); return u; }
static inline float gld_flt(uint32_t u) { float f; __builtin_memcpy(&f, &u, 4); return f; }

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

/* ---- Pixel format (gld_pixelformat.c) -------------------------------
 * Our private object returned by gldChoosePixelFormat and handed back to
 * gldCreateContext (the framework treats it opaquely; we alloc + free it).
 * The attribs array + token semantics were recovered from the reference
 * gldChoosePixelFormat jump table (see docs/re-notes/gld-layer.md). */
#define GLD_PF_DOUBLEBUFFER  0x1
#define GLD_PF_STEREO        0x2
typedef struct {
    /* Framework-owned header: GLEngine treats offset 0 as a linked-list "next"
     * pointer (0 = single format; it derefs it if non-zero -> our old layout put
     * color_size here and it bus-errored) and writes flag words into +0x04/+0x08.
     * Leave this zeroed and let the framework decorate it. */
    uint8_t  _fw[0x40];
    /* Our parsed attributes live after the framework header. */
    uint32_t color_size;    /* GLD_PFA_COLOR_SIZE   (bits)                 */
    uint32_t alpha_size;    /* GLD_PFA_ALPHA_SIZE                          */
    uint32_t depth_size;    /* GLD_PFA_DEPTH_SIZE   (0 = no depth)         */
    uint32_t stencil_size;  /* recorded; mach64 has no hw stencil          */
    uint32_t accum_size;    /* recorded; mach64 has no hw accum            */
    uint32_t aux_buffers;
    uint32_t flags;         /* GLD_PF_DOUBLEBUFFER | GLD_PF_STEREO         */
} gld_pixel_format;

/* ---- Our per-context object (mirrors the 0x200-byte struct malloc'd by
 * gldCreateContext).  Only the fields the handshake touches are named. */
/* Apple's per-context object: a 0x200-byte struct with a FIXED field layout that
 * BOTH GLEngine and the driver read/write (recovered from ati128gld
 * gldCreateContext @0x4648).  Offsets MUST match exactly or GLEngine reads
 * garbage where it expects render state.  Padding makes each named field land at
 * its true offset; GLEngine-only fields (share ptrs @0x0c-0x18, drawable @0x20,
 * flags @0x24/0x28, target @0x94, dims @0x1f0/0x1f4, misc state) live in the
 * _padXX gaps and are written by gldAttachDrawable via raw offsets. */
typedef struct {
    uint32_t     renderer_index;   /* 0x00                                */
    io_connect_t connect;          /* 0x04 IOServiceOpen result           */
    uint32_t     chip_flag;        /* 0x08                                */
    uint32_t     _pad0c[(0x2c-0x0c)/4];   /* 0x0c..0x2b GLEngine share/drawable/flags */
    uint32_t     fmt_caps;         /* 0x2c decoded pixel-format caps       */
    uint32_t     mode_word;        /* 0x30                                */
    uint32_t     device_info[3];   /* 0x34..0x3c  <- get_config (sel 3)   */
    uint32_t     _pad40[(0xb8-0x40)/4];   /* 0x40..0xb7 GLEngine render state  */
    void        *regs;             /* 0xb8 mapped region type 1            */
    uint32_t     regs_size;        /* 0xbc                                */
    void        *surface;          /* 0xc0 type 2                         */
    uint32_t     surface_size;     /* 0xc4                                */
    uint32_t     _padc8[2];        /* 0xc8,0xcc                           */
    void        *fb;               /* 0xd0 type 4 command buffer          */
    uint32_t     fb_size;          /* 0xd4                                */
    uint32_t     _padd8;           /* 0xd8                                */
    void        *cmd_regs;         /* 0xdc type 0 register aperture       */
    void        *backend;          /* 0xe0 our mach64_priv (driver-private slot) */
    uint8_t      _tail[0x200 - 0xe4];     /* 0xe4..0x1ff (incl target/dims) */
} gld_context;
typedef char _gld_ctx_size_check[(sizeof(gld_context)==0x200)?1:-1];


#define MACH64_DIRTY_STATE   0x1     /* re-emit the state block next draw   */
#define GLD_BATCH_DWORDS     32768u  /* host-order batch staging (dwords)   */
#define GLD_BATCH_LIMIT      (GLD_BATCH_DWORDS - 512u)

typedef struct {
    rgb_t      rb;                 /* the kernel GL client (target, Z, scissor, textures, submit, present) */
    m64_state  hw;                 /* shadow of the engine's raster state  */
    uint32_t   dirty;              /* MACH64_DIRTY_* flags                 */
    m64_cmdbuf batch;              /* persistent packet buffer over batchbuf */
    uint32_t   batchbuf[GLD_BATCH_DWORDS];
    unsigned char *gc;             /* GLEngine's gc for this context (pixel-store state) */
    uint32_t   dbuf;               /* double-buffered pixel format          */
    uint32_t   cbpp;               /* colour bytes per pixel of the render target: 2 (RGB565/1555) or 4 (ARGB8888, scanout 32 bpp); set when the surface is built */
    uint32_t   fb_w, fb_h;         /* drawable size */
    uint32_t   win_x, win_y;       /* window content origin on the Rage's display (present destination) */
    uint32_t   win_mode;           /* 1 = a window (present at win_x,win_y), 0 = full screen */
    uint32_t   cgs_win, cgs_surf;
    uint32_t   frame_new;          /* 1 => the colour/Z clear of this frame is still owed */
    uint32_t   init_clear;         /* 1 => the colour buffer has never been defined (new surface): clear it once even if the app does not glClear */
    uint32_t   z_clear_pending;
    uint32_t   scr_w, scr_h;       /* scanout size at surface build; fs_scale = full-screen drawable smaller than it -> presented stretched (bilinear quad) */
    uint32_t   fs_scale;
    uint32_t   use_ovl;            /* fs_scale presented through the hardware overlay scaler (double buffered, nearest-neighbour) instead of the bilinear quad */
    uint32_t   mode_gen;           /* display generation the surface was built for */
    uint32_t   attached;           /* a surface exists */
    uint32_t   tex_off;            /* bound texture VRAM offset */
    uint32_t   tex_w, tex_h, tex2_w, tex2_h;   /* bound texture sizes (texel inset) */
    const void *tex_slot;
    int        tex_lvl;
    uint32_t   tex_active;
    uint32_t   triangles;          /* statistics */
    /* idle repaint (windowed only): WindowServer paints the window white when it is exposed; an app that is not drawing (Chess) never restores it, so a
     * small asynchronous thread re-presents the last frame once the context has been idle for ~300 ms.  `volatile` counters only: nothing on the hot path. */
    void      *zs;                 /* stencil/depth emulation state (gld_stencil.c) */
    void      *accum;              /* CPU accumulation buffer (gld_pixels.c): 3 x int32 per pixel, Q8 of a colour byte */
    void      *query;              /* the active occlusion query (gld_entry.c): while set every triangle runs through the CPU rasteriser, which counts the passing samples */
    uint32_t   zs_mode;            /* 0 stencil off, 1 hardware Z-as-stencil, 2 CPU stencil (gld_stencil.c) */
    uint32_t   nonpoly;            /* 1 while a point/line quad is drawn: it uses the front-face stencil state */
    uint32_t   tsig[9], tsig_valid;/* texture sampler setup of the last emitted state (see mach64_draw_triangle) */
    volatile uint32_t presents;    /* completed presents (activity together with `triangles`) */
    volatile uint32_t k2d_touched; /* the thread's blit reprogrammed the engine: the next draw must re-emit the state */
    volatile int rp_run;
    int        rp_started;
    unsigned long rp_thread;       /* pthread_t */
} mach64_priv;

extern float mach64_zbias;              /* added to the window depth of expanded points/lines (glPolygonOffset in line/point polygon mode) */
extern float mach64_zforce;             /* >= 0: every vertex gets this window depth (stencil emulation) */
/* gld_stencil.c */
void     mach64_zs_free(mach64_priv *p);
void     mach64_zs_reset(mach64_priv *p);
void     mach64_zs_sync(gld_context *ctx, uint32_t gli);
void     mach64_zs_prepare(gld_context *ctx);
void     mach64_zs_note_hw(gld_context *ctx);
int      mach64_zs_cpu_draw(gld_context *ctx, const m64_vertex *a, const m64_vertex *b, const m64_vertex *c);
void     mach64_zs_clear_depth(gld_context *ctx);
void     mach64_zs_depth_to_hw(gld_context *ctx);
void     mach64_zs_clear_stencil(gld_context *ctx, uint32_t v);
uint32_t mach64_zs_peek(gld_context *ctx, int which, int x, int y);     /* which 0 = stencil, 1 = depth (tests) */
int      mach64_hw_tri(gld_context *ctx, const m64_vertex *v0, const m64_vertex *v1, const m64_vertex *v2);
void mach64_repaint_start(mach64_priv *p);
void mach64_repaint_stop(mach64_priv *p);

static __inline__ mach64_priv *MACH64_PRIV(gld_context *c) { return (mach64_priv *)c->backend; }
#define M64_CBPP(p) 2u

/* GLEngine's vertex layout and the ctx fields it reads */
#define MACH64_GLIVERTEX_STRIDE  0x100
#define M64_UADDR_MAX 0xFFFEFFFFu
typedef void (*gld_render_fn)(gld_context *ctx, const void *verts, uint32_t count);

/* gld_core.c */
void     mach64_init_state(mach64_priv *p);
int      mach64_draw_triangle(gld_context *ctx, const m64_vertex *v0, const m64_vertex *v1, const m64_vertex *v2);
void     mach64_flush_batch(gld_context *ctx);
void     mach64_sync_depth_from_gli(gld_context *ctx);
/* Read a gli field (0 if the gli can't be located).  The cached-gli fast path is inline (the established SonicR-plugin fix, gld.h there): bind_texture / state sync read ~100 fields
 * per primitive and the out-of-line call + re-validation per field was ~3% of the main thread in the race profile. */
extern gld_context *g_gli_ctx; extern uint32_t g_gli_p10, g_gli_g;
uint32_t mach64_gli_u32_slow(gld_context *ctx, uint32_t off);
static inline __attribute__((always_inline)) uint32_t mach64_gli_u32(gld_context *ctx, uint32_t off)
{
    uint32_t g = g_gli_g;
    if (g_gli_ctx == ctx && g_gli_p10 == *(const uint32_t *)((const char *)ctx + 0x10) && g && *(const uint32_t *)(uintptr_t)(g + 0x7898) == (uint32_t)(uintptr_t)ctx)
        return *(const uint32_t *)(uintptr_t)(g + off);
    return mach64_gli_u32_slow(ctx, off);
}
void mach64_signal_fence(gld_context *ctx, void *fence);
void mach64_begin_query(gld_context *ctx, void *query);
void mach64_end_query(gld_context *ctx);
void mach64_query_add(gld_context *ctx, uint32_t n);
void     mach64_frame_start(gld_context *ctx);
void     mach64_clear_color(gld_context *ctx);
void     mach64_clear_zbuffer(gld_context *ctx);
void     mach64_present(gld_context *ctx);
int      mach64_mode_check(gld_context *ctx);
int      mach64_build_surface(gld_context *ctx);
void     mach64_pack_vertex(m64_vertex *out, float x, float y, float z, float w, uint32_t argb);
void     mach64_gli_forget(gld_context *ctx);
int      gldFlush(gld_context *ctx);
int      gldFinish(gld_context *ctx);
int      mach64_flush(gld_context *, uint32_t, uint32_t, uint32_t, uint32_t, uint32_t, uint32_t);
void     mach64_dispatch_complete(gld_context *, const void *, uint32_t);
void     mach64_dispatch_render_ctl(gld_context *, uint32_t, uint32_t, uint32_t, uint32_t, uint32_t);

/* gld_pixels.c / control slots */
void mach64_ctl00(gld_context *, uint32_t op, float value);   /* table[0x00] = glAccum (GL_ACCUM 0x100, LOAD 0x101, RETURN 0x102, MULT 0x103, ADD 0x104) */
void mach64_accum_clear(gld_context *);
typedef struct m64_cpuenv_s { const struct m64_cpuenv_s *e2; int nlev; const uint32_t *limg[12]; int lw[12], lh[12]; const uint32_t *img; int w, h, wsh, clamp_s, clamp_t, bil, has_a, cube, vol, depth, clamp_r; const uint32_t *vimg; const uint32_t *face[6]; uint32_t mode, rgbf, alf, src[3], asrc[3], op[3], aop[3]; int rscale, ascale, ec[4]; } m64_cpuenv;
int mach64_cpu_env(gld_context *, m64_cpuenv *);
int mach64_env_needs_cpu(uint32_t gli);
int mach64_copy_tex_sub(gld_context *, void *texobj, int face, uint32_t level, uint32_t xoff, uint32_t yoff, int zoff, uint32_t srcx, uint32_t srcy, int w, int h);
void *mach64_cb_open(gld_context *, int fr0, int nr);
int mach64_cb_get(void *h, int fbrow, int x, uint32_t *v);
void mach64_cb_put(void *h, int fbrow, int x, uint32_t v);
int mach64_cb_is32(gld_context *);
void mach64_cb_close(gld_context *, void *h, int write_back);
uint32_t mach64_cb_pack(gld_context *, unsigned R, unsigned G, unsigned B);
int mach64_cb_is555(gld_context *);
void mach64_cb_unpack(gld_context *, unsigned v, unsigned *R, unsigned *G, unsigned *B);
void mach64_accum_free(mach64_priv *);
int  mach64_ctl0c(gld_context *, uint32_t, uint32_t, uint32_t, uint32_t, uint32_t, uint32_t);
int  mach64_ctl10(gld_context *, uint32_t, uint32_t, uint32_t, uint32_t, uint32_t, uint32_t);
int  mach64_ctl14(gld_context *, uint32_t, uint32_t, uint32_t, uint32_t, uint32_t, uint32_t);
int  mach64_ctl08(void *rb_ctx, int x, int y, int w, int h, unsigned format, unsigned type, void *pixels);
void mach64_texture_dirtied(void *texobj);
void mach64_texture_bound(void *texobj);
void mach64_texture_deleted(gld_context *ctx, void *texobj);

/* gld_texture.c */
int      mach64_bind_texture(gld_context *ctx);
void     mach64_tex_select_lod(gld_context *ctx, float pix_area, float uv_area);
extern float mach64_tex_mscale[2], mach64_tex_mbias[2];
extern int mach64_tex_noinset;
extern int mach64_tex_cube;
extern int mach64_fog_cpu, mach64_fog_on, mach64_spec_mode, mach64_flat_on, mach64_env_add;
extern uint32_t mach64_tex_vofs[2];

/* gld_cgs.c */
int mach64_cgs_resolve_bounds(uint32_t window_id, uint32_t surface_id, int allow_enum, int *x, int *y, int *w, int *h);
int mach64_cgs_visible_rects(uint32_t window_id, uint32_t surface_id, int lx, int ly, int w, int h, short (*out)[4], int maxr);
int mach64_cgs_off_display(void);

void mach64_read_vertex(const void *v, m64_vertex *out, float fb_h);
void mach64_render_points     (gld_context *, const void *, uint32_t);
void mach64_render_lines      (gld_context *, const void *, uint32_t);
void mach64_render_line_strip (gld_context *, const void *, uint32_t);
void mach64_render_triangles  (gld_context *, const void *, uint32_t);
void mach64_render_elt_tris   (gld_context *, const void *, uint32_t);
void mach64_render_elt_points (gld_context *, const void *, uint32_t);
void mach64_render_elt_lines  (gld_context *, const void *, uint32_t);
void mach64_render_tri_strip  (gld_context *, const void *, uint32_t);
void mach64_render_tri_fan    (gld_context *, const void *, uint32_t);
void mach64_render_tri_fan_slot(gld_context *, const void *, const void *, uint32_t);
void mach64_render_polygon    (gld_context *, const void *, uint32_t);
void mach64_render_quads      (gld_context *, const void *, uint32_t);
void mach64_render_quad_strip (gld_context *, const void *, uint32_t);

int  gldUpdateDispatch(gld_context *ctx, void *table, uint32_t flags);
int  gldAttachDrawable(gld_context *ctx, int type, void *drawable, uint32_t flags);
const char *gldGetString(gld_context *ctx, uint32_t name);
int  gldGetError(gld_context *ctx);
int  gldGetVersion(int *v0, int *v1, int *v2, int *v3);
int  gldGetInteger(gld_context *ctx, uint32_t pname, int *params);
int  gldSetInteger(gld_context *ctx, uint32_t pname, int value);
void gldInitDispatch(gld_context *ctx, void *table, void *outinfo);
int  gldFlushBuffer(gld_context *ctx, void *drawable);
void *gldInitializeLibrary(void *iface, void *a2, void *a3, void *a4, void *a5);
void gldTerminateLibrary(void);
int  gldGetRendererInfo(gld_renderer_info *out, uint32_t renderer_mask);
int  gldChoosePixelFormat(void **pf, const int *attribs);
int  gldDestroyPixelFormat(void *pf);
int  gldCreateContext(gld_context **out, void *pixfmt, void *shared, void *share_io, void *a7, void *a8, void *a9);
int  gldDestroyContext(gld_context *ctx);
void *gldAllocVertexBuffer(gld_context *ctx, uint32_t size);
int   gldCompleteVertexBuffer(gld_context *ctx, void *vb);
void  gldFreeVertexBuffer(gld_context *ctx, void *vb);

#endif /* GLD_H */

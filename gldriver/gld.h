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

/* ---- Our per-context backend state (hung off gld_context.backend) -----
 * Not part of Apple's 0x200 struct -- allocated separately so we can grow it. */
#define MACH64_DIRTY_STATE   0x1     /* re-emit the state block next draw   */

typedef struct {
    m64_state  hw;                 /* shadow of the engine's raster state  */
    uint32_t   dirty;              /* MACH64_DIRTY_* flags                 */
    uint32_t   last_stamp;         /* newest new_vertex_buffer stamp       */
    uint32_t   target_off;         /* current render target byte offset    */
    uint32_t   target_pitch;       /* current render target pitch (pixels) */
    uint32_t   z_off;              /* depth buffer byte offset (0 = none)  */
    uint32_t   z_pitch;            /* depth buffer pitch (16-bit Z pixels)  */
    uint32_t   frame_new;          /* 1 => clear Z before next frame's geom */
    unsigned char *gc;             /* GLEngine's gc for THIS context (dispatch table - 0x78a8):
                                    * holds GL_PACK / GL_UNPACK pixel-store + raster state.  Per-context
                                    * (set in gldUpdateDispatch) so pixel ops read the running context's
                                    * state, not whichever context last updated dispatch. */
    /* ---- double buffering (dbuf=0 => render straight to the front/scanout) - */
    uint32_t   dbuf;               /* 1 => render to back, blit on present  */
    uint32_t   front_off;          /* scanout (front) surface byte offset   */
    uint32_t   back_off;           /* off-screen back surface byte offset   */
    uint32_t   render_off;         /* page-flip: the currently-HIDDEN buffer we render into */
    uint32_t   fb_w, fb_h;         /* surface dimensions (swap blit extent) */
    uint32_t   fb_pitch;           /* surface pitch in 32-bit pixels        */
    /* ---- WINDOWED coexistence probe (env MACH64_WIN_RECT="x,y,w,h") ------
     * win_mode=1 => render the w x h scene into the top-left of the back buffer and BLIT it to
     * the LIVE front buffer at (win_x,win_y) on present (no CRTC page-flip), so the desktop
     * stays visible and the WindowServer keeps compositing -- the test for engine coexistence. */
    uint32_t   win_mode;           /* 1 => windowed blit present into the live desktop */
    uint32_t   win_x, win_y;       /* window content origin in screen pixels (top-left) */
    uint32_t   win_buf_off;        /* per-context windowed back+Z buffer (0 = none/shared); freed on
                                    * detach.  Lets multiple windowed contexts render independently. */
    uint32_t   win_buf_size;       /* size of that pool allocation, for the free */
    /* ---- Path B port (GLD_APPLE_PROBE): live connections to Apple's ATIRagePro kext ---- */
    unsigned int apple_cn;         /* io_connect_t to Apple's type-1 GL user client (0=none) */
    unsigned int apple_surf_cn;    /* io_connect_t to Apple's type-2 IOAccelSurface client (0=none) */
    uint32_t   present_unsafe;     /* 1 = bound surface is display-associated (surf[0x178]!=0xffff) ->
                                    * its present DST aliases the live scanout -> would race IONDRV ->
                                    * skip prime + present (setup_target/render_ctl) to avoid a panic */
    /* ---- fullscreen shrink-surface (MACH64_FS_SHRINK): render into a SMALLER backing so VRAM frees
     * for a texheap, then set_scale presents it scaled up to the native scanout.  fs_disp_* hold the
     * NATIVE display size (present DEST); width/height (render dims) are set to the shrunk size. 0 =
     * shrink inactive (native-res flat path). */
    uint32_t   fs_disp_w, fs_disp_h;   /* native display (present DEST); 0 = shrink inactive */
    uint32_t   fs_shrink_w, fs_shrink_h; /* shrunk render/backing dims (present SRC)          */
    /* ---- Path-B VRAM allocator records (from the sel-10 op=ALLOC kext path); opaque kernel handles,
     * freed on detach/destroy so VRAM isn't leaked.  0 = allocated via the small-window guess. */
    uint32_t   z_record;           /* Apple-allocator record for the Z buffer   */
    uint32_t   tex_record;         /* Apple-allocator record for the texture heap */
    /* ---- texture state (first cut: one bound 2D texture) -------------- */
    void      *tex_obj;            /* bound/uploaded texobj (t[0x30]=state) */
    uint32_t   tex_off;            /* texel VRAM byte offset (0 = none up)  */
    uint32_t   tex_w, tex_h;       /* texel dimensions                     */
    uint32_t   tex_active;         /* 1 => emit textured state this draw    */
    uint32_t   scratch[1024];      /* host-order command-buffer staging    */
    /* ---- triangle batching: accumulate many triangles' register writes into one
     * command buffer and DMA-kick once per flush, instead of one kick per triangle
     * (each kick quiesces/activates/waits the bus-master ring).  Flushed at
     * gldFlush/Finish/FlushBuffer, before any fill/blit/clear/upload, and when full. */
    m64_cmdbuf batch;              /* persistent cmd buffer over batchbuf    */
    uint32_t   batchbuf[32768];    /* host-order batch staging (128KB, matches the kext
                                    * fCmdBuf so a big batch DMA-kicks once via descriptor
                                    * chaining instead of ~80 sub-4KB kicks/frame) */
} mach64_priv;

static __inline__ mach64_priv *MACH64_PRIV(gld_context *c)
{ return (mach64_priv *)c->backend; }

/* ---- state translation (gld_state.c): GL state -> m64_state ---------- */
void mach64_init_state(mach64_priv *p);
void mach64_set_render_target(gld_context *ctx, uint32_t off, uint32_t pitch_px);
void mach64_set_shade_model(gld_context *ctx, int smooth);  /* GL_SMOOTH? */
void mach64_set_depth_test(gld_context *ctx, int enable, uint32_t gl_func);
void mach64_set_zbuffer(gld_context *ctx, uint32_t off, uint32_t pitch_px);
void mach64_clear_zbuffer(gld_context *ctx);
void mach64_flush_batch(gld_context *ctx);   /* submit accumulated triangles */
void mach64_clear_color(gld_context *ctx);
void mach64_batch_clear_color(gld_context *ctx);  /* clear folded into the triangle batch */
void mach64_fill_rect(gld_context *ctx, uint32_t off, uint32_t x, uint32_t y,
                      uint32_t w, uint32_t h, uint32_t pitch_px, uint32_t color);
void mach64_blit_surface(gld_context *ctx, uint32_t src_off, uint32_t dst_off,
                         uint32_t sx, uint32_t sy, uint32_t dx, uint32_t dy,
                         uint32_t w, uint32_t h, uint32_t pitch_px);
void mach64_set_texheap(uint32_t base, uint32_t end);
/* Windowed per-context back+Z VRAM pool (gld_texture.c): lazily init the region once, then
 * alloc/free window-sized buffers so multiple windowed contexts render independently. */
void     mach64_winpool_init(uint32_t base, uint32_t end);
uint32_t mach64_winbuf_alloc(uint32_t bytes);   /* 0 = pool exhausted */
void     mach64_winbuf_free(uint32_t off, uint32_t bytes);
void mach64_present(gld_context *ctx);   /* double-buffer back->front swap */
void mach64_pathb_present(gld_context *ctx);   /* Path B: present via Apple's surface_flush */
void mach64_pathb_setup_target(gld_context *ctx); /* Path B: prime surface + set render target off */
uint32_t mach64_vram_alloc(gld_context *ctx, uint32_t size, uint32_t *record_out); /* Path B: Apple VRAM allocator */
void mach64_vram_free(gld_context *ctx, uint32_t record);
uint32_t mach64_agp_alloc(gld_context *ctx, uint32_t size,                          /* Path B: AGP/GART alloc (v7 kext op=3) */
                          uint32_t *token_out, uint32_t *kva_out, uint32_t *handle_out);
void mach64_agp_upload(gld_context *ctx, uint32_t handle,                           /* Path B: AGP texel upload (v7 kext op=5) */
                       const uint32_t *argb, uint32_t count);
void mach64_flip(gld_context *ctx, uint32_t off);   /* page-flip CRTC scanout to `off` */
void mach64_wait_flip(gld_context *ctx);            /* block until the pending flip retired */
void mach64_sync_depth_from_gli(gld_context *ctx);
uint32_t mach64_gli_u32(gld_context *ctx, uint32_t off);   /* read a gli field */
void mach64_set_scissor(gld_context *ctx, int x, int y, int w, int h);

/* ---- texture upload/bind (gld_texture.c) ----------------------------
 * The gld* texture entry points only set dirty bits; the real VRAM upload is
 * the driver's job at draw time.  mach64_texture_dirtied() records the texobj
 * GLEngine image-dirtied (from gldModifyTexture, a3&0x80); mach64_bind_texture()
 * -- called from the draw path -- pulls the image from the texobj's state object
 * (t[0x30]+0x9c record: w/h/fmt/pixels), converts to ARGB8888, DMAs it into
 * VRAM, and programs the mach64 texture registers into p->hw.  Returns 1 if a
 * texture is bound for this draw (so the primitive emits S/T). */
void mach64_texture_dirtied(void *texobj);   /* gldModifyTexture a3&0x80: queue upload */
void mach64_texture_bound(void *texobj);     /* gldModifyTexture: mark current texture */
void mach64_texture_deleted(void *texobj);   /* gldDeleteTexture: free its VRAM slot   */
int  mach64_bind_texture(gld_context *ctx);

/* ---- geometry translation + submission (gld_render.c) --------------- */
void mach64_pack_vertex(m64_vertex *out, float x, float y, float z,
                        float w, uint32_t argb);
int  mach64_draw_triangle(gld_context *ctx, const m64_vertex *v0,
                          const m64_vertex *v1, const m64_vertex *v2);
uint32_t mach64_submit(gld_context *ctx, m64_cmdbuf *cb);

/* ---- render-slot bodies + dispatch install (gld_prims.c/gld_dispatch.c)
 * GLEngine calls these through the dispatch table with the recovered signature
 * f(ctx, verts, count); `verts` points at the first of `count` incoming gli
 * vertices at stride MACH64_GLIVERTEX_STRIDE.  See docs/re-notes/gld-layer.md. */
#define MACH64_GLIVERTEX_STRIDE  0x100   /* incoming vertex stride (recovered) */
typedef void (*gld_render_fn)(gld_context *ctx, const void *verts, uint32_t count);

void mach64_read_vertex(const void *v, m64_vertex *out, float fb_h); /* gli->hw vtx */
/* Set by mach64_bind_texture per draw: when nonzero, GL_FOG is active for this
 * geometry, so mach64_read_vertex sources the vertex ALPHA from the per-vertex fog
 * factor (gli-vertex +0x4c) instead of the colour alpha (+0x3c) -- the setup engine
 * interpolates it and the fog pipe blends the fragment against DP_FOG_CLR by it. */
extern int mach64_fog_on;
extern int mach64_flat_on;   /* glShadeModel(GL_FLAT): replicate the provoking vertex colour */
#ifdef GLD_PERF
extern unsigned long g_perf_dispatch_us;   /* gld_prims.c accumulates emit_tri time here */
unsigned long perf_now_us(void);
#endif
/* Resolve a windowed drawable's CGS window id (descriptor d[1]) + surface id (d[2]) -> on-screen
 * content bounds via CGS (gld_cgs.c).  Returns 1 + fills x/y/w/h (screen px), 0 if unresolved. */
int mach64_cgs_resolve_bounds(uint32_t window_id, uint32_t surface_id, int *x, int *y, int *w, int *h);
int mach64_display_size(int *w, int *h);   /* main-display pixel size (fullscreen surface shaping) */

#ifdef GLD_APPLE_PROBE
unsigned int mach64_apple_glctx_open(void);   /* open Apple's type-1 GL client, return connection */
unsigned int mach64_apple_make_surface(uint32_t surface_id, uint32_t mode,
                                       uint32_t w, uint32_t h); /* type-0 + set_id_mode + set_shape */
void mach64_apple_bind_surface(unsigned int cn, uint32_t surface_id,
                               uint32_t x, uint32_t y, uint32_t w, uint32_t h);
#endif
void mach64_render_points     (gld_context *, const void *, uint32_t);
void mach64_render_lines      (gld_context *, const void *, uint32_t);
void mach64_render_line_strip (gld_context *, const void *, uint32_t);
void mach64_render_triangles  (gld_context *, const void *, uint32_t);
void mach64_render_elt_tris   (gld_context *, const void *, uint32_t);  /* clipped convex polygons */
void mach64_render_elt_points (gld_context *, const void *, uint32_t);  /* clipped points          */
void mach64_render_elt_lines  (gld_context *, const void *, uint32_t);  /* clipped lines           */
void mach64_render_tri_strip  (gld_context *, const void *, uint32_t);
void mach64_render_tri_fan    (gld_context *, const void *, uint32_t);
void mach64_render_tri_fan_slot(gld_context *, const void *, const void *, uint32_t);
void mach64_render_polygon    (gld_context *, const void *, uint32_t);
void mach64_render_quads      (gld_context *, const void *, uint32_t);
void mach64_render_quad_strip (gld_context *, const void *, uint32_t);
void mach64_dispatch_complete (gld_context *, const void *, uint32_t);
void mach64_dispatch_render_ctl(gld_context *, uint32_t, uint32_t, uint32_t, uint32_t, uint32_t);
int  mach64_flush(gld_context *, uint32_t, uint32_t, uint32_t, uint32_t, uint32_t, uint32_t);
int  mach64_ctl00(gld_context *, uint32_t, uint32_t, uint32_t, uint32_t, uint32_t, uint32_t);
/* table[0x08] is GLEngine's per-renderer glReadPixels READBACK hook (gc+0x78b0): called
 * (rb_ctx, x, y, w, h, format, type, pixels).  NOT the ctl-stub shape. */
int  mach64_ctl08(void *rb_ctx, int x, int y, int w, int h,
                  unsigned format, unsigned type, void *pixels);
extern gld_context *g_gld_readback_ctx;   /* current context for the readback hook (set in gldInitDispatch) */
extern unsigned char *g_gld_gc;           /* GLEngine's gc (= dispatch table - 0x78a8); holds GL_PACK_* state */
int  mach64_ctl0c(gld_context *, uint32_t, uint32_t, uint32_t, uint32_t, uint32_t, uint32_t);
int  mach64_ctl10(gld_context *, uint32_t, uint32_t, uint32_t, uint32_t, uint32_t, uint32_t);
int  mach64_ctl14(gld_context *, uint32_t, uint32_t, uint32_t, uint32_t, uint32_t, uint32_t);

int gldUpdateDispatch(gld_context *ctx, void *table, uint32_t flags);

/* ---- core entry points the framework requires (gld_entry.c) ---------
 * Recovered signatures / GL conventions; refined empirically via gld_log. */
int  gldAttachDrawable(gld_context *ctx, int type, void *drawable, uint32_t flags);
const char *gldGetString(gld_context *ctx, uint32_t name);
int  gldGetError(gld_context *ctx);
int  gldGetVersion(int *v0, int *v1, int *v2, int *v3);  /* the discovery gate */
int  gldGetInteger(gld_context *ctx, uint32_t pname, int *params);
int  gldSetInteger(gld_context *ctx, uint32_t pname, int value);
void gldInitDispatch(gld_context *ctx, void *table, void *outinfo);
int  gldFlushBuffer(gld_context *ctx, void *drawable);

/* diagnostic log -> /tmp/mach64gld.log, so we can watch how OpenGL.framework
 * drives the bundle when it is loaded into a real GL app. */
void gld_log(const char *fmt, ...);

/* ---- The gld* entry points (subset; full list in ABI.md) ------------ */
void *gldInitializeLibrary(void *iface, void *a2, void *a3, void *a4, void *a5);
void gldTerminateLibrary(void);
int  gldGetRendererInfo(gld_renderer_info *out, uint32_t renderer_mask);
int  gldChoosePixelFormat(void **pf, const int *attribs);
int  gldDestroyPixelFormat(void *pf);
int  gldCreateContext(gld_context **out, void *pixfmt, void *shared,
                      void *share_io, void *a7, void *a8, void *a9);
int  gldDestroyContext(gld_context *ctx);
/* gldUpdateDispatch declared above with the recovered 3-arg signature.    */
/* vertex path -> new_vertex_buffer/delete_vertex_buffer selectors        */
void *gldAllocVertexBuffer(gld_context *ctx, uint32_t size);
int   gldCompleteVertexBuffer(gld_context *ctx, void *vb);
void  gldFreeVertexBuffer(gld_context *ctx, void *vb);
int   gldFlush(gld_context *ctx);
int   gldFinish(gld_context *ctx);

#endif /* GLD_H */

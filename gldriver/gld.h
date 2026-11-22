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

#include <stdlib.h>
#include <stdint.h>
#include <IOKit/IOKitLib.h>   /* io_connect_t + IOConnectMethod* (user side) */
#include "../common/mach64_emit.h"

/* Single-thread engine (gld_engine.c): while it holds Apple's accelerator mutex across calls, NO call may reach Apple's kext (it would take the same mutex and deadlock), so every
 * IOKit call the driver makes goes through the guard, which drains the engine and releases the mutex first when needed.  Calls on the Path A connection are exempt. */
/* (int)(f * 2^sh) truncating toward zero, integer-only and bit-identical to the FPU cast (fctiwz saturation: overflow/NaN -> INT_MAX / INT_MIN like the hardware).  sh is a compile-time
 * constant.  Measured on the MDD: 9.7 ns vs 27 ns for (int)(f*4.0f) (the FPU path's store->load round trip). */
static inline __attribute__((always_inline)) int m64_f2i(float f, int sh)
{
    union { float f; uint32_t u; } x; int e; uint32_t m, r; x.f = f;
    e = (int)((x.u >> 23) & 255u) - 127 + sh;
    if (e < 0) return 0;
    if (e >= 31) return ((x.u & 0x7fffffffu) > 0x7f800000u || (int32_t)x.u < 0) ? (int)0x80000000u : 0x7fffffff;
    m = (x.u & 0x7fffffu) | 0x800000u;
    r = e >= 23 ? m << (e - 23) : m >> (23 - e);
    return (int32_t)x.u < 0 ? -(int)r : (int)r;
}
/* float [0,1] -> byte 0..255, integer-only on the float's bits (== floor(f*255+0.5) clamped, evaluated exactly).  The FPU version (fmul, fadd, fctiwz, store, load) is ~3x slower on a G4. */
static inline __attribute__((always_inline)) uint32_t m64_f2u8(float f)
{
    union { float f; uint32_t u; } x; uint32_t e, m, v;
    x.f = f;
    if ((int32_t)x.u <= 0) return 0u;                                   /* negative, -0, 0 */
    e = x.u >> 23;
    if (e >= 127u) return (e == 255u && (x.u & 0x7fffffu)) ? 0u : 255u; /* >= 1.0 (NaN -> 0 as the old cast did) */
    if (e < 118u) return 0u;                                            /* < 2^-9 */
    m = (x.u & 0x7fffffu) | 0x800000u; v = m * 255u;                    /* 255f = v * 2^-(150-e) */
    if (e == 118u) return v >> 31;
    return ((v >> 1) + (1u << (148u - e))) >> (149u - e);
}
extern void mach64_eng_guard(io_connect_t c);
#ifndef GLD_NO_ENG_GUARD
#define IOConnectMethodScalarIScalarO(c, ...)     (mach64_eng_guard(c), IOConnectMethodScalarIScalarO(c, __VA_ARGS__))
#define IOConnectMethodScalarIStructureI(c, ...)  (mach64_eng_guard(c), IOConnectMethodScalarIStructureI(c, __VA_ARGS__))
#define IOConnectMethodStructureIStructureO(c, ...) (mach64_eng_guard(c), IOConnectMethodStructureIStructureO(c, __VA_ARGS__))
#define IOConnectMethodScalarIStructureO(c, ...)  (mach64_eng_guard(c), IOConnectMethodScalarIStructureO(c, __VA_ARGS__))
#define IOConnectMapMemory(c, ...)                (mach64_eng_guard(c), IOConnectMapMemory(c, __VA_ARGS__))
#define IOConnectUnmapMemory(c, ...)              (mach64_eng_guard(c), IOConnectUnmapMemory(c, __VA_ARGS__))
#define IOServiceOpen(s, ...)                     (mach64_eng_guard(MACH_PORT_NULL), IOServiceOpen(s, __VA_ARGS__))
#define IOServiceClose(c)                         (mach64_eng_guard(c), IOServiceClose(c))
#endif

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
    uint32_t   z_clear_pending;    /* 1 => this frame's Z clear is still owed; done on the first draw that has Z_EN (the frame's first draws often run with depth off) */
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
    /* ---- windowed mode: win_mode=1 => render the w x h scene into the top-left of the back buffer
     * and BLIT it to the LIVE front buffer at (win_x,win_y) on present (no CRTC page-flip), so the
     * desktop stays visible and the WindowServer keeps compositing. */
    uint32_t   win_mode;           /* 1 => windowed blit present into the live desktop */
    uint32_t   win_no_blit;        /* 1 => our own back->front blit is NOT used: the front geometry is unknown or its pixel depth differs from the render buffer; Apple's surface_flush presents */
    uint32_t   win_x, win_y;       /* window content origin in screen pixels (top-left) */
    uint32_t   cgs_win, cgs_surf;  /* CGS window/surface ids of the attached drawable (descriptor d[1]/d[2]) */
    uint32_t   off_display;        /* 1 = the drawable's window is on ANOTHER display (not the Rage): this context must not touch Apple's surfaces or the Rage scanout */
    uint32_t   vis_hidden;         /* 1 = completely covered by windows in front: skip the present */
    uint32_t   vis_sig, vis_tick;  /* occlusion tracking: signature of the last visible-rect list applied, present counter */
    uint32_t   win_buf_off;        /* per-context windowed back+Z buffer (0 = none/shared); freed on
                                    * detach.  Lets multiple windowed contexts render independently. */
    uint32_t   win_buf_size;       /* size of that pool allocation, for the free */
    /* ---- Path B port (GLD_APPLE_PROBE): live connections to Apple's ATIRagePro kext ---- */
    unsigned int apple_cn;         /* io_connect_t to Apple's type-1 GL user client (0=none) */
    unsigned int apple_surf_cn;    /* io_connect_t to Apple's type-2 IOAccelSurface client (0=none) */
    uint32_t   present_unsafe;     /* 1 = bound surface is display-associated (surf[0x178]!=0xffff) ->
                                    * its present DST aliases the live scanout -> would race IONDRV ->
                                    * skip prime + present (setup_target/render_ctl) to avoid a panic */
    uint32_t   render_skip;            /* 1 => bound surface backing is INVALID (rt=0, e.g. a full-screen
                                        * 1024x768 surface that didn't fit 8MB VRAM) -> SKIP all geometry/
                                        * clears/present so we never render into live/colliding VRAM (panic).
                                        * Fail-to-black, stable.  Real fix = shrink the fullscreen backing. */
    /* ---- Path-B VRAM allocator records (from the sel-10 op=ALLOC kext path); opaque kernel handles,
     * freed on detach/destroy so VRAM isn't leaked.  0 = allocated via the small-window guess. */
    uint32_t   z_record;           /* Apple-allocator record for the Z buffer   */
    uint32_t   tex_record;         /* Apple-allocator record for the texture heap */
    /* ---- texture state (first cut: one bound 2D texture) -------------- */
    void      *tex_obj;            /* bound/uploaded texobj (t[0x30]=state) */
    uint32_t   tex_off;            /* texel VRAM byte offset (0 = none up)  */
    uint32_t   tex_w, tex_h;       /* texel dimensions                     */
    uint32_t   tex2_w, tex2_h;     /* secondary (unit-1) texel dimensions when m64_state.multitex */
    const void *tex_slot;          /* bound texture's slot (gld_texture.c), for per-triangle LOD */
    int        tex_lvl;            /* mip level currently bound as the base (0 = full size)     */
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
    uint32_t   batchbuf2[32768];   /* second batch buffer: while the DMA worker runs one, the game fills the other  */
    uint32_t   batchbuf3[32768], batchbuf4[32768];   /* ring of 4 batch buffers: the game keeps filling one while the DMA worker drains the others */
    int        batch_sel;          /* which of the four batch buffers p->batch is built in */
    uint32_t   cbpp;               /* colour-buffer bytes per pixel: 2 (RGB565) or 4 (ARGB8888, default 0 = 4) */
} mach64_priv;

/* Colour-target pixel size.  16-bpp contexts render to RGB565 (dithered by the 3D engine); everything below the 3D pipe (fills, blits, CPU pixel paths, VRAM span checks)
 * keys off these.  The depth buffer is always 16-bit and is NOT affected. */
#define M64_CBPP(p)     (((p)->cbpp == 2u) ? 2u : 4u)
#define M64_PIXW(p)     (((p)->cbpp == 2u) ? 0x60040464u : 0x60060666u)   /* DP_PIX_WIDTH: RGB565 | ARGB8888 */
static inline uint32_t m64_pack_cpix(const mach64_priv *p, uint32_t argb)
{
    if (p->cbpp != 2u) return argb;
    return (((argb >> 16) & 0xffu) >> 3 << 11) | (((argb >> 8) & 0xffu) >> 2 << 5) | ((argb & 0xffu) >> 3);
}
void mach64_set_cbpp(gld_context *ctx, uint32_t bytes_per_pixel);

static __inline__ mach64_priv *MACH64_PRIV(gld_context *c)
{ return (mach64_priv *)c->backend; }

/* ---- state translation (gld_state.c): GL state -> m64_state ---------- */
void mach64_init_state(mach64_priv *p);
void mach64_set_render_target(gld_context *ctx, uint32_t off, uint32_t pitch_px);
void mach64_blit_2pitch(gld_context *ctx, uint32_t src_off, uint32_t src_pitch_px,   /* stock-ICD windowed present */
                        uint32_t dst_off, uint32_t dst_pitch_px,
                        uint32_t sx, uint32_t sy, uint32_t dx, uint32_t dy,
                        uint32_t w, uint32_t h);
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
/* Kernel-memory peek through the patched ATIRagePro's selector (flag 0x80000000 | kernel address).  The kext dereferences the address
 * UNCHECKED, so a stale/garbage pointer is a kernel panic: every peek goes through here, which refuses addresses outside the range
 * where kalloc'd driver objects live (0x10000 .. M64_KADDR_MAX) or not 4-byte aligned.
 * Returns the selector's result, or -1 (and *out = 0) when refused. */
/* Kernel pointers are VIRTUAL: xnu-517 (Panther) osfmk/mach/ppc/vm_param.h has VM_MIN_KERNEL_ADDRESS 0x1000 .. VM_MAX_KERNEL_ADDRESS
 * 0xDFFFFFFF regardless of RAM size (the G4 has 2GB; objects seen so far 0x2bfa000..0x3260fa00). */
#define M64_KADDR_MAX 0xDFFFFFFFu
/* User-space pointers the plugin dereferences directly (GLEngine's gli/texture objects, app texel buffers).  xnu-517 (Panther)
 * osfmk/mach/ppc/vm_param.h: VM_MAX_ADDRESS 0xFFFFFFFF, USER_STACK_END 0xffff0000.  Sanity bound only (a bad user pointer is an app
 * crash, not a kernel panic); the old 1GB cap silently dropped any texture whose data sat above it (stack buffers, big mmaps). */
#define M64_UADDR_MAX 0xFFFEFFFFu
int m64_peek(io_connect_t conn, int selector, uint32_t kaddr, uint32_t *out);
/* Windowed per-context back+Z VRAM pool (gld_texture.c): lazily init the region once, then
 * alloc/free window-sized buffers so multiple windowed contexts render independently. */
void     mach64_winpool_init(uint32_t base, uint32_t end);
uint32_t mach64_winbuf_alloc(uint32_t bytes);   /* 0 = pool exhausted */
void     mach64_winbuf_free(uint32_t off, uint32_t bytes);
void mach64_present(gld_context *ctx);   /* double-buffer back->front swap */
int  mach64_vis_update(gld_context *ctx, int force);   /* clip the surface shape to the window's visible region; 1 = shape changed */
void mach64_pathb_present(gld_context *ctx);   /* Path B: present via Apple's surface_flush */
void mach64_pathb_setup_target(gld_context *ctx); /* Path B: prime surface + set render target off */
uint32_t mach64_vram_alloc(gld_context *ctx, uint32_t size, uint32_t *record_out); /* Path B: Apple VRAM allocator */
void mach64_vram_free(gld_context *ctx, uint32_t record);
void mach64_flip(gld_context *ctx, uint32_t off);   /* page-flip CRTC scanout to `off` */
void mach64_wait_flip(gld_context *ctx);            /* block until the pending flip retired */
void mach64_sync_depth_from_gli(gld_context *ctx);
/* Read a gli field (0 if the gli can't be located).  The fast path (cached gli for this context, still valid) is inline: bind_texture / state sync read ~100 fields per primitive and the
 * out-of-line call + re-validation per field was ~3% of the main thread in the race profile. */
extern gld_context *g_gli_ctx; extern uint32_t g_gli_p10, g_gli_g;
uint32_t mach64_gli_u32_slow(gld_context *ctx, uint32_t off);
static inline __attribute__((always_inline)) uint32_t mach64_gli_u32(gld_context *ctx, uint32_t off)
{
    uint32_t g = g_gli_g;
    if (g_gli_ctx == ctx && g_gli_p10 == *(const uint32_t *)((const char *)ctx + 0x10) && g && *(const uint32_t *)(uintptr_t)(g + 0x7898) == (uint32_t)(uintptr_t)ctx)
        return *(const uint32_t *)(uintptr_t)(g + off);
    return mach64_gli_u32_slow(ctx, off);
}
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
void mach64_tex_select_lod(gld_context *ctx, float pix_area, float uv_area);   /* per-triangle mip level + min/mag filter */

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
extern int mach64_spec_mode;   /* GL_SEPARATE_SPECULAR_COLOR: 0 off, 1 hardware (textured: ALPHA_TST_CNTL SPECULAR_LIGHT_EN + SPEC_ARGB rgb), 2 folded into the vertex colour (untextured) */
extern uint32_t mach64_tex_vofs[2];   /* gli-vertex byte offset of S/w,T/w for the primary / secondary texture unit (secondary 0 = single texture); set per draw by mach64_bind_texture */
extern int mach64_flat_on;   /* glShadeModel(GL_FLAT): replicate the provoking vertex colour */
/* Resolve a windowed drawable's CGS window id (descriptor d[1]) + surface id (d[2]) -> on-screen
 * content bounds via CGS (gld_cgs.c).  Returns 1 + fills x/y/w/h (screen px), 0 if unresolved. */
int mach64_cgs_resolve_bounds(uint32_t window_id, uint32_t surface_id, int allow_enum,
                              int *x, int *y, int *w, int *h);
int mach64_display_bytes_per_pixel(void);   /* bytes per pixel of the Rage's display (2 for 15/16 bpp, 4 for 32); 0 if unknown */
int mach64_display_size(int *w, int *h);   /* pixel size of the display this driver scans out (the Rage's; main display if it cannot be identified) -- fullscreen surface shaping */

#ifdef GLD_APPLE_PROBE
unsigned int mach64_apple_glctx_open(void);   /* open Apple's type-1 GL client, return connection */
int mach64_cgs_off_display(void);   /* last mach64_cgs_resolve_bounds: window found, but on a display this driver does not scan out */
int mach64_cgs_visible_rects(uint32_t window_id, uint32_t surface_id, int lx, int ly, int w, int h, short (*out)[4], int maxr);
void mach64_apple_reshape(unsigned int cn, const short (*r)[4], int n, int x, int y, int w, int h);
unsigned int mach64_apple_make_surface(uint32_t surface_id, uint32_t mode, uint32_t x, uint32_t y,
                                       uint32_t w, uint32_t h); /* type-0 + set_id_mode + set_shape (shape placed at the display-local x,y) */
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
 * Recovered signatures / GL conventions. */
int  gldAttachDrawable(gld_context *ctx, int type, void *drawable, uint32_t flags);
const char *gldGetString(gld_context *ctx, uint32_t name);
int  gldGetError(gld_context *ctx);
int  gldGetVersion(int *v0, int *v1, int *v2, int *v3);  /* the discovery gate */
int  gldGetInteger(gld_context *ctx, uint32_t pname, int *params);
int  gldSetInteger(gld_context *ctx, uint32_t pname, int value);
void gldInitDispatch(gld_context *ctx, void *table, void *outinfo);
int  gldFlushBuffer(gld_context *ctx, void *drawable);

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

/*
 * mach64_icd.c - mach64 (Rage Mobility-P) hardware backend for the drop-in
 * opengl32.dll.  Layered onto WMesa's GL context (from wgl.c): on wglMakeCurrent
 * we install the tnl render hooks + a hardware glClear so transformed geometry is
 * emitted to the GPU via common/mach64_emit -> mach64_winsys (kmd bus-master DMA),
 * exactly the path proven standalone in mach64_gl.c.  On wglSwapBuffers we present
 * the window-sized 32bpp VRAM target to the visible RGB565 scanout.
 *
 * If the kmd driver is not loaded (m64ws_open fails) every entry point is a no-op
 * that returns "not handled", so wgl.c falls back to the software WMesa path and
 * the DLL still works with no hardware helper.
 *
 * Generalised from mach64_gl.c: per-window target size + pitch, the app's real
 * glViewport, mid-batch DMA flushing (a submit holds ~1000 dwords ~= 37 tris), and
 * a windowed present that reads the LIVE CRTC pitch (the game ChangeDisplaySettings
 * away from the 1024x768 desktop).
 */
#include "main/glheader.h"
#include "main/mtypes.h"
#include "main/context.h"
#include "main/texformat.h"
#include "tnl/t_context.h"
#include "tnl/t_vertex.h"  /* GET_VERTEX_STATE, _tnl_get_attr (read built vertices) */
#include "GL/gl.h"
#include "wmesadef.h"      /* struct wmesa_framebuffer: back-buffer DIB (pbPixels) */
#include "mach64_emit.h"
#include "mach64_winsys.h"

/* ---------------------------------------------------------------- winsys */
static m64_winsys *g_ws;
static int g_probed, g_have;

int m64icd_available(void)
{
    if (!g_probed) { g_probed = 1; g_ws = m64ws_open(); g_have = g_ws ? 1 : 0; }
    return g_have;
}

/* --- crash-forensic log (append+flush per line so it survives a fault) --- */
__declspec(dllimport) int WINAPIV wsprintfA(char*,const char*,...);
static int g_logn;
static void icdlog(const char *fmt, unsigned a, unsigned b, unsigned c, unsigned d, unsigned e, unsigned f)
{
    char buf[192]; DWORD wr;
    HANDLE h = CreateFileA("C:\\m64icd.log", GENERIC_WRITE, FILE_SHARE_READ|FILE_SHARE_WRITE,
                           0, OPEN_ALWAYS, FILE_ATTRIBUTE_NORMAL, 0);
    if (h==INVALID_HANDLE_VALUE) return;
    SetFilePointer(h, 0, 0, FILE_END);
    wsprintfA(buf, fmt, a, b, c, d, e, f);
    WriteFile(h, buf, lstrlenA(buf), &wr, 0);
    CloseHandle(h);
}

/* --- debug exports (for hwicdtest): read VRAM / query the target via the DLL's
 * own winsys handle (the kmd device opens exclusive, so a test can't open its
 * own second handle while the DLL holds one). --- */
int m64icd_read_vram(unsigned off, void *dst, unsigned len)
{ if (!g_have || !g_ws) return 1; return m64ws_read_vram(g_ws, off, dst, len); }

/* ---------------------------------------------------------------- state */
#define FBBASE 0x00400000u              /* VRAM target base (above the 1024x768
                                         * scanout ~0x180000; ATI's desktop lives low) */
#define Z_EN      (1u<<0)
#define Z_MASK_EN (1u<<8)
#define TEX_TMP   0x00700000u           /* scratch VRAM for uploaded texels */

static m64_state  g_st;
static m64_cmdbuf g_cb;
static uint32_t   g_cbs[1024];          /* one submit's worth (<=4092 bytes) */
#define CB_FLUSH_AT 980                 /* leave headroom for a tri + terminator */

static int      g_active;               /* hooks installed + target sized */
static int      g_W, g_H;               /* target (window client) size */
static unsigned g_pitch;                /* target pitch in pixels (8-aligned) */
static unsigned g_fboff, g_zoff;        /* VRAM byte offsets */
static int      g_textured;
static unsigned g_clearcol;             /* last glClear colour, 0xRRGGBB (composite key) */
static unsigned g_ntris, g_nquads, g_cleared;   /* per-frame diagnostics */
/* viewport captured per-batch from the GL context */
static float    g_vpx, g_vpy, g_vpw, g_vph;

static void (*g_orig_update)(GLcontext *, GLbitfield);
static void (*g_orig_start)(GLcontext *);      /* swsetup RenderStart (build/clip setup) */
static void (*g_orig_finish)(GLcontext *);     /* swsetup RenderFinish */

static uint32_t clampb(float f){ int c=(int)(f*255.0f+0.5f); return c<0?0:(c>255?255:(uint32_t)c); }
static uint32_t packcol(const GLfloat *c){
    return (clampb(c[3])<<24)|(clampb(c[0])<<16)|(clampb(c[1])<<8)|clampb(c[2]);
}
static unsigned z_testfunc(GLenum f){
    switch(f){ case GL_NEVER:return 0u<<4; case GL_LESS:return 1u<<4; case GL_LEQUAL:return 2u<<4;
    case GL_EQUAL:return 3u<<4; case GL_GEQUAL:return 4u<<4; case GL_GREATER:return 5u<<4;
    case GL_NOTEQUAL:return 6u<<4; default:return 7u<<4; }
}

static const struct gl_texture_format *hw_choose_texformat(GLcontext *ctx, GLint i, GLenum f, GLenum t)
{ (void)ctx;(void)i;(void)f;(void)t; return &_mesa_texformat_argb8888; }

/* -------------------------------------------------------------- render hooks */
static void hw_triangle(GLcontext *ctx, GLuint e0, GLuint e1, GLuint e2);  /* fwd */
static void hw_quad(GLcontext *ctx, GLuint e0, GLuint e1, GLuint e2, GLuint e3);

static void hw_start(GLcontext *ctx)
{
    /* Let swsetup build the clipspace vertex format + choose funcs first, so
     * Mesa's clip/interp path is valid (fixes the __tnl_generic_interp NULL),
     * then override the primitive emitters with ours. */
    if (g_orig_start) g_orig_start(ctx);
    { TNLcontext *tnl = TNL_CONTEXT(ctx);
      tnl->Driver.Render.Triangle = hw_triangle;
      tnl->Driver.Render.Quad     = hw_quad; }

    /* capture the app viewport for get_vert's NDC->window map */
    g_vpx=(float)ctx->Viewport.X;  g_vpy=(float)ctx->Viewport.Y;
    g_vpw=(float)ctx->Viewport.Width; g_vph=(float)ctx->Viewport.Height;
    if (g_vpw<=0) g_vpw=(float)g_W;  if (g_vph<=0) g_vph=(float)g_H;

    /* per-batch texture */
    struct gl_texture_unit *u = &ctx->Texture.Unit[0];
    g_textured = 0; g_st.textured = 0;
    if (u->_ReallyEnabled & TEXTURE_2D_BIT) {
        struct gl_texture_object *to = u->_Current;
        struct gl_texture_image  *im = to ? to->Image[0][to->BaseLevel] : 0;
        if (im && im->Data) {
            int wl=im->WidthLog2, hl=im->HeightLog2;
            m64ws_write_vram(g_ws, TEX_TMP, im->Data, im->Width*im->Height*4);
            g_st.textured=1; g_st.tex_0_off=TEX_TMP;
            g_st.tex_size_pitch=(wl)|((wl>hl?wl:hl)<<4)|(hl<<8);
            g_st.tex_maxlog2=(wl>hl?wl:hl); g_st.tex_mip=0;
            g_st.tex_cntl=(1u<<9)|(1u<<23)|(1u<<30)|(1u<<17)|(1u<<18);
            g_st.tex_scale_3d=0x0141009bu;
            g_textured=1;
        }
    }
    /* per-batch depth */
    if (ctx->Depth.Test) {
        g_st.z_cntl = Z_EN | z_testfunc(ctx->Depth.Func) | (ctx->Depth.Mask ? Z_MASK_EN : 0);
        g_st.z_off_pitch = ((g_pitch/8)<<22)|(g_zoff>>3);
    } else { g_st.z_cntl = 0; g_st.z_off_pitch = 0; }

    m64_cb_init(&g_cb, g_cbs, 1024);
    m64_emit_state(&g_cb, &g_st);
}
static void hw_flush_batch(void){ m64_emit_terminator(&g_cb); m64ws_submit(g_ws,&g_cb); }
static void hw_finish(GLcontext *ctx){ hw_flush_batch(); if (g_orig_finish) g_orig_finish(ctx); }

/* Read a BUILT clipspace vertex by index (valid for original AND clip-generated
 * vertices, since Mesa builds+interpolates them).  _tnl_get_attr(POS) returns NDC
 * x,y,z (viewport reversed) and pos[3]=w. */
static void get_vert(GLcontext *ctx, GLuint e, m64_vertex *out)
{
    struct tnl_clipspace *vtx = GET_VERTEX_STATE(ctx);
    const GLubyte *v = vtx->vertex_buf + (size_t)e * vtx->vertex_size;
    GLfloat pos[4]={0,0,0,1}, col[4]={1,1,1,1}, tex[4]={0,0,0,1};
    _tnl_get_attr(ctx, v, _TNL_ATTRIB_POS,    pos);
    _tnl_get_attr(ctx, v, _TNL_ATTRIB_COLOR0, col);
    float w = (pos[3]!=0.0f)?pos[3]:1.0f;
    float wx = g_vpx + (pos[0]*0.5f+0.5f)*g_vpw;
    /* GL is bottom-up; store the target TOP-DOWN so present is a 1:1 screen copy. */
    float wy = (float)g_H - (g_vpy + (pos[1]*0.5f+0.5f)*g_vph);
    uint32_t wb; __builtin_memcpy(&wb,&w,4);
    out->s=out->t=out->z=out->spec_argb=0;
    out->x_y = (((int)(wx*4.0f))<<16) | (((int)(wy*4.0f))&0xffff);
    out->w = (int32_t)wb;
    out->argb = packcol(col);
    { float wz=pos[2]*0.5f+0.5f; int zi; if(wz<0)wz=0; if(wz>1)wz=1; zi=(int)(wz*65535.0f+0.5f);
      out->z=(int32_t)((uint32_t)zi<<15); }
    if (g_textured) {
        _tnl_get_attr(ctx, v, _TNL_ATTRIB_TEX0, tex);
        __builtin_memcpy(&out->s,&tex[0],4); __builtin_memcpy(&out->t,&tex[1],4);
    }
}

static void hw_triangle(GLcontext *ctx, GLuint e0, GLuint e1, GLuint e2)
{
    m64_vertex v[3];
    if (g_cb.n >= CB_FLUSH_AT) {                 /* submit fills up in a big batch */
        hw_flush_batch();
        m64_cb_init(&g_cb, g_cbs, 1024);
        m64_emit_state(&g_cb, &g_st);
    }
    g_ntris++;
    get_vert(ctx,e0,&v[0]); get_vert(ctx,e1,&v[1]); get_vert(ctx,e2,&v[2]);
    { int x0=(short)(v[0].x_y>>16),y0=(short)(v[0].x_y&0xffff);
      int x1=(short)(v[1].x_y>>16),y1=(short)(v[1].x_y&0xffff);
      int x2=(short)(v[2].x_y>>16),y2=(short)(v[2].x_y&0xffff);
      long area=(long)(x0-x2)*(y1-y2)-(long)(y0-y2)*(x1-x2);
      if (area<0){ m64_vertex t=v[1]; v[1]=v[2]; v[2]=t; } }
    m64_emit_triangle(&g_cb,&v[0],&v[1],&v[2]);
}

static void hw_quad(GLcontext *ctx, GLuint e0, GLuint e1, GLuint e2, GLuint e3)
{ g_nquads++; hw_triangle(ctx,e0,e1,e2); hw_triangle(ctx,e0,e2,e3); }

/* Hardware glClear.  NOTE: Driver.Clear's mask uses Mesa's internal BUFFER_BIT_*
 * flags, NOT the GL_*_BUFFER_BIT tokens (that mismatch silently no-op'd the colour
 * clear in mach64_gl.c -- masked there because clear-black == stale-black). */
#define COLOR_BITS (BUFFER_BIT_FRONT_LEFT|BUFFER_BIT_BACK_LEFT)
static void hw_clear(GLcontext *ctx, GLbitfield buffers)
{
    if ((buffers & BUFFER_BIT_DEPTH) && g_zoff) {
        static unsigned short zb[2048]; unsigned i, rows=g_H;
        unsigned short zc=(unsigned short)(ctx->Depth.Clear*65535.0+0.5);
        for(i=0;i<g_pitch;i++) zb[i]=zc;
        for(i=0;i<rows;i++) m64ws_write_vram(g_ws, g_zoff + i*g_pitch*2, zb, g_pitch*2);
    }
    if (!(buffers & COLOR_BITS)) return;
    uint32_t col = packcol(ctx->Color.ClearColor);
    g_clearcol = col & 0xffffffu;
    g_cleared = 1;
    /* (1) clear the hardware VRAM target (CPU fill; ARGB8888) */
    { static uint32_t crow[2048]; unsigned i, rows=(unsigned)g_H;
      for(i=0;i<g_pitch;i++) crow[i]=col;
      for(i=0;i<rows;i++) m64ws_write_vram(g_ws, g_fboff + i*g_pitch*4u, crow, g_pitch*4u); }
    /* (2) clear WMesa's DIB to the same colour, so swrast 2D (DrawPixels/Bitmap)
     * starts on a known background and the composite key matches everywhere the
     * hardware didn't draw geometry. */
    if (ctx->DrawBuffer) {
        struct wmesa_framebuffer *fb = (struct wmesa_framebuffer *)ctx->DrawBuffer;
        unsigned char *dib = fb->pbPixels;
        if (dib) {
            int W=fb->Base.Width, H=fb->Base.Height, x; unsigned y, stride=fb->ScanWidth;
            unsigned r=(col>>16)&0xff, gg=(col>>8)&0xff, b=col&0xff;
            unsigned short c16 = (unsigned short)(((r>>3)<<10)|((gg>>3)<<5)|(b>>3));
            for (y=0;y<(unsigned)H;y++){ unsigned char *d=dib+y*stride;
                if (fb->cColorBits==32){ for(x=0;x<W;x++) *(unsigned*)(d+x*4)=col&0xffffff; }
                else { unsigned short *dw=(unsigned short*)d; for(x=0;x<W;x++) dw[x]=c16; } }
        }
    }
}

static void install_render_hooks(GLcontext *ctx)
{
    TNLcontext *tnl = TNL_CONTEXT(ctx);
    /* Wrap swsetup's Start/Finish (capture the originals once, not our wrappers)
     * so its vertex-build + clip/interp setup keeps running; Triangle/Quad are
     * overridden inside hw_start after swsetup chooses its funcs.  Leave
     * BuildVertices/Interp/CopyPV/ClippedPolygon as swsetup installed them. */
    if (tnl->Driver.Render.Start  != hw_start)  g_orig_start  = tnl->Driver.Render.Start;
    if (tnl->Driver.Render.Finish != hw_finish) g_orig_finish = tnl->Driver.Render.Finish;
    tnl->Driver.Render.Start  = hw_start;
    tnl->Driver.Render.Finish = hw_finish;
}
static void hw_update_state(GLcontext *ctx, GLbitfield new_state)
{
    if (g_orig_update) g_orig_update(ctx, new_state);
    install_render_hooks(ctx);
}

/* --------------------------------------------------------- WGL integration */

/* Called from wgl.c's wglMakeCurrent after WMesaMakeCurrent.  Sizes the VRAM
 * target to the window and installs the hardware backend on the current ctx. */
void m64icd_make_current(void *hdc_v)
{
    HDC  hdc  = (HDC)hdc_v;
    if (!m64icd_available() || !hdc) { g_active = 0; return; }
    GLcontext *ctx = _mesa_get_current_context();
    if (!ctx) { g_active = 0; return; }

    /* window client size */
    HWND hwnd = WindowFromDC(hdc);
    RECT rc; int W=0,H=0;
    if (hwnd && GetClientRect(hwnd,&rc)) { W=rc.right-rc.left; H=rc.bottom-rc.top; }
    if (W<=0||H<=0) { W=640; H=480; }              /* sane default */
    if (W>2048) W=2048; if (H>1536) H=1536;
    g_W=W; g_H=H;
    g_pitch=((unsigned)W + 7u) & ~7u;              /* 8-pixel aligned */
    g_fboff=FBBASE;
    { unsigned fbbytes=g_pitch*(unsigned)H*4u; g_zoff=(g_fboff+fbbytes+0xfffu)&~0xfffu; }

    /* base 3D state (gouraud, ARGB8888 target); per-batch bits set in hw_start */
    { unsigned char *p=(unsigned char*)&g_st; unsigned i; for(i=0;i<sizeof(g_st);i++) p[i]=0; }
    g_st.scale_3d_cntl=0x000100c1u; g_st.setup_cntl=0; g_st.z_cntl=0; g_st.z_off_pitch=0;
    g_st.dp_pix_width=0x60060666u; g_st.dst_off_pitch=((g_pitch/8)<<22)|(g_fboff>>3);
    g_st.sc_left_right=(0x1fffu<<16); g_st.sc_top_bottom=(0x1fffu<<16);
    g_st.dp_write_mask=0xffffffffu; g_st.clr_cmp_cntl=0;
    g_st.dp_mix=0x00070007u; g_st.dp_src=0x00000505u;

    /* install hooks (survive swsetup re-selection via the UpdateState wrap) */
    if (ctx->Driver.UpdateState != hw_update_state) {
        g_orig_update = ctx->Driver.UpdateState;
        ctx->Driver.UpdateState = hw_update_state;
    }
    ctx->Driver.Clear             = hw_clear;
    ctx->Driver.ChooseTextureFormat = hw_choose_texformat;
    install_render_hooks(ctx);
    g_active = 1;
    g_logn = 0;
    icdlog("MC W=%u H=%u pitch=%u fboff=0x%x\n", (unsigned)g_W,(unsigned)g_H,g_pitch,g_fboff,0,0);
}

/* Present: copy the hardware VRAM target into WMesa's back-buffer DIB, then
 * return 0 so wgl.c calls WMesaSwapBuffers -- its BitBlt(DIB->window) is the
 * proven present path (respects the game's window/flip management).  This avoids
 * writing the live scanout directly (which fought the game's double-buffering and
 * risked out-of-bounds VRAM writes).  Both target and DIB are TOP-DOWN.
 * DIB format (WMesa, BI_RGB): 32bpp = XRGB8888, 16bpp = 5-5-5 (BGR16). */
static unsigned char g_rb[1280*1024*4];            /* readback (<=~1.3M px) */
int m64icd_swap(void *hdc_v)
{
    (void)hdc_v;
    if (!g_active || !g_have) return 0;
    GLcontext *ctx = _mesa_get_current_context();
    if (!ctx || !ctx->DrawBuffer) return 0;
    struct wmesa_framebuffer *fb = (struct wmesa_framebuffer *)ctx->DrawBuffer;
    unsigned char *dib = fb->pbPixels;
    if (!dib) return 0;                            /* single-buffered: nothing to blit */

    int W = fb->Base.Width, H = fb->Base.Height;
    if (W > g_W) W = g_W;  if (H > g_H) H = g_H;
    unsigned rows = (unsigned)H;
    if (g_pitch*rows*4u > sizeof(g_rb)) rows = sizeof(g_rb)/(g_pitch*4u);
    m64ws_read_vram(g_ws, g_fboff, g_rb, g_pitch*rows*4u);   /* whole target, one call */

    /* Present the FULL hardware target (the game renders its whole frame -- 3D
     * world + textured-quad UI -- through our geometry hooks). No colour-keying:
     * the game runs gl_clear=0 (never colour-clears), so a black pixel is a real
     * scene colour, not "transparent" -- keying against it blacked out/flickered
     * dark areas. Direct copy of the target into the DIB, then BitBlt. */
    unsigned stride = fb->ScanWidth; int bpp = fb->cColorBits;
    unsigned y; int x;
    for (y=0; y<rows; y++) {
        unsigned char *s = g_rb + y*g_pitch*4u;
        unsigned char *d = dib + y*stride;
        if (bpp == 32) {
            for (x=0; x<W; x++) *(unsigned*)(d+x*4) = *(unsigned*)(s+x*4) & 0xffffff;
        } else {                                   /* 16bpp, 5-5-5 to match BGR16 */
            unsigned short *dw = (unsigned short*)d;
            for (x=0; x<W; x++){ unsigned px=*(unsigned*)(s+x*4);
                unsigned r=(px>>16)&0xff,gg=(px>>8)&0xff,b=px&0xff;
                dw[x]=(unsigned short)(((r>>3)<<10)|((gg>>3)<<5)|(b>>3)); }
        }
    }
    if (g_logn < 30) { g_logn++;
        unsigned ty=(unsigned)(H>1?H/2:0), tx=(unsigned)(W>1?W/2:0);
        unsigned tgt = *(unsigned*)(g_rb + ty*g_pitch*4u + tx*4u) & 0xffffff;
        unsigned dibc;
        if (bpp==32) dibc = *(unsigned*)(dib + ty*stride + tx*4u) & 0xffffff;
        else { unsigned short w=*(unsigned short*)(dib + ty*stride + tx*2u);
               dibc=(w<<8); }   /* rough 555->show */
        icdlog("F%u clr=%u tri=%u quad=%u tgt=%06x dib=%06x\n",
               g_logn, g_cleared, g_ntris, g_nquads, tgt, (unsigned)dibc); }
    g_cleared=0; g_ntris=0; g_nquads=0;
    return 0;   /* wgl.c now calls WMesaSwapBuffers -> BitBlt DIB to the window */
}

/* debug export: current target layout (for hwicdtest) */
void m64icd_target(unsigned *fboff, int *w, int *h, unsigned *pitch)
{ if(fboff)*fboff=g_fboff; if(w)*w=g_W; if(h)*h=g_H; if(pitch)*pitch=g_pitch; }

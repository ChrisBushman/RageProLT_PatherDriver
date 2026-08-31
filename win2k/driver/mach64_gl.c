/*
 * mach64_gl.c - T2 first hardware triangle through the full Mesa GL pipeline.
 *
 * Reuses OSMesa's context (proven in T1a) for the GL front-end + T&L, then overrides
 * the tnl render hooks so transformed triangles are emitted to the Rage Mobility-P
 * via common/mach64_emit -> mach64_winsys (kmd bus-master DMA), instead of swrast.
 * Renders off-screen to VRAM 0x400000 and verifies with m64ws_read_vram.
 *
 * This is the minimal proof of the Mesa->hardware path; the full driver later moves
 * these hooks into a proper mach64 context (see driver/README.md T2).
 */
#include "main/glheader.h"
#include "main/mtypes.h"
#include "main/context.h"
#include "tnl/t_context.h"
#include "main/texformat.h"
#include "GL/gl.h"
#include "GL/osmesa.h"
#include "mach64_emit.h"
#include "mach64_winsys.h"

/* Win32 output: windows.h comes in via glheader.h -> gl.h. Use wsprintfA (user32),
 * not sprintf, to avoid the UCRT stdio inlines. */
static HANDLE g_out;
static void outs(const char *s){ DWORD n; WriteFile(g_out, s, lstrlenA(s), &n, 0); }
static char g_b[256];
#define OUTF(...) do{ wsprintfA(g_b,__VA_ARGS__); outs(g_b);}while(0)

/* ---- hardware emit state (off-screen 32x32 @ 0x400000, gouraud) ---- */
#define FBOFF   0x400000u
#define VPW 32
#define VPH 32

#define TEXOFF 0x500000u                 /* uploaded texture texels in VRAM */
#define ZOFF   0x600000u                 /* 16-bit Z buffer (32x32x2)       */
#define Z_EN (1u<<0)
#define Z_MASK_EN (1u<<8)
static unsigned z_testfunc(GLenum f){   /* GL depth func -> Z_CNTL test field [6:4] */
    switch(f){ case GL_NEVER:return 0u<<4; case GL_LESS:return 1u<<4; case GL_LEQUAL:return 2u<<4;
    case GL_EQUAL:return 3u<<4; case GL_GEQUAL:return 4u<<4; case GL_GREATER:return 5u<<4;
    case GL_NOTEQUAL:return 6u<<4; default:return 7u<<4; }  /* GL_ALWAYS */
}
static m64_winsys *g_ws;
static m64_cmdbuf  g_cb;
static uint32_t    g_cbs[1024];
static m64_state   g_st;
static int         g_textured;           /* set per-batch in hw_start from GL state */

/* Force Mesa to store textures as ARGB8888 so we can upload them straight to VRAM. */
static const struct gl_texture_format *hw_choose_texformat(GLcontext *ctx, GLint intFmt, GLenum fmt, GLenum type)
{
    (void)ctx;(void)intFmt;(void)fmt;(void)type;
    return &_mesa_texformat_argb8888;
}

static uint32_t clampb(float f){ int c=(int)(f*255.0f+0.5f); return c<0?0:(c>255?255:(uint32_t)c); }
static uint32_t packcol(const GLfloat *c){
    return (clampb(c[3])<<24)|(clampb(c[0])<<16)|(clampb(c[1])<<8)|clampb(c[2]);
}

static void hw_triangle(GLcontext *ctx, GLuint e0, GLuint e1, GLuint e2);  /* fwd */

static void hw_start(GLcontext *ctx){
    /* per-batch: if GL_TEXTURE_2D is enabled, upload the bound texture + arm the
     * textured register state (proven values from gld_texture.c); else untextured. */
    struct gl_texture_unit *u = &ctx->Texture.Unit[0];
    g_textured = 0;
    g_st.textured = 0;
    if (u->_ReallyEnabled & TEXTURE_2D_BIT) {
        struct gl_texture_object *to = u->_Current;
        struct gl_texture_image  *im = to ? to->Image[0][to->BaseLevel] : 0;
        if (im && im->Data) {
            int wl = im->WidthLog2, hl = im->HeightLog2;
            m64ws_write_vram(g_ws, TEXOFF, im->Data, im->Width * im->Height * 4);
            g_st.textured      = 1;
            g_st.tex_0_off     = TEXOFF;
            g_st.tex_size_pitch = (wl) | ((wl > hl ? wl : hl) << 4) | (hl << 8);
            g_st.tex_maxlog2   = (wl > hl ? wl : hl);
            g_st.tex_mip       = 0;
            g_st.tex_cntl      = (1u<<9)|(1u<<23)|(1u<<30)|(1u<<17)|(1u<<18);  /* +clamp */
            g_st.tex_scale_3d  = 0x0141009bu;                 /* FCN_TEXTURE, MODULATE */
            g_textured = 1;
        }
    }
    /* depth state from GL */
    if (ctx->Depth.Test) {
        g_st.z_cntl = Z_EN | z_testfunc(ctx->Depth.Func) | (ctx->Depth.Mask ? Z_MASK_EN : 0);
        g_st.z_off_pitch = ((32/8)<<22)|(ZOFF>>3);
    } else { g_st.z_cntl = 0; g_st.z_off_pitch = 0; }
    m64_cb_init(&g_cb, g_cbs, 1024);
    m64_emit_state(&g_cb, &g_st);
}
static void hw_finish(GLcontext *ctx){
    (void)ctx;
    m64_emit_terminator(&g_cb);
    m64ws_submit(g_ws, &g_cb);
}
static void hw_build(GLcontext *ctx, GLuint s, GLuint c, GLuint f){ (void)ctx;(void)s;(void)c;(void)f; }

/* Hardware glClear: 2D solid fill of the render target (the proven fill sequence). */
static void hw_clear(GLcontext *ctx, GLbitfield buffers)
{
    /* Z clear: CPU-fill the 16-bit Z buffer to the GL clear-depth value. */
    if (buffers & GL_DEPTH_BUFFER_BIT) {
        static unsigned short zb[32*32]; unsigned i;
        unsigned short zc = (unsigned short)(ctx->Depth.Clear * 65535.0 + 0.5);
        for (i=0;i<32*32;i++) zb[i]=zc;
        m64ws_write_vram(g_ws, ZOFF, zb, sizeof(zb));
    }
    if (!(buffers & GL_COLOR_BUFFER_BIT)) return;
    uint32_t col = packcol(ctx->Color.ClearColor);
    m64_cmdbuf cb; static uint32_t cbs2[64]; m64_cb_init(&cb, cbs2, 64);
    m64_reg(&cb, 0x054c, 0);                    /* Z_CNTL          */
    m64_reg(&cb, 0x05fc, 0);                    /* SCALE_3D_CNTL   */
    m64_reg(&cb, 0x0730, 0x3);                  /* GUI_TRAJ_CNTL   */
    m64_reg(&cb, 0x06d0, 0x60060666u);          /* DP_PIX_WIDTH    */
    m64_reg(&cb, 0x06c4, col & 0xffffff);       /* DP_FRGD_CLR     */
    m64_reg(&cb, 0x06c8, 0xffffffffu);          /* DP_WRITE_MASK   */
    m64_reg(&cb, 0x06d4, 0x00070003u);          /* DP_MIX          */
    m64_reg(&cb, 0x06d8, 0x00000101u);          /* DP_SRC          */
    m64_reg(&cb, 0x06a8, 0x1fff0000u);          /* SC_LEFT_RIGHT   */
    m64_reg(&cb, 0x06b4, 0x1fff0000u);          /* SC_TOP_BOTTOM   */
    m64_reg(&cb, 0x0708, 0);                    /* CLR_CMP_CNTL    */
    m64_reg(&cb, 0x0500, ((32/8)<<22)|(FBOFF>>3)); /* DST_OFF_PITCH */
    m64_reg(&cb, 0x06e8, 0);                    /* DST_X_Y         */
    m64_reg(&cb, 0x06ec, (32<<16)|32);          /* DST_WIDTH_HEIGHT (trigger) */
    m64_emit_terminator(&cb);
    m64ws_submit(g_ws, &cb);
}

/* Wrap Driver.UpdateState so our render hooks survive Mesa's per-validation
 * re-selection (swsetup resets tnl->Driver.Render.* on every state change). */
static void (*g_orig_update)(GLcontext *, GLbitfield);
static void install_render_hooks(GLcontext *ctx)
{
    TNLcontext *tnl = TNL_CONTEXT(ctx);
    tnl->Driver.Render.Start         = hw_start;
    tnl->Driver.Render.Finish        = hw_finish;
    tnl->Driver.Render.Triangle      = hw_triangle;
    tnl->Driver.Render.BuildVertices = hw_build;
}
static void hw_update_state(GLcontext *ctx, GLbitfield new_state)
{
    if (g_orig_update) g_orig_update(ctx, new_state);
    install_render_hooks(ctx);      /* reinstall after swsetup clobbers them */
}

static void get_vert(struct vertex_buffer *VB, GLuint e, m64_vertex *out){
    GLfloat *clip = (GLfloat*)((GLubyte*)VB->ClipPtr->data + (size_t)e*VB->ClipPtr->stride);
    GLfloat *col  = (GLfloat*)((GLubyte*)VB->ColorPtr[0]->data + (size_t)e*VB->ColorPtr[0]->stride);
    float w = clip[3], iw = (w != 0.0f) ? 1.0f/w : 1.0f;
    float wx = (clip[0]*iw*0.5f + 0.5f) * (float)VPW;         /* viewport 0,0,VPW,VPH */
    float wy = (clip[1]*iw*0.5f + 0.5f) * (float)VPH;
    float fby = wy;   /* off-screen target: keep GL orientation (readback-verified) */
    uint32_t wb; { float ww = w; __builtin_memcpy(&wb, &ww, 4); }
    out->s = out->t = out->z = out->spec_argb = 0;
    out->x_y = (((int)(wx*4.0f)) << 16) | (((int)(fby*4.0f)) & 0xffff);
    out->w = (int32_t)wb;
    out->argb = packcol(col);
    { float wz = clip[2]*iw*0.5f + 0.5f;               /* clip z -> window z [0,1] */
      int zi; if(wz<0)wz=0; if(wz>1)wz=1; zi=(int)(wz*65535.0f+0.5f);
      out->z = (int32_t)((uint32_t)zi << 15); }        /* 17.15 (harmless if Z off) */
    if (g_textured && VB->TexCoordPtr[0]) {   /* float s,t in [0,1] (gld_prims.c) */
        GLfloat *tc = (GLfloat*)((GLubyte*)VB->TexCoordPtr[0]->data + (size_t)e*VB->TexCoordPtr[0]->stride);
        __builtin_memcpy(&out->s, &tc[0], 4);
        __builtin_memcpy(&out->t, &tc[1], 4);
    }
}

static void hw_triangle(GLcontext *ctx, GLuint e0, GLuint e1, GLuint e2){
    struct vertex_buffer *VB = &TNL_CONTEXT(ctx)->vb;
    m64_vertex v[3];
    get_vert(VB, e0, &v[0]); get_vert(VB, e1, &v[1]); get_vert(VB, e2, &v[2]);
    /* mach64 setup engine culls area<0; force CCW-positive so the tri always draws */
    int x0=(short)(v[0].x_y>>16), y0=(short)(v[0].x_y&0xffff);
    int x1=(short)(v[1].x_y>>16), y1=(short)(v[1].x_y&0xffff);
    int x2=(short)(v[2].x_y>>16), y2=(short)(v[2].x_y&0xffff);
    long area = (long)(x0-x2)*(y1-y2) - (long)(y0-y2)*(x1-x2);
    if (area < 0) { m64_vertex t=v[1]; v[1]=v[2]; v[2]=t; }
    m64_emit_triangle(&g_cb, &v[0], &v[1], &v[2]);
}

int main(void)
{
    g_out = GetStdHandle((unsigned long)-11);   /* STD_OUTPUT_HANDLE */

    g_ws = m64ws_open();
    if (!g_ws) { outs("m64ws_open failed (mach64kmd not started?)\n"); ExitProcess(2); }

    /* hardware emit state: 32x32 gouraud target at VRAM 0x400000 */
    { unsigned char *p=(unsigned char*)&g_st; unsigned i; for(i=0;i<sizeof(g_st);i++) p[i]=0; }
    g_st.scale_3d_cntl=0x000100c1u; g_st.setup_cntl=0; g_st.z_cntl=0; g_st.z_off_pitch=0;
    g_st.dp_pix_width=0x60060666u; g_st.dst_off_pitch=((32/8)<<22)|(FBOFF>>3);
    g_st.sc_left_right=(0x1fffu<<16); g_st.sc_top_bottom=(0x1fffu<<16);
    g_st.dp_write_mask=0xffffffffu; g_st.clr_cmp_cntl=0; g_st.dp_mix=0x00070007u; g_st.dp_src=0x00000505u;

    static GLubyte osbuf[VPW*VPH*4];
    OSMesaContext osc = OSMesaCreateContext(OSMESA_RGBA, 0);
    if (!osc || !OSMesaMakeCurrent(osc, osbuf, GL_UNSIGNED_BYTE, VPW, VPH)) { outs("OSMesa setup failed\n"); ExitProcess(3); }

    /* install the hardware backend: robust render hooks (via UpdateState wrap) +
     * hardware glClear. No warm-up/freeze trick -- hooks survive state changes. */
    GLcontext *ctx = _mesa_get_current_context();
    g_orig_update = ctx->Driver.UpdateState;
    ctx->Driver.UpdateState = hw_update_state;
    ctx->Driver.Clear       = hw_clear;
    install_render_hooks(ctx);

    glViewport(0,0,VPW,VPH);
    glMatrixMode(GL_PROJECTION); glLoadIdentity(); glOrtho(0,VPW,0,VPH,-1,1);
    glMatrixMode(GL_MODELVIEW);  glLoadIdentity();

    glEnable(GL_DEPTH_TEST); glDepthFunc(GL_LESS);
    glClearColor(0,0,0,1);
    glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);    /* hw clear colour + Z */

    /* NEAR green triangle (window z 0.3 <- object z 0.4 under this ortho), then FAR red
     * (window z 0.7 <- object z -0.4) overlapping it. GL_LESS must keep the near green
     * where they overlap, even though red is drawn second. */
    glColor3f(0,1,0);
    glBegin(GL_TRIANGLES);
        glVertex3f(8,10,0.4f); glVertex3f(24,10,0.4f); glVertex3f(16,26,0.4f);
    glEnd();
    glColor3f(1,0,0);
    glBegin(GL_TRIANGLES);
        glVertex3f(2,2,-0.4f); glVertex3f(30,2,-0.4f); glVertex3f(16,30,-0.4f);
    glEnd();
    glFinish();

    static unsigned char fb[VPW*VPH*4];
    m64ws_read_vram(g_ws, FBOFF, fb, VPW*VPH*4);
    #define PX(x,y) (*(unsigned*)(fb+((y)*VPW+(x))*4)&0xffffff)
    unsigned center=PX(16,16), redonly=PX(16,5);
    unsigned i, grn=0, red=0; for(i=0;i<VPW*VPH;i++){ unsigned px=*(unsigned*)(fb+i*4)&0xffffff;
        if(px==0x00FF00)grn++; else if(px==0xFF0000)red++; }
    OUTF("mesa depth: center(16,16)=0x%06x  red-only(16,5)=0x%06x  green=%u red=%u\n", center,redonly,grn,red);
    /* PASS: near green survives at the overlap (occludes the later far red) -> hardware
     * depth testing driven by GL_DEPTH_TEST/glDepthFunc through the Mesa pipeline. */
    int pass=(center==0x00FF00 && redonly==0xFF0000 && grn>40 && red>40);
    outs(pass ? "RESULT: DEPTH-TESTED GL via mach64 HARDWARE through Mesa (near occludes far)!\n"
              : "RESULT: mesa depth mismatch\n");
    OSMesaDestroyContext(osc);
    m64ws_close(g_ws);
    ExitProcess(pass ? 0 : 1);
    return 0;
}

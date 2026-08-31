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

static m64_winsys *g_ws;
static m64_cmdbuf  g_cb;
static uint32_t    g_cbs[1024];
static m64_state   g_st;

static uint32_t clampb(float f){ int c=(int)(f*255.0f+0.5f); return c<0?0:(c>255?255:(uint32_t)c); }
static uint32_t packcol(const GLfloat *c){
    return (clampb(c[3])<<24)|(clampb(c[0])<<16)|(clampb(c[1])<<8)|clampb(c[2]);
}

static void hw_triangle(GLcontext *ctx, GLuint e0, GLuint e1, GLuint e2);  /* fwd */

static void hw_start(GLcontext *ctx){
    (void)ctx;
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
    (void)buffers;
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

    glClearColor(0.125f, 0.25f, 0.125f, 1.0f);   /* C = 0x204020, distinct from black */
    glClear(GL_COLOR_BUFFER_BIT);                 /* -> hw_clear (2D fill) -> VRAM = C  */

    glShadeModel(GL_SMOOTH);
    glBegin(GL_TRIANGLES);                          /* GOURAUD: per-vertex R/G/B          */
        glColor3f(1,0,0); glVertex2f(4, 4);         /* v0 red   (state changes inside     */
        glColor3f(0,1,0); glVertex2f(28, 4);        /* v1 green   glBegin -- hooks survive)*/
        glColor3f(0,0,1); glVertex2f(4, 28);        /* v2 blue                            */
    glEnd();
    glFinish();

    static unsigned char fb[VPW*VPH*4];
    m64ws_read_vram(g_ws, FBOFF, fb, VPW*VPH*4);
    /* sample a few px in from each vertex: colour must be dominated by that vertex's channel */
    #define PX(x,y) (*(unsigned*)(fb+((y)*VPW+(x))*4))
    /* interior points nearer each vertex (the tri narrows to a point at v1/v2) */
    unsigned pv0=PX(8,8)&0xffffff, pv1=PX(22,6)&0xffffff, pv2=PX(6,22)&0xffffff, corner=PX(28,28)&0xffffff;
    #define R(p) (((p)>>16)&0xff)
    #define G(p) (((p)>>8)&0xff)
    #define B(p) ((p)&0xff)
    int v0_red   = R(pv0)>80 && R(pv0)>G(pv0) && R(pv0)>B(pv0);
    int v1_green = G(pv1)>80 && G(pv1)>R(pv1) && G(pv1)>B(pv1);
    int v2_blue  = B(pv2)>80 && B(pv2)>R(pv2) && B(pv2)>G(pv2);
    unsigned i, black=0; for(i=0;i<VPW*VPH;i++){ if((*(unsigned*)(fb+i*4)&0xffffff)==0) black++; }
    OUTF("gouraud: v0=0x%06x v1=0x%06x v2=0x%06x corner=0x%06x black=%u\n", pv0,pv1,pv2,corner,black);
    /* PASS proves: hw glClear (black==0), per-vertex colour INTERPOLATION through Mesa
     * -> mach64 (each vertex region dominated by its channel), and hook robustness. */
    int pass=(v0_red && v1_green && v2_blue && corner==0x204020 && black==0);
    outs(pass ? "RESULT: gouraud (per-vertex colour) GL triangle via mach64 hardware through Mesa!\n"
              : "RESULT: gouraud mismatch\n");
    OSMesaDestroyContext(osc);
    m64ws_close(g_ws);
    ExitProcess(pass ? 0 : 1);
    return 0;
}

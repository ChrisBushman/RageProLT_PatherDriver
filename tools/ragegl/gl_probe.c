/*
 * gl_probe.c - what does a REAL GL application get from the renderer?  Real AGL context in a window on the Rage display (or the main display with AGL_MAIN=1, the
 * GeForce reference that validates the expectations), real GL calls through GLEngine into the driver, pixels read back with glReadPixels.  One test per feature,
 * PASS / FAIL / UNSUPPORTED (GL error, missing extension or pixel format).  Run through the GUI session: ~/rungui.sh /tmp/probe.out 300 "/path/gl_probe [names...]".
 * Build: cc -O1 -w gl_probe.c -o gl_probe -framework AGL -framework OpenGL -framework Carbon
 */
#include <AGL/agl.h>
#include <OpenGL/gl.h>
#include <OpenGL/glu.h>
#include <OpenGL/glext.h>
#include <Carbon/Carbon.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
static int g_errcode;

#ifndef GL_GENERATE_MIPMAP_SGIS
#define GL_GENERATE_MIPMAP_SGIS 0x8191
#endif
#ifndef GL_MIRROR_CLAMP_EXT
#define GL_MIRROR_CLAMP_EXT 0x8742
#endif
#ifndef GL_POINT_SPRITE_ARB
#define GL_POINT_SPRITE_ARB 0x8861
#define GL_COORD_REPLACE_ARB 0x8862
#endif
#ifndef GL_SAMPLES_PASSED_ARB
#define GL_SAMPLES_PASSED_ARB 0x8914
#define GL_QUERY_RESULT_AVAILABLE_ARB 0x8867
#define GL_QUERY_RESULT_ARB 0x8866
#endif
#define VW 128
static int g_pass, g_fail, g_unsup;
static char g_act[160];
static AGLContext g_ctx;
static const char *g_exts;
static int have_ext(const char *e) { return g_exts && strstr(g_exts, e) != 0; }

static void report(const char *name, int r, const char *note)
{
    char nb[256]; if (g_act[0]) { snprintf(nb, sizeof nb, "%s%s", note ? note : "", g_act); note = nb; g_act[0] = 0; }
    if (r < 0 && g_errcode) { size_t n; if (note != nb) { snprintf(nb, sizeof nb, "%s", note ? note : ""); note = nb; } n = strlen(nb); snprintf(nb + n, sizeof nb - n, " [glError 0x%x]", g_errcode); }
    g_errcode = 0;
    if (r > 0) { g_pass++; printf("PASS         %-34s %s\n", name, note ? note : ""); }
    else if (r == 0) { g_fail++; printf("FAIL         %-34s %s\n", name, note ? note : ""); }
    else { g_unsup++; printf("UNSUPPORTED  %-34s %s\n", name, note ? note : ""); }
}
static void px(int x, int y, int *r, int *g, int *b)
{
    unsigned char v[4] = { 0, 0, 0, 0 };
    glFinish(); glReadPixels(x, y, 1, 1, GL_RGB, GL_UNSIGNED_BYTE, v); *r = v[0]; *g = v[1]; *b = v[2];
}
static int near3(int x, int y, int er, int eg, int eb, int tol)
{
    int r, g, b, ok; char t[48]; px(x, y, &r, &g, &b);
    ok = abs(r - er) <= tol && abs(g - eg) <= tol && abs(b - eb) <= tol;
    snprintf(t, sizeof t, " (%d,%d)=%d,%d,%d%s", x, y, r, g, b, ok ? "" : "!"); if (strlen(g_act) + strlen(t) < sizeof g_act) strcat(g_act, t);
    return ok;
}
static void reset(float cr, float cg, float cb)
{
    int i;
    aglSwapBuffers(g_ctx);                                   /* end the previous test's frame: the driver clears at a frame's first draw */
    while (glGetError() != GL_NO_ERROR) ;
    glDisable(GL_BLEND); glDisable(GL_DEPTH_TEST); glDisable(GL_TEXTURE_2D); glDisable(GL_STENCIL_TEST); glDisable(GL_SCISSOR_TEST); glDisable(GL_CULL_FACE);
    glDisable(GL_LINE_SMOOTH); glDisable(GL_POINT_SMOOTH); glDisable(GL_LINE_STIPPLE); glDisable(GL_POLYGON_OFFSET_FILL); glDisable(GL_CLIP_PLANE0); glDisable(GL_LIGHTING);
    glDisable(GL_FOG); glDisable(GL_ALPHA_TEST); glDisable(GL_COLOR_LOGIC_OP);
    glPolygonMode(GL_FRONT_AND_BACK, GL_FILL); glColorMask(1, 1, 1, 1); glDepthMask(1); glDepthFunc(GL_LESS); glLineWidth(1); glPointSize(1);
    glBlendFunc(GL_ONE, GL_ZERO); glActiveTexture(GL_TEXTURE0);
    glViewport(0, 0, VW, VW); glMatrixMode(GL_PROJECTION); glLoadIdentity(); glOrtho(0, VW, 0, VW, -1, 1); glMatrixMode(GL_MODELVIEW); glLoadIdentity();
    glClearColor(cr, cg, cb, 0); glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT | GL_STENCIL_BUFFER_BIT);
    glFinish(); glLoadIdentity();                            /* GLEngine only passes glClear to the driver when other state changes in the frame (what real apps do) */
    (void)i; while (glGetError() != GL_NO_ERROR) ;
}
static void quad(float x0, float y0, float x1, float y1, float r, float g, float b, float a)
{
    glColor4f(r, g, b, a); glBegin(GL_QUADS); glVertex2f(x0, y0); glVertex2f(x1, y0); glVertex2f(x1, y1); glVertex2f(x0, y1); glEnd();
}
static int glerr(void) { GLenum e = glGetError(); if (e != GL_NO_ERROR) { g_errcode = (int)e; return 1; } return 0; }

/* ---- blending ---------------------------------------------------------- */
static int blendeq(GLenum eq, int er, int eg, int eb, const char *name)
{
    reset(0.5f, 0.5f, 0.5f); glEnable(GL_BLEND); glBlendFunc(GL_ONE, GL_ONE); glBlendEquationEXT(eq); quad(10, 10, 100, 100, 0.75f, 0.5f, 0.25f, 1);
    glBlendEquationEXT(GL_FUNC_ADD_EXT);
    if (glerr()) return -1;
    return near3(50, 50, er, eg, eb, 24);
}
static void t_clearcolor(void)
{
    reset(0.5f, 0.25f, 0.75f); quad(100, 100, 110, 110, 1, 1, 1, 1);
    report("glClearColor honoured", near3(20, 20, 128, 64, 191, 12), 0);
}
static void t_blend_subtract(void) { report("blend: FUNC_SUBTRACT", blendeq(GL_FUNC_SUBTRACT_EXT, 64, 0, 0, ""), 0); }
static void t_blend_revsub(void) { report("blend: FUNC_REVERSE_SUBTRACT", blendeq(GL_FUNC_REVERSE_SUBTRACT_EXT, 0, 0, 64, ""), 0); }
static void t_blend_min(void) { report("blend: MIN", blendeq(GL_MIN_EXT, 128, 128, 64, ""), 0); }
static void t_blend_max(void) { report("blend: MAX", blendeq(GL_MAX_EXT, 191, 128, 128, ""), 0); }
static void t_blend_color(void)
{
    reset(0.5f, 0.5f, 0.5f); glEnable(GL_BLEND); glBlendColorEXT(0.25f, 0.5f, 0.75f, 1.0f); glBlendFunc(GL_CONSTANT_COLOR_EXT, GL_ZERO); quad(10, 10, 100, 100, 1, 1, 1, 1);
    report("blend: CONSTANT_COLOR factor", glerr() ? -1 : near3(50, 50, 64, 128, 191, 24), 0);
}
static void t_blend_sep(void)
{
    reset(0.5f, 0.5f, 0.5f); glEnable(GL_BLEND); glBlendFuncSeparateEXT(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA, GL_ONE, GL_ONE); quad(10, 10, 100, 100, 1, 0, 0, 0.5f);
    report("blend: func_separate (RGB part)", glerr() ? -1 : near3(50, 50, 191, 64, 64, 24), 0);
}
static void t_blend_dstcolor(void)
{
    reset(0.5f, 0.5f, 0.5f); glEnable(GL_BLEND); glBlendFunc(GL_DST_COLOR, GL_ZERO); quad(10, 10, 100, 100, 1, 0.5f, 0.25f, 1);
    report("blend: DST_COLOR src factor", glerr() ? -1 : near3(50, 50, 128, 64, 32, 24), 0);
}
static void t_blend_srccolor_dst(void)
{
    reset(0.5f, 0.5f, 0.5f); glEnable(GL_BLEND); glBlendFunc(GL_ZERO, GL_SRC_COLOR); quad(10, 10, 100, 100, 1, 0.5f, 0.25f, 1);
    report("blend: SRC_COLOR dst factor", glerr() ? -1 : near3(50, 50, 128, 64, 32, 24), 0);
}
static void t_blend_satur(void)
{
    reset(0.5f, 0.5f, 0.5f); glEnable(GL_BLEND); glBlendFunc(GL_SRC_ALPHA_SATURATE, GL_ONE); quad(10, 10, 100, 100, 1, 0.5f, 0.25f, 0.5f);
    report("blend: SRC_ALPHA_SATURATE", glerr() ? -1 : near3(50, 50, 128, 128, 128, 24), 0);       /* the target has no destination alpha (Ad = 1): f = min(As, 1 - Ad) = 0, result = destination */
}

/* ---- primitives ---------------------------------------------------------- */
static void t_line_smooth(void)
{
    reset(0, 0, 0); glEnable(GL_BLEND); glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA); glEnable(GL_LINE_SMOOTH); glColor4f(1, 1, 1, 1);
    glBegin(GL_LINES); glVertex2f(20, 64); glVertex2f(100, 64); glEnd();
    { int r1, g, b, r2; px(60, 63, &r1, &g, &b); px(60, 64, &r2, &g, &b);
      { char n[64]; snprintf(n, sizeof n, "rows 63/64 = %d/%d (partial coverage both)", r1, r2); report("line smooth (antialiased line)", glerr() ? -1 : (r1 > 60 && r1 < 200 && r2 > 60 && r2 < 200), n); } }
}
static void t_point_smooth(void)
{
    int rc, rr, g, b;
    reset(0, 0, 0); glEnable(GL_BLEND); glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA); glEnable(GL_POINT_SMOOTH); glPointSize(8); glColor4f(1, 1, 1, 1);
    glBegin(GL_POINTS); glVertex2f(64, 64); glEnd();
    px(64, 64, &rc, &g, &b); px(67, 67, &rr, &g, &b);
    { char n[64]; snprintf(n, sizeof n, "centre %d, corner %d (round, soft rim)", rc, rr); report("point smooth (round antialiased point)", glerr() ? -1 : (rc > 200 && rr < 100), n); }
}
static void t_line_stipple(void)
{
    int x, lit = 0, trans = 0, prev = -1, r, g, b;
    reset(0, 0, 0); glEnable(GL_LINE_STIPPLE); glLineStipple(1, 0x0f0f); glColor4f(1, 1, 1, 1);
    glBegin(GL_LINES); glVertex2f(10, 64.5f); glVertex2f(74, 64.5f); glEnd();
    for (x = 10; x < 74; x++) { int on; px(x, 64, &r, &g, &b); on = r > 128; lit += on; if (prev >= 0 && on != prev) trans++; prev = on; }
    { char n[64]; snprintf(n, sizeof n, "%d of 64 lit, %d transitions", lit, trans); report("line stipple", glerr() ? -1 : (lit > 20 && lit < 44 && trans >= 6), n); }
}
static void t_line_stipple3(void)
{
    int x, lit = 0, trans = 0, prev = -1, r, g, b;
    reset(0, 0, 0); glEnable(GL_LINE_STIPPLE); glLineStipple(3, 0x00ff); glColor4f(1, 1, 1, 1);
    glBegin(GL_LINES); glVertex2f(10, 64.5f); glVertex2f(100, 64.5f); glEnd();
    for (x = 10; x < 100; x++) { int on; px(x, 64, &r, &g, &b); on = r > 128; lit += on; if (prev >= 0 && on != prev) trans++; prev = on; }
    { char n[64]; snprintf(n, sizeof n, "%d of 90 lit, %d transitions (want 4 runs of 24)", lit, trans); report("line stipple factor 3", glerr() ? -1 : (lit > 38 && lit < 52 && trans >= 3), n); }
}
static void t_polygon_offset(void)
{
    reset(0, 0, 0); glEnable(GL_DEPTH_TEST); glDepthFunc(GL_LESS);
    glBegin(GL_QUADS); glColor3f(1, 0, 0); glVertex3f(10, 10, 0); glVertex3f(100, 10, 0); glVertex3f(100, 100, 0); glVertex3f(10, 100, 0); glEnd();
    glEnable(GL_POLYGON_OFFSET_FILL); glPolygonOffset(-1, -1000);
    glBegin(GL_QUADS); glColor3f(0, 0, 1); glVertex3f(10, 10, 0); glVertex3f(100, 10, 0); glVertex3f(100, 100, 0); glVertex3f(10, 100, 0); glEnd();
    report("polygon offset (coplanar quad wins)", glerr() ? -1 : near3(50, 50, 0, 0, 255, 24), 0);
}
static void t_polygon_mode_line(void)
{
    int r, g, b;
    reset(0, 0, 0); glPolygonMode(GL_FRONT_AND_BACK, GL_LINE); glColor3f(1, 1, 1);
    glBegin(GL_QUADS); glVertex2f(20, 20); glVertex2f(100, 20); glVertex2f(100, 100); glVertex2f(20, 100); glEnd();
    px(60, 60, &r, &g, &b);
    report("polygon mode LINE (hollow)", glerr() ? -1 : (r < 40 && (near3(60, 20, 255, 255, 255, 60) || near3(60, 19, 255, 255, 255, 60))), 0);
}
static void t_polygon_mode_point(void)
{
    reset(0, 0, 0); glPolygonMode(GL_FRONT_AND_BACK, GL_POINT); glPointSize(3); glColor3f(1, 1, 1);
    glBegin(GL_QUADS); glVertex2f(20, 20); glVertex2f(100, 20); glVertex2f(100, 100); glVertex2f(20, 100); glEnd();
    report("polygon mode POINT", glerr() ? -1 : (near3(20, 20, 255, 255, 255, 60) && near3(60, 60, 0, 0, 0, 30)), 0);
}
static void t_cull(void)
{
    reset(0, 0, 0); glEnable(GL_CULL_FACE); glCullFace(GL_BACK); glFrontFace(GL_CCW); glColor3f(1, 1, 1);
    glBegin(GL_QUADS); glVertex2f(10, 10); glVertex2f(10, 60); glVertex2f(60, 60); glVertex2f(60, 10); glEnd();          /* clockwise: back */
    glBegin(GL_QUADS); glVertex2f(70, 10); glVertex2f(120, 10); glVertex2f(120, 60); glVertex2f(70, 60); glEnd();        /* ccw: front */
    report("cull face", glerr() ? -1 : (near3(30, 30, 0, 0, 0, 20) && near3(90, 30, 255, 255, 255, 20)), 0);
}
static void t_scissor(void)
{
    reset(0, 0, 0); glEnable(GL_SCISSOR_TEST); glScissor(20, 20, 40, 40); quad(0, 0, 128, 128, 1, 1, 1, 1);
    report("scissor test", glerr() ? -1 : (near3(30, 30, 255, 255, 255, 20) && near3(80, 80, 0, 0, 0, 20) && near3(10, 10, 0, 0, 0, 20)), 0);
}
static void t_clip_plane(void)
{
    GLdouble pl[4] = { -1, 0, 0, 64 };
    reset(0, 0, 0); glClipPlane(GL_CLIP_PLANE0, pl); glEnable(GL_CLIP_PLANE0); quad(0, 0, 128, 128, 1, 1, 1, 1);
    report("user clip plane", glerr() ? -1 : (near3(30, 60, 255, 255, 255, 20) && near3(100, 60, 0, 0, 0, 20)), 0);
}
static void t_alpha_logic(void)
{
    reset(0, 0, 0); glEnable(GL_COLOR_LOGIC_OP); glLogicOp(GL_XOR); quad(10, 10, 100, 100, 1, 0.5f, 0, 1); quad(10, 10, 100, 100, 1, 0.5f, 0, 1);
    report("logic op XOR (twice = restore)", glerr() ? -1 : near3(50, 50, 0, 0, 0, 24), 0);
}

/* ---- textures ---------------------------------------------------------------- */
static GLuint mktex(GLenum target, int w, int h, const unsigned char *rgb, GLenum filt, GLenum wrap)
{
    GLuint id; glGenTextures(1, &id); glBindTexture(target, id);
    glTexParameteri(target, GL_TEXTURE_MIN_FILTER, filt); glTexParameteri(target, GL_TEXTURE_MAG_FILTER, filt);
    glTexParameteri(target, GL_TEXTURE_WRAP_S, wrap); glTexParameteri(target, GL_TEXTURE_WRAP_T, wrap);
    glPixelStorei(GL_UNPACK_ALIGNMENT, 1);
    glTexImage2D(target, 0, GL_RGB, w, h, 0, GL_RGB, GL_UNSIGNED_BYTE, rgb);
    return id;
}
static const unsigned char RG2[6] = { 255, 0, 0, 0, 255, 0 };               /* 2x1: red, green */
static void tquad(float s0, float s1, float x0, float x1)
{
    glColor3f(1, 1, 1); glBegin(GL_QUADS); glTexCoord2f(s0, 0); glVertex2f(x0, 10); glTexCoord2f(s1, 0); glVertex2f(x1, 10); glTexCoord2f(s1, 1); glVertex2f(x1, 100); glTexCoord2f(s0, 1); glVertex2f(x0, 100); glEnd();
}
static void t_tex_npot(void)
{
    unsigned char t[60 * 40 * 3]; int x, y; GLuint id;
    for (y = 0; y < 40; y++) for (x = 0; x < 60; x++) { unsigned char *p = t + (y * 60 + x) * 3; p[0] = x < 30 ? 255 : 0; p[1] = x < 30 ? 0 : 255; p[2] = 0; }
    reset(0, 0, 0); glEnable(GL_TEXTURE_2D); id = mktex(GL_TEXTURE_2D, 60, 40, t, GL_NEAREST, GL_CLAMP); if (glerr()) { report("texture non-power-of-two (60x40)", -1, "glTexImage2D error"); return; }
    tquad(0, 1, 4, 124);
    report("texture non-power-of-two (60x40)", glerr() ? -1 : (near3(30, 50, 255, 0, 0, 30) && near3(100, 50, 0, 255, 0, 30)), 0);
    glDeleteTextures(1, &id);
}
static void t_tex_rect(void)
{
    unsigned char t[50 * 30 * 3]; int x, y; GLuint id;
    for (y = 0; y < 30; y++) for (x = 0; x < 50; x++) { unsigned char *p = t + (y * 50 + x) * 3; p[0] = x < 25 ? 255 : 0; p[1] = x < 25 ? 0 : 255; p[2] = 0; }
    reset(0, 0, 0); glEnable(GL_TEXTURE_RECTANGLE_EXT);
    glGenTextures(1, &id); glBindTexture(GL_TEXTURE_RECTANGLE_EXT, id);
    glTexParameteri(GL_TEXTURE_RECTANGLE_EXT, GL_TEXTURE_MIN_FILTER, GL_NEAREST); glTexParameteri(GL_TEXTURE_RECTANGLE_EXT, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_RECTANGLE_EXT, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE); glTexParameteri(GL_TEXTURE_RECTANGLE_EXT, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    glTexImage2D(GL_TEXTURE_RECTANGLE_EXT, 0, GL_RGB, 50, 30, 0, GL_RGB, GL_UNSIGNED_BYTE, t);
    if (glerr()) { report("texture rectangle (50x30, texel coords)", -1, "glTexImage2D error"); glDisable(GL_TEXTURE_RECTANGLE_EXT); return; }
    glColor3f(1, 1, 1); glBegin(GL_QUADS); glTexCoord2f(0, 0); glVertex2f(4, 10); glTexCoord2f(50, 0); glVertex2f(104, 10); glTexCoord2f(50, 30); glVertex2f(104, 70); glTexCoord2f(0, 30); glVertex2f(4, 70); glEnd();
    report("texture rectangle (50x30, texel coords)", glerr() ? -1 : (near3(30, 40, 255, 0, 0, 30) && near3(80, 40, 0, 255, 0, 30)), 0);
    glDisable(GL_TEXTURE_RECTANGLE_EXT); glDeleteTextures(1, &id);
}
static void t_tex_cube(void)
{
    static const unsigned char face[6][3] = { { 255, 0, 0 }, { 0, 255, 0 }, { 0, 0, 255 }, { 255, 255, 0 }, { 0, 255, 255 }, { 255, 0, 255 } };
    unsigned char t[4 * 4 * 3]; int f, i; GLuint id;
    reset(0, 0, 0); glEnable(GL_TEXTURE_CUBE_MAP_ARB); glGenTextures(1, &id); glBindTexture(GL_TEXTURE_CUBE_MAP_ARB, id);
    glTexParameteri(GL_TEXTURE_CUBE_MAP_ARB, GL_TEXTURE_MIN_FILTER, GL_NEAREST); glTexParameteri(GL_TEXTURE_CUBE_MAP_ARB, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
    for (f = 0; f < 6; f++) { for (i = 0; i < 16; i++) { t[i * 3] = face[f][0]; t[i * 3 + 1] = face[f][1]; t[i * 3 + 2] = face[f][2]; }
        glTexImage2D(GL_TEXTURE_CUBE_MAP_POSITIVE_X_ARB + f, 0, GL_RGB, 4, 4, 0, GL_RGB, GL_UNSIGNED_BYTE, t); }
    if (glerr()) { report("cube map", -1, "glTexImage2D error"); glDisable(GL_TEXTURE_CUBE_MAP_ARB); return; }
    glColor3f(1, 1, 1);
    glBegin(GL_QUADS); glTexCoord3f(1, 0, 0); glVertex2f(5, 10); glTexCoord3f(1, 0, 0); glVertex2f(55, 10); glTexCoord3f(1, 0, 0); glVertex2f(55, 100); glTexCoord3f(1, 0, 0); glVertex2f(5, 100); glEnd();
    glBegin(GL_QUADS); glTexCoord3f(0, 0, -1); glVertex2f(70, 10); glTexCoord3f(0, 0, -1); glVertex2f(120, 10); glTexCoord3f(0, 0, -1); glVertex2f(120, 100); glTexCoord3f(0, 0, -1); glVertex2f(70, 100); glEnd();
    report("cube map", glerr() ? -1 : (near3(30, 50, 255, 0, 0, 30) && near3(95, 50, 255, 0, 255, 30)), "+X red, -Z magenta");
    glDisable(GL_TEXTURE_CUBE_MAP_ARB); glDeleteTextures(1, &id);
}
static int wrap_test(GLenum wrap, float s0, float s1, const int e[4][3], GLfloat *border)
{
    GLuint id; int i, ok = 1;
    reset(0, 0, 0); glEnable(GL_TEXTURE_2D); id = mktex(GL_TEXTURE_2D, 2, 1, RG2, GL_NEAREST, wrap);
    if (border) glTexParameterfv(GL_TEXTURE_2D, GL_TEXTURE_BORDER_COLOR, border);
    if (glerr()) { glDeleteTextures(1, &id); return -1; }
    tquad(s0, s1, 0, 128);
    for (i = 0; i < 4; i++) ok &= near3(16 + 32 * i, 50, e[i][0], e[i][1], e[i][2], 30);
    if (glerr()) ok = -1;
    glDeleteTextures(1, &id); return ok;
}
static void t_wrap_mirror(void) { static const int e[4][3] = { { 255, 0, 0 }, { 0, 255, 0 }, { 0, 255, 0 }, { 255, 0, 0 } }; report("wrap: MIRRORED_REPEAT", wrap_test(GL_MIRRORED_REPEAT_ARB, 0, 2, e, 0), 0); }
static void t_wrap_mirror_clamp(void) { static const int e[4][3] = { { 0, 255, 0 }, { 255, 0, 0 }, { 255, 0, 0 }, { 0, 255, 0 } }; report("wrap: MIRROR_CLAMP", wrap_test(GL_MIRROR_CLAMP_EXT, -1, 1, e, 0), 0); }
static void t_wrap_edge(void) { static const int e[4][3] = { { 255, 0, 0 }, { 255, 0, 0 }, { 0, 255, 0 }, { 0, 255, 0 } }; report("wrap: CLAMP_TO_EDGE", wrap_test(GL_CLAMP_TO_EDGE, -1, 2, e, 0), 0); }
static void t_wrap_border(void) { static const int e[4][3] = { { 0, 0, 255 }, { 255, 0, 0 }, { 0, 255, 0 }, { 0, 0, 255 } }; GLfloat bc[4] = { 0, 0, 1, 1 }; report("wrap: CLAMP_TO_BORDER", wrap_test(GL_CLAMP_TO_BORDER_ARB, -1, 2, e, bc), 0); }
static void t_wrap_repeat(void) { static const int e[4][3] = { { 255, 0, 0 }, { 0, 255, 0 }, { 255, 0, 0 }, { 0, 255, 0 } }; report("wrap: REPEAT", wrap_test(GL_REPEAT, 0, 2, e, 0), 0); }
static void persp_draw(void)
{
    glMatrixMode(GL_PROJECTION); glPushMatrix(); glLoadIdentity(); glFrustum(-0.5, 0.5, -0.5, 0.5, 1.0, 10.0);
    glMatrixMode(GL_MODELVIEW); glPushMatrix(); glLoadIdentity(); glTranslatef(0, -0.4f, -2.0f);
    glBegin(GL_QUADS); glTexCoord2f(0, 0); glVertex3f(-1.5f, 0, 3.0f); glTexCoord2f(1, 0); glVertex3f(1.5f, 0, 3.0f); glTexCoord2f(1, 1); glVertex3f(1.5f, 0, -6.0f); glTexCoord2f(0, 1); glVertex3f(-1.5f, 0, -6.0f); glEnd();
    glMatrixMode(GL_PROJECTION); glPopMatrix(); glMatrixMode(GL_MODELVIEW); glPopMatrix();
}
static void t_cpu_persp(void)
{
    unsigned char t[8 * 8 * 3]; int x, y, i, bad = 0, n = 0; static unsigned char a[128 * 128 * 4], b[128 * 128 * 4]; GLuint id; char nm[96];
    for (y = 0; y < 8; y++) for (x = 0; x < 8; x++) { unsigned char *q = t + (y * 8 + x) * 3; int on = ((x >> 1) + (y >> 1)) & 1; q[0] = on ? 255 : 0; q[1] = on ? 200 : 40; q[2] = on ? 0 : 255; }
    reset(0, 0, 0); glEnable(GL_TEXTURE_2D); id = mktex(GL_TEXTURE_2D, 8, 8, t, GL_NEAREST, GL_REPEAT); glTexEnvi(GL_TEXTURE_ENV, GL_TEXTURE_ENV_MODE, GL_REPLACE); glColor3f(1, 1, 1);
    glViewport(0, 0, 128, 128); persp_draw(); glPixelStorei(GL_PACK_ALIGNMENT, 1); glReadPixels(0, 0, 128, 128, GL_RGBA, GL_UNSIGNED_BYTE, a);
    reset(0, 0, 0); glEnable(GL_TEXTURE_2D); glTexEnvi(GL_TEXTURE_ENV, GL_TEXTURE_ENV_MODE, GL_COMBINE_ARB); glTexEnvi(GL_TEXTURE_ENV, GL_COMBINE_RGB_ARB, GL_SUBTRACT_ARB);
    glTexEnvi(GL_TEXTURE_ENV, GL_SOURCE1_RGB_ARB, GL_CONSTANT_ARB); { GLfloat z[4] = { 0, 0, 0, 0 }; glTexEnvfv(GL_TEXTURE_ENV, GL_TEXTURE_ENV_COLOR, z); }   /* texture - 0 = texture, shaded on the CPU */
    glColor3f(1, 1, 1); glViewport(0, 0, 128, 128); persp_draw(); glReadPixels(0, 0, 128, 128, GL_RGBA, GL_UNSIGNED_BYTE, b);
    for (i = 0; i < 128 * 128; i++) { if (a[i*4] || a[i*4+1] || a[i*4+2]) { n++; if (abs(a[i*4] - b[i*4]) > 24 || abs(a[i*4+1] - b[i*4+1]) > 24 || abs(a[i*4+2] - b[i*4+2]) > 24) bad++; } }
    { FILE *f = fopen("/tmp/persp.ppm", "wb"); if (f) { fprintf(f, "P6\n256 128\n255\n"); for (y = 127; y >= 0; y--) { for (x = 0; x < 128; x++) fwrite(a + (y * 128 + x) * 4, 1, 3, f); for (x = 0; x < 128; x++) fwrite(b + (y * 128 + x) * 4, 1, 3, f); } fclose(f); } }
    snprintf(nm, sizeof nm, "%d of %d lit pixels differ (hardware REPLACE vs CPU COMBINE)", bad, n);
    report("CPU texturing: perspective-correct vs hardware", n > 500 && bad * 50 < n, nm); glDeleteTextures(1, &id);
}
static void t_envstate(void)
{
    static const struct { GLenum pn; GLint v; } st[] = {
        { GL_RGB_SCALE_ARB, 4 }, { GL_ALPHA_SCALE, 2 }, { GL_SOURCE2_RGB_ARB, GL_CONSTANT_ARB }, { GL_OPERAND0_RGB_ARB, GL_ONE_MINUS_SRC_COLOR }, { GL_OPERAND1_RGB_ARB, GL_SRC_ALPHA },
        { GL_OPERAND2_RGB_ARB, GL_ONE_MINUS_SRC_ALPHA }, { GL_COMBINE_ALPHA_ARB, GL_ADD }, { GL_SOURCE0_ALPHA_ARB, GL_CONSTANT_ARB }, { GL_SOURCE1_ALPHA_ARB, GL_PRIMARY_COLOR_ARB },
        { GL_SOURCE2_ALPHA_ARB, GL_TEXTURE }, { GL_OPERAND0_ALPHA_ARB, GL_ONE_MINUS_SRC_ALPHA }, { GL_OPERAND1_ALPHA_ARB, GL_ONE_MINUS_SRC_ALPHA }, { GL_OPERAND2_ALPHA_ARB, GL_ONE_MINUS_SRC_ALPHA },
        { GL_COMBINE_RGB_ARB, GL_INTERPOLATE_ARB }, { GL_SOURCE0_RGB_ARB, GL_PRIMARY_COLOR_ARB }, { GL_SOURCE1_RGB_ARB, GL_CONSTANT_ARB } };
    static const unsigned char c[3] = { 10, 20, 30 }; GLuint id; unsigned i; GLfloat ec[4] = { 0.1f, 0.2f, 0.3f, 0.4f };
    reset(0, 0, 0); glEnable(GL_TEXTURE_2D); id = mktex(GL_TEXTURE_2D, 1, 1, c, GL_NEAREST, GL_CLAMP); glTexEnvi(GL_TEXTURE_ENV, GL_TEXTURE_ENV_MODE, GL_COMBINE_ARB);
    for (i = 0; i < sizeof st / sizeof st[0] + 1; i++) {
        reset(0, 0, 0); glEnable(GL_TEXTURE_2D);
        if (i < sizeof st / sizeof st[0]) glTexEnvi(GL_TEXTURE_ENV, st[i].pn, st[i].v); else glTexEnvfv(GL_TEXTURE_ENV, GL_TEXTURE_ENV_COLOR, ec);
        glBegin(GL_QUADS); glTexCoord2f(.5f, .5f); glVertex2f(10, 10); glVertex2f(20, 10); glVertex2f(20, 20); glVertex2f(10, 20); glEnd();
    }
    { static const struct { GLenum pn; GLint v; } d[] = { { GL_RGB_SCALE_ARB, 1 }, { GL_ALPHA_SCALE, 1 }, { GL_COMBINE_RGB_ARB, GL_MODULATE }, { GL_COMBINE_ALPHA_ARB, GL_MODULATE },
        { GL_SOURCE0_RGB_ARB, GL_TEXTURE }, { GL_SOURCE1_RGB_ARB, GL_PREVIOUS_ARB }, { GL_SOURCE2_RGB_ARB, GL_CONSTANT_ARB }, { GL_SOURCE0_ALPHA_ARB, GL_TEXTURE }, { GL_SOURCE1_ALPHA_ARB, GL_PREVIOUS_ARB },
        { GL_SOURCE2_ALPHA_ARB, GL_CONSTANT_ARB }, { GL_OPERAND0_RGB_ARB, GL_SRC_COLOR }, { GL_OPERAND1_RGB_ARB, GL_SRC_COLOR }, { GL_OPERAND2_RGB_ARB, GL_SRC_ALPHA }, { GL_OPERAND0_ALPHA_ARB, GL_SRC_ALPHA },
        { GL_OPERAND1_ALPHA_ARB, GL_SRC_ALPHA }, { GL_OPERAND2_ALPHA_ARB, GL_SRC_ALPHA }, { GL_TEXTURE_ENV_MODE, GL_MODULATE } }; GLfloat z[4] = { 0, 0, 0, 0 };
      for (i = 0; i < sizeof d / sizeof d[0]; i++) glTexEnvi(GL_TEXTURE_ENV, d[i].pn, d[i].v); glTexEnvfv(GL_TEXTURE_ENV, GL_TEXTURE_ENV_COLOR, z); }
    reset(0, 0, 0); glDeleteTextures(1, &id); report("env state dump", 1, "see gli diff");
}
static void t_tex1x1(void)
{
    static const unsigned char c[3] = { 0, 128, 64 }, c4[12] = { 0, 128, 64, 0, 128, 64, 0, 128, 64, 0, 128, 64 }; GLuint id; char n[64];
    reset(0, 0, 0); glEnable(GL_TEXTURE_2D); id = mktex(GL_TEXTURE_2D, 1, 1, c, GL_NEAREST, GL_CLAMP); glTexEnvi(GL_TEXTURE_ENV, GL_TEXTURE_ENV_MODE, GL_REPLACE);
    glColor3f(1, 1, 1); glBegin(GL_QUADS); glTexCoord2f(.5f, .5f); glVertex2f(10, 10); glVertex2f(100, 10); glVertex2f(100, 100); glVertex2f(10, 100); glEnd();
    glTexEnvi(GL_TEXTURE_ENV, GL_TEXTURE_ENV_MODE, GL_MODULATE);
    report("texture 1x1 REPLACE", glerr() ? -1 : near3(50, 50, 0, 128, 64, 12), 0); glDeleteTextures(1, &id);
    reset(0, 0, 0); glEnable(GL_TEXTURE_2D); id = mktex(GL_TEXTURE_2D, 2, 2, c4, GL_NEAREST, GL_CLAMP); glTexEnvi(GL_TEXTURE_ENV, GL_TEXTURE_ENV_MODE, GL_ADD);
    glColor3f(0.5f, 0, 0); glBegin(GL_QUADS); glTexCoord2f(.5f, .5f); glVertex2f(10, 10); glVertex2f(100, 10); glVertex2f(100, 100); glVertex2f(10, 100); glEnd();
    glTexEnvi(GL_TEXTURE_ENV, GL_TEXTURE_ENV_MODE, GL_MODULATE); (void)n;
    report("texture 2x2 ADD", glerr() ? -1 : near3(50, 50, 128, 128, 64, 30), 0); glDeleteTextures(1, &id);
}
static void t_env_add(void)
{
    static const unsigned char c[3] = { 0, 128, 64 }; GLuint id;
    reset(0, 0, 0); glEnable(GL_TEXTURE_2D); id = mktex(GL_TEXTURE_2D, 1, 1, c, GL_NEAREST, GL_CLAMP); glTexEnvi(GL_TEXTURE_ENV, GL_TEXTURE_ENV_MODE, GL_ADD);
    glColor3f(0.5f, 0, 0); glBegin(GL_QUADS); glTexCoord2f(.5f, .5f); glVertex2f(10, 10); glVertex2f(100, 10); glVertex2f(100, 100); glVertex2f(10, 100); glEnd();
    glTexEnvi(GL_TEXTURE_ENV, GL_TEXTURE_ENV_MODE, GL_MODULATE);
    report("texture env ADD", glerr() ? -1 : near3(50, 50, 128, 128, 64, 30), 0); glDeleteTextures(1, &id);
}
static int combine_test(GLenum func, const unsigned char *tex, float fr, float fg, float fb, int er, int eg, int eb)
{
    GLuint id;
    reset(0, 0, 0); glEnable(GL_TEXTURE_2D); id = mktex(GL_TEXTURE_2D, 1, 1, tex, GL_NEAREST, GL_CLAMP);
    glTexEnvi(GL_TEXTURE_ENV, GL_TEXTURE_ENV_MODE, GL_COMBINE_ARB); glTexEnvi(GL_TEXTURE_ENV, GL_COMBINE_RGB_ARB, func);
    glTexEnvi(GL_TEXTURE_ENV, GL_SOURCE0_RGB_ARB, GL_TEXTURE); glTexEnvi(GL_TEXTURE_ENV, GL_OPERAND0_RGB_ARB, GL_SRC_COLOR);
    glTexEnvi(GL_TEXTURE_ENV, GL_SOURCE1_RGB_ARB, GL_PRIMARY_COLOR_ARB); glTexEnvi(GL_TEXTURE_ENV, GL_OPERAND1_RGB_ARB, GL_SRC_COLOR);
    glColor3f(fr, fg, fb); glBegin(GL_QUADS); glTexCoord2f(.5f, .5f); glVertex2f(10, 10); glVertex2f(100, 10); glVertex2f(100, 100); glVertex2f(10, 100); glEnd();
    glTexEnvi(GL_TEXTURE_ENV, GL_TEXTURE_ENV_MODE, GL_MODULATE); glDeleteTextures(1, &id);
    if (glerr()) return -1;
    return near3(50, 50, er, eg, eb, 36);
}
static void t_combine_addsigned(void) { static const unsigned char t[3] = { 191, 128, 64 }; report("env combine: ADD_SIGNED", combine_test(GL_ADD_SIGNED_ARB, t, 0.25f, 0.75f, 1.0f, 128, 191, 191), 0); }
static void t_combine_interp(void) { static const unsigned char t[3] = { 255, 0, 0 }; report("env combine: SUBTRACT", combine_test(GL_SUBTRACT_ARB, t, 0.25f, 0, 0, 191, 0, 0), 0); }
static void t_combine_dot3(void) { static const unsigned char t[3] = { 255, 128, 128 }; report("env combine: DOT3_RGB", combine_test(GL_DOT3_RGB_ARB, t, 1.0f, 0.5f, 0.5f, 255, 255, 255), 0); }
static void t_tex_s3tc(void)
{
    static const unsigned char blk[8] = { 0x00, 0xf8, 0x00, 0xf8, 0, 0, 0, 0 };       /* DXT1: both colours 0xf800 (red), all indices 0 */
    GLuint id;
    reset(0, 0, 0); glEnable(GL_TEXTURE_2D); glGenTextures(1, &id); glBindTexture(GL_TEXTURE_2D, id);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST); glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
    glCompressedTexImage2DARB(GL_TEXTURE_2D, 0, GL_COMPRESSED_RGB_S3TC_DXT1_EXT, 4, 4, 0, 8, blk);
    if (glerr()) { report("S3TC DXT1 texture", -1, "upload error"); return; }
    tquad(0, 1, 10, 100);
    report("S3TC DXT1 texture", glerr() ? -1 : near3(50, 50, 255, 0, 0, 30), 0); glDeleteTextures(1, &id);
}
static void t_tex_paletted(void)
{
    static const unsigned char idx[2] = { 0, 1 }; unsigned char pal[256 * 3]; GLuint id;
    memset(pal, 0, sizeof pal); pal[0] = 255; pal[4] = 255;
    reset(0, 0, 0); glEnable(GL_TEXTURE_2D); glGenTextures(1, &id); glBindTexture(GL_TEXTURE_2D, id);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST); glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST); glPixelStorei(GL_UNPACK_ALIGNMENT, 1);
    glColorTableEXT(GL_TEXTURE_2D, GL_RGB8, 256, GL_RGB, GL_UNSIGNED_BYTE, pal);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_COLOR_INDEX8_EXT, 2, 1, 0, GL_COLOR_INDEX, GL_UNSIGNED_BYTE, idx);
    if (glerr()) { report("paletted texture", -1, "upload error"); return; }
    tquad(0, 1, 0, 128);
    report("paletted texture", glerr() ? -1 : (near3(32, 50, 255, 0, 0, 30) && near3(96, 50, 0, 255, 0, 30)), 0); glDeleteTextures(1, &id);
}
static void t_tex_aniso(void)
{
    GLuint id; unsigned char c[3] = { 255, 0, 0 };
    reset(0, 0, 0); glEnable(GL_TEXTURE_2D); id = mktex(GL_TEXTURE_2D, 1, 1, c, GL_LINEAR, GL_CLAMP); glTexParameterf(GL_TEXTURE_2D, GL_TEXTURE_MAX_ANISOTROPY_EXT, 4.0f);
    tquad(0, 1, 10, 100);
    report("anisotropic filtering (accepted, drawn)", glerr() ? -1 : near3(50, 50, 255, 0, 0, 30), 0); glDeleteTextures(1, &id);
}
static void t_tex_3d(void)
{
    static const unsigned char v[2 * 2 * 2 * 3] = { 255, 0, 0, 255, 0, 0, 255, 0, 0, 255, 0, 0, 0, 255, 0, 0, 255, 0, 0, 255, 0, 0, 255, 0 }; GLuint id;
    reset(0, 0, 0); glEnable(GL_TEXTURE_3D); glGenTextures(1, &id); glBindTexture(GL_TEXTURE_3D, id);
    glTexParameteri(GL_TEXTURE_3D, GL_TEXTURE_MIN_FILTER, GL_NEAREST); glTexParameteri(GL_TEXTURE_3D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
    glPixelStorei(GL_UNPACK_ALIGNMENT, 1); glTexImage3D(GL_TEXTURE_3D, 0, GL_RGB, 2, 2, 2, 0, GL_RGB, GL_UNSIGNED_BYTE, v); glPixelStorei(GL_UNPACK_ALIGNMENT, 4);
    if (glerr()) { report("3D texture", -1, "upload error"); glDisable(GL_TEXTURE_3D); return; }
    glColor3f(1, 1, 1); glBegin(GL_QUADS); glTexCoord3f(.5f, .5f, .25f); glVertex2f(5, 10); glVertex2f(55, 10); glVertex2f(55, 100); glVertex2f(5, 100); glEnd();
    glBegin(GL_QUADS); glTexCoord3f(.5f, .5f, .75f); glVertex2f(70, 10); glVertex2f(120, 10); glVertex2f(120, 100); glVertex2f(70, 100); glEnd();
    report("3D texture", glerr() ? -1 : (near3(30, 50, 255, 0, 0, 30) && near3(95, 50, 0, 255, 0, 30)), 0); glDisable(GL_TEXTURE_3D); glDeleteTextures(1, &id);
}
static void t_copyteximage(void)
{
    GLuint id;
    reset(0, 0, 0); quad(0, 0, 64, 64, 1, 0, 0, 1);
    glEnable(GL_TEXTURE_2D); glGenTextures(1, &id); glBindTexture(GL_TEXTURE_2D, id);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST); glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
    glCopyTexImage2D(GL_TEXTURE_2D, 0, GL_RGB, 0, 0, 64, 64, 0);
    if (glerr()) { report("glCopyTexImage2D (framebuffer -> texture)", -1, "error"); return; }
    glClearColor(0, 0, 0, 0); glClear(GL_COLOR_BUFFER_BIT); tquad(0, 1, 10, 100);
    report("glCopyTexImage2D (framebuffer -> texture)", glerr() ? -1 : near3(50, 50, 255, 0, 0, 30), 0); glDeleteTextures(1, &id);
}
static void t_copytexsub(void)
{
    GLuint id; static unsigned char base[64 * 64 * 3];
    reset(0, 0, 0); quad(0, 0, 32, 32, 0, 1, 0, 1);
    glEnable(GL_TEXTURE_2D); id = mktex(GL_TEXTURE_2D, 64, 64, base, GL_NEAREST, GL_CLAMP);
    glCopyTexSubImage2D(GL_TEXTURE_2D, 0, 0, 0, 0, 0, 32, 32);
    if (glerr()) { report("glCopyTexSubImage2D", -1, "error"); return; }
    glClearColor(0, 0, 0, 0); glClear(GL_COLOR_BUFFER_BIT); tquad(0, 1, 0, 128);
    report("glCopyTexSubImage2D", glerr() ? -1 : (near3(30, 30, 0, 255, 0, 30) && near3(30, 90, 0, 0, 0, 30) && near3(100, 40, 0, 0, 0, 30)), 0); glDeleteTextures(1, &id);
}
static void t_point_sprite(void)
{
    static const unsigned char q[4 * 3] = { 255, 0, 0, 0, 255, 0, 0, 0, 255, 255, 255, 255 }; GLuint id; int a[3], b[3], c[3], d[3], ok;
    reset(0, 0, 0); glEnable(GL_TEXTURE_2D); id = mktex(GL_TEXTURE_2D, 2, 2, q, GL_NEAREST, GL_CLAMP);
    glEnable(GL_POINT_SPRITE_ARB); glTexEnvi(GL_POINT_SPRITE_ARB, GL_COORD_REPLACE_ARB, GL_TRUE); glPointSize(32);
    glColor3f(1, 1, 1); glBegin(GL_POINTS); glVertex2f(64, 64); glEnd();
    px(56, 56, &a[0], &a[1], &a[2]); px(72, 56, &b[0], &b[1], &b[2]); px(56, 72, &c[0], &c[1], &c[2]); px(72, 72, &d[0], &d[1], &d[2]);
    ok = !(a[0] == b[0] && a[1] == b[1] && a[2] == b[2]) && !(a[0] == c[0] && a[1] == c[1] && a[2] == c[2]) && !(a[0] == d[0] && a[1] == d[1] && a[2] == d[2]) && !(b[0] == c[0] && b[1] == c[1] && b[2] == c[2]);
    glDisable(GL_POINT_SPRITE_ARB); glDeleteTextures(1, &id);
    report("point sprite (texcoord across the point)", glerr() ? -1 : ok, 0);
}
static void t_point_params(void)
{
    GLfloat att[3] = { 0, 0, 0.0004f }; int r, g, b, on = 0, x;
    reset(0, 0, 0); glPointSize(1); glPointParameterfvARB(GL_POINT_DISTANCE_ATTENUATION_ARB, att); glColor3f(1, 1, 1);
    glMatrixMode(GL_MODELVIEW); glLoadIdentity(); glBegin(GL_POINTS); glVertex3f(64, 64, 0); glEnd();   /* eye distance 0 (ortho: d = 0 -> size 1/sqrt(0) clamps) */
    for (x = 56; x < 72; x++) { px(x, 64, &r, &g, &b); on += r > 128; }
    { att[0] = 1; att[1] = 0; att[2] = 0; glPointParameterfvARB(GL_POINT_DISTANCE_ATTENUATION_ARB, att); }
    report("point parameters (accepted)", glerr() ? -1 : 1, "attenuation accepted");
    (void)on;
}
static void t_displaylist(void)
{
    GLuint l; reset(0, 0, 0); l = glGenLists(1); glNewList(l, GL_COMPILE); quad(10, 10, 100, 100, 0, 1, 0, 1); glEndList(); glCallList(l); glDeleteLists(l, 1);
    report("display list", glerr() ? -1 : near3(50, 50, 0, 255, 0, 20), 0);
}
static void t_vertex_array(void)
{
    static const float v[8] = { 10, 10, 100, 10, 100, 100, 10, 100 }; reset(0, 0, 0); glColor3f(1, 0, 1); glEnableClientState(GL_VERTEX_ARRAY); glVertexPointer(2, GL_FLOAT, 0, v);
    glDrawArrays(GL_QUADS, 0, 4); glDisableClientState(GL_VERTEX_ARRAY);
    report("vertex arrays", glerr() ? -1 : near3(50, 50, 255, 0, 255, 20), 0);
}
static void t_lighting(void)
{
    GLfloat pos[4] = { 0, 0, 1, 0 }, amb[4] = { 0, 0, 0, 1 }, dif[4] = { 1, 0, 0, 1 };
    reset(0, 0, 0); glEnable(GL_LIGHTING); glEnable(GL_LIGHT0); glLightfv(GL_LIGHT0, GL_POSITION, pos); glLightfv(GL_LIGHT0, GL_DIFFUSE, dif); glMaterialfv(GL_FRONT_AND_BACK, GL_AMBIENT_AND_DIFFUSE, dif);
    glLightModelfv(GL_LIGHT_MODEL_AMBIENT, amb); glNormal3f(0, 0, 1); glBegin(GL_QUADS); glVertex2f(10, 10); glVertex2f(100, 10); glVertex2f(100, 100); glVertex2f(10, 100); glEnd();
    glDisable(GL_LIGHTING);
    report("lighting (software T&L)", glerr() ? -1 : near3(50, 50, 255, 0, 0, 30), 0);
}
static void t_fog_coord(void)
{
    reset(0, 0, 0); glEnable(GL_FOG); glFogi(GL_FOG_MODE, GL_LINEAR); glFogf(GL_FOG_START, 0); glFogf(GL_FOG_END, 1); { GLfloat fc[4] = { 0, 0, 1, 1 }; glFogfv(GL_FOG_COLOR, fc); }
    glFogi(GL_FOG_COORDINATE_SOURCE_EXT, GL_FOG_COORDINATE_EXT);
    glColor3f(1, 0, 0); glBegin(GL_QUADS); glFogCoordfEXT(1.0f); glVertex2f(10, 10); glFogCoordfEXT(1.0f); glVertex2f(100, 10); glFogCoordfEXT(1.0f); glVertex2f(100, 100); glFogCoordfEXT(1.0f); glVertex2f(10, 100); glEnd();
    glDisable(GL_FOG);
    report("fog coordinate", glerr() ? -1 : near3(50, 50, 0, 0, 255, 40), 0);
}
static void t_secondary_color(void)
{
    reset(0, 0, 0); glEnable(GL_COLOR_SUM_EXT); glColor3f(0.5f, 0, 0); glSecondaryColor3fEXT(0, 0.5f, 0); quad(10, 10, 100, 100, 0.5f, 0, 0, 1);
    glDisable(GL_COLOR_SUM_EXT);
    report("secondary colour (colour sum)", glerr() ? -1 : near3(50, 50, 128, 128, 0, 30), "colour sum: primary (0.5,0,0) + secondary (0,0.5,0)");
}
static void t_stencil_basic(void)
{
    reset(0, 0, 0); glEnable(GL_STENCIL_TEST); glClearStencil(0); glClear(GL_STENCIL_BUFFER_BIT); glStencilFunc(GL_ALWAYS, 1, 0xff); glStencilOp(GL_KEEP, GL_KEEP, GL_INCR);
    glColorMask(0, 0, 0, 0); quad(10, 10, 100, 100, 1, 1, 1, 1); quad(10, 10, 50, 50, 1, 1, 1, 1); glColorMask(1, 1, 1, 1);
    glStencilFunc(GL_EQUAL, 2, 0xff); glStencilOp(GL_KEEP, GL_KEEP, GL_KEEP); quad(0, 0, 128, 128, 0, 1, 0, 1); glDisable(GL_STENCIL_TEST);
    report("stencil (INCR twice, EQUAL 2)", glerr() ? -1 : (near3(30, 30, 0, 255, 0, 20) && near3(80, 80, 0, 0, 0, 20)), 0);
}
static void t_stencil_twoside(void)
{
    reset(0, 0, 0); glEnable(GL_STENCIL_TEST_TWO_SIDE_EXT); glEnable(GL_STENCIL_TEST); glClearStencil(0); glClear(GL_STENCIL_BUFFER_BIT);
    glActiveStencilFaceEXT(GL_FRONT); glStencilFunc(GL_ALWAYS, 0, 0xff); glStencilOp(GL_KEEP, GL_KEEP, GL_INCR);
    glActiveStencilFaceEXT(GL_BACK); glStencilFunc(GL_ALWAYS, 0, 0xff); glStencilOp(GL_KEEP, GL_KEEP, GL_INCR_WRAP);
    glActiveStencilFaceEXT(GL_FRONT);
    glColorMask(0, 0, 0, 0); quad(10, 10, 100, 100, 1, 1, 1, 1); glColorMask(1, 1, 1, 1);
    glDisable(GL_STENCIL_TEST_TWO_SIDE_EXT); glStencilFunc(GL_EQUAL, 1, 0xff); glStencilOp(GL_KEEP, GL_KEEP, GL_KEEP); quad(0, 0, 128, 128, 0, 1, 0, 1); glDisable(GL_STENCIL_TEST);
    report("stencil two-sided + wrap", glerr() ? -1 : (near3(50, 50, 0, 255, 0, 20) && near3(120, 120, 0, 0, 0, 20)), 0);
}

/* ---- queries, programs ------------------------------------------------------- */
static void t_occlusion(void)
{
    GLuint q = 0; GLuint n = 0; GLint avail = 0; int i;
    reset(0, 0, 0); glGenQueriesARB(1, &q); if (glerr()) { report("occlusion query", -1, "no entry"); return; }
    glBeginQueryARB(GL_SAMPLES_PASSED_ARB, q); quad(10, 10, 60, 60, 1, 1, 1, 1); glEndQueryARB(GL_SAMPLES_PASSED_ARB);
    for (i = 0; i < 100 && !avail; i++) glGetQueryObjectivARB(q, GL_QUERY_RESULT_AVAILABLE_ARB, &avail);
    glGetQueryObjectuivARB(q, GL_QUERY_RESULT_ARB, &n);
    { char nm[64]; snprintf(nm, sizeof nm, "samples passed = %u (want ~2500)", n); report("occlusion query", glerr() ? -1 : (n > 2000 && n < 3000), nm); }
    glDeleteQueriesARB(1, &q);
}
static void t_vertex_program(void)
{
    static const char *prog = "!!ARBvp1.0\nPARAM mvp[4] = { state.matrix.mvp };\nDP4 result.position.x, mvp[0], vertex.position;\nDP4 result.position.y, mvp[1], vertex.position;\nDP4 result.position.z, mvp[2], vertex.position;\nDP4 result.position.w, mvp[3], vertex.position;\nMOV result.color, program.local[0];\nEND\n";
    GLuint id; reset(0, 0, 0); glGenProgramsARB(1, &id); glBindProgramARB(GL_VERTEX_PROGRAM_ARB, id); glProgramStringARB(GL_VERTEX_PROGRAM_ARB, GL_PROGRAM_FORMAT_ASCII_ARB, (GLsizei)strlen(prog), prog);
    if (glerr()) { report("ARB vertex program", -1, "program rejected"); return; }
    glProgramLocalParameter4fARB(GL_VERTEX_PROGRAM_ARB, 0, 0, 1, 0, 1); glEnable(GL_VERTEX_PROGRAM_ARB); quad(10, 10, 100, 100, 1, 0, 0, 1); glDisable(GL_VERTEX_PROGRAM_ARB);
    report("ARB vertex program", glerr() ? -1 : near3(50, 50, 0, 255, 0, 20), 0);
}
static void t_fragment_program(void)
{
    static const char *prog = "!!ARBfp1.0\nMOV result.color, program.local[0];\nEND\n"; GLuint id;
    reset(0, 0, 0); glGenProgramsARB(1, &id); glBindProgramARB(GL_FRAGMENT_PROGRAM_ARB, id); glProgramStringARB(GL_FRAGMENT_PROGRAM_ARB, GL_PROGRAM_FORMAT_ASCII_ARB, (GLsizei)strlen(prog), prog);
    if (glerr()) { report("ARB fragment program", -1, "program rejected"); return; }
    glProgramLocalParameter4fARB(GL_FRAGMENT_PROGRAM_ARB, 0, 0, 1, 0, 1); glEnable(GL_FRAGMENT_PROGRAM_ARB); quad(10, 10, 100, 100, 1, 0, 0, 1); glDisable(GL_FRAGMENT_PROGRAM_ARB);
    report("ARB fragment program", glerr() ? -1 : near3(50, 50, 0, 255, 0, 20), 0);
}
static void t_depth_clamp_bounds(void)
{
    reset(0, 0, 0); glEnable(GL_DEPTH_CLAMP_NV); glDisable(GL_DEPTH_CLAMP_NV);
    report("depth clamp (accepted)", glerr() ? -1 : 1, 0);
}


/* ---- extensions the Rage renderer ADVERTISES (GLEngine filters the list by the driver's caps) ------------------------------------------------------- */
static void t_fence(void)
{
    GLuint f = 0; int ok = 1;
    reset(0, 0, 0); glGenFencesAPPLE(1, &f); quad(10, 10, 100, 100, 1, 0, 0, 1); glSetFenceAPPLE(f); glFinishFenceAPPLE(f);
    ok = glTestFenceAPPLE(f) && near3(50, 50, 255, 0, 0, 20); glDeleteFencesAPPLE(1, &f);
    report("APPLE_fence (set, finish, test)", glerr() ? -1 : ok, 0);
}
static void t_flush_render(void)
{
    reset(0, 0, 0); quad(10, 10, 100, 100, 0, 1, 0, 1); glFlushRenderAPPLE();
    report("APPLE_flush_render", glerr() ? -1 : near3(50, 50, 0, 255, 0, 20), 0);
}
static void t_vao(void)
{
    static const float v[8] = { 10, 10, 100, 10, 100, 100, 10, 100 }; GLuint a = 0;
    reset(0, 0, 0); glGenVertexArraysAPPLE(1, &a); glBindVertexArrayAPPLE(a); glEnableClientState(GL_VERTEX_ARRAY); glVertexPointer(2, GL_FLOAT, 0, v);
    glBindVertexArrayAPPLE(0); glBindVertexArrayAPPLE(a); glColor3f(0, 0, 1); glDrawArrays(GL_QUADS, 0, 4); glBindVertexArrayAPPLE(0); glDisableClientState(GL_VERTEX_ARRAY);
    report("APPLE_vertex_array_object", glerr() ? -1 : near3(50, 50, 0, 0, 255, 20), 0);
}
static void t_multidraw(void)
{
    static const float v[16] = { 10, 10, 50, 10, 50, 50, 10, 50, 60, 60, 100, 60, 100, 100, 60, 100 }; GLint first[2] = { 0, 4 }; GLsizei cnt[2] = { 4, 4 };
    reset(0, 0, 0); glColor3f(1, 1, 0); glEnableClientState(GL_VERTEX_ARRAY); glVertexPointer(2, GL_FLOAT, 0, v); glMultiDrawArraysEXT(GL_QUADS, first, cnt, 2); glDisableClientState(GL_VERTEX_ARRAY);
    report("EXT_multi_draw_arrays", glerr() ? -1 : (near3(30, 30, 255, 255, 0, 20) && near3(80, 80, 255, 255, 0, 20) && near3(80, 30, 0, 0, 0, 20)), 0);
}
static void t_range_elements(void)
{
    static const float v[8] = { 10, 10, 100, 10, 100, 100, 10, 100 }; static const unsigned short idx[4] = { 0, 1, 2, 3 };
    reset(0, 0, 0); glColor3f(0, 1, 1); glEnableClientState(GL_VERTEX_ARRAY); glVertexPointer(2, GL_FLOAT, 0, v); glDrawRangeElementsEXT(GL_QUADS, 0, 3, 4, GL_UNSIGNED_SHORT, idx); glDisableClientState(GL_VERTEX_ARRAY);
    report("EXT_draw_range_elements", glerr() ? -1 : near3(50, 50, 0, 255, 255, 20), 0);
}
static void t_genmip(void)
{
    static unsigned char t[64 * 64 * 3]; GLuint id; int i; for (i = 0; i < 64 * 64; i++) { t[i * 3] = 255; t[i * 3 + 1] = 0; t[i * 3 + 2] = 0; }
    reset(0, 0, 0); glEnable(GL_TEXTURE_2D); glGenTextures(1, &id); glBindTexture(GL_TEXTURE_2D, id);
    glTexParameteri(GL_TEXTURE_2D, GL_GENERATE_MIPMAP_SGIS, GL_TRUE); glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR_MIPMAP_NEAREST); glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glPixelStorei(GL_UNPACK_ALIGNMENT, 1); glTexImage2D(GL_TEXTURE_2D, 0, GL_RGB, 64, 64, 0, GL_RGB, GL_UNSIGNED_BYTE, t);
    glColor3f(1, 1, 1); glBegin(GL_QUADS); glTexCoord2f(0, 0); glVertex2f(10, 10); glTexCoord2f(1, 0); glVertex2f(18, 10); glTexCoord2f(1, 1); glVertex2f(18, 18); glTexCoord2f(0, 1); glVertex2f(10, 18); glEnd();
    report("SGIS_generate_mipmap (minified draw uses generated levels)", glerr() ? -1 : near3(14, 14, 255, 0, 0, 30), 0); glDeleteTextures(1, &id);
}
static void t_windowpos(void)
{
    static unsigned char pix[8 * 8 * 3]; int i; for (i = 0; i < 64; i++) { pix[i * 3] = 0; pix[i * 3 + 1] = 255; pix[i * 3 + 2] = 0; }
    reset(0, 0, 0); glWindowPos2iARB(40, 40); glPixelStorei(GL_UNPACK_ALIGNMENT, 1); glDrawPixels(8, 8, GL_RGB, GL_UNSIGNED_BYTE, pix);
    report("ARB_window_pos + glDrawPixels", glerr() ? -1 : near3(43, 43, 0, 255, 0, 20), 0);
}

/* ---- pixel formats (accumulation, aux, 32 bit, multisample) ------------------------ */
static void pf_test(const char *name, GLint *attrs, GDHandle gd)
{
    AGLPixelFormat pf = aglChoosePixelFormat(&gd, gd ? 1 : 0, attrs); GLint rid = 0;
    if (pf) aglDescribePixelFormat(pf, AGL_RENDERER_ID, &rid);
    { char n[64]; snprintf(n, sizeof n, "renderer 0x%x", (unsigned)rid); report(name, pf ? 1 : -1, pf ? n : "no accelerated pixel format"); }
    if (pf) aglDestroyPixelFormat(pf);
}
static void pf_tests(GDHandle gd)
{
    GLint a1[] = { AGL_RGBA, AGL_DOUBLEBUFFER, AGL_ACCELERATED, AGL_DEPTH_SIZE, 16, AGL_ACCUM_RED_SIZE, 16, AGL_ACCUM_GREEN_SIZE, 16, AGL_ACCUM_BLUE_SIZE, 16, AGL_NONE };
    GLint a2[] = { AGL_RGBA, AGL_DOUBLEBUFFER, AGL_ACCELERATED, AGL_DEPTH_SIZE, 16, AGL_AUX_BUFFERS, 2, AGL_NONE };
    GLint a3[] = { AGL_RGBA, AGL_DOUBLEBUFFER, AGL_ACCELERATED, AGL_DEPTH_SIZE, 16, AGL_STENCIL_SIZE, 8, AGL_NONE };
    GLint a4[] = { AGL_RGBA, AGL_DOUBLEBUFFER, AGL_ACCELERATED, AGL_DEPTH_SIZE, 16, AGL_ALPHA_SIZE, 8, AGL_NONE };
    GLint a5[] = { AGL_RGBA, AGL_DOUBLEBUFFER, AGL_ACCELERATED, AGL_DEPTH_SIZE, 16, AGL_SAMPLE_BUFFERS_ARB, 1, AGL_SAMPLES_ARB, 4, AGL_NONE };
    GLint a6[] = { AGL_RGBA, AGL_DOUBLEBUFFER, AGL_ACCELERATED, AGL_DEPTH_SIZE, 24, AGL_NONE };
    GLint a7[] = { AGL_RGBA, AGL_ACCELERATED, AGL_DEPTH_SIZE, 16, AGL_NONE };
    GLint a8[] = { AGL_RGBA, AGL_DOUBLEBUFFER, AGL_ACCELERATED, AGL_PIXEL_SIZE, 32, AGL_DEPTH_SIZE, 16, AGL_NONE };
    pf_test("pixel format: accumulation buffer", a1, gd); pf_test("pixel format: aux buffers", a2, gd); pf_test("pixel format: stencil 8", a3, gd);
    pf_test("pixel format: destination alpha 8", a4, gd); pf_test("pixel format: multisample", a5, gd); pf_test("pixel format: depth 24", a6, gd);
    pf_test("pixel format: single buffered", a7, gd); pf_test("pixel format: 32-bit pixels", a8, gd);
}
static void t_accum(void)
{
    int r, g, b;
    reset(0, 0, 0); glClearAccum(0, 0, 0, 0); glClear(GL_ACCUM_BUFFER_BIT);
    quad(0, 0, 128, 128, 0.5f, 0.25f, 0.0f, 1); glAccum(GL_LOAD, 1.0f); glClear(GL_COLOR_BUFFER_BIT); quad(0, 0, 128, 128, 0.0f, 0.25f, 0.5f, 1); glAccum(GL_ACCUM, 1.0f);
    glAccum(GL_RETURN, 1.0f);
    px(50, 50, &r, &g, &b);
    { char n[64]; snprintf(n, sizeof n, "%d,%d,%d (want 128,128,128)", r, g, b); report("accumulation buffer (LOAD, ACCUM, RETURN)", glerr() ? -1 : near3(50, 50, 128, 128, 128, 30), n); }
}

static void t_accum2(void)
{
    int r, g, b; char n[96];
    reset(0, 0, 0); quad(0, 0, 128, 128, 0.5f, 0.25f, 0.0f, 1); glAccum(GL_LOAD, 1.0f); glClear(GL_COLOR_BUFFER_BIT); glAccum(GL_RETURN, 1.0f); px(50, 50, &r, &g, &b);
    snprintf(n, sizeof n, "LOAD then RETURN: %d,%d,%d (want 128,64,0)", r, g, b); report("accum: LOAD + RETURN", glerr() ? -1 : near3(50, 50, 128, 64, 0, 20), n);
    reset(0, 0, 0); glClearAccum(0.25f, 0.25f, 0.25f, 0); glClear(GL_ACCUM_BUFFER_BIT); glAccum(GL_RETURN, 1.0f); px(50, 50, &r, &g, &b);
    snprintf(n, sizeof n, "ClearAccum 0.25 then RETURN: %d,%d,%d (want 64,64,64)", r, g, b); report("accum: glClearAccum + RETURN", glerr() ? -1 : near3(50, 50, 64, 64, 64, 20), n);
}
#include "gl_probe_audit.inc"
typedef void (*tfn)(void);
static const struct { const char *name; tfn fn; const char *ext; } TESTS[] = {
    { "clearcolor", t_clearcolor, 0 }, { "blend_subtract", t_blend_subtract, "GL_EXT_blend_subtract" }, { "blend_revsub", t_blend_revsub, "GL_EXT_blend_subtract" }, { "blend_min", t_blend_min, "GL_EXT_blend_minmax" },
    { "blend_max", t_blend_max, "GL_EXT_blend_minmax" }, { "blend_color", t_blend_color, "GL_EXT_blend_color" }, { "blend_sep", t_blend_sep, "GL_EXT_blend_func_separate" },
    { "blend_dstcolor", t_blend_dstcolor, 0 }, { "blend_srccolor", t_blend_srccolor_dst, 0 }, { "blend_satur", t_blend_satur, 0 },
    { "line_smooth", t_line_smooth, 0 }, { "point_smooth", t_point_smooth, 0 }, { "line_stipple", t_line_stipple, 0 }, { "line_stipple3", t_line_stipple3, 0 }, { "polygon_offset", t_polygon_offset, 0 },
    { "polymode_line", t_polygon_mode_line, 0 }, { "polymode_point", t_polygon_mode_point, 0 }, { "cull", t_cull, 0 }, { "scissor", t_scissor, 0 }, { "clip_plane", t_clip_plane, 0 },
    { "logic", t_alpha_logic, 0 },
    { "tex_npot", t_tex_npot, "GL_ARB_texture_non_power_of_two" }, { "tex_rect", t_tex_rect, "GL_EXT_texture_rectangle" }, { "tex_cube", t_tex_cube, "GL_ARB_texture_cube_map" },
    { "wrap_mirror", t_wrap_mirror, "GL_ARB_texture_mirrored_repeat" }, { "wrap_mirror_clamp", t_wrap_mirror_clamp, "GL_EXT_texture_mirror_clamp" },
    { "wrap_edge", t_wrap_edge, 0 }, { "wrap_border", t_wrap_border, "GL_ARB_texture_border_clamp" }, { "wrap_repeat", t_wrap_repeat, 0 },
    { "tex1x1", t_tex1x1, 0 }, { "envstate", t_envstate, 0 }, { "cpu_persp", t_cpu_persp, 0 }, { "env_add", t_env_add, "GL_ARB_texture_env_add" }, { "combine_addsigned", t_combine_addsigned, "GL_ARB_texture_env_combine" }, { "combine_subtract", t_combine_interp, "GL_ARB_texture_env_combine" },
    { "combine_dot3", t_combine_dot3, "GL_ARB_texture_env_dot3" }, { "tex_s3tc", t_tex_s3tc, "GL_EXT_texture_compression_s3tc" }, { "tex_paletted", t_tex_paletted, "GL_EXT_paletted_texture" },
    { "tex_aniso", t_tex_aniso, "GL_EXT_texture_filter_anisotropic" }, { "tex_3d", t_tex_3d, 0 }, { "copyteximage", t_copyteximage, 0 }, { "copytexsub", t_copytexsub, 0 },
    { "point_sprite", t_point_sprite, "GL_ARB_point_sprite" }, { "point_params", t_point_params, "GL_ARB_point_parameters" }, { "displaylist", t_displaylist, 0 }, { "vertex_array", t_vertex_array, 0 },
    { "lighting", t_lighting, 0 }, { "fog_coord", t_fog_coord, "GL_EXT_fog_coord" }, { "secondary_color", t_secondary_color, "GL_EXT_secondary_color" },
    { "stencil", t_stencil_basic, 0 }, { "stencil_twoside", t_stencil_twoside, "GL_EXT_stencil_two_side" },
    { "occlusion", t_occlusion, "GL_ARB_occlusion_query" }, { "vertex_program", t_vertex_program, "GL_ARB_vertex_program" }, { "fragment_program", t_fragment_program, "GL_ARB_fragment_program" },
    { "depth_clamp", t_depth_clamp_bounds, "GL_NV_depth_clamp" }, { "accum", t_accum, 0 }, { "accum2", t_accum2, 0 },
    { "fence", t_fence, "GL_APPLE_fence" }, { "flush_render", t_flush_render, "GL_APPLE_flush_render" }, { "vao", t_vao, "GL_APPLE_vertex_array_object" }, { "multidraw", t_multidraw, "GL_EXT_multi_draw_arrays" },
    { "range_elements", t_range_elements, "GL_EXT_draw_range_elements" }, { "genmip", t_genmip, "GL_SGIS_generate_mipmap" }, { "windowpos", t_windowpos, "GL_ARB_window_pos" },
    AUDIT_ENTRIES
};

int main(int argc, char **argv)
{
    int bx = getenv("AGL_MAIN") ? 0 : 1920, i, a, run; Rect bounds = { 60, bx + 80, 60 + VW + 20, bx + 80 + VW + 20 }; WindowRef win = NULL; GDHandle gd = NULL; AGLPixelFormat pf; AGLContext ctx;
    GLint rid_req = getenv("PROBE_RID") ? (GLint)strtol(getenv("PROBE_RID"), 0, 0) : 0;
    GLint attrs[] = { AGL_RGBA, AGL_DOUBLEBUFFER, AGL_ACCELERATED, AGL_DEPTH_SIZE, 16, AGL_STENCIL_SIZE, 8, AGL_RENDERER_ID, 0, AGL_NONE };
    setvbuf(stdout, NULL, _IONBF, 0);
    setenv("RAGE_ADD_SIGNED", "exact", 0);                  /* the driver defaults to the fast hardware approximation; the probes check the exact path */
    if (CreateNewWindow(kDocumentWindowClass, kWindowStandardDocumentAttributes, &bounds, &win) || !win) { printf("no window\n"); return 1; }
    ShowWindow(win);
    { CGDirectDisplayID ids[8]; CGDisplayCount nd = 0, k; CGGetActiveDisplayList(8, ids, &nd);
      for (k = 0; k < nd; k++) if ((int)CGDisplayBounds(ids[k]).origin.x == bx) DMGetGDeviceByDisplayID((DisplayIDType)ids[k], &gd, false); }
    if (rid_req) attrs[8] = rid_req; else { attrs[7] = AGL_NONE; }
    pf = aglChoosePixelFormat(&gd, gd ? 1 : 0, attrs);
    if (!pf) { printf("no pixel format\n"); return 1; }
    { GLint rid = 0; aglDescribePixelFormat(pf, AGL_RENDERER_ID, &rid); printf("renderer id 0x%x\n", (unsigned)rid); }
    ctx = aglCreateContext(pf, NULL); g_ctx = ctx; aglSetDrawable(ctx, GetWindowPort(win)); aglSetCurrentContext(ctx);
    printf("GL_RENDERER %s | GL_VERSION %s\n", glGetString(GL_RENDERER), glGetString(GL_VERSION));
    g_exts = (const char *)glGetString(GL_EXTENSIONS);
    if (getenv("PROBE_EXTS")) { const char *q = g_exts; int n = 0; printf("GL_VENDOR %s\n", glGetString(GL_VENDOR)); while (q && *q) { const char *e = strchr(q, ' '); int l = e ? (int)(e - q) : (int)strlen(q); if (l) printf("EXT %.*s\n", l, q), n++; q = e ? e + 1 : 0; } printf("%d extensions\n", n);
        { GLint v; glGetIntegerv(GL_MAX_TEXTURE_SIZE, &v); printf("MAX_TEXTURE_SIZE %d\n", (int)v); glGetIntegerv(GL_MAX_TEXTURE_UNITS, &v); printf("MAX_TEXTURE_UNITS %d\n", (int)v); glGetIntegerv(GL_MAX_LIGHTS, &v); printf("MAX_LIGHTS %d\n", (int)v); glGetIntegerv(GL_MAX_CLIP_PLANES, &v); printf("MAX_CLIP_PLANES %d\n", (int)v); glGetIntegerv(GL_STENCIL_BITS, &v); printf("STENCIL_BITS %d\n", (int)v); glGetIntegerv(GL_DEPTH_BITS, &v); printf("DEPTH_BITS %d\n", (int)v); glGetIntegerv(GL_ALPHA_BITS, &v); printf("ALPHA_BITS %d\n", (int)v); glGetIntegerv(GL_MAX_ELEMENTS_VERTICES, &v); printf("MAX_ELEMENTS_VERTICES %d\n", (int)v); } }
    for (i = 0; i < (int)(sizeof TESTS / sizeof TESTS[0]); i++) {
        run = (argc < 2);
        for (a = 1; a < argc; a++) if (!strcmp(argv[a], TESTS[i].name)) run = 1;
        if (!run) continue;
        if (TESTS[i].ext && !have_ext(TESTS[i].ext)) { printf("not advertised: %s (%s) -- running anyway\n", TESTS[i].name, TESTS[i].ext); }
        TESTS[i].fn(); if (getenv("PROBE_NORESET") == 0 && 0) {}
    }
    if (argc < 2 || !strcmp(argv[1], "pf")) pf_tests(gd);
    if (getenv("PROBE_HOLD")) { aglSwapBuffers(ctx); sleep(atoi(getenv("PROBE_HOLD"))); }
    printf("\nPROBE: %d pass, %d fail, %d unsupported\n", g_pass, g_fail, g_unsup);
    return 0;
}

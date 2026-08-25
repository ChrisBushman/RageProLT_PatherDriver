/*
 * mach64_texsmall.c - validate uploads of textures narrower than 8 texels (the
 * w<8 linear-blit path).  A solid colour would look correct even with a wrong row
 * stride, so every texture here is PATTERNED: a wrong stride shears/scrambles the
 * pattern, a correct one shows it cleanly under NEAREST magnification.
 *
 *   Q0 1x1  solid magenta            (degenerate: pitch=1 linear, sanity)
 *   Q1 2x2  R G / B W                (4 colour quadrants)
 *   Q2 4x4  checkerboard yellow/blue (4x4 checker)
 *   Q3 4x8  vertical stripes (w<8, tall)   -> R G B W columns
 *   Q4 8x8  H stripes (control, proven path)-> red top half / green bottom
 *
 * PASS: Q1 four quadrants, Q2 a clean 4x4 checker, Q3 four vertical colour
 * stripes, Q4 red-over-green -- none sheared/repeated/scrambled.
 * FAIL (bad stride): Q1/Q2/Q3 diagonally smeared or colour-shifted per row.
 * Build: cc -framework Carbon -framework AGL -framework OpenGL
 */
#include <Carbon/Carbon.h>
#include <AGL/agl.h>
#include <OpenGL/gl.h>
#include <stdio.h>
#include <unistd.h>

/* rgb[] is w*h*3 bytes, row-major. */
static GLuint make_tex(int w, int h, const GLubyte *rgb)
{
    GLuint t = 0;
    glGenTextures(1, &t);
    glBindTexture(GL_TEXTURE_2D, t);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGB, w, h, 0, GL_RGB, GL_UNSIGNED_BYTE, rgb);
    return t;
}

static void quad(GLuint t, int slot)
{
    float x0 = -0.95f + (float)slot * 0.39f, x1 = x0 + 0.32f;
    GLfloat v[12] = { x0,-0.5f, x1,-0.5f, x1,0.5f,  x0,-0.5f, x1,0.5f, x0,0.5f };
    GLfloat tc[12]= { 0,0, 1,0, 1,1,  0,0, 1,1, 0,1 };
    glBindTexture(GL_TEXTURE_2D, t);
    glVertexPointer(2, GL_FLOAT, 0, v);
    glTexCoordPointer(2, GL_FLOAT, 0, tc);
    glDrawArrays(GL_TRIANGLES, 0, 6);
    glFlush(); glFinish();
}

int main(void)
{
    GDHandle dev = GetMainDevice();
    GLint attribs[] = { AGL_RGBA, AGL_ACCELERATED, AGL_FULLSCREEN,
                        AGL_DEPTH_SIZE, 16, AGL_NONE };
    AGLPixelFormat pf = aglChoosePixelFormat(&dev, 1, attribs);
    AGLContext ctx;
    GLuint t1, t2, t4, t48, t8;
    static GLubyte p1[1*1*3], p2[2*2*3], p4[4*4*3], p48[4*8*3], p8[8*8*3];
    int x, y;
    if (!pf) { printf("no pf\n"); return 1; }
    ctx = aglCreateContext(pf, NULL);
    if (!ctx) return 1;
    aglSetFullScreen(ctx, 0, 0, 0, 0);
    aglSetCurrentContext(ctx);
    printf("GL_RENDERER=%s\n", (const char *)glGetString(GL_RENDERER));
    glViewport(0, 0, 1024, 768);
    /* Tightly-packed source rows: without this, GL's default 4-byte unpack
     * alignment mis-strides sub-4-wide RGB images before the driver sees them. */
    glPixelStorei(GL_UNPACK_ALIGNMENT, 1);

    /* 1x1 magenta */
    p1[0]=255; p1[1]=0; p1[2]=255;
    /* 2x2: (0,0)red (1,0)green (0,1)blue (1,1)white */
    {   GLubyte q[4][3] = {{255,0,0},{0,255,0},{0,0,255},{255,255,255}};
        for (y=0;y<2;y++) for (x=0;x<2;x++){ GLubyte *d=&p2[(y*2+x)*3];
            d[0]=q[y*2+x][0]; d[1]=q[y*2+x][1]; d[2]=q[y*2+x][2]; } }
    /* 4x4 checker yellow/blue */
    for (y=0;y<4;y++) for (x=0;x<4;x++){ GLubyte *d=&p4[(y*4+x)*3];
        int c=(x^y)&1; d[0]=c?255:0; d[1]=c?255:0; d[2]=c?0:255; }
    /* 4x8 vertical stripes: col0 red, col1 green, col2 blue, col3 white */
    {   GLubyte col[4][3]={{255,0,0},{0,255,0},{0,0,255},{255,255,255}};
        for (y=0;y<8;y++) for (x=0;x<4;x++){ GLubyte *d=&p48[(y*4+x)*3];
            d[0]=col[x][0]; d[1]=col[x][1]; d[2]=col[x][2]; } }
    /* 8x8 horizontal stripes: top 4 rows red, bottom 4 green (control) */
    for (y=0;y<8;y++) for (x=0;x<8;x++){ GLubyte *d=&p8[(y*8+x)*3];
        int top=y<4; d[0]=top?255:0; d[1]=top?0:255; d[2]=0; }

    t1  = make_tex(1,1,p1);
    t2  = make_tex(2,2,p2);
    t4  = make_tex(4,4,p4);
    t48 = make_tex(4,8,p48);
    t8  = make_tex(8,8,p8);

    glTexEnvi(GL_TEXTURE_ENV, GL_TEXTURE_ENV_MODE, GL_REPLACE);  /* show raw texels */
    glEnable(GL_TEXTURE_2D);
    glEnableClientState(GL_VERTEX_ARRAY);
    glEnableClientState(GL_TEXTURE_COORD_ARRAY);
    glClearColor(0.f, 0.f, 0.f, 1.f);
    glClear(GL_COLOR_BUFFER_BIT);

    printf("Q0 1x1 magenta / Q1 2x2 quadrants / Q2 4x4 checker / "
           "Q3 4x8 stripes / Q4 8x8 red-green\n");
    quad(t1,0); quad(t2,1); quad(t4,2); quad(t48,3); quad(t8,4);
    printf("drew 1x1,2x2,4x4,4x8,8x8\n"); fflush(stdout);
    sleep(5);
    aglSetCurrentContext(NULL); aglSetDrawable(ctx, NULL);
    aglDestroyContext(ctx); aglDestroyPixelFormat(pf);
    printf("done\n");
    return 0;
}

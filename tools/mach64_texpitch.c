/*
 * mach64_texpitch.c - measure the texture-unit ROW PITCH for small widths.
 * The 2x2 four-colour test (mach64_texsmall) showed the 2x2 top row read at a
 * pitch of 3 texels, not 2.  Checker/stripe patterns are shift-invariant and
 * hide this; SOLID PER-QUADRANT blocks turn a wrong pitch into a visible
 * stairstep at the block boundary.  Each texture's 4 quadrants are solid
 * red(TL) green(TR) blue(BL) white(BR) in TEXTURE space (t=0 is data row 0):
 *
 *   Q0 2x2 quadrants (1 texel each)  -> is the boundary a clean cross?
 *   Q1 4x4 quadrants (2x2 each)      -> does w=4 also stairstep (pitch 5)?
 *   Q2 8x8 quadrants (4x4 each)      -> control (rectangular blit, known good)
 *   Q3 4x4 columns R G B W           -> control (w=4 horizontal, known good)
 *
 * A clean cross on Q1 => w=4 row pitch correct, the 2x2 issue is width-specific
 * (a minimum pitch).  A stairstepped/wrapped Q1 => the pitch is wrong for w<8
 * generally.  Q2 must stay a clean cross either way.
 * Build: cc -framework Carbon -framework AGL -framework OpenGL
 */
#include <Carbon/Carbon.h>
#include <AGL/agl.h>
#include <OpenGL/gl.h>
#include <stdio.h>
#include <unistd.h>

/* Fill an n x n texture with 4 solid quadrants: TL red, TR green, BL blue,
 * BR white in texture space (data row 0 = the "bottom" half here: t<n/2). */
static GLuint quad_tex(int n)
{
    static GLubyte buf[64*64*3];
    int x, y; GLuint t = 0;
    GLubyte col[4][3] = {{255,0,0},{0,255,0},{0,0,255},{255,255,255}};
    for (y=0;y<n;y++) for (x=0;x<n;x++){
        int q = (x >= n/2 ? 1 : 0) + (y >= n/2 ? 2 : 0);
        GLubyte *d = &buf[(y*n+x)*3];
        d[0]=col[q][0]; d[1]=col[q][1]; d[2]=col[q][2];
    }
    glGenTextures(1,&t); glBindTexture(GL_TEXTURE_2D,t);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGB, n, n, 0, GL_RGB, GL_UNSIGNED_BYTE, buf);
    return t;
}

/* n x n with 4 solid vertical COLUMNS split (only meaningful for n>=4). */
static GLuint col_tex(int n)
{
    static GLubyte buf[64*64*3];
    int x, y; GLuint t = 0;
    GLubyte col[4][3] = {{255,0,0},{0,255,0},{0,0,255},{255,255,255}};
    for (y=0;y<n;y++) for (x=0;x<n;x++){
        GLubyte *d = &buf[(y*n+x)*3]; int c = (x*4)/n;
        d[0]=col[c][0]; d[1]=col[c][1]; d[2]=col[c][2];
    }
    glGenTextures(1,&t); glBindTexture(GL_TEXTURE_2D,t);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGB, n, n, 0, GL_RGB, GL_UNSIGNED_BYTE, buf);
    return t;
}

static void draw(GLuint t, int slot)
{
    float x0 = -0.95f + (float)slot * 0.48f, x1 = x0 + 0.40f;
    GLfloat v[12] = { x0,-0.6f, x1,-0.6f, x1,0.6f,  x0,-0.6f, x1,0.6f, x0,0.6f };
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
    GLuint q2, q4, q8, c4;
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
    q2 = quad_tex(2); q4 = quad_tex(4); q8 = quad_tex(8); c4 = col_tex(4);
    glTexEnvi(GL_TEXTURE_ENV, GL_TEXTURE_ENV_MODE, GL_REPLACE);
    glEnable(GL_TEXTURE_2D);
    glEnableClientState(GL_VERTEX_ARRAY);
    glEnableClientState(GL_TEXTURE_COORD_ARRAY);
    glClearColor(0.f, 0.f, 0.f, 1.f);
    glClear(GL_COLOR_BUFFER_BIT);
    printf("Q0 2x2quad Q1 4x4quad Q2 8x8quad(ctl) Q3 4x4cols(ctl)\n");
    draw(q2,0); draw(q4,1); draw(q8,2); draw(c4,3);
    printf("drew\n"); fflush(stdout);
    sleep(5);
    aglSetCurrentContext(NULL); aglSetDrawable(ctx, NULL);
    aglDestroyContext(ctx); aglDestroyPixelFormat(pf);
    printf("done\n");
    return 0;
}

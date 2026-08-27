/* mach64_texptln.c - textured POINTS and LINES (per-vertex texcoords).
 *
 * Texture encodes its own coordinates: texel(s,t) = (R=s, G=t, B=0.5).  So a textured POINT
 * shows a solid colour = its vertex's texel, and a textured LINE shows the texture
 * interpolated along its length.
 *
 * Top row: 4 big POINTS at texcoords (0,0)/(1,0)/(0,1)/(1,1) ->
 *   blue / magenta / green-cyan / light-yellow (R=s grows right, G=t grows with the 2nd coord).
 * Below: 3 LINES with texcoord gradients:
 *   A (0,0)->(1,0) blue->magenta,  B (0,1)->(1,1) green->yellow,  C (0,0)->(1,1) blue->yellow.
 * PASS: points are 4 distinct solid colours; lines are smooth colour gradients (NOT all one
 * flat colour, which would mean texel(0,0) sampled uniformly = the old behaviour).
 * Build: cc -framework Carbon -framework AGL -framework OpenGL */
#include <Carbon/Carbon.h>
#include <AGL/agl.h>
#include <OpenGL/gl.h>
#include <stdio.h>
#include <unistd.h>

static GLubyte texdata[64*64*3];
static void build_texture(void)
{
    int x, y;
    for (y = 0; y < 64; y++) for (x = 0; x < 64; x++) {
        GLubyte *p = &texdata[(y*64 + x)*3];
        p[0] = (GLubyte)(x * 255 / 63);   /* R = s */
        p[1] = (GLubyte)(y * 255 / 63);   /* G = t */
        p[2] = 128;                       /* B = const */
    }
}

/* 4 points */
static const GLfloat ppos[4*2] = { -0.6f,0.6f, -0.2f,0.6f, 0.2f,0.6f, 0.6f,0.6f };
static const GLfloat ptex[4*2] = {  0,0,        1,0,        0,1,       1,1 };
/* 3 lines (6 verts) */
static const GLfloat lpos[6*2] = { -0.7f,0.1f, -0.1f,0.1f,   0.1f,0.1f, 0.7f,0.1f,
                                   -0.7f,-0.5f, 0.7f,-0.5f };
static const GLfloat ltex[6*2] = {  0,0, 1,0,    0,1, 1,1,    0,0, 1,1 };

int main(void)
{
    GDHandle dev = GetMainDevice();
    GLint at[] = { AGL_RGBA, AGL_ACCELERATED, AGL_FULLSCREEN, AGL_NONE };
    AGLPixelFormat pf = aglChoosePixelFormat(&dev, 1, at);
    AGLContext ctx;

    build_texture();
    if (!pf) { printf("no pf\n"); return 1; }
    ctx = aglCreateContext(pf, NULL);
    if (!ctx) return 1;
    aglSetFullScreen(ctx, 0, 0, 0, 0);
    aglSetCurrentContext(ctx);
    printf("GL_RENDERER=%s\n", (const char *)glGetString(GL_RENDERER));
    glViewport(0, 0, 1024, 768);

    glEnable(GL_TEXTURE_2D);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTexEnvi(GL_TEXTURE_ENV, GL_TEXTURE_ENV_MODE, GL_MODULATE);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGB, 64, 64, 0, GL_RGB, GL_UNSIGNED_BYTE, texdata);
    glColor3f(1, 1, 1);

    glClearColor(0.15f, 0.15f, 0.15f, 1); glClear(GL_COLOR_BUFFER_BIT);

    glEnableClientState(GL_VERTEX_ARRAY);
    glEnableClientState(GL_TEXTURE_COORD_ARRAY);

    glPointSize(28.0f);
    glVertexPointer(2, GL_FLOAT, 0, ppos);
    glTexCoordPointer(2, GL_FLOAT, 0, ptex);
    glDrawArrays(GL_POINTS, 0, 4);

    glLineWidth(8.0f);
    glVertexPointer(2, GL_FLOAT, 0, lpos);
    glTexCoordPointer(2, GL_FLOAT, 0, ltex);
    glDrawArrays(GL_LINES, 0, 6);

    glFlush(); glFinish();
    printf("drew 4 textured points + 3 textured lines\n");
    fflush(stdout);
    sleep(8);

    aglSetCurrentContext(NULL); aglSetDrawable(ctx, NULL);
    aglDestroyContext(ctx); aglDestroyPixelFormat(pf);
    return 0;
}

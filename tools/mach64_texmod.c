/*
 * mach64_texmod.c - prove GL_MODULATE actually multiplies the texture by the
 * (non-white) vertex colour, instead of replacing it.  Every prior texture test
 * used glColor3f(1,1,1), where white*texel == texel, so MODULATE and REPLACE are
 * indistinguishable.  This drives four quads with non-white colour:
 *
 *   Q0  WHITE texture, flat glColor3f(1,0,0)         -> MODULATE: RED
 *                                                       REPLACE : white  (BUG)
 *   Q1  WHITE texture, flat glColor3f(0.25,0.5,1.0)  -> that light-blue tint
 *   Q2  ORANGE texture(255,128,0), flat gray(0.5)    -> darker orange (~128,64,0)
 *   Q3  WHITE texture, PER-VERTEX colour array       -> a red/green/blue gradient
 *                                                       (proves Gouraud colour is
 *                                                        interpolated under the
 *                                                        texture, not flat)
 *
 * PASS: Q0 red, Q1 light blue, Q2 dim orange, Q3 a smooth colour gradient.
 * FAIL (replace-like): every quad shows its raw texture colour (Q0/Q1/Q3 white,
 * Q2 full orange) -> the vertex colour is not reaching the texel modulator.
 * Build: cc -framework Carbon -framework AGL -framework OpenGL
 */
#include <Carbon/Carbon.h>
#include <AGL/agl.h>
#include <OpenGL/gl.h>
#include <stdio.h>
#include <unistd.h>

static GLuint make_tex(GLubyte r, GLubyte g, GLubyte b)
{
    static GLubyte px[8*8*3];
    GLuint t = 0; int i;
    for (i = 0; i < 8*8; i++) { px[i*3]=r; px[i*3+1]=g; px[i*3+2]=b; }
    glGenTextures(1, &t);
    glBindTexture(GL_TEXTURE_2D, t);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGB, 8, 8, 0, GL_RGB, GL_UNSIGNED_BYTE, px);
    return t;
}

/* Flat-colour textured quad in screen slot `slot` (colour set by the caller via
 * glColor before calling; GL_COLOR_ARRAY must be OFF so the flat colour is used). */
static void quad_flat(GLuint tex, int slot)
{
    float x0 = -0.95f + (float)slot * 0.48f, x1 = x0 + 0.40f;
    GLfloat v[12] = { x0,-0.5f, x1,-0.5f, x1,0.5f,  x0,-0.5f, x1,0.5f, x0,0.5f };
    GLfloat tc[12]= { 0,0, 1,0, 1,1,  0,0, 1,1, 0,1 };
    glBindTexture(GL_TEXTURE_2D, tex);
    glVertexPointer(2, GL_FLOAT, 0, v);
    glTexCoordPointer(2, GL_FLOAT, 0, tc);
    glDrawArrays(GL_TRIANGLES, 0, 6);
    glFlush(); glFinish();
}

/* Per-vertex colour textured quad: RGB corners -> a Gouraud gradient the texture
 * must modulate.  Enables GL_COLOR_ARRAY for the duration. */
static void quad_gradient(GLuint tex, int slot)
{
    float x0 = -0.95f + (float)slot * 0.48f, x1 = x0 + 0.40f;
    GLfloat v[12] = { x0,-0.5f, x1,-0.5f, x1,0.5f,  x0,-0.5f, x1,0.5f, x0,0.5f };
    GLfloat tc[12]= { 0,0, 1,0, 1,1,  0,0, 1,1, 0,1 };
    GLfloat c[18] = { 1,0,0,  0,1,0,  0,0,1,   1,0,0,  0,0,1,  1,1,0 };
    glBindTexture(GL_TEXTURE_2D, tex);
    glVertexPointer(2, GL_FLOAT, 0, v);
    glTexCoordPointer(2, GL_FLOAT, 0, tc);
    glColorPointer(3, GL_FLOAT, 0, c);
    glEnableClientState(GL_COLOR_ARRAY);
    glDrawArrays(GL_TRIANGLES, 0, 6);
    glDisableClientState(GL_COLOR_ARRAY);
    glFlush(); glFinish();
}

int main(void)
{
    GDHandle dev = GetMainDevice();
    GLint attribs[] = { AGL_RGBA, AGL_ACCELERATED, AGL_FULLSCREEN,
                        AGL_DEPTH_SIZE, 16, AGL_NONE };
    AGLPixelFormat pf = aglChoosePixelFormat(&dev, 1, attribs);
    AGLContext ctx;
    GLuint white, orange;
    if (!pf) { printf("no pf\n"); return 1; }
    ctx = aglCreateContext(pf, NULL);
    if (!ctx) return 1;
    aglSetFullScreen(ctx, 0, 0, 0, 0);
    aglSetCurrentContext(ctx);
    printf("GL_RENDERER=%s\n", (const char *)glGetString(GL_RENDERER));

    glViewport(0, 0, 1024, 768);
    white  = make_tex(255, 255, 255);
    orange = make_tex(255, 128, 0);
    glTexEnvi(GL_TEXTURE_ENV, GL_TEXTURE_ENV_MODE, GL_MODULATE);
    glEnable(GL_TEXTURE_2D);
    glEnableClientState(GL_VERTEX_ARRAY);
    glEnableClientState(GL_TEXTURE_COORD_ARRAY);
    glClearColor(0.f, 0.f, 0.f, 1.f);
    glClear(GL_COLOR_BUFFER_BIT);

    printf("Q0 white*red        -> expect RED\n");
    glColor3f(1.f, 0.f, 0.f);        quad_flat(white,  0);
    printf("Q1 white*(.25,.5,1) -> expect light blue\n");
    glColor3f(0.25f, 0.5f, 1.f);     quad_flat(white,  1);
    printf("Q2 orange*gray(.5)  -> expect dim orange\n");
    glColor3f(0.5f, 0.5f, 0.5f);     quad_flat(orange, 2);
    printf("Q3 white*vtx-RGB    -> expect a colour gradient\n");
    glColor3f(1.f, 1.f, 1.f);        quad_gradient(white, 3);

    printf("drew Q0 red / Q1 lightblue / Q2 dim-orange / Q3 gradient\n");
    fflush(stdout);
    sleep(5);

    glDeleteTextures(1, &white);
    glDeleteTextures(1, &orange);
    aglSetCurrentContext(NULL); aglSetDrawable(ctx, NULL);
    aglDestroyContext(ctx); aglDestroyPixelFormat(pf);
    printf("done\n");
    return 0;
}

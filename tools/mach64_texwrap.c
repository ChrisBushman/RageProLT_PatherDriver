/*
 * mach64_texwrap.c - prove the driver honors GL_TEXTURE_WRAP_S/T.  An 8x8 texture
 * with a white corner marker (a 3x3 block) on dark blue is drawn over a quad with
 * texcoords 0..3, once with GL_REPEAT and once with GL_CLAMP:
 *   Q0 REPEAT -> the texture TILES 3x3: a grid of 9 white markers.
 *   Q1 CLAMP  -> one marker; the edge texels are clamped/stretched for s,t>1, so
 *                the rest is the (dark blue) edge colour -- NO tiling.
 *
 * PASS: Q0 shows a 3x3 grid of markers (repeat tiles), Q1 shows a single marker
 * (clamp).  Before wrap support (clamp was hardcoded) BOTH looked like Q1.
 * Build: cc -framework Carbon -framework AGL -framework OpenGL
 */
#include <Carbon/Carbon.h>
#include <AGL/agl.h>
#include <OpenGL/gl.h>
#include <stdio.h>
#include <unistd.h>

static GLuint marker_tex(void)
{
    static GLubyte px[8*8*3];
    int x, y; GLuint t = 0;
    for (y=0;y<8;y++) for (x=0;x<8;x++){
        GLubyte *d = &px[(y*8+x)*3];
        int mark = (x < 3 && y < 3);           /* white 3x3 corner marker */
        d[0]= mark?255:0; d[1]= mark?255:0; d[2]= mark?255:80;
    }
    glGenTextures(1,&t); glBindTexture(GL_TEXTURE_2D,t);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGB, 8, 8, 0, GL_RGB, GL_UNSIGNED_BYTE, px);
    return t;
}

static void quad(GLuint t, GLint wrap, int slot)
{
    float x0 = -0.9f + (float)slot * 0.95f, x1 = x0 + 0.8f;
    GLfloat v[12] = { x0,-0.7f, x1,-0.7f, x1,0.7f,  x0,-0.7f, x1,0.7f, x0,0.7f };
    GLfloat tc[12]= { 0,0, 3,0, 3,3,  0,0, 3,3, 0,3 };     /* texcoords 0..3 */
    glBindTexture(GL_TEXTURE_2D, t);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, wrap);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, wrap);
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
    GLuint tr, tc2;
    if (!pf) { printf("no pf\n"); return 1; }
    ctx = aglCreateContext(pf, NULL);
    if (!ctx) return 1;
    aglSetFullScreen(ctx, 0, 0, 0, 0);
    aglSetCurrentContext(ctx);
    printf("GL_RENDERER=%s\n", (const char *)glGetString(GL_RENDERER));
    glViewport(0, 0, 1024, 768);
    glPixelStorei(GL_UNPACK_ALIGNMENT, 1);
    tr  = marker_tex();     /* separate texobjs so each keeps its own wrap mode */
    tc2 = marker_tex();
    glTexEnvi(GL_TEXTURE_ENV, GL_TEXTURE_ENV_MODE, GL_REPLACE);
    glEnable(GL_TEXTURE_2D);
    glEnableClientState(GL_VERTEX_ARRAY);
    glEnableClientState(GL_TEXTURE_COORD_ARRAY);
    glClearColor(0.f, 0.f, 0.f, 1.f);
    glClear(GL_COLOR_BUFFER_BIT);
    printf("Q0 REPEAT -> 3x3 grid of markers; Q1 CLAMP -> single marker\n");
    quad(tr,  GL_REPEAT, 0);
    quad(tc2, GL_CLAMP,  1);
    printf("drew\n"); fflush(stdout);
    sleep(5);
    aglSetCurrentContext(NULL); aglSetDrawable(ctx, NULL);
    aglDestroyContext(ctx); aglDestroyPixelFormat(pf);
    printf("done\n");
    return 0;
}

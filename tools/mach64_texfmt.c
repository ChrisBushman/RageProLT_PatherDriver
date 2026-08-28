/* mach64_texfmt.c - does GL_LUMINANCE (and friends) work?  Uploads a 2x2 GL_LUMINANCE texture with
 * values 64/128/192/255, draws it NEAREST/REPLACE, and glReadPixels each texel: if it works each
 * reads back grey (L,L,L).  The driver's "mach64 tex:" log line shows the fmt GLEngine handed us --
 * 0x1908 means GLEngine EXPANDED luminance to RGBA (so it already works, no driver gap); a distinct
 * code (0x1909 GL_LUMINANCE) means we must convert it.  Build: cc -framework Carbon -framework AGL
 * -framework OpenGL */
#include <Carbon/Carbon.h>
#include <AGL/agl.h>
#include <OpenGL/gl.h>
#include <stdio.h>
#include <unistd.h>

#ifndef GL_TEXTURE_ENV
#define GL_TEXTURE_ENV       0x2300
#define GL_TEXTURE_ENV_MODE  0x2200
#define GL_REPLACE           0x1E01
#endif

static void rd(int x, int y, const char *w)
{
    GLubyte px[4]; px[0]=9;px[1]=9;px[2]=9;px[3]=9;
    glReadPixels(x, y, 1, 1, GL_RGBA, GL_UNSIGNED_BYTE, px);
    printf("READ %-12s (%d,%d) = %3d %3d %3d\n", w, x, y, px[0], px[1], px[2]);
}

int main(void)
{
    GDHandle dev = GetMainDevice();
    GLint at[] = { AGL_RGBA, AGL_ACCELERATED, AGL_FULLSCREEN, AGL_NONE };
    AGLPixelFormat pf = aglChoosePixelFormat(&dev, 1, at);
    AGLContext ctx;
    GLuint tid;
    GLubyte lum[4];   /* 2x2 luminance: (0,0)=64 (1,0)=128 (0,1)=192 (1,1)=255 */
    GLfloat verts[8], tc[8];

    lum[0]=64; lum[1]=128; lum[2]=192; lum[3]=255;
    verts[0]=400;verts[1]=300; verts[2]=600;verts[3]=300; verts[4]=600;verts[5]=500; verts[6]=400;verts[7]=500;
    tc[0]=0;tc[1]=0; tc[2]=1;tc[3]=0; tc[4]=1;tc[5]=1; tc[6]=0;tc[7]=1;

    if (!pf) { printf("no pf\n"); return 1; }
    ctx = aglCreateContext(pf, NULL); if (!ctx) return 1;
    aglSetFullScreen(ctx, 0, 0, 0, 0); aglSetCurrentContext(ctx);
    printf("GL_RENDERER=%s\n", (const char *)glGetString(GL_RENDERER));
    glViewport(0, 0, 1024, 768);
    glMatrixMode(GL_PROJECTION); glLoadIdentity(); glOrtho(0, 1024, 0, 768, -1, 1);
    glMatrixMode(GL_MODELVIEW);  glLoadIdentity();
    glClearColor(0, 0, 0.5f, 1); glClear(GL_COLOR_BUFFER_BIT);   /* blue bg -> distinct from grey */

    glEnable(GL_TEXTURE_2D);
    glTexEnvi(GL_TEXTURE_ENV, GL_TEXTURE_ENV_MODE, GL_REPLACE);
    glGenTextures(1, &tid); glBindTexture(GL_TEXTURE_2D, tid);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
    glPixelStorei(GL_UNPACK_ALIGNMENT, 1);   /* 2 luminance bytes/row -> no 4-byte row padding */
    glTexImage2D(GL_TEXTURE_2D, 0, GL_LUMINANCE, 2, 2, 0, GL_LUMINANCE, GL_UNSIGNED_BYTE, lum);

    glColor3f(1, 1, 1);
    glEnableClientState(GL_VERTEX_ARRAY);        glVertexPointer(2, GL_FLOAT, 0, verts);
    glEnableClientState(GL_TEXTURE_COORD_ARRAY); glTexCoordPointer(2, GL_FLOAT, 0, tc);
    glDrawArrays(GL_QUADS, 0, 4);
    glFinish();

    rd(450, 350, "L=64");    /* texel(0,0) -> expect  64  64  64 */
    rd(550, 350, "L=128");   /* texel(1,0) -> expect 128 128 128 */
    rd(450, 450, "L=192");   /* texel(0,1) -> expect 192 192 192 */
    rd(550, 450, "L=255");   /* texel(1,1) -> expect 255 255 255 */
    printf("grey = GL_LUMINANCE works;  blue/black/garbage = gap.  (check the tex log fmt)\n");
    printf("glGetError = 0x%x\n", (unsigned)glGetError());
    fflush(stdout); sleep(3);

    aglSetCurrentContext(NULL); aglSetDrawable(ctx, NULL);
    aglDestroyContext(ctx); aglDestroyPixelFormat(pf);
    return 0;
}

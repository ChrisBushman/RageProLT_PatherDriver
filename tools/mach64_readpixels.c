/* mach64_readpixels.c - does glReadPixels work?  Discovery + verify.
 *
 * Renders a known pattern (clear to a distinct colour + a white centre quad), then
 * glReadPixels a pixel from the CLEAR region and a pixel from the QUAD, and prints both.
 *   clear px == (64,128,191) and quad px == (255,255,255)  -> glReadPixels WORKS.
 *   zeros / garbage / wrong                                 -> a readback path is needed
 *                                                              (see which gld* entry fires).
 * glReadPixels uses GL's lower-left origin, so y is measured from the BOTTOM.
 * Build: cc -framework Carbon -framework AGL -framework OpenGL */
#include <Carbon/Carbon.h>
#include <AGL/agl.h>
#include <OpenGL/gl.h>
#include <stdio.h>
#include <unistd.h>

static void rd(int x, int y, const char *what)
{
    GLubyte px[4] = { 1,2,3,4 };
    glReadPixels(x, y, 1, 1, GL_RGBA, GL_UNSIGNED_BYTE, px);
    printf("READPIX %-10s (%d,%d) = %3d %3d %3d %3d\n", what, x, y, px[0],px[1],px[2],px[3]);
}

int main(void)
{
    GDHandle dev = GetMainDevice();
    GLint at[] = { AGL_RGBA, AGL_ACCELERATED, AGL_FULLSCREEN, AGL_NONE };
    AGLPixelFormat pf = aglChoosePixelFormat(&dev, 1, at);
    AGLContext ctx;
    GLfloat q[8] = { -0.3f,-0.3f, 0.3f,-0.3f, 0.3f,0.3f, -0.3f,0.3f };

    if (!pf) { printf("no pf\n"); return 1; }
    ctx = aglCreateContext(pf, NULL);
    if (!ctx) return 1;
    aglSetFullScreen(ctx, 0, 0, 0, 0);
    aglSetCurrentContext(ctx);
    printf("GL_RENDERER=%s\n", (const char *)glGetString(GL_RENDERER));
    glViewport(0, 0, 1024, 768);
    glDisable(GL_TEXTURE_2D);

    glClearColor(0.25f, 0.5f, 0.75f, 1.0f);   /* -> ~ (64,128,191) */
    glClear(GL_COLOR_BUFFER_BIT);
    glColor3f(1, 1, 1);
    glEnableClientState(GL_VERTEX_ARRAY);
    glVertexPointer(2, GL_FLOAT, 0, q);
    glDrawArrays(GL_QUADS, 0, 4);
    glFinish();

    rd( 80,  80, "clear");    /* corner -> clear colour        */
    rd(512, 384, "quad");     /* centre -> white quad          */
    printf("glGetError after readpixels = 0x%x (0=no error; 0x502=GL_INVALID_OPERATION)\n",
           (unsigned)glGetError());
    printf("expect clear=(64,128,191,*) quad=(255,255,255,*)\n");
    fflush(stdout);
    sleep(3);

    aglSetCurrentContext(NULL); aglSetDrawable(ctx, NULL);
    aglDestroyContext(ctx); aglDestroyPixelFormat(pf);
    return 0;
}

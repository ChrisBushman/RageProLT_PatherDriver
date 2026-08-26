/* mach64_dbgate.c - test the DOUBLE-BUFFER dispatch gate.  Requests a real
 * AGL_DOUBLEBUFFER pixel format (needs the driver built -DGLD_DBUF), then draws a
 * plain UNTEXTURED gouraud scene and swaps.  This isolates ONE question: does
 * GLEngine now dispatch the app's primitives (because gldAttachDrawable set the
 * DB gli flag word0|=2), or does it still route to no-op slot 0x60?
 *   PASS = a big triangle (red/green/blue corners) on black -> geometry dispatched.
 *   FAIL = black screen -> still routed to slot 0x60.
 * Build: cc -framework Carbon -framework AGL -framework OpenGL */
#include <Carbon/Carbon.h>
#include <AGL/agl.h>
#include <OpenGL/gl.h>
#include <stdio.h>
#include <unistd.h>

static void frame(void)
{
    glClearColor(0, 0, 0, 1);
    glClear(GL_COLOR_BUFFER_BIT);
    glBegin(GL_TRIANGLES);
        glColor3f(1, 0, 0); glVertex2f(-0.8f, -0.7f);
        glColor3f(0, 1, 0); glVertex2f( 0.8f, -0.7f);
        glColor3f(0, 0, 1); glVertex2f( 0.0f,  0.8f);
    glEnd();
    glFinish();
}

int main(void)
{
    GDHandle dev = GetMainDevice();
    GLint at[] = { AGL_RGBA, AGL_ACCELERATED, AGL_FULLSCREEN,
                   AGL_DOUBLEBUFFER, AGL_DEPTH_SIZE, 16, AGL_NONE };
    AGLPixelFormat pf = aglChoosePixelFormat(&dev, 1, at);
    AGLContext ctx; GLint dbl = -1;

    if (!pf) { printf("no DOUBLEBUFFER pixel format! (build driver -DGLD_DBUF)\n"); return 1; }
    aglDescribePixelFormat(pf, AGL_DOUBLEBUFFER, &dbl);
    ctx = aglCreateContext(pf, NULL);
    if (!ctx) { printf("no ctx\n"); return 1; }
    aglSetFullScreen(ctx, 0, 0, 0, 0);
    aglSetCurrentContext(ctx);
    printf("GL_RENDERER=%s  AGL_DOUBLEBUFFER=%d\n",
           (const char *)glGetString(GL_RENDERER), dbl);
    glViewport(0, 0, 1024, 768);
    glDisable(GL_TEXTURE_2D);

    frame();
    aglSwapBuffers(ctx);      /* -> gldFlushBuffer -> present */
    printf("swapped frame 1 (expect RGB triangle on black)\n"); fflush(stdout);
    sleep(6);

    aglSetCurrentContext(NULL);
    aglSetDrawable(ctx, NULL);
    aglDestroyContext(ctx);
    aglDestroyPixelFormat(pf);
    return 0;
}

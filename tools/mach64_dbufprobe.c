/*
 * mach64_dbufprobe.c - investigation step for double buffering.  Requests a
 * DOUBLE-BUFFERED pixel format and calls aglSwapBuffers, to learn (from the
 * driver log) whether (a) our pixel format yields a hardware double-buffered
 * context, (b) aglSwapBuffers actually drives gldFlushBuffer, and (c) the color
 * depth the context reports.  Draws two visibly different frames with a swap
 * between: if the driver is single-front-buffered today, the screen shows the
 * LAST frame drawn; a working double buffer would show only post-swap frames.
 *
 * Frame A: clear RED,  draw a green triangle top-left.   swap.
 * Frame B: clear BLUE, draw a white triangle bottom-right. swap.
 * Build: cc -framework Carbon -framework AGL -framework OpenGL
 */
#include <Carbon/Carbon.h>
#include <AGL/agl.h>
#include <OpenGL/gl.h>
#include <stdio.h>
#include <unistd.h>

static void tri(float x, float y, float r, float g, float b)
{
    glColor3f(r,g,b);
    glBegin(GL_TRIANGLES);
      glVertex2f(x,       y);
      glVertex2f(x+0.5f,  y);
      glVertex2f(x,       y-0.5f);
    glEnd();
}

int main(void)
{
    GDHandle dev = GetMainDevice();
    GLint attribs[] = { AGL_RGBA, AGL_ACCELERATED, AGL_FULLSCREEN,
                        AGL_DOUBLEBUFFER, AGL_DEPTH_SIZE, 16, AGL_NONE };
    AGLPixelFormat pf = aglChoosePixelFormat(&dev, 1, attribs);
    AGLContext ctx;
    GLint dbl = -1;
    if (!pf) { printf("no DOUBLEBUFFER pixel format!\n"); return 1; }
    aglDescribePixelFormat(pf, AGL_DOUBLEBUFFER, &dbl);
    printf("pf AGL_DOUBLEBUFFER = %d\n", dbl);
    ctx = aglCreateContext(pf, NULL); if (!ctx) { printf("no ctx\n"); return 1; }
    aglSetFullScreen(ctx, 0, 0, 0, 0); aglSetCurrentContext(ctx);
    printf("GL_RENDERER=%s\n", (const char *)glGetString(GL_RENDERER));
    glViewport(0, 0, 1024, 768);

    printf(">>> frame A: clear RED + green tri, then swap\n"); fflush(stdout);
    glClearColor(1.f, 0.f, 0.f, 1.f); glClear(GL_COLOR_BUFFER_BIT);
    tri(-0.8f, 0.8f, 0.f, 1.f, 0.f);
    aglSwapBuffers(ctx);
    sleep(2);

    printf(">>> frame B: clear BLUE + white tri, then swap\n"); fflush(stdout);
    glClearColor(0.f, 0.f, 1.f, 1.f); glClear(GL_COLOR_BUFFER_BIT);
    tri(0.3f, 0.3f, 1.f, 1.f, 1.f);
    aglSwapBuffers(ctx);
    sleep(4);

    printf("done\n"); fflush(stdout);
    aglSetCurrentContext(NULL); aglSetDrawable(ctx, NULL);
    aglDestroyContext(ctx); aglDestroyPixelFormat(pf);
    return 0;
}

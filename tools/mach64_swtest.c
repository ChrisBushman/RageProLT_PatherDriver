/*
 * mach64_swtest.c - SOFTWARE-renderer twin of mach64_fstest.c.
 *
 * Identical geometry (same triangle, same glDrawArrays path), but the pixel
 * format OMITS AGL_ACCELERATED, so with our bundle NOT installed the app falls
 * through to Apple's built-in software renderer.  The software renderer drives
 * GLEngine's T&L correctly, so breaking sub_4530 (the renderer-agnostic batch
 * builder) captures a KNOWN-GOOD populated batch + the gli context state that
 * produced it -- the reference to diff our empty 0x4bc hardware batch against.
 *
 * Build: cc -framework Carbon -framework AGL -framework OpenGL
 */
#include <Carbon/Carbon.h>
#include <AGL/agl.h>
#include <OpenGL/gl.h>
#include <stdio.h>
#include <unistd.h>

int main(void)
{
    GDHandle dev = GetMainDevice();
    /* NO AGL_ACCELERATED -> software renderer (with our bundle uninstalled). */
    GLint attribs[] = { AGL_RGBA, AGL_FULLSCREEN,
                        AGL_DEPTH_SIZE, 16, AGL_NONE };
    AGLPixelFormat pf = aglChoosePixelFormat(&dev, 1, attribs);
    printf("aglChoosePixelFormat(software) -> %p (err %d)\n",
           (void*)pf, (int)aglGetError());
    if (!pf) return 1;

    AGLContext ctx = aglCreateContext(pf, NULL);
    printf("aglCreateContext -> %p (err %d)\n", (void*)ctx, (int)aglGetError());
    if (!ctx) { aglDestroyPixelFormat(pf); return 1; }

    GLboolean ok = aglSetFullScreen(ctx, 0, 0, 0, 0);
    printf("aglSetFullScreen -> %d (err %d)\n", (int)ok, (int)aglGetError());

    aglSetCurrentContext(ctx);
    printf("GL_RENDERER = %s\n", (const char *)glGetString(GL_RENDERER));

    /* EXACT same geometry as mach64_fstest.c so the batches are comparable. */
    glViewport(0, 0, 1024, 768);
    glClearColor(0.1f, 0.2f, 0.4f, 1.f);
    glClear(GL_COLOR_BUFFER_BIT);
    {
        static const GLfloat verts[6]  = { -0.5f,-0.5f,  0.5f,-0.5f,  0.0f,0.5f };
        static const GLfloat cols[9]   = { 1,0,0,  0,1,0,  0,0,1 };
        glEnableClientState(GL_VERTEX_ARRAY);
        glVertexPointer(2, GL_FLOAT, 0, verts);
        glEnableClientState(GL_COLOR_ARRAY);
        glColorPointer(3, GL_FLOAT, 0, cols);
        glDrawArrays(GL_TRIANGLES, 0, 3);
    }
    glFlush();
    glFinish();
    printf("drew triangle (software) -- holding 2s\n");
    fflush(stdout);
    sleep(2);

    aglSetCurrentContext(NULL);
    aglSetDrawable(ctx, NULL);
    aglDestroyContext(ctx);
    aglDestroyPixelFormat(pf);
    printf("done (software)\n");
    return 0;
}

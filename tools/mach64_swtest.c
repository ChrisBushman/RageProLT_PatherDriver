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
#include <stdlib.h>
#include <unistd.h>

#define OW 1024
#define OH 768
int main(void)
{
    /* AGL_OFFSCREEN -> Apple software renderer into a malloc'd buffer; no
     * display capture, no window server, reliable.  Same viewport/geometry as
     * fstest so the T&L batch at sub_4530 is comparable. */
    GLint attribs[] = { AGL_RGBA, AGL_OFFSCREEN,
                        AGL_DEPTH_SIZE, 16, AGL_NONE };
    AGLPixelFormat pf = aglChoosePixelFormat(NULL, 0, attribs);
    printf("aglChoosePixelFormat(offscreen) -> %p (err %d)\n",
           (void*)pf, (int)aglGetError());
    if (!pf) return 1;

    AGLContext ctx = aglCreateContext(pf, NULL);
    printf("aglCreateContext -> %p (err %d)\n", (void*)ctx, (int)aglGetError());
    if (!ctx) { aglDestroyPixelFormat(pf); return 1; }

    void *buf = malloc(OW * OH * 4);
    GLboolean ok = aglSetOffScreen(ctx, OW, OH, OW * 4, buf);
    printf("aglSetOffScreen -> %d (err %d)\n", (int)ok, (int)aglGetError());

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

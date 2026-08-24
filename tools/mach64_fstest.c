/*
 * mach64_fstest.c - FULL-SCREEN accelerated GL via AGL, to trigger the
 * full-screen gldAttachDrawable (type 0x5a) and capture its drawable descriptor.
 *
 * Full-screen fits our engine-takeover model: the app captures the display and
 * we DMA straight to the framebuffer VRAM (no WindowServer compositing).  This
 * build ATTACHES ONLY (no render) so we can read the descriptor safely first;
 * it captures the display (brief black screen) then releases cleanly.
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
    GLint attribs[] = { AGL_RGBA, AGL_ACCELERATED, AGL_FULLSCREEN,
                        AGL_DEPTH_SIZE, 16, AGL_NONE };
    AGLPixelFormat pf = aglChoosePixelFormat(&dev, 1, attribs);
    printf("aglChoosePixelFormat(fullscreen) -> %p (err %d)\n",
           (void*)pf, (int)aglGetError());
    if (!pf) return 1;

    AGLContext ctx = aglCreateContext(pf, NULL);
    printf("aglCreateContext -> %p (err %d)\n", (void*)ctx, (int)aglGetError());
    if (!ctx) { aglDestroyPixelFormat(pf); return 1; }

    /* THE trigger: go full-screen at the current resolution -> gldAttachDrawable
     * (type 0x5a) with the full-screen surface descriptor. */
    GLboolean ok = aglSetFullScreen(ctx, 0, 0, 0, 0);
    printf("aglSetFullScreen -> %d (err %d)\n", (int)ok, (int)aglGetError());

    aglSetCurrentContext(ctx);
    printf("GL_RENDERER = %s\n", (const char *)glGetString(GL_RENDERER));

    /* Draw one gouraud triangle.  The render target is still the driver's
     * off-screen placeholder (not the visible framebuffer), so this validates
     * the framework -> render-slot -> DMA path WITHOUT writing to the screen.
     * The log (mach64_render_triangles + vtx0) shows if the dispatch fired. */
    glViewport(0, 0, 1024, 768);
    glBegin(GL_TRIANGLES);
        glColor3f(1.f, 0.f, 0.f); glVertex2f(-0.5f, -0.5f);
        glColor3f(0.f, 1.f, 0.f); glVertex2f( 0.5f, -0.5f);
        glColor3f(0.f, 0.f, 1.f); glVertex2f( 0.0f,  0.5f);
    glEnd();
    glFinish();
    printf("drew triangle\n");

    aglSetCurrentContext(NULL);
    aglSetDrawable(ctx, NULL);            /* exits full-screen / releases capture */
    aglDestroyContext(ctx);
    aglDestroyPixelFormat(pf);
    printf("done (no render, display released)\n");
    return 0;
}

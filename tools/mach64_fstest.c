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
    glClearColor(0.1f, 0.2f, 0.4f, 1.f);
    glClear(GL_COLOR_BUFFER_BIT);
    {   /* vertex-array path (glDrawArrays) -- dispatches differently than the
         * immediate-mode glBegin/glEnd flush that no-ops in GLEngine. */
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
    printf("drew triangle (glDrawArrays) -- holding full-screen 4s so it's visible\n");
    fflush(stdout);
    sleep(4);   /* hold the captured display so the rendered triangle is visible */

    aglSetCurrentContext(NULL);
    aglSetDrawable(ctx, NULL);            /* exits full-screen / releases capture */
    aglDestroyContext(ctx);
    aglDestroyPixelFormat(pf);
    printf("done (no render, display released)\n");
    return 0;
}

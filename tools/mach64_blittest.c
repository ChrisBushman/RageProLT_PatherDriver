/*
 * mach64_blittest.c - de-risk the GUI-engine screen-to-screen blit in isolation,
 * BEFORE any double-buffer plumbing.  Requires the driver built -DGLD_BLIT_TEST,
 * which self-blits the front buffer's (0,0) 300x200 box to (600,400) on glFinish.
 *
 * We draw a green right-triangle filling the TOP-LEFT corner (inside that source
 * box) on a black field, then glFinish.  PASS = the green corner ALSO appears as a
 * copied box near screen (600,400); the original top-left is undisturbed.
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
    AGLContext ctx;
    if (!pf) { printf("no pf\n"); return 1; }
    ctx = aglCreateContext(pf, NULL); if (!ctx) return 1;
    aglSetFullScreen(ctx, 0, 0, 0, 0); aglSetCurrentContext(ctx);
    printf("GL_RENDERER=%s\n", (const char *)glGetString(GL_RENDERER));
    glViewport(0, 0, 1024, 768);
    glClearColor(0.f, 0.f, 0.f, 1.f); glClear(GL_COLOR_BUFFER_BIT);

    /* green quad filling the TOP-LEFT corner (FB 0,0), inside the 300x200 source
     * box (~NDC x[-1,-0.41], y[+1,+0.48]).  Vertex arrays -- the known-good path
     * (texprim/texelem) -- 2 tris, gouraud green. */
    {
        GLfloat pos[12] = { -1.0f,1.0f, -0.45f,1.0f, -0.45f,0.5f,
                            -1.0f,1.0f, -0.45f,0.5f, -1.0f,0.5f };
        GLfloat col[18] = { 0,1,0, 0,1,0, 0,1,0,  0,1,0, 0,1,0, 0,1,0 };
        glEnableClientState(GL_VERTEX_ARRAY);
        glEnableClientState(GL_COLOR_ARRAY);
        glVertexPointer(2, GL_FLOAT, 0, pos);
        glColorPointer(3, GL_FLOAT, 0, col);
        glDrawArrays(GL_TRIANGLES, 0, 6);
    }

    glFinish();   /* -> driver self-blits (0,0)300x200 -> (600,400) */
    printf("drew + finished (self-blit should have fired)\n"); fflush(stdout);
    sleep(6);
    aglSetCurrentContext(NULL); aglSetDrawable(ctx, NULL);
    aglDestroyContext(ctx); aglDestroyPixelFormat(pf);
    printf("done\n");
    return 0;
}

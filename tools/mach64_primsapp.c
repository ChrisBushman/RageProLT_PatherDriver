/*
 * mach64_primsapp.c - exercise the new POINT and LINE primitives through the
 * real GL path.  Draws (top to bottom):
 *   - GL_POINTS       : a row of white points
 *   - GL_LINES        : 3 separate red segments
 *   - GL_LINE_STRIP   : a green zigzag
 *   - GL_LINE_LOOP    : a blue square outline (must close)
 * Each primitive family expands to quads through mach64_draw_triangle.
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
    if (!pf) { printf("no pf\n"); return 1; }
    AGLContext ctx = aglCreateContext(pf, NULL);
    if (!ctx) return 1;
    aglSetFullScreen(ctx, 0, 0, 0, 0);
    aglSetCurrentContext(ctx);
    printf("GL_RENDERER = %s\n", (const char *)glGetString(GL_RENDERER));

    glViewport(0, 0, 1024, 768);
    glDisable(GL_DEPTH_TEST);
    glClearColor(0.08f, 0.08f, 0.10f, 1.f);
    glClear(GL_COLOR_BUFFER_BIT);
    glEnableClientState(GL_VERTEX_ARRAY);

    {   static const GLfloat pts[] = { -0.8f,0.7f, -0.4f,0.7f, 0.f,0.7f, 0.4f,0.7f, 0.8f,0.7f };
        glColor3f(1.f, 1.f, 1.f);
        glVertexPointer(2, GL_FLOAT, 0, pts);
        glDrawArrays(GL_POINTS, 0, 5);
    }
    {   static const GLfloat ln[] = { -0.8f,0.4f, -0.45f,0.55f,  -0.2f,0.4f, 0.2f,0.55f,  0.45f,0.4f, 0.8f,0.55f };
        glColor3f(1.f, 0.f, 0.f);
        glVertexPointer(2, GL_FLOAT, 0, ln);
        glDrawArrays(GL_LINES, 0, 6);
    }
    {   static const GLfloat st[] = { -0.8f,0.05f, -0.4f,0.25f, 0.f,0.05f, 0.4f,0.25f, 0.8f,0.05f };
        glColor3f(0.f, 1.f, 0.f);
        glVertexPointer(2, GL_FLOAT, 0, st);
        glDrawArrays(GL_LINE_STRIP, 0, 5);
    }
    {   static const GLfloat lp[] = { -0.5f,-0.55f, 0.5f,-0.55f, 0.5f,-0.15f, -0.5f,-0.15f };
        glColor3f(0.3f, 0.6f, 1.f);
        glVertexPointer(2, GL_FLOAT, 0, lp);
        glDrawArrays(GL_LINE_LOOP, 0, 4);
    }
    glFlush(); glFinish();
    printf("drew points + lines + line strip + line loop\n"); fflush(stdout);
    sleep(5);

    aglSetCurrentContext(NULL); aglSetDrawable(ctx, NULL);
    aglDestroyContext(ctx); aglDestroyPixelFormat(pf);
    printf("done\n");
    return 0;
}

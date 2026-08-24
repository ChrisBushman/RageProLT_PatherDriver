/*
 * mach64_depthapp.c - depth-buffer occlusion demo through the real GL path.
 *
 * Draws two overlapping triangles with the NEAR one FIRST, so only working
 * depth testing (not draw order) can produce the correct image:
 *   1. GREEN triangle, near (z = -0.5)   -- drawn 1st
 *   2. RED   triangle, far  (z = +0.5)   -- drawn 2nd, overlaps the green
 * With GL_LESS + a cleared Z buffer the overlap must stay GREEN (the nearer
 * surface wins even though red was drawn last).  Without depth the last-drawn
 * red would cover the overlap.  So: overlap GREEN => hardware depth works.
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
    printf("pf=%p (err %d)\n", (void*)pf, (int)aglGetError());
    if (!pf) return 1;
    AGLContext ctx = aglCreateContext(pf, NULL);
    if (!ctx) { aglDestroyPixelFormat(pf); return 1; }
    aglSetFullScreen(ctx, 0, 0, 0, 0);
    aglSetCurrentContext(ctx);
    printf("GL_RENDERER = %s\n", (const char *)glGetString(GL_RENDERER));

    glViewport(0, 0, 1024, 768);
    glClearColor(0.08f, 0.08f, 0.12f, 1.f);
    glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
    glEnable(GL_DEPTH_TEST);
    glDepthFunc(GL_LESS);

    glEnableClientState(GL_VERTEX_ARRAY);

    /* GREEN, near (z=-0.5), drawn FIRST */
    {   static const GLfloat vG[9] = { -0.6f,-0.6f,-0.5f,  0.3f,-0.6f,-0.5f,  -0.15f,0.55f,-0.5f };
        glColor3f(0.f, 1.f, 0.f);
        glVertexPointer(3, GL_FLOAT, 0, vG);
        glDrawArrays(GL_TRIANGLES, 0, 3);
    }
    /* RED, far (z=+0.5), drawn SECOND, overlapping the green */
    {   static const GLfloat vR[9] = { -0.3f,-0.6f,0.5f,   0.6f,-0.6f,0.5f,    0.15f,0.55f,0.5f };
        glColor3f(1.f, 0.f, 0.f);
        glVertexPointer(3, GL_FLOAT, 0, vR);
        glDrawArrays(GL_TRIANGLES, 0, 3);
    }
    glFlush();
    glFinish();
    printf("drew near-green then far-red overlapping -- overlap should be GREEN\n");
    fflush(stdout);
    sleep(5);

    aglSetCurrentContext(NULL);
    aglSetDrawable(ctx, NULL);
    aglDestroyContext(ctx);
    aglDestroyPixelFormat(pf);
    printf("done\n");
    return 0;
}

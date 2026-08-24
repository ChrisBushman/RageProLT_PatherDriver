/*
 * mach64_depthanim.c - verify PER-FRAME depth clear (double-buffered).
 *
 * Each frame: glClear(COLOR|DEPTH), draw ONE triangle at a fixed depth (z=0)
 * with a cycling colour, then aglSwapBuffers (-> gldFlushBuffer arms the clear).
 * Depth test is GL_LESS (forced on by the driver).
 *   - per-frame Z clear WORKS  -> triangle drawn every frame (colour animates).
 *   - per-frame Z clear BROKEN -> frame 0 writes Z=0 where the triangle is; on
 *     every later frame LESS (0<0) fails, so the triangle VANISHES after the
 *     first frame (only the cleared background shows).
 *
 * Build: cc -framework Carbon -framework AGL -framework OpenGL
 */
#include <Carbon/Carbon.h>
#include <AGL/agl.h>
#include <OpenGL/gl.h>
#include <stdio.h>
#include <unistd.h>
#include <math.h>

int main(void)
{
    GDHandle dev = GetMainDevice();
    GLint attribs[] = { AGL_RGBA, AGL_ACCELERATED, AGL_FULLSCREEN,
                        AGL_DOUBLEBUFFER, AGL_DEPTH_SIZE, 16, AGL_NONE };
    AGLPixelFormat pf = aglChoosePixelFormat(&dev, 1, attribs);
    printf("double-buffered pf = %p (err %d)\n", (void*)pf, (int)aglGetError());
    if (!pf) { printf("no double-buffered accelerated format available\n"); return 1; }
    AGLContext ctx = aglCreateContext(pf, NULL);
    if (!ctx) { aglDestroyPixelFormat(pf); return 1; }
    aglSetFullScreen(ctx, 0, 0, 0, 0);
    aglSetCurrentContext(ctx);
    printf("GL_RENDERER = %s\n", (const char *)glGetString(GL_RENDERER));

    glViewport(0, 0, 1024, 768);
    glEnable(GL_DEPTH_TEST);
    glDepthFunc(GL_LESS);
    glEnableClientState(GL_VERTEX_ARRAY);

    {   static const GLfloat v[9] = { -0.5f,-0.5f,0.f,  0.5f,-0.5f,0.f,  0.f,0.5f,0.f };
        int f;
        glVertexPointer(3, GL_FLOAT, 0, v);
        for (f = 0; f < 150; f++) {
            float t = f * 0.06f;
            glClearColor(0.06f, 0.06f, 0.10f, 1.f);
            glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
            glColor3f(0.5f+0.5f*sinf(t), 0.5f+0.5f*sinf(t+2.1f), 0.5f+0.5f*sinf(t+4.2f));
            glDrawArrays(GL_TRIANGLES, 0, 3);
            aglSwapBuffers(ctx);      /* -> gldFlushBuffer -> arms per-frame Z clear */
            usleep(30000);
        }
    }
    printf("animated 150 frames -- triangle should stay visible (colour cycling)\n");
    fflush(stdout);

    aglSetCurrentContext(NULL);
    aglSetDrawable(ctx, NULL);
    aglDestroyContext(ctx);
    aglDestroyPixelFormat(pf);
    printf("done\n");
    return 0;
}

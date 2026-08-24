/*
 * mach64_ptlnstate.c - set glPointSize/glLineWidth (by argv[1]) then draw, so
 * the gli can be dumped at sub_4530 and diffed to locate where GLEngine stores
 * them (expected as IEEE floats).
 *   argv[1]=0 : PointSize 1.0, LineWidth 1.0  (defaults)
 *   argv[1]=1 : PointSize 8.0, LineWidth 5.0
 * Build: cc -framework Carbon -framework AGL -framework OpenGL
 */
#include <Carbon/Carbon.h>
#include <AGL/agl.h>
#include <OpenGL/gl.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>

int main(int argc, char **argv)
{
    int cfg = (argc > 1) ? atoi(argv[1]) : 0;
    GDHandle dev = GetMainDevice();
    GLint attribs[] = { AGL_RGBA, AGL_ACCELERATED, AGL_FULLSCREEN,
                        AGL_DEPTH_SIZE, 16, AGL_NONE };
    AGLPixelFormat pf = aglChoosePixelFormat(&dev, 1, attribs);
    if (!pf) { printf("no pf\n"); return 1; }
    AGLContext ctx = aglCreateContext(pf, NULL);
    if (!ctx) return 1;
    aglSetFullScreen(ctx, 0, 0, 0, 0);
    aglSetCurrentContext(ctx);
    printf("cfg=%d GL_RENDERER=%s\n", cfg, (const char *)glGetString(GL_RENDERER));

    glViewport(0, 0, 1024, 768);
    glPointSize(cfg == 1 ? 8.0f : 1.0f);
    glLineWidth(cfg == 1 ? 5.0f : 1.0f);

    glClearColor(0.1f, 0.1f, 0.1f, 1.f);
    glClear(GL_COLOR_BUFFER_BIT);
    {   static const GLfloat v[9] = { -0.5f,-0.5f,0.f,  0.5f,-0.5f,0.f,  0.f,0.5f,0.f };
        glColor3f(1.f, 1.f, 1.f);
        glEnableClientState(GL_VERTEX_ARRAY);
        glVertexPointer(3, GL_FLOAT, 0, v);
        glDrawArrays(GL_TRIANGLES, 0, 3);   /* reach sub_4530 with state set */
    }
    glFlush(); glFinish();
    printf("drew (cfg=%d)\n", cfg); fflush(stdout);
    sleep(2);
    aglSetCurrentContext(NULL); aglSetDrawable(ctx, NULL);
    aglDestroyContext(ctx); aglDestroyPixelFormat(pf);
    return 0;
}

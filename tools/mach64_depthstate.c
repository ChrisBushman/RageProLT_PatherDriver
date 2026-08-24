/*
 * mach64_depthstate.c - set a specific GL depth state (selected by argv[1]) then
 * draw one triangle, so the gli context can be dumped at sub_4530 and diffed to
 * locate where GLEngine stores glDepthFunc / glDepthMask / GL_DEPTH_TEST enable.
 *   argv[1]=0 : enabled, GL_LESS,    depthMask TRUE   (baseline)
 *   argv[1]=1 : enabled, GL_GREATER, depthMask FALSE
 *   argv[1]=2 : DISABLED depth test
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

    if (cfg == 2) {
        glDisable(GL_DEPTH_TEST);
    } else {
        glEnable(GL_DEPTH_TEST);
        glDepthFunc(cfg == 1 ? GL_GREATER : GL_LESS);
        glDepthMask(cfg == 1 ? GL_FALSE : GL_TRUE);
    }

    glClearColor(0.1f, 0.1f, 0.1f, 1.f);
    glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
    {   static const GLfloat v[9] = { -0.5f,-0.5f,0.f,  0.5f,-0.5f,0.f,  0.f,0.5f,0.f };
        glColor3f(1.f, 1.f, 1.f);
        glEnableClientState(GL_VERTEX_ARRAY);
        glVertexPointer(3, GL_FLOAT, 0, v);
        glDrawArrays(GL_TRIANGLES, 0, 3);
    }
    glFlush(); glFinish();
    printf("drew (cfg=%d)\n", cfg); fflush(stdout);
    sleep(2);
    aglSetCurrentContext(NULL); aglSetDrawable(ctx, NULL);
    aglDestroyContext(ctx); aglDestroyPixelFormat(pf);
    return 0;
}

/*
 * mach64_blendstate.c - set a GL blend configuration (argv[1]) then draw, so the
 * gli can be dumped at sub_4530 (0x509530) and diffed across configs to locate
 * where GLEngine stores (a) the GL_BLEND enable and (b) the blend src/dst factors.
 * glBlendFunc/glEnable(GL_BLEND) have no gld hooks, so we recover the offsets the
 * same way tex-enable (gli+0x6748) and env-mode (gli+0x674c) were found.
 *   argv[1]=0 : GL_BLEND disabled (baseline)
 *   argv[1]=1 : blend on, glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA)
 *   argv[1]=2 : blend on, glBlendFunc(GL_ONE, GL_ONE)           (additive)
 * diff 0-vs-1 => the enable bit (+ func fields); diff 1-vs-2 => the func fields.
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
    AGLContext ctx;
    if (!pf) { printf("no pf\n"); return 1; }
    ctx = aglCreateContext(pf, NULL);
    if (!ctx) return 1;
    aglSetFullScreen(ctx, 0, 0, 0, 0);
    aglSetCurrentContext(ctx);
    printf("cfg=%d GL_RENDERER=%s\n", cfg, (const char *)glGetString(GL_RENDERER));
    glViewport(0, 0, 1024, 768);

    /* The ONLY difference between configs: the blend state. */
    if (cfg == 0) {
        glDisable(GL_BLEND);
    } else {
        glEnable(GL_BLEND);
        if (cfg == 2) glBlendFunc(GL_ONE, GL_ONE);
        else          glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
    }

    glClearColor(0.1f, 0.1f, 0.1f, 1.f);
    glClear(GL_COLOR_BUFFER_BIT);
    {   static const GLfloat v[6]  = { -0.5f,-0.5f,  0.5f,-0.5f,  0.f,0.5f };
        glColor4f(1.f, 1.f, 1.f, 0.5f);
        glEnableClientState(GL_VERTEX_ARRAY);
        glVertexPointer(2, GL_FLOAT, 0, v);
        glDrawArrays(GL_TRIANGLES, 0, 3);
    }
    glFlush(); glFinish();
    printf("drew (cfg=%d)\n", cfg); fflush(stdout);
    sleep(2);
    aglSetCurrentContext(NULL); aglSetDrawable(ctx, NULL);
    aglDestroyContext(ctx); aglDestroyPixelFormat(pf);
    return 0;
}

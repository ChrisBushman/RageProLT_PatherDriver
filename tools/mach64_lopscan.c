/* mach64_lopscan.c - recover the GL_COLOR_LOGIC_OP enable + glLogicOp value gli
 * offsets.  Three frames, changing ONLY logic-op state between them so the driver's
 * GLD_FOGSCAN state-cluster delta pinpoints the words:
 *   frame 1: logic op OFF, default op            (silent baseline snapshot)
 *   frame 2: glEnable(GL_COLOR_LOGIC_OP)+XOR      (delta = enable word + op-value word)
 *   frame 3: glLogicOp(GL_OR)                     (delta = ONLY the op-value word,
 *                                                  so the other frame-2 word is enable)
 * Build the DRIVER with -DGLD_FOGSCAN; build this tool:
 *   cc -o mach64_lopscan mach64_lopscan.c -framework Carbon -framework AGL -framework OpenGL
 * Run under sudo, then read /tmp/mach64gld.log (the "  gli+0x...." delta lines --
 * they have no FOGSCAN prefix). */
#include <Carbon/Carbon.h>
#include <AGL/agl.h>
#include <OpenGL/gl.h>
#include <stdio.h>
#include <unistd.h>

static void quad(void)
{
    GLfloat v[12] = { -0.5f,-0.5f,0.f,  0.5f,-0.5f,0.f,  0.5f,0.5f,0.f,  -0.5f,0.5f,0.f };
    glClearColor(0, 0, 0.5f, 1);
    glClear(GL_COLOR_BUFFER_BIT);
    glColor3f(1, 1, 1);
    glVertexPointer(3, GL_FLOAT, 0, v);
    glDrawArrays(GL_QUADS, 0, 4);
    glFlush();
    glFinish();
}

int main(void)
{
    GDHandle dev = GetMainDevice();
    GLint at[] = { AGL_RGBA, AGL_ACCELERATED, AGL_FULLSCREEN, AGL_NONE };
    AGLPixelFormat pf = aglChoosePixelFormat(&dev, 1, at);
    AGLContext ctx;

    if (!pf) { printf("no pf\n"); return 1; }
    ctx = aglCreateContext(pf, NULL);
    if (!ctx) return 1;
    aglSetFullScreen(ctx, 0, 0, 0, 0);
    aglSetCurrentContext(ctx);
    printf("GL_RENDERER=%s\n", (const char *)glGetString(GL_RENDERER));
    glViewport(0, 0, 1024, 768);
    glDisable(GL_TEXTURE_2D);
    glEnableClientState(GL_VERTEX_ARRAY);

    glDisable(GL_COLOR_LOGIC_OP);
    printf("frame 1: logic op OFF (baseline)\n"); fflush(stdout);
    quad();

    glEnable(GL_COLOR_LOGIC_OP);
    glLogicOp(GL_XOR);
    printf("frame 2: LOGIC_OP ON, GL_XOR (delta = enable + op-value)\n"); fflush(stdout);
    quad();

    glLogicOp(GL_OR);
    printf("frame 3: GL_OR (delta = op-value word only)\n"); fflush(stdout);
    quad();

    printf("done -- read /tmp/mach64gld.log 'gli+0x....' delta lines\n"); fflush(stdout);
    sleep(3);
    aglSetCurrentContext(NULL);
    aglSetDrawable(ctx, NULL);
    aglDestroyContext(ctx);
    aglDestroyPixelFormat(pf);
    return 0;
}

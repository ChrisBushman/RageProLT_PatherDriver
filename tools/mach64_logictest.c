/* mach64_logictest.c - verify HARDWARE GL_COLOR_LOGIC_OP (glLogicOp) via DP_MIX.
 * Blue background (glClear), then two quads with GL_XOR:
 *   left  quad WHITE ^ blue = (1,1,0) YELLOW
 *   right quad RED   ^ blue = (1,0,1) MAGENTA
 * then a third quad GL_INVERT over blue = ~blue = (1,1,0) YELLOW (ignores source).
 * Saturated colours only, so dithering (which perturbs mid-tones and is likely why
 * the reference DRI driver fell back to software) can't muddy the XOR.
 *   works    -> yellow, magenta, yellow bars on blue
 *   no ROP   -> white, red, (source) bars  (3D scaler ignores DP_MIX)
 * Build: cc -framework Carbon -framework AGL -framework OpenGL */
#include <Carbon/Carbon.h>
#include <AGL/agl.h>
#include <OpenGL/gl.h>
#include <stdio.h>
#include <unistd.h>

static void bar(float x0, float x1, float r, float g, float b)
{
    GLfloat v[12] = { x0,-0.6f,0.f,  x1,-0.6f,0.f,  x1,0.6f,0.f,  x0,0.6f,0.f };
    glColor3f(r, g, b);
    glVertexPointer(3, GL_FLOAT, 0, v);
    glDrawArrays(GL_QUADS, 0, 4);
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

    glClearColor(0, 0, 1, 1);                 /* blue background */
    glClear(GL_COLOR_BUFFER_BIT);

    glEnable(GL_COLOR_LOGIC_OP);
    glLogicOp(GL_XOR);
    bar(-0.9f, -0.5f, 1, 1, 1);               /* white ^ blue -> YELLOW  */
    bar(-0.3f,  0.1f, 1, 0, 0);               /* red   ^ blue -> MAGENTA */
    glLogicOp(GL_INVERT);
    bar( 0.5f,  0.9f, 0, 1, 0);               /* ~blue -> YELLOW (src ignored) */

    glFlush(); glFinish();
    printf("drew XOR(white),XOR(red),INVERT bars on blue\n"); fflush(stdout);
    sleep(6);
    aglSetCurrentContext(NULL);
    aglSetDrawable(ctx, NULL);
    aglDestroyContext(ctx);
    aglDestroyPixelFormat(pf);
    return 0;
}

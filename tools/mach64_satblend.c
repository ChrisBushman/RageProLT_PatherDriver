/* mach64_satblend.c - verify GL_SRC_ALPHA_SATURATE (mapped to ALPHA_BLEND_SRC_SRCALPHA | ALPHA_BLEND_SAT
 * bit13).  The saturate source factor is min(As, 1-Ad); on a BLACK-cleared buffer Ad=0 so it reduces to
 * As -- i.e. it must give the SAME result as plain GL_SRC_ALPHA.  Two quads, dst factor GL_ONE, black bg:
 *   LEFT  = glBlendFunc(GL_SRC_ALPHA_SATURATE, GL_ONE), color(200,100,50) As=0.5 -> expect (100,50,25)
 *   RIGHT = glBlendFunc(GL_SRC_ALPHA,          GL_ONE), same                     -> expect (100,50,25)
 * PASS = LEFT ~= RIGHT ~= (100,50,25), no hang, glGetError==0.  A garbage/black LEFT or a hang would
 * mean the SAT bit is wrong.  Build: cc -framework Carbon -framework AGL -framework OpenGL */
#include <Carbon/Carbon.h>
#include <AGL/agl.h>
#include <OpenGL/gl.h>
#include <stdio.h>
#include <unistd.h>

#ifndef GL_SRC_ALPHA_SATURATE
#define GL_SRC_ALPHA_SATURATE 0x0308
#endif

static void rd(int x, int y, const char *w)
{
    GLubyte px[4]; px[0]=px[1]=px[2]=9;
    glReadPixels(x, y, 1, 1, GL_RGBA, GL_UNSIGNED_BYTE, px);
    printf("READ %-16s (%d,%d) = R%3d G%3d B%3d\n", w, x, y, px[0], px[1], px[2]);
}

static void quad(GLenum sfactor, const GLfloat *v)
{
    glBlendFunc(sfactor, GL_ONE);
    glColor4f(200/255.0f, 100/255.0f, 50/255.0f, 0.5f);
    glVertexPointer(2, GL_FLOAT, 0, v);
    glDrawArrays(GL_QUADS, 0, 4);
}

int main(void)
{
    GDHandle dev = GetMainDevice();
    GLint at[] = { AGL_RGBA, AGL_ACCELERATED, AGL_FULLSCREEN, AGL_NONE };
    AGLPixelFormat pf = aglChoosePixelFormat(&dev, 1, at);
    AGLContext ctx;
    GLfloat vL[8], vR[8];

    vL[0]=200;vL[1]=300; vL[2]=360;vL[3]=300; vL[4]=360;vL[5]=460; vL[6]=200;vL[7]=460;   /* LEFT  */
    vR[0]=600;vR[1]=300; vR[2]=760;vR[3]=300; vR[4]=760;vR[5]=460; vR[6]=600;vR[7]=460;   /* RIGHT */

    if (!pf) { printf("no pf\n"); return 1; }
    ctx = aglCreateContext(pf, NULL); if (!ctx) return 1;
    aglSetFullScreen(ctx, 0, 0, 0, 0); aglSetCurrentContext(ctx);
    printf("GL_RENDERER=%s\n", (const char *)glGetString(GL_RENDERER));
    glViewport(0, 0, 1024, 768);
    glMatrixMode(GL_PROJECTION); glLoadIdentity(); glOrtho(0, 1024, 0, 768, -1, 1);
    glMatrixMode(GL_MODELVIEW);  glLoadIdentity();
    glClearColor(0, 0, 0, 1); glClear(GL_COLOR_BUFFER_BIT);   /* black -> Ad=0 -> saturate == SRC_ALPHA */
    glEnable(GL_BLEND);
    glEnableClientState(GL_VERTEX_ARRAY);

    quad(GL_SRC_ALPHA_SATURATE, vL);
    quad(GL_SRC_ALPHA,          vR);
    glFinish();

    rd(280, 380, "SAT(sat)");        /* expect ~100,50,25 */
    rd(680, 380, "SRC_ALPHA(ref)");  /* expect ~100,50,25 */
    printf("PASS if both ~= (100,50,25).  SAT differing/garbage/black = wrong bit.\n");
    printf("glGetError = 0x%x\n", (unsigned)glGetError());
    fflush(stdout); sleep(6);

    aglSetCurrentContext(NULL); aglSetDrawable(ctx, NULL);
    aglDestroyContext(ctx); aglDestroyPixelFormat(pf);
    return 0;
}

/* mach64_stiptest.c - is GL_POLYGON_STIPPLE reachable in hardware?  Feasibility probe.
 * Draws two big white quads: LEFT with stipple OFF (reference), RIGHT with stipple ON
 * and a COARSE horizontal-stripe pattern.  Our driver does NOT handle stipple yet, so:
 *   RIGHT quad SOLID white  -> GLEngine dispatched it to our HW slots (we can add the
 *                              8x8 pattern) -- FEASIBLE.
 *   RIGHT quad STRIPED       -> GLEngine software-rasterised the stipple itself -- NOT
 *                              reachable in HW (like the reference's software fallback).
 *   RIGHT quad MISSING       -> GLEngine fell back to software and it failed on our HW.
 * Build the driver -DGLD_FOGSCAN to also capture the GL_POLYGON_STIPPLE enable offset
 * (state-cluster delta between the two quads).
 * Build: cc -framework Carbon -framework AGL -framework OpenGL */
#include <Carbon/Carbon.h>
#include <AGL/agl.h>
#include <OpenGL/gl.h>
#include <stdio.h>
#include <unistd.h>

int main(void)
{
    GDHandle dev = GetMainDevice();
    GLint at[] = { AGL_RGBA, AGL_ACCELERATED, AGL_FULLSCREEN, AGL_NONE };
    AGLPixelFormat pf = aglChoosePixelFormat(&dev, 1, at);
    AGLContext ctx; int i;
    GLubyte stip[128];                 /* 32x32 / 8 = 128 bytes */
    GLfloat lq[8] = { -0.9f,-0.6f, -0.1f,-0.6f, -0.1f,0.6f, -0.9f,0.6f };  /* left  */
    GLfloat rq[8] = {  0.1f,-0.6f,  0.9f,-0.6f,  0.9f,0.6f,  0.1f,0.6f };  /* right */

    /* coarse stripes: 4 solid rows then 4 empty rows, repeated */
    for (i = 0; i < 128; i++) stip[i] = ((i / 4) & 1) ? 0x00 : 0xff;

    if (!pf) { printf("no pf\n"); return 1; }
    ctx = aglCreateContext(pf, NULL);
    if (!ctx) return 1;
    aglSetFullScreen(ctx, 0, 0, 0, 0);
    aglSetCurrentContext(ctx);
    printf("GL_RENDERER=%s\n", (const char *)glGetString(GL_RENDERER));
    glViewport(0, 0, 1024, 768);
    glDisable(GL_TEXTURE_2D);
    glColor3f(1, 1, 1);
    glEnableClientState(GL_VERTEX_ARRAY);
    glClearColor(0, 0, 0.4f, 1); glClear(GL_COLOR_BUFFER_BIT);   /* blue bg */

    glDisable(GL_POLYGON_STIPPLE);
    glVertexPointer(2, GL_FLOAT, 0, lq); glDrawArrays(GL_QUADS, 0, 4);   /* LEFT: no stipple */

    glPolygonStipple(stip);
    glEnable(GL_POLYGON_STIPPLE);
    glVertexPointer(2, GL_FLOAT, 0, rq); glDrawArrays(GL_QUADS, 0, 4);   /* RIGHT: stipple on */

    glFlush(); glFinish();
    printf("drew LEFT(no-stipple) + RIGHT(stipple).  RIGHT solid=HW-reachable, "
           "striped=SW, missing=fallback-failed\n"); fflush(stdout);
    sleep(6);
    aglSetCurrentContext(NULL); aglSetDrawable(ctx, NULL);
    aglDestroyContext(ctx); aglDestroyPixelFormat(pf);
    return 0;
}

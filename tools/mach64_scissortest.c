/* mach64_scissortest.c - GL_SCISSOR_TEST verify (and the original gli-offset scan harness).
 *
 * Draws TWO frames of a big white region (two GL_TRIANGLES) on a blue clear:
 *   Frame 1: GL_SCISSOR_TEST OFF                        -> full white region
 *   Frame 2: glScissor(101,203,305,407) + SCISSOR ON    -> clipped to the box
 *
 * On this stack GL_SCISSOR_TEST is NOT a hardware-scissor feature: Apple's GLEngine does
 * T&L + clipping in software and re-dispatches the CLIPPED convex polygons through the
 * elements-tris vector (slot 0x48) as an array of vertex pointers -- the driver renders
 * them via mach64_render_elt_tris (fan-tessellated).  See docs/re-notes/scissor.md.
 *
 * PASS: frame 2 shows a white 305x407 rectangle, LEFT edge 101 px in, sitting NEARER THE
 * TOP (top margin 768-(203+407)=158 < bottom margin 203) -- proving the box and the GL
 * bottom-left -> FB top-left Y-flip.  The box is asymmetric/off-centre on purpose so both
 * axes are unambiguous.
 *
 * Historical: build the driver -DGLD_SCISSCAN + run tools/scisscan.sh to re-scan the gli
 * offsets where GLEngine stores the box (x@+0x66f4,y@+0x66f8,w@+0x66fc,h@+0x6700,
 * enable@+0x6704 bit24).  Those are unused by the render path now (GLEngine self-clips) but
 * documented there.
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
    AGLContext ctx;
    /* A big screen-covering region drawn as TWO GL_TRIANGLES (6 verts), NOT GL_QUADS:
     * enabling GL_SCISSOR_TEST makes GLEngine reinstall the dispatch table, which reroutes
     * the (mis-mapped) QUAD slot onto our triangle slot and drops half the quad -- a test
     * artifact.  GL_TRIANGLES dispatches stably, so the clip result is unambiguous. */
    GLfloat q[12] = { -0.9f,-0.9f,  0.9f,-0.9f,  0.9f,0.9f,     /* tri 1 */
                      -0.9f,-0.9f,  0.9f,0.9f,  -0.9f,0.9f };   /* tri 2 */

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
    glVertexPointer(2, GL_FLOAT, 0, q);

    /* Frame 1: scissor OFF -- full white quad (SCISSCAN baseline). */
    glDisable(GL_SCISSOR_TEST);
    glClearColor(0, 0, 0.4f, 1); glClear(GL_COLOR_BUFFER_BIT);
    glDrawArrays(GL_TRIANGLES, 0, 6);
    glFlush(); glFinish();
    sleep(2);

    /* Frame 2: scissor ON with the distinctive box -- SAME quad (SCISSCAN delta). Clear
     * with scissor OFF first so the blue background is full-screen, not box-clipped. */
    glDisable(GL_SCISSOR_TEST);
    glClearColor(0, 0, 0.4f, 1); glClear(GL_COLOR_BUFFER_BIT);
    glScissor(101, 203, 305, 407);
    glEnable(GL_SCISSOR_TEST);
    glDrawArrays(GL_TRIANGLES, 0, 6);
    glFlush(); glFinish();
    printf("drew scissor OFF then ON glScissor(101,203,305,407).  FULL white quad now = "
           "HW scissor feasible; a box = GLEngine clipped host-side.\n");
    fflush(stdout);
    sleep(6);

    aglSetCurrentContext(NULL); aglSetDrawable(ctx, NULL);
    aglDestroyContext(ctx); aglDestroyPixelFormat(pf);
    return 0;
}

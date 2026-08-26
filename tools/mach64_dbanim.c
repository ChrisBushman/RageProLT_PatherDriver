/* mach64_dbanim.c - double-buffer SWAP-CHAIN test.  Real AGL_DOUBLEBUFFER context
 * (driver -DGLD_DBUF): sweeps an RGB gouraud triangle left<->right across ~80 frames,
 * one aglSwapBuffers each, then settles centered and holds.  Confirms the swap chain
 * across many frames: each frame must clear the back, render, and present a COMPLETE
 * frame.
 *   PASS  = smooth motion while running, and a final snapshot with ONE clean triangle
 *           centered on clean black (no residue/smearing from earlier frames).
 *   FAIL  = flicker/tearing while running, or multiple/ghost triangles or VRAM garbage
 *           in the snapshot (back-buffer clear or swap broken).
 * Build: cc -framework Carbon -framework AGL -framework OpenGL */
#include <Carbon/Carbon.h>
#include <AGL/agl.h>
#include <OpenGL/gl.h>
#include <stdio.h>
#include <unistd.h>

static void tri(float cx)
{
    glClearColor(0, 0, 0, 1);
    glClear(GL_COLOR_BUFFER_BIT);
    glBegin(GL_TRIANGLES);
        glColor3f(1, 0, 0); glVertex2f(cx - 0.35f, -0.6f);
        glColor3f(0, 1, 0); glVertex2f(cx + 0.35f, -0.6f);
        glColor3f(0, 0, 1); glVertex2f(cx,          0.6f);
    glEnd();
}

int main(void)
{
    GDHandle dev = GetMainDevice();
    GLint at[] = { AGL_RGBA, AGL_ACCELERATED, AGL_FULLSCREEN,
                   AGL_DOUBLEBUFFER, AGL_DEPTH_SIZE, 16, AGL_NONE };
    AGLPixelFormat pf = aglChoosePixelFormat(&dev, 1, at);
    AGLContext ctx; GLint dbl = -1; int f;

    if (!pf) { printf("no DOUBLEBUFFER pixel format! (build driver -DGLD_DBUF)\n"); return 1; }
    aglDescribePixelFormat(pf, AGL_DOUBLEBUFFER, &dbl);
    ctx = aglCreateContext(pf, NULL);
    if (!ctx) { printf("no ctx\n"); return 1; }
    aglSetFullScreen(ctx, 0, 0, 0, 0);
    aglSetCurrentContext(ctx);
    printf("GL_RENDERER=%s  AGL_DOUBLEBUFFER=%d\n",
           (const char *)glGetString(GL_RENDERER), dbl);
    glViewport(0, 0, 1024, 768);
    glDisable(GL_TEXTURE_2D);

    /* Sweep: triangle bounces between x=-0.55 and x=+0.55 over 80 frames. */
    for (f = 0; f < 80; f++) {
        int p = f % 40;                 /* 0..39 */
        float t = (p < 20 ? p : 40 - p) / 20.0f;   /* 0..1..0 triangle wave */
        tri(-0.55f + 1.10f * t);
        aglSwapBuffers(ctx);            /* -> gldFlushBuffer -> present + frame_new */
        usleep(40000);                  /* ~25 fps, ~3.2s total */
    }

    /* Settle centered and hold for the snapshot. */
    tri(0.0f);
    aglSwapBuffers(ctx);
    printf("swept %d frames, settled centered\n", f); fflush(stdout);
    sleep(6);

    aglSetCurrentContext(NULL);
    aglSetDrawable(ctx, NULL);
    aglDestroyContext(ctx);
    aglDestroyPixelFormat(pf);
    return 0;
}

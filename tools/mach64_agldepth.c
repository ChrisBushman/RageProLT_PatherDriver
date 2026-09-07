/*
 * mach64_agldepth.c - windowed AGL DEPTH-TEST validation for Path B.
 *
 * Draws two overlapping triangles at different depths with GL_DEPTH_TEST on:
 *   - a GREEN triangle at the FAR plane (z = +0.5), drawn SECOND
 *   - a RED   triangle at the NEAR plane (z = -0.5), drawn FIRST
 * Because the near (red) triangle is drawn BEFORE the far (green) one, a correct
 * hardware depth test must REJECT the green fragments where red is already nearer
 * -> red stays on top in the overlap.  Without depth testing (or a broken Z path)
 * the later green triangle would paint over the red (painter's order).
 *
 * So: red-over-green in the overlap = the mach64 Z buffer / Z_CNTL / per-vertex Z
 * path works through Apple's kext.  Both spin so it's unmistakably live 3D.
 * MUST run AT THE CONSOLE.  Build:
 *   cc -O2 -o ~/agldepth mach64_agldepth.c -framework AGL -framework OpenGL -framework Carbon
 */
#include <AGL/agl.h>
#include <OpenGL/gl.h>
#include <Carbon/Carbon.h>
#include <stdio.h>
#include <unistd.h>

int main(void)
{
    Rect bounds = { 80, 80, 320, 400 };              /* 320x240 (small: colour+Z fit under 0x400000
                                                      * in VRAM, before Apple's desktop surfaces) */
    WindowRef win = NULL;
    OSStatus err = CreateNewWindow(kDocumentWindowClass,
                                   kWindowStandardDocumentAttributes, &bounds, &win);
    if (err || !win) { printf("CreateNewWindow err=%d\n", (int)err); return 1; }
    SetWindowTitleWithCFString(win, CFSTR("mach64 Path-B depth test"));
    ShowWindow(win);

    GLint attrs[] = { AGL_RGBA, AGL_DOUBLEBUFFER, AGL_ACCELERATED,
                      AGL_DEPTH_SIZE, 16, AGL_NONE };
    AGLPixelFormat pf = aglChoosePixelFormat(NULL, 0, attrs);
    printf("aglChoosePixelFormat(accel,depth16) -> %p (err %d)\n", (void*)pf, (int)aglGetError());
    if (!pf) { printf("no ACCELERATED depth pixel format\n"); return 1; }

    AGLContext ctx = aglCreateContext(pf, NULL);
    if (!ctx) { printf("aglCreateContext failed (err %d)\n", (int)aglGetError()); return 1; }
    GLboolean ok = aglSetDrawable(ctx, GetWindowPort(win));
    printf("aglSetDrawable -> %d (err %d)\n", (int)ok, (int)aglGetError());
    aglSetCurrentContext(ctx);

    glViewport(0, 0, 320, 240);
    glMatrixMode(GL_PROJECTION); glLoadIdentity();
    glOrtho(-1.0, 1.0, -1.0, 1.0, -1.0, 1.0);        /* matches the proven agltri setup (good W).
                                                      * NB glOrtho INVERTS z: eye z=+0.8 -> near. */
    glMatrixMode(GL_MODELVIEW);  glLoadIdentity();

    glEnable(GL_DEPTH_TEST);
    glDepthFunc(GL_LESS);                            /* smaller window-z (nearer) wins */
    glShadeModel(GL_SMOOTH);                         /* match the proven agltri path (was GL_FLAT) */

    /* STATIC, unambiguous occlusion: a GREEN triangle at the NEAR plane (z=-0.8 -> window 0.1)
     * drawn FIRST, then a RED triangle at the FAR plane (z=+0.8 -> window 0.9) drawn LAST, shifted
     * to overlap.  With a working depth test the later red is REJECTED where the nearer green
     * already wrote Z -> GREEN stays on top in the overlap (painter's order would show red).  No
     * spin, so the image is a clean still: left green, right red, and the overlap column GREEN. */
    printf("rendering 180 static frames (near GREEN drawn first, far RED drawn last)...\n");
    fflush(stdout);
    int i;
    for (i = 0; i < 180; i++) {
        glClearColor(0.10f, 0.10f, 0.15f, 1.0f);
        glClear(GL_COLOR_BUFFER_BIT);   /* driver clears Z itself (gated on depth-test-enabled);
                                         * dropping GL_DEPTH_BUFFER_BIT tests whether GLEngine's
                                         * depth-clear dispatch is what dirties the surface -> the
                                         * present backing-copy clobber (white triangles). */

        /* NEAR green (eye z=+0.8 -> near via ortho) FIRST -- big triangle pointing UP */
        glColor3f(0.0f, 1.0f, 0.0f);
        glBegin(GL_TRIANGLES);
            glVertex3f(-0.9f, -0.9f, 0.8f);
            glVertex3f( 0.9f, -0.9f, 0.8f);
            glVertex3f( 0.0f,  0.9f, 0.8f);
        glEnd();

        /* FAR red (eye z=-0.8 -> far) LAST -- big triangle pointing DOWN, heavily OVERLAPPING the
         * green.  Depth must reject red in the overlap so GREEN shows through the central diamond
         * even though red is drawn on top (painter's order would show red). */
        glColor3f(1.0f, 0.0f, 0.0f);
        glBegin(GL_TRIANGLES);
            glVertex3f(-0.9f,  0.9f, -0.8f);
            glVertex3f( 0.9f,  0.9f, -0.8f);
            glVertex3f( 0.0f, -0.9f, -0.8f);
        glEnd();

        aglSwapBuffers(ctx);
        usleep(16000);
    }
    printf("done -- 180 static depth frames presented\n");

    aglSetCurrentContext(NULL);
    aglSetDrawable(ctx, NULL);
    aglDestroyContext(ctx);
    aglDestroyPixelFormat(pf);
    DisposeWindow(win);
    return 0;
}

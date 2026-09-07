/*
 * mach64_agltri.c - windowed AGL GEOMETRY test for Path B (3D triangle path).
 *
 * Unlike mach64_aglrender (which only glClear's), this draws a real
 * GL_TRIANGLES primitive so the driver's mach64_submit path emits the
 * block-0 SETUP_CNTL / VERTEX_{1,2,3} / ONE_OVER_AREA_UC registers.  If the
 * triangle hardware-renders through Apple's ATIRagePro.kext, the corrected
 * PIO offset formula (block 0 reachable at its real offset) is proven and
 * 3D works with NO DMA subsystem.
 *
 * A big Gouraud-shaded triangle (red/green/blue corners) on a dark background
 * = the 3D setup/vertex path works.  Trace: /tmp/mach64gld.log.
 * MUST be run AT THE CONSOLE (needs the WindowServer), not over ssh:
 *     /tmp/agltri
 * Build: cc -O2 -o /tmp/agltri mach64_agltri.c -framework AGL -framework OpenGL -framework Carbon
 */
#include <AGL/agl.h>
#include <OpenGL/gl.h>
#include <Carbon/Carbon.h>
#include <stdio.h>
#include <unistd.h>
#include <math.h>

int main(void)
{
    Rect bounds = { 80, 80, 480, 560 };              /* top,left,bottom,right -> 480x400 */
    WindowRef win = NULL;
    OSStatus err = CreateNewWindow(kDocumentWindowClass,
                                   kWindowStandardDocumentAttributes, &bounds, &win);
    if (err || !win) { printf("CreateNewWindow err=%d\n", (int)err); return 1; }
    SetWindowTitleWithCFString(win, CFSTR("mach64 Path-B 3D triangle"));
    ShowWindow(win);

    GLint attrs[] = { AGL_RGBA, AGL_DOUBLEBUFFER, AGL_ACCELERATED,
                      AGL_DEPTH_SIZE, 16, AGL_NONE };
    AGLPixelFormat pf = aglChoosePixelFormat(NULL, 0, attrs);
    printf("aglChoosePixelFormat(accelerated) -> %p (err %d)\n", (void*)pf, (int)aglGetError());
    if (!pf) { printf("no ACCELERATED pixel format\n"); return 1; }

    AGLContext ctx = aglCreateContext(pf, NULL);
    printf("aglCreateContext -> %p (err %d)\n", (void*)ctx, (int)aglGetError());
    if (!ctx) return 1;

    GLboolean ok = aglSetDrawable(ctx, GetWindowPort(win));
    printf("aglSetDrawable -> %d (err %d)\n", (int)ok, (int)aglGetError());
    aglSetCurrentContext(ctx);

    glViewport(0, 0, 480, 400);
    glMatrixMode(GL_PROJECTION); glLoadIdentity();
    glOrtho(-1.0, 1.0, -1.0, 1.0, -1.0, 1.0);
    glMatrixMode(GL_MODELVIEW);  glLoadIdentity();
    glDisable(GL_DEPTH_TEST);
    glShadeModel(GL_SMOOTH);

    /* SPINNING Gouraud triangle: rotates about Z so the setup engine re-transforms and
     * re-rasterizes fresh geometry (and both windings) every frame -- exercises the full
     * 3D PIO path continuously, now that the static case is proven. */
    printf("rendering 180 frames (spinning Gouraud triangle)...\n"); fflush(stdout);
    int i;
    for (i = 0; i < 180; i++) {
        float a = (float)i * 2.0f;                   /* degrees/frame */
        glClearColor(0.10f, 0.10f, 0.15f, 1.0f);     /* dark slate background */
        glClear(GL_COLOR_BUFFER_BIT);

        glLoadIdentity();
        glRotatef(a, 0.0f, 0.0f, 1.0f);              /* spin about Z */

        glBegin(GL_TRIANGLES);
            glColor3f(1.0f, 0.0f, 0.0f); glVertex2f( 0.0f,  0.8f);
            glColor3f(0.0f, 1.0f, 0.0f); glVertex2f(-0.8f, -0.6f);
            glColor3f(0.0f, 0.0f, 1.0f); glVertex2f( 0.8f, -0.6f);
        glEnd();

        aglSwapBuffers(ctx);                         /* -> gldFlushBuffer -> Path-B present */
        usleep(16000);                               /* ~60 Hz; window stays up ~3s */
    }
    printf("done -- 180 spinning triangle frames presented\n");

    aglSetCurrentContext(NULL);
    aglSetDrawable(ctx, NULL);
    aglDestroyContext(ctx);
    aglDestroyPixelFormat(pf);
    DisposeWindow(win);
    return 0;
}

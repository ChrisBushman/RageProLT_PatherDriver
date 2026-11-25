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
#include <stdlib.h>

int main(void)
{
    Rect bounds = { 60, 1920 + 80, 460, 1920 + 560 };      /* on the Rage display (global x 1920..2560) */              /* top,left,bottom,right -> 480x400 */
    WindowRef win = NULL;
    OSStatus err = CreateNewWindow(kDocumentWindowClass,
                                   kWindowStandardDocumentAttributes, &bounds, &win);
    if (err || !win) { printf("CreateNewWindow err=%d\n", (int)err); return 1; }
    SetWindowTitleWithCFString(win, CFSTR("mach64 Path-B 3D triangle"));
    ShowWindow(win);

    GLint attrs[] = { AGL_RGBA, AGL_DOUBLEBUFFER, AGL_ACCELERATED,
                      AGL_DEPTH_SIZE, 16, AGL_NONE };
    GDHandle gd = NULL; { CGDirectDisplayID ids[8]; CGDisplayCount nd = 0, k; CGGetActiveDisplayList(8, ids, &nd);
        for (k = 0; k < nd; k++) if ((int)CGDisplayBounds(ids[k]).origin.x == 1920) DMGetGDeviceByDisplayID((DisplayIDType)ids[k], &gd, false); }
    printf("rage gdevice %p\n", (void *)gd);
    AGLPixelFormat pf = aglChoosePixelFormat(&gd, gd ? 1 : 0, attrs);
    { GLint rid = 0; aglDescribePixelFormat(pf, AGL_RENDERER_ID, &rid); printf("renderer id 0x%x\n", (unsigned)rid); }
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
        float a = getenv("AGL_STATIC") ? 20.0f : (float)i * 2.0f;                   /* degrees/frame */
        glClearColor(0.10f, 0.10f, 0.15f, 1.0f);     /* dark slate background */
        glClear(GL_COLOR_BUFFER_BIT);

        glLoadIdentity();
        glRotatef(a, 0.0f, 0.0f, 1.0f);              /* spin about Z */

        glBegin(GL_TRIANGLES);
            glColor3f(1.0f, 0.0f, 0.0f); glVertex2f( 0.0f,  0.8f);
            glColor3f(0.0f, 1.0f, 0.0f); glVertex2f(-0.8f, -0.6f);
            glColor3f(0.0f, 0.0f, 1.0f); glVertex2f( 0.8f, -0.6f);
        glEnd();

        if (i == 90) { int gx; unsigned char rowpx[480 * 4]; glReadPixels(0, 200, 480, 1, GL_RGBA, GL_UNSIGNED_BYTE, rowpx); printf("row GL y=200:"); for (gx = 20; gx < 480; gx += 30) printf(" %d:%02x%02x%02x", gx, rowpx[gx*4], rowpx[gx*4+1], rowpx[gx*4+2]); printf("\n"); }
        if (i == 90) { unsigned char px[4] = {0,0,0,0}; glReadPixels(240, 60, 1, 1, GL_RGBA, GL_UNSIGNED_BYTE, px); printf("readpixels (240,60) = %02x %02x %02x %02x\n", px[0], px[1], px[2], px[3]); }
        if (i == 100) system("./scanshot /tmp/agl_scan.ppm");
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

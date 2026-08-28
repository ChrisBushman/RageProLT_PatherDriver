/* mach64_wintest.c - WINDOWED coexistence probe for the Rage LT Pro GL driver.
 *
 * Binds an ACCELERATED double-buffered AGL context to a normal 400x300 Carbon WINDOW and
 * renders a spinning triangle into it for a few seconds WHILE THE DESKTOP STAYS LIVE.  The
 * driver (env MACH64_WIN_RECT) renders into an off-screen back buffer and BLITs the window
 * rect into the live front buffer on each swap -- no CRTC page-flip -- so the WindowServer
 * keeps compositing the desktop concurrently with our bus-master blit + geometry DMA.
 *
 * This is the coexistence test: does sharing the engine with the live WindowServer render
 * cleanly in the window, or does it hard-lock?  (Earlier full-screen game mode dodged this by
 * effectively owning the display.)  We also still capture the windowed drawable descriptor
 * (driver descdump) and print the window CONTENT bounds.
 *
 * Build:  cc -o mach64_wintest mach64_wintest.c -framework Carbon -framework AGL -framework OpenGL
 */
#include <Carbon/Carbon.h>
#include <AGL/agl.h>
#include <OpenGL/gl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

/* Private CGS API (Panther): probe whether a REAL accelerated drawable surface enumerates and what
 * surface-ID/bounds it carries -- the data the bundle's surface-ID -> window-bounds resolver needs. */
typedef int CGSConnectionID; typedef int CGSWindowID; typedef int CGSSurfaceID;
extern CGSConnectionID _CGSDefaultConnection(void);
extern int CGSGetWindowList(CGSConnectionID,CGSConnectionID,int,CGSWindowID*,int*);
extern int CGSGetSurfaceList(CGSConnectionID,CGSWindowID,int,CGSSurfaceID*,int*);
extern int CGSGetScreenRectForWindow(CGSConnectionID,CGSWindowID,CGRect*);
extern int CGSGetSurfaceBounds(CGSConnectionID,CGSWindowID,CGSSurfaceID,CGRect*);

int main(int argc, char **argv)
{
    /* "noenv" arg: skip setting MACH64_WIN_RECT, forcing the driver to rely SOLELY on its
     * automatic CGS surface-ID -> window-bounds resolver (no manual-rect crutch to mask a failure). */
    int noenv = (argc > 1 && argv[1] && !strcmp(argv[1], "noenv"));
    Rect r = { 80, 120, 380, 520 };   /* {top,left,bottom,right}: 400x300 content at (120,80) */
    WindowRef win = NULL;
    OSStatus err = CreateNewWindow(kDocumentWindowClass,
                                   kWindowStandardDocumentAttributes | kWindowStandardHandlerAttribute,
                                   &r, &win);
    if (err != noErr || !win) { fprintf(stderr, "CreateNewWindow failed: %d\n", (int)err); return 1; }
    SetWindowTitleWithCFString(win, CFSTR("mach64 wintest"));
    ShowWindow(win);

    /* Actual content bounds (global screen coords).  Hand them to the driver via the env var
     * BEFORE attach so it can blit our render into the live desktop at the right spot. */
    Rect cb; GetWindowBounds(win, kWindowContentRgn, &cb);
    int wx = cb.left, wy = cb.top, ww = cb.right - cb.left, wh = cb.bottom - cb.top;
    fprintf(stderr, "CONTENT bounds l=%d t=%d r=%d b=%d  w=%d h=%d\n",
            cb.left, cb.top, cb.right, cb.bottom, ww, wh);
    if (noenv) {
        unsetenv("MACH64_WIN_RECT");
        fprintf(stderr, "NOENV: MACH64_WIN_RECT unset -- driver must resolve via CGS alone\n");
    } else {
        char buf[64]; snprintf(buf, sizeof buf, "%d,%d,%d,%d", wx, wy, ww, wh);
        setenv("MACH64_WIN_RECT", buf, 1);
        fprintf(stderr, "MACH64_WIN_RECT=%s\n", buf);
    }

    GLint attribs[] = { AGL_RGBA, AGL_DOUBLEBUFFER, AGL_DEPTH_SIZE, 16,
                        AGL_ACCELERATED, AGL_NO_RECOVERY, AGL_NONE };
    AGLPixelFormat pf = aglChoosePixelFormat(NULL, 0, attribs);
    if (!pf) { fprintf(stderr, "aglChoosePixelFormat(ACCELERATED) FAILED (err=%d)\n", (int)aglGetError()); return 2; }
    AGLContext ctx = aglCreateContext(pf, NULL);
    if (!ctx) { fprintf(stderr, "aglCreateContext failed: %d\n", (int)aglGetError()); return 3; }
    if (!aglSetDrawable(ctx, GetWindowPort(win)))
        fprintf(stderr, "aglSetDrawable failed: %d\n", (int)aglGetError());
    aglSetCurrentContext(ctx);

    fprintf(stderr, "GL_RENDERER=%s\n", (const char *)glGetString(GL_RENDERER));
    fprintf(stderr, "GL_VENDOR=%s\n",   (const char *)glGetString(GL_VENDOR));

    /* CGS PROBE (option b resolver): with a REAL accelerated drawable now bound, does its surface
     * enumerate?  Log each app window's screen rect + surfaces + surface bounds.  Compare the
     * surface id/bounds against the known content (%d,%d %dx%d) and the 0x80xx drawable-id the
     * bundle sees -- to decide surface-based vs window-based resolution. */
    {   CGSConnectionID cid = _CGSDefaultConnection();
        CGSWindowID wins[256]; int nwin = 0, i, j;
        CGSGetWindowList(cid, cid, 256, wins, &nwin);
        fprintf(stderr, "[cgs] cid=%d nwin=%d (known content %d,%d %dx%d)\n",
                cid, nwin, wx, wy, ww, wh);
        for (i = 0; i < nwin; i++) {
            CGRect wr = {{0,0},{0,0}}; CGSSurfaceID surfs[64]; int ns = 0;
            CGSGetScreenRectForWindow(cid, wins[i], &wr);
            CGSGetSurfaceList(cid, wins[i], 64, surfs, &ns);
            fprintf(stderr, "[cgs] win=%d rect=(%.0f,%.0f %.0fx%.0f) nsurf=%d\n",
                    wins[i], wr.origin.x, wr.origin.y, wr.size.width, wr.size.height, ns);
            for (j = 0; j < ns; j++) {
                CGRect sb = {{0,0},{0,0}};
                CGSGetSurfaceBounds(cid, wins[i], surfs[j], &sb);
                fprintf(stderr, "[cgs]   surf=0x%x bounds=(%.0f,%.0f %.0fx%.0f)\n",
                        (unsigned)surfs[j], sb.origin.x, sb.origin.y, sb.size.width, sb.size.height);
            }
        }
    }

    glViewport(0, 0, ww, wh);
    fprintf(stderr, "rendering spinning triangle into the window for ~4s (desktop stays live)...\n");
    {   int i;
        for (i = 0; i < 200; i++) {
            glClearColor(0.1f, 0.4f, 0.8f, 1.0f);
            glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
            glMatrixMode(GL_MODELVIEW);
            glLoadIdentity();
            glRotatef((float)i * 3.0f, 0.0f, 0.0f, 1.0f);
            glBegin(GL_TRIANGLES);
              glColor3f(1.0f, 0.0f, 0.0f); glVertex2f(-0.6f, -0.5f);
              glColor3f(0.0f, 1.0f, 0.0f); glVertex2f( 0.6f, -0.5f);
              glColor3f(0.0f, 0.0f, 1.0f); glVertex2f( 0.0f,  0.6f);
            glEnd();
            aglSwapBuffers(ctx);
            usleep(20000);   /* ~50fps pace */
        }
    }

    fprintf(stderr, "render loop done, tearing down.\n");
    aglSetCurrentContext(NULL);
    aglSetDrawable(ctx, NULL);
    aglDestroyContext(ctx);
    aglDestroyPixelFormat(pf);
    DisposeWindow(win);
    fprintf(stderr, "wintest done.\n");
    return 0;
}

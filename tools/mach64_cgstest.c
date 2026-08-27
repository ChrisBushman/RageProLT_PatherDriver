/* mach64_cgstest.c - validate the CGS surface-ID -> window-bounds resolver for the windowed
 * handshake (option b).  Creates a window at a KNOWN position (120,80, 400x300), binds an
 * accelerated AGL context (which creates the CGS drawable surface), then enumerates the app's
 * windows + their surfaces + bounds via the private CoreGraphics CGS API -- exactly what the bundle
 * will do to turn GLEngine's drawable surface-ID into the on-screen (x,y,w,h) for the blit.
 *
 * Pure userspace CoreGraphics + AGL -- NO kext, no fullscreen takeover -> SAFE (no panic risk).
 * Must run in the console GUI session (Carbon needs the WindowServer): sudo not required.
 * Build: cc -O2 -o mach64_cgstest mach64_cgstest.c -framework Carbon -framework AGL
 *           -framework OpenGL -framework ApplicationServices
 *
 * What to look for: does a window's screenRect == (120,80 400x300), and what surface-ID does its
 * drawable carry (does it look like the 0x80xx GLEngine hands the bundle as drawable[0])?
 */
#include <Carbon/Carbon.h>
#include <AGL/agl.h>
#include <OpenGL/gl.h>
#include <ApplicationServices/ApplicationServices.h>
#include <stdio.h>
#include <stdlib.h>

/* Private CGS API (Panther), signatures reversed from CoreGraphics. */
typedef int CGSConnectionID; typedef int CGSWindowID; typedef int CGSSurfaceID; typedef int CGError;
extern CGSConnectionID _CGSDefaultConnection(void);
extern CGError CGSGetWindowList(CGSConnectionID cid, CGSConnectionID target, int cap, CGSWindowID *list, int *cnt);
extern CGError CGSGetOnScreenWindowList(CGSConnectionID cid, CGSConnectionID target, int cap, CGSWindowID *list, int *cnt);
extern CGError CGSGetSurfaceList(CGSConnectionID cid, CGSWindowID wid, int cap, CGSSurfaceID *list, int *cnt);
extern CGError CGSGetScreenRectForWindow(CGSConnectionID cid, CGSWindowID wid, CGRect *r);
extern CGError CGSGetSurfaceBounds(CGSConnectionID cid, CGSWindowID wid, CGSSurfaceID sid, CGRect *b);

static void enumerate(const char *tag, CGSConnectionID cid,
                      CGError (*getlist)(CGSConnectionID,CGSConnectionID,int,CGSWindowID*,int*))
{
    CGSWindowID wins[512]; int nwin = 0;
    CGError e = getlist(cid, cid, 512, wins, &nwin);
    printf("== %s -> err=%d nwin=%d ==\n", tag, e, nwin);
    int i, j;
    for (i = 0; i < nwin; i++) {
        CGRect wr = {{0,0},{0,0}};
        CGError we = CGSGetScreenRectForWindow(cid, wins[i], &wr);
        CGSSurfaceID surfs[64]; int ns = 0;
        CGError se = CGSGetSurfaceList(cid, wins[i], 64, surfs, &ns);
        printf("  win id=%d screenRect_err=%d (%.0f,%.0f %.0fx%.0f)  surflist_err=%d nsurf=%d\n",
               wins[i], we, wr.origin.x, wr.origin.y, wr.size.width, wr.size.height, se, ns);
        for (j = 0; j < ns; j++) {
            CGRect sb = {{0,0},{0,0}};
            CGError be = CGSGetSurfaceBounds(cid, wins[i], surfs[j], &sb);
            printf("      surf id=0x%x (%d)  bounds_err=%d (%.0f,%.0f %.0fx%.0f)\n",
                   surfs[j], surfs[j], be, sb.origin.x, sb.origin.y, sb.size.width, sb.size.height);
        }
    }
    fflush(stdout);
}

int main(void)
{
    Rect r; r.top = 80; r.left = 120; r.bottom = 80 + 300; r.right = 120 + 400;  /* (120,80) 400x300 */
    WindowRef win = NULL;
    if (CreateNewWindow(kDocumentWindowClass,
            kWindowStandardDocumentAttributes | kWindowStandardHandlerAttribute, &r, &win) != noErr || !win) {
        printf("CreateNewWindow FAILED\n"); return 1;
    }
    SetWindowTitleWithCFString(win, CFSTR("mach64 cgstest"));
    ShowWindow(win);

    GLint attrs[] = { AGL_RGBA, AGL_DOUBLEBUFFER, AGL_ACCELERATED, AGL_NONE };
    AGLPixelFormat pf = aglChoosePixelFormat(NULL, 0, attrs);
    AGLContext ctx = pf ? aglCreateContext(pf, NULL) : NULL;
    if (ctx) {
        aglSetDrawable(ctx, GetWindowPort(win));
        aglSetCurrentContext(ctx);
        printf("GL_RENDERER = %s\n", (const char *)glGetString(GL_RENDERER));
        glClearColor(0,0,0,1); glClear(GL_COLOR_BUFFER_BIT); aglSwapBuffers(ctx);
    } else {
        printf("no accelerated pixel format/context (continuing to enumerate anyway)\n");
    }

    Rect cb; GetWindowBounds(win, kWindowContentRgn, &cb);
    printf("KNOWN window content bounds (Carbon): (%d,%d) %dx%d\n",
           cb.left, cb.top, cb.right - cb.left, cb.bottom - cb.top);

    CGSConnectionID cid = _CGSDefaultConnection();
    printf("_CGSDefaultConnection = %d\n\n", cid);
    enumerate("CGSGetWindowList(app)", cid, CGSGetWindowList);
    enumerate("CGSGetOnScreenWindowList(app)", cid, CGSGetOnScreenWindowList);

    printf("=== done ===\n");
    return 0;
}

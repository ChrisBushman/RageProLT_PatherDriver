/* mach64_multiwin.c - multi-window disambiguation PROBE.  Creates TWO accelerated GL windows of
 * different size at different positions, binds a context to each, and renders a distinct colour to
 * each (A=red, B=green).  The point is the DRIVER LOG: two "descdump[0..15]" lines (one per attach)
 * and two "[cgs] ... -> screen" / "[win] WINDOWED" lines.  We compare:
 *   - do the two descriptors differ in a field that CORRELATES with the window (bounds/id)?  (If yes,
 *     the resolver can match on it; if only d[0] differs and it's opaque, it can't.)
 *   - does the current heuristic resolve BOTH to the SAME window (the bug), or two different rects?
 * Also prints each window's true CONTENT bounds + its CGS window/surface, for correlation.
 * Build: cc -framework Carbon -framework AGL -framework OpenGL */
#include <Carbon/Carbon.h>
#include <AGL/agl.h>
#include <OpenGL/gl.h>
#include <stdio.h>
#include <unistd.h>

typedef int CGSConnectionID; typedef int CGSWindowID; typedef int CGSSurfaceID;
extern CGSConnectionID _CGSDefaultConnection(void);
extern int CGSGetWindowList(CGSConnectionID,CGSConnectionID,int,CGSWindowID*,int*);
extern int CGSGetSurfaceList(CGSConnectionID,CGSWindowID,int,CGSSurfaceID*,int*);
extern int CGSGetScreenRectForWindow(CGSConnectionID,CGSWindowID,CGRect*);
extern int CGSGetSurfaceBounds(CGSConnectionID,CGSWindowID,CGSSurfaceID,CGRect*);

static WindowRef mkwin(short t, short l, short b, short r, const char *title)
{
    Rect rc = { t, l, b, r }; WindowRef w = NULL;
    CreateNewWindow(kDocumentWindowClass, kWindowStandardDocumentAttributes|kWindowStandardHandlerAttribute, &rc, &w);
    if (w) { SetWindowTitleWithCFString(w, CFStringCreateWithCString(NULL,title,kCFStringEncodingASCII)); ShowWindow(w); }
    return w;
}

static void render(AGLContext c, WindowRef w, float R, float G, float B)
{
    aglSetDrawable(c, GetWindowPort(w));
    aglSetCurrentContext(c);
    glClearColor(R, G, B, 1); glClear(GL_COLOR_BUFFER_BIT);
    glViewport(0,0,300,200);
    aglSwapBuffers(c);
}

static void cgs_dump(void)
{
    CGSConnectionID cid = _CGSDefaultConnection();
    CGSWindowID wins[256]; int nwin=0, i, j;
    CGSGetWindowList(cid, cid, 256, wins, &nwin);
    fprintf(stderr, "[cgs] nwin=%d\n", nwin);
    for (i=0;i<nwin;i++){ CGRect wr={{0,0},{0,0}}; CGSSurfaceID s[64]; int ns=0;
        CGSGetScreenRectForWindow(cid,wins[i],&wr); CGSGetSurfaceList(cid,wins[i],64,s,&ns);
        if (ns<=0) continue;
        fprintf(stderr,"[cgs] win=%d rect=(%.0f,%.0f %.0fx%.0f) nsurf=%d",
                wins[i],wr.origin.x,wr.origin.y,wr.size.width,wr.size.height,ns);
        for (j=0;j<ns;j++){ CGRect sb={{0,0},{0,0}}; CGSGetSurfaceBounds(cid,wins[i],s[j],&sb);
            fprintf(stderr,"  surf=0x%x(%.0f,%.0f %.0fx%.0f)",(unsigned)s[j],sb.origin.x,sb.origin.y,sb.size.width,sb.size.height); }
        fprintf(stderr,"\n"); }
}

int main(void)
{
    GLint at[] = { AGL_RGBA, AGL_DOUBLEBUFFER, AGL_ACCELERATED, AGL_NO_RECOVERY, AGL_NONE };
    AGLPixelFormat pf = aglChoosePixelFormat(NULL, 0, at);
    AGLContext cA, cB;
    WindowRef wA, wB;
    Rect cbA, cbB;

    if (!pf) { fprintf(stderr, "no pf\n"); return 1; }
    wA = mkwin(80, 100, 280, 400, "winA");   /* 300x200 content ~ (100,80) */
    wB = mkwin(300, 500, 450, 700, "winB");  /* 200x150 content ~ (500,300) */
    if (!wA || !wB) { fprintf(stderr, "window create failed\n"); return 1; }
    GetWindowBounds(wA, kWindowContentRgn, &cbA);
    GetWindowBounds(wB, kWindowContentRgn, &cbB);
    fprintf(stderr, "winA content (%d,%d %dx%d)  winB content (%d,%d %dx%d)\n",
            cbA.left,cbA.top,cbA.right-cbA.left,cbA.bottom-cbA.top,
            cbB.left,cbB.top,cbB.right-cbB.left,cbB.bottom-cbB.top);
    cgs_dump();

    cA = aglCreateContext(pf, NULL);
    cB = aglCreateContext(pf, NULL);
    if (!cA || !cB) { fprintf(stderr, "ctx create failed\n"); return 1; }
    fprintf(stderr, "GL_RENDERER=%s\n", (const char *)(aglSetCurrentContext(cA), glGetString(GL_RENDERER)));

    fprintf(stderr, "--- render A=RED into winA ---\n");   render(cA, wA, 1,0,0);
    fprintf(stderr, "--- render B=GREEN into winB ---\n"); render(cB, wB, 0,1,0);
    fprintf(stderr, "check the two descdump/[win] driver-log lines: same rect twice = heuristic bug\n");
    sleep(6);

    aglSetCurrentContext(NULL);
    aglDestroyContext(cA); aglDestroyContext(cB); aglDestroyPixelFormat(pf);
    return 0;
}

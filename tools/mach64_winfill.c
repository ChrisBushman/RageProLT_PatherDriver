/* mach64_winfill.c - minimal repro for "bottom 44 rows of a windowed GL surface never reach the screen".
 * Opens a 640x384 content window at screen (0,44) (same geometry as the Sonic R window on the 640x480 Rage display),
 * draws 12 horizontal colour bands (32 rows each, a distinct colour per band), a white bar in the last 8 rows and a
 * moving square, swaps.  Pure GL, no game code.  Env: WF_DEPTH=1 (16-bit depth buffer + depth test), WF_FRAMES=n,
 * WF_Y=top of content (default 44), WF_H=height (default 384).
 * Build: cc -o mach64_winfill mach64_winfill.c -framework Carbon -framework AGL -framework OpenGL */
#include <Carbon/Carbon.h>
#include <AGL/agl.h>
#include <OpenGL/gl.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
int main(void)
{
    int top = getenv("WF_Y") ? atoi(getenv("WF_Y")) : 44, h = getenv("WF_H") ? atoi(getenv("WF_H")) : 384, w = 640;
    int depth = getenv("WF_DEPTH") != 0, nf = getenv("WF_FRAMES") ? atoi(getenv("WF_FRAMES")) : 1500, i, b;
    int reqh = getenv("WF_REQH") ? atoi(getenv("WF_REQH")) : 0, vph = getenv("WF_VPH") ? atoi(getenv("WF_VPH")) : 0, ctx_first = getenv("WF_CONSTRAIN_AFTER_CTX") != 0;
    Rect r; WindowRef win = NULL; r.top = top; r.left = 0; r.bottom = top + (reqh ? reqh : h); r.right = w;
    if (CreateNewWindow(kDocumentWindowClass, kWindowStandardDocumentAttributes | kWindowStandardHandlerAttribute, &r, &win) || !win) return 1;
    SetWindowTitleWithCFString(win, CFSTR("winfill")); ShowWindow(win);
    { ProcessSerialNumber psn = { 0, kCurrentProcess }; SetFrontProcess(&psn); SelectWindow(win); }   /* come to front: the launching Terminal otherwise covers the window */
    if (reqh && !ctx_first) ConstrainWindowToScreen(win, kWindowStructureRgn, kWindowConstrainStandardOptions, NULL, NULL);
    {   Rect cb; GetWindowBounds(win, kWindowContentRgn, &cb); h = cb.bottom - cb.top; fprintf(stderr, "content l=%d t=%d r=%d b=%d (h=%d)\n", cb.left, cb.top, cb.right, cb.bottom, h); }
    GLint at[] = { AGL_RGBA, AGL_DOUBLEBUFFER, AGL_DEPTH_SIZE, depth ? 16 : 0, AGL_ACCELERATED, AGL_NO_RECOVERY, AGL_NONE };
    AGLPixelFormat pf = aglChoosePixelFormat(NULL, 0, at); if (!pf) return 2;
    AGLContext ctx = aglCreateContext(pf, NULL); if (!ctx) return 3;
    aglSetDrawable(ctx, GetWindowPort(win)); aglSetCurrentContext(ctx);
    fprintf(stderr, "GL_RENDERER=%s depth=%d\n", (const char *)glGetString(GL_RENDERER), depth);
    if (reqh && ctx_first) { ConstrainWindowToScreen(win, kWindowStructureRgn, kWindowConstrainStandardOptions, NULL, NULL);
        { Rect cb; GetWindowBounds(win, kWindowContentRgn, &cb); h = cb.bottom - cb.top; fprintf(stderr, "after ctx: content t=%d b=%d (h=%d)\n", cb.top, cb.bottom, h); } aglUpdateContext(ctx); }
    { int V = vph ? vph : h; glViewport(0, 0, w, V); glMatrixMode(GL_PROJECTION); glLoadIdentity(); glOrtho(0, w, V, 0, -1, 1); } glMatrixMode(GL_MODELVIEW); glLoadIdentity();
    if (depth) glEnable(GL_DEPTH_TEST);
    for (i = 0; i < nf; i++) {
        glClearColor(0, 0, 0, 1); glClear(GL_COLOR_BUFFER_BIT | (depth ? GL_DEPTH_BUFFER_BIT : 0));
        for (b = 0; b < h / 32; b++) {
            float c = (float)(b + 1) / (float)(h / 32);
            glColor3f((b & 1) ? c : 0.2f, (b & 2) ? 1.0f - c : 0.3f, (b & 4) ? c : 0.6f);
            glBegin(GL_QUADS); glVertex2f(0, b * 32); glVertex2f(w, b * 32); glVertex2f(w, b * 32 + 32); glVertex2f(0, b * 32 + 32); glEnd();
        }
        glColor3f(1, 1, 1);
        glBegin(GL_QUADS); glVertex2f(0, h - 8); glVertex2f(w, h - 8); glVertex2f(w, h); glVertex2f(0, h); glEnd();
        glColor3f(1, 1, 0); { float x = (float)((i * 7) % (w - 40));
        glBegin(GL_QUADS); glVertex2f(x, h / 2 - 20); glVertex2f(x + 40, h / 2 - 20); glVertex2f(x + 40, h / 2 + 20); glVertex2f(x, h / 2 + 20); glEnd(); }
        aglSwapBuffers(ctx); usleep(30000);
    }
    aglSetCurrentContext(NULL); aglSetDrawable(ctx, NULL); aglDestroyContext(ctx); DisposeWindow(win);
    return 0;
}

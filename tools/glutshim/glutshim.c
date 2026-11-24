/* glutshim.c - GLUT subset on Carbon + AGL (see GLUT/glut.h).  Windows are positioned relative to the 640x480 display (the Rage on the
 * MDD rig) unless GLUTSHIM_ORIGIN=main.  Env: GLUTSHIM_SECS (run time, default 30), GLUTSHIM_POS="x,y" (offset on that display, applied to
 * the first window; further windows are cascaded 40 px), GLUTSHIM_NOCAP=1 (no ~60 fps idle pacing).  Keyboard/menu/text calls are accepted but do
 * nothing.  Build: cc -Iglutshim prog.c ... glutshim/glutshim.c -framework Carbon -framework AGL -framework OpenGL */
#include <Carbon/Carbon.h>
#include <AGL/agl.h>
#include <GLUT/glut.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/time.h>

#define MAXW 8
typedef struct { WindowRef win; AGLContext ctx; void (*disp)(void), (*idle_unused)(void); void (*resh)(int,int); void (*vis)(int); int lw, lh, lx, ly, redisp, started; } GW;
static GW g_w[MAXW]; static int g_n = 0, g_cur = 0;
static void (*g_idle)(void);
static int g_iw = 300, g_ih = 300, g_ix = -1, g_iy = -1, g_depth = 16;
static int g_ox = 0, g_oy = 0, g_orgdone = 0;

void glutInit(int *argc, char **argv) { (void)argc; (void)argv; }
void glutInitDisplayMode(unsigned int m) { (void)m; }
void glutInitDisplayString(const char *s) { const char *d = s ? strstr(s, "depth=") : 0; if (d) g_depth = atoi(d + 6) > 16 ? 16 : atoi(d + 6); }
void glutInitWindowSize(int w, int h) { g_iw = w; g_ih = h; }
void glutInitWindowPosition(int x, int y) { g_ix = x; g_iy = y; }

static void find_origin(void)
{
    CGDirectDisplayID ids[8]; CGDisplayCount n = 0, k; const char *o = getenv("GLUTSHIM_ORIGIN");
    if (g_orgdone) return; g_orgdone = 1;
    if (o && !strcmp(o, "main")) return;
    CGGetActiveDisplayList(8, ids, &n);
    for (k = 0; k < n; k++) { CGRect b = CGDisplayBounds(ids[k]); if ((int)b.size.width == 640 && (int)b.size.height == 480) { g_ox = (int)b.origin.x; g_oy = (int)b.origin.y; } }
}

int glutCreateWindow(const char *title)
{
    GW *w; Rect r; int x, y; GLint at[16], na = 0; AGLPixelFormat pf; const char *p;
    if (g_n >= MAXW) return 0;
    find_origin();
    x = g_ix >= 0 ? g_ix : 100; y = g_iy >= 0 ? g_iy : 80;
    if (y < 60) y = 60;   /* the content must sit below the 22 px menu bar AND the 22 px title bar, or the window frame is hidden under the menu bar */
    if (g_n == 0 && (p = getenv("GLUTSHIM_POS"))) sscanf(p, "%d,%d", &x, &y);
    x += g_n * 40; y += g_n * 40;
    r.left = (short)(g_ox + x); r.top = (short)(g_oy + y); r.right = r.left + (short)g_iw; r.bottom = r.top + (short)g_ih;
    w = &g_w[g_n]; memset(w, 0, sizeof *w);
    if (CreateNewWindow(kDocumentWindowClass, kWindowStandardDocumentAttributes | kWindowStandardHandlerAttribute, &r, &w->win) != noErr || !w->win) return 0;
    SetWindowTitleWithCFString(w->win, CFStringCreateWithCString(NULL, title, kCFStringEncodingASCII));
    ShowWindow(w->win);
    at[na++] = AGL_RGBA; at[na++] = AGL_DOUBLEBUFFER; at[na++] = AGL_DEPTH_SIZE; at[na++] = g_depth;
    at[na++] = AGL_ACCELERATED; at[na++] = AGL_NO_RECOVERY; at[na++] = AGL_NONE;
    pf = aglChoosePixelFormat(NULL, 0, at);
    if (!pf) { fprintf(stderr, "glutshim: no accelerated pixel format (agl %d)\n", (int)aglGetError()); return 0; }
    w->ctx = aglCreateContext(pf, NULL);
    if (!w->ctx || !aglSetDrawable(w->ctx, GetWindowPort(w->win))) { fprintf(stderr, "glutshim: context/drawable failed (agl %d)\n", (int)aglGetError()); return 0; }
    aglSetCurrentContext(w->ctx);
    fprintf(stderr, "glutshim: window %d at (%d,%d) %dx%d  GL_RENDERER=%s\n", g_n + 1, (int)r.left, (int)r.top, g_iw, g_ih, (const char *)glGetString(GL_RENDERER));
    g_cur = g_n; g_n++;
    return g_n;
}
void glutSetWindow(int id) { if (id >= 1 && id <= g_n) { g_cur = id - 1; aglSetCurrentContext(g_w[g_cur].ctx); } }
void glutDisplayFunc(void (*f)(void)) { g_w[g_cur].disp = f; }
void glutReshapeFunc(void (*f)(int, int)) { g_w[g_cur].resh = f; }
void glutKeyboardFunc(void (*f)(unsigned char, int, int)) { (void)f; }
void glutVisibilityFunc(void (*f)(int)) { g_w[g_cur].vis = f; }
void glutIdleFunc(void (*f)(void)) { g_idle = f; }
void glutPostRedisplay(void) { g_w[g_cur].redisp = 1; }
void glutSwapBuffers(void) { aglSwapBuffers(g_w[g_cur].ctx); }
int  glutCreateMenu(void (*f)(int)) { (void)f; return 1; }
void glutAddMenuEntry(const char *l, int v) { (void)l; (void)v; }
void glutAddSubMenu(const char *l, int m) { (void)l; (void)m; }
void glutAttachMenu(int b) { (void)b; }
void glutBitmapCharacter(void *font, int c) { (void)font; (void)c; }
void glutGameModeString(const char *s) { (void)s; }
int  glutEnterGameMode(void) { fprintf(stderr, "glutshim: game mode unsupported\n"); exit(3); }
void glutLeaveGameMode(void) {}

static double now(void) { struct timeval t; gettimeofday(&t, 0); return t.tv_sec + t.tv_usec * 1e-6; }

void glutMainLoop(void)
{
    double t0 = now(), secs = getenv("GLUTSHIM_SECS") ? atof(getenv("GLUTSHIM_SECS")) : 30.0, tf = t0; long frames = 0; int i;
    for (;;) {
        EventRef e;
        while (ReceiveNextEvent(0, NULL, 0.0, true, &e) == noErr) { SendEventToEventTarget(e, GetEventDispatcherTarget()); ReleaseEvent(e); }
        if (now() - t0 > secs) break;
        for (i = 0; i < g_n; i++) {
            GW *w = &g_w[i]; Rect cb; int cw, ch;
            if (!IsValidWindowPtr(w->win)) continue;
            GetWindowBounds(w->win, kWindowContentRgn, &cb); cw = cb.right - cb.left; ch = cb.bottom - cb.top;
            g_cur = i; aglSetCurrentContext(w->ctx);
            /* aglUpdateContext only when the window was resized or moved (as a real GLUT does).  Calling it every pass made the GL driver
             * re-attach the drawable each frame: a new Apple surface + a fresh, never-rendered backing per frame, presented as noise. */
            if (!w->started || cw != w->lw || ch != w->lh || cb.left != w->lx || cb.top != w->ly) { aglUpdateContext(w->ctx); w->lx = cb.left; w->ly = cb.top; }
            if (!w->started) { w->started = 1; if (w->vis) w->vis(GLUT_VISIBLE); w->lw = -1; w->redisp = 1; }
            if (cw != w->lw || ch != w->lh) { w->lw = cw; w->lh = ch; if (w->resh) w->resh(cw, ch); w->redisp = 1; }
            if (g_idle && i == 0) g_idle();
            if (w->redisp) { w->redisp = 0; if (w->disp) w->disp(); frames++; }
        }
        if (now() - tf > 5.0) { fprintf(stderr, "glutshim: %.1f display callbacks/s\n", frames / (now() - tf)); frames = 0; tf = now(); }
        if (!getenv("GLUTSHIM_NOCAP")) usleep(8000);
    }
    for (i = 0; i < g_n; i++) if (IsValidWindowPtr(g_w[i].win)) { aglSetCurrentContext(NULL); aglSetDrawable(g_w[i].ctx, NULL); aglDestroyContext(g_w[i].ctx); DisposeWindow(g_w[i].win); }
    fprintf(stderr, "glutshim: done\n"); exit(0);
}

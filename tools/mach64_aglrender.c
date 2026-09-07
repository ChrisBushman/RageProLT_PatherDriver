/*
 * mach64_aglrender.c - minimal WINDOWED AGL render+present test for Path B.
 *
 * Exercises the whole windowed pipeline against our ATIRageProGLDriver.bundle:
 *   CreateNewWindow -> aglChoosePixelFormat(ACCELERATED) -> aglCreateContext ->
 *   aglSetDrawable (triggers gldAttachDrawable -> surface create/bind) ->
 *   glClear (blue) -> aglSwapBuffers (triggers gldFlushBuffer -> mach64_pathb_present).
 *
 * A blue 400x300 window = the Path-B render+present works.  Trace: /tmp/mach64gld.log.
 * MUST be run AT THE CONSOLE (needs the WindowServer), not over ssh:
 *     /tmp/aglrender
 * Build: cc -O2 -o /tmp/aglrender mach64_aglrender.c -framework AGL -framework OpenGL -framework Carbon
 */
#include <AGL/agl.h>
#include <OpenGL/gl.h>
#include <Carbon/Carbon.h>
#include <stdio.h>
#include <unistd.h>

int main(void)
{
    Rect bounds = { 100, 100, 400, 500 };            /* top,left,bottom,right -> 400x300 */
    WindowRef win = NULL;
    OSStatus err = CreateNewWindow(kDocumentWindowClass,
                                   kWindowStandardDocumentAttributes, &bounds, &win);
    if (err || !win) { printf("CreateNewWindow err=%d\n", (int)err); return 1; }
    SetWindowTitleWithCFString(win, CFSTR("mach64 Path-B render test"));
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

    printf("rendering 120 frames (blue)...\n"); fflush(stdout);
    int i;
    for (i = 0; i < 120; i++) {
        glClearColor(0.0f, 0.4f, 0.85f, 1.0f);       /* blue */
        glClear(GL_COLOR_BUFFER_BIT);
        aglSwapBuffers(ctx);                         /* -> gldFlushBuffer -> Path-B present */
        usleep(16000);                               /* ~60 Hz; window stays up ~2s */
    }
    printf("done -- 120 frames presented\n");

    aglSetCurrentContext(NULL);
    aglSetDrawable(ctx, NULL);
    aglDestroyContext(ctx);
    aglDestroyPixelFormat(pf);
    DisposeWindow(win);
    return 0;
}

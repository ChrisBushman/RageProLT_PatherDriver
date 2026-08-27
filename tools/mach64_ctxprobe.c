/* mach64_ctxprobe.c - minimal AGL accelerated-context creation, to trigger our bundle's
 * gldCreateContext (where the Path B Phase-1 apple-probe fires).  Creates a window + an
 * ACCELERATED context, prints GL_RENDERER, and quits -- NO rendering, NO swap (safe).
 * Build: cc -o mach64_ctxprobe mach64_ctxprobe.c -framework Carbon -framework AGL -framework OpenGL */
#include <Carbon/Carbon.h>
#include <AGL/agl.h>
#include <OpenGL/gl.h>
#include <stdio.h>

int main(void)
{
    Rect r = { 80, 120, 380, 520 };
    WindowRef win = NULL;
    if (CreateNewWindow(kDocumentWindowClass, kWindowStandardDocumentAttributes, &r, &win) != noErr || !win) {
        fprintf(stderr, "CreateNewWindow failed\n"); return 1;
    }
    ShowWindow(win);

    GLint attribs[] = { AGL_RGBA, AGL_DOUBLEBUFFER, AGL_DEPTH_SIZE, 16,
                        AGL_ACCELERATED, AGL_NO_RECOVERY, AGL_NONE };
    AGLPixelFormat pf = aglChoosePixelFormat(NULL, 0, attribs);
    if (!pf) { fprintf(stderr, "no accelerated pixel format (err=%d)\n", (int)aglGetError()); return 2; }
    AGLContext ctx = aglCreateContext(pf, NULL);   /* gldCreateContext fires here -> apple-probe */
    if (!ctx) { fprintf(stderr, "aglCreateContext failed (err=%d)\n", (int)aglGetError()); return 3; }
    aglSetDrawable(ctx, GetWindowPort(win));
    aglSetCurrentContext(ctx);
    fprintf(stderr, "GL_RENDERER=%s\n", (const char *)glGetString(GL_RENDERER));

    aglSetCurrentContext(NULL);
    aglSetDrawable(ctx, NULL);
    aglDestroyContext(ctx);
    aglDestroyPixelFormat(pf);
    DisposeWindow(win);
    fprintf(stderr, "ctxprobe done.\n");
    return 0;
}

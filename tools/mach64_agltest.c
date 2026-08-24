/*
 * mach64_agltest.c - windowed accelerated GL via AGL, to trigger gldAttachDrawable
 * and capture the CGS drawable descriptor the framework passes our driver.
 *
 * This build DELIBERATELY DOES NOT RENDER (no glClear/no draw) -- it only creates
 * a window, an accelerated context, and attaches the window drawable, then quits.
 * Goal: get gldAttachDrawable called and log the drawable[] descriptor safely
 * (no DMA to the framebuffer) so we can reverse the surface format before wiring
 * the real render path.
 *
 * Build: cc -framework Carbon -framework AGL -framework OpenGL
 */
#include <Carbon/Carbon.h>
#include <AGL/agl.h>
#include <OpenGL/gl.h>
#include <stdio.h>

int main(void)
{
    Rect r = { 80, 80, 340, 400 };          /* top, left, bottom, right */
    WindowRef win = NULL;
    OSStatus err = CreateNewWindow(kDocumentWindowClass,
                                   kWindowStandardDocumentAttributes, &r, &win);
    if (err || !win) { printf("CreateNewWindow err=%d\n", (int)err); return 1; }
    ShowWindow(win);

    /* single-buffer for now: our config nodes don't yet advertise the double-
     * buffer capability, so AGL_DOUBLEBUFFER makes aglChoosePixelFormat fail.
     * Single-buffer still triggers gldAttachDrawable (our actual goal here). */
    GLint attribs[] = { AGL_RGBA, AGL_ACCELERATED,
                        AGL_DEPTH_SIZE, 16, AGL_NONE };
    AGLPixelFormat pf = aglChoosePixelFormat(NULL, 0, attribs);
    printf("aglChoosePixelFormat -> %p (err %d)\n", (void*)pf, (int)aglGetError());
    if (!pf) return 1;

    AGLContext ctx = aglCreateContext(pf, NULL);
    printf("aglCreateContext -> %p (err %d)\n", (void*)ctx, (int)aglGetError());
    if (!ctx) { aglDestroyPixelFormat(pf); return 1; }

    /* THE trigger: attach the window's drawable -> gldAttachDrawable in our driver */
    GLboolean ok = aglSetDrawable(ctx, GetWindowPort(win));
    printf("aglSetDrawable -> %d (err %d)\n", (int)ok, (int)aglGetError());

    aglSetCurrentContext(ctx);
    printf("GL_RENDERER = %s\n", (const char *)glGetString(GL_RENDERER));

    /* NO rendering this build -- just tear down cleanly. */
    aglSetCurrentContext(NULL);
    aglSetDrawable(ctx, NULL);
    aglDestroyContext(ctx);
    aglDestroyPixelFormat(pf);
    DisposeWindow(win);
    printf("done (no render)\n");
    return 0;
}

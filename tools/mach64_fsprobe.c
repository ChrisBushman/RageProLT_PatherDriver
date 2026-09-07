/*
 * mach64_fsprobe.c -- SAFE fullscreen ATTACH PROBE (no render, no swap).
 *
 * Purpose: trigger our gldAttachDrawable(type 0x5a) for a FULLSCREEN AGL context so the driver logs
 * the fullscreen drawable descriptor + whether the Path-B surface bind + rt-peek succeed -- WITHOUT
 * rendering anything to the scanout (the panic-prone part).  This is the first data point for the
 * "3D on fullscreen surfaces" work: is the fullscreen descriptor a usable CGS surface id, does
 * make_surface/bind succeed, and what render-target offset/pitch does the peek return fullscreen?
 *
 * It captures the display (brief black screen), attaches, sleeps 1s so attach runs, then releases and
 * exits.  NO glClear / glDraw / aglSwapBuffers -- attach's surface prime (write_lock/flush) is the same
 * safe present fspresent uses, nothing writes geometry to VRAM.
 *
 * Build (on the Lombard):  cc -O2 -o ~/fsprobe tools/mach64_fsprobe.c \
 *                              -framework Carbon -framework AGL -framework OpenGL
 * Run AT THE CONSOLE:       MACH64_GLD_FSYNC=1 ~/fsprobe   ; then read ~/mach64gld.log
 */
#include <Carbon/Carbon.h>
#include <AGL/agl.h>
#include <OpenGL/gl.h>
#include <stdio.h>
#include <unistd.h>

int main(void)
{
    GDHandle dev = GetMainDevice();
    GLint attribs[] = { AGL_RGBA, AGL_ACCELERATED, AGL_FULLSCREEN,
                        AGL_DOUBLEBUFFER, AGL_DEPTH_SIZE, 16, AGL_NONE };
    AGLPixelFormat pf = aglChoosePixelFormat(&dev, 1, attribs);
    printf("aglChoosePixelFormat(fullscreen) -> %p (err %d)\n", (void*)pf, (int)aglGetError());
    if (!pf) return 1;

    AGLContext ctx = aglCreateContext(pf, NULL);
    printf("aglCreateContext -> %p (err %d)\n", (void*)ctx, (int)aglGetError());
    if (!ctx) { aglDestroyPixelFormat(pf); return 1; }

    /* THE trigger: full-screen at the current resolution -> gldAttachDrawable(type 0x5a). */
    GLboolean ok = aglSetFullScreen(ctx, 0, 0, 0, 0);
    printf("aglSetFullScreen -> %d (err %d)\n", (int)ok, (int)aglGetError());
    aglSetCurrentContext(ctx);
    printf("GL_RENDERER = %s\n", (const char *)glGetString(GL_RENDERER));
    fflush(stdout);

    /* Give the driver a moment (attach ran synchronously in aglSetFullScreen; this is just to be
     * sure any deferred setup settles).  NO rendering. */
    sleep(1);

    /* Clean release so the display is restored no matter what. */
    aglSetCurrentContext(NULL);
    aglSetDrawable(ctx, NULL);
    aglDestroyContext(ctx);
    aglDestroyPixelFormat(pf);
    printf("released fullscreen cleanly.  read ~/mach64gld.log\n");
    return 0;
}

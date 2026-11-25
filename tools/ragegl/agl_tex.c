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
#include <string.h>
#include <OpenGL/glu.h>

static unsigned char lum[512 * 512], la[512 * 512 * 2], rgba[512 * 512 * 4];
#define lum_global lum
int main(void)
{
    int ox = getenv("AGL_DX") ? atoi(getenv("AGL_DX")) : 0, oy = getenv("AGL_DY") ? atoi(getenv("AGL_DY")) : 0;
    int bx = getenv("AGL_MAIN") ? 0 : 1920;
    Rect bounds = { 60 + oy, bx + 80 + ox, 460 + oy, bx + 560 + ox };      /* on the Rage display (global x 1920..2560) */              /* top,left,bottom,right -> 480x400 */
    WindowRef win = NULL;
    OSStatus err = CreateNewWindow(kDocumentWindowClass,
                                   kWindowStandardDocumentAttributes, &bounds, &win);
    if (err || !win) { printf("CreateNewWindow err=%d\n", (int)err); return 1; }
    SetWindowTitleWithCFString(win, CFSTR("agl_tex"));
    ShowWindow(win);

    GLint attrs[] = { AGL_RGBA, AGL_DOUBLEBUFFER, AGL_ACCELERATED,
                      AGL_DEPTH_SIZE, 16, AGL_NONE };
    GDHandle gd = NULL; { CGDirectDisplayID ids[8]; CGDisplayCount nd = 0, k; CGGetActiveDisplayList(8, ids, &nd);
        for (k = 0; k < nd; k++) if ((int)CGDisplayBounds(ids[k]).origin.x == bx) DMGetGDeviceByDisplayID((DisplayIDType)ids[k], &gd, false); }
    printf("rage gdevice %p\n", (void *)gd);
    AGLPixelFormat pf = aglChoosePixelFormat(&gd, gd ? 1 : 0, attrs);
    { GLint rid = 0; aglDescribePixelFormat(pf, AGL_RENDERER_ID, &rid); printf("renderer id 0x%x\n", (unsigned)rid); }
    printf("aglChoosePixelFormat(accelerated) -> %p (err %d)\n", (void*)pf, (int)aglGetError());
    if (!pf) { printf("no ACCELERATED pixel format\n"); return 1; }

    AGLContext ctx = aglCreateContext(pf, NULL);
    printf("aglCreateContext -> %p (err %d)\n", (void*)ctx, (int)aglGetError());
    if (!ctx) return 1;

    int early = getenv("TEX_EARLY") != NULL;
    if (!early) {
    GLboolean ok = aglSetDrawable(ctx, GetWindowPort(win));
    printf("aglSetDrawable -> %d (err %d)\n", (int)ok, (int)aglGetError());
    aglSetCurrentContext(ctx);
    } else aglSetCurrentContext(ctx);

    glViewport(0, 0, 480, 400);
    glMatrixMode(GL_PROJECTION); glLoadIdentity();
    glOrtho(-1.0, 1.0, -1.0, 1.0, -1.0, 1.0);
    glMatrixMode(GL_MODELVIEW);  glLoadIdentity();
    glDisable(GL_DEPTH_TEST);
    {   int mode = getenv("TEX_MODE") ? atoi(getenv("TEX_MODE")) : 1, x, y, N = getenv("TEX_N") ? atoi(getenv("TEX_N")) : 64; GLuint id;  GLenum err;
        for (y = 0; y < N; y++) for (x = 0; x < N; x++) { float dx = (x - (N - 1) / 2.0f) / (N / 2.0f), dy = (y - (N - 1) / 2.0f) / (N / 2.0f), r = dx * dx + dy * dy, v = r < 1.0f ? (1.0f - r) * 255.0f : 0.0f; unsigned char b = (unsigned char)v;
            lum[y * N + x] = b; la[(y * N + x) * 2] = b; la[(y * N + x) * 2 + 1] = b; rgba[(y * N + x) * 4] = b; rgba[(y * N + x) * 4 + 1] = b / 2; rgba[(y * N + x) * 4 + 2] = 0; rgba[(y * N + x) * 4 + 3] = b; }
        if (getenv("TEX_MAGIC")) memset(lum, 0xA7, sizeof lum);
        glGenTextures(1, &id); glBindTexture(GL_TEXTURE_2D, id);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP); glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR); glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR_MIPMAP_LINEAR);
        glPixelStorei(GL_UNPACK_ALIGNMENT, 1);
        if (mode == 1) gluBuild2DMipmaps(GL_TEXTURE_2D, GL_LUMINANCE, N, N, GL_LUMINANCE, GL_UNSIGNED_BYTE, lum);
        else if (mode == 2) { glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR); glTexImage2D(GL_TEXTURE_2D, 0, GL_LUMINANCE, N, N, 0, GL_LUMINANCE, GL_UNSIGNED_BYTE, lum); }
        else if (mode == 3) gluBuild2DMipmaps(GL_TEXTURE_2D, GL_RGBA, N, N, GL_RGBA, GL_UNSIGNED_BYTE, rgba);
        else if (mode == 4) gluBuild2DMipmaps(GL_TEXTURE_2D, GL_LUMINANCE_ALPHA, N, N, GL_LUMINANCE_ALPHA, GL_UNSIGNED_BYTE, la);
        else if (mode == 5) { glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR); glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, N, N, 0, GL_RGBA, GL_UNSIGNED_BYTE, rgba); }
        err = glGetError(); printf("texture mode %d built, glGetError 0x%x\n", mode, (unsigned)err);
        glEnable(GL_TEXTURE_2D); glTexEnvf(GL_TEXTURE_ENV, GL_TEXTURE_ENV_MODE, GL_MODULATE);
    }
    if (early) { GLboolean ok = aglSetDrawable(ctx, GetWindowPort(win)); printf("late aglSetDrawable -> %d (err %d)\n", (int)ok, (int)aglGetError()); aglSetCurrentContext(ctx); glViewport(0, 0, 480, 400); glMatrixMode(GL_PROJECTION); glLoadIdentity(); glOrtho(-1.0, 1.0, -1.0, 1.0, -1.0, 1.0); glMatrixMode(GL_MODELVIEW); glLoadIdentity(); glEnable(GL_TEXTURE_2D);
        if (getenv("TEX_REBIND")) { glBindTexture(GL_TEXTURE_2D, 0); glBindTexture(GL_TEXTURE_2D, 1); }
        if (getenv("TEX_SUB")) { glTexSubImage2D(GL_TEXTURE_2D, 0, 0, 0, 8, 8, GL_LUMINANCE, GL_UNSIGNED_BYTE, lum_global); }
        if (getenv("TEX_REUP")) { gluBuild2DMipmaps(GL_TEXTURE_2D, GL_LUMINANCE, 256, 256, GL_LUMINANCE, GL_UNSIGNED_BYTE, lum_global); } }
    glEnable(GL_BLEND); glBlendFunc(GL_SRC_ALPHA, GL_ONE);
    int i, nfr = getenv("AGL_FRAMES") ? atoi(getenv("AGL_FRAMES")) : 30;
    for (i = 0; i < nfr; i++) {
        glClearColor(0.0f, 0.0f, 0.2f, 1.0f); glClear(GL_COLOR_BUFFER_BIT);
        glColor4f(1.0f, 1.0f, 0.5f, 1.0f);
        glBegin(GL_QUADS);
            glTexCoord2f(0, 0); glVertex2f(-0.6f, -0.6f); glTexCoord2f(1, 0); glVertex2f(0.6f, -0.6f);
            glTexCoord2f(1, 1); glVertex2f(0.6f, 0.6f);  glTexCoord2f(0, 1); glVertex2f(-0.6f, 0.6f);
        glEnd();
        if (i == nfr - 2) { int gx; unsigned char rowpx[480 * 4]; glReadPixels(0, 200, 480, 1, GL_RGBA, GL_UNSIGNED_BYTE, rowpx); printf("row y=200:"); for (gx = 60; gx < 480; gx += 40) printf(" %d:%02x%02x%02x", gx, rowpx[gx*4], rowpx[gx*4+1], rowpx[gx*4+2]); printf("\n"); }
        if (i == nfr - 1 && !getenv("AGL_NOSHOT")) system("./scanshot /tmp/agl_scan.ppm");
        aglSwapBuffers(ctx);
        usleep(16000);
    }
    aglSetCurrentContext(NULL); aglSetDrawable(ctx, NULL); aglDestroyContext(ctx); aglDestroyPixelFormat(pf); DisposeWindow(win);
    return 0;
}

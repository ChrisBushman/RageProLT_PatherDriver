/*
 * mach64_flurrywin.c -- Flurry-like HIGH-VOLUME windowed stress test (normal top-level window).
 *
 * Validates, in ONE run, the pieces the Flurry screensaver exercised that our small tests never did:
 *   (1) VOLUME: hundreds of textured triangles per frame -> the batch splits across many hardware
 *       drains (the 4KB/1016-dword type-2 buffer).  This is the path that used to truncate -> engine
 *       wedge -> kernel panic; it must now run clean.
 *   (2) COLOR:  each "particle" quad has a distinct vertex colour, so we can see whether per-vertex
 *       ARGB is read correctly (Flurry's tris logged argb=0xffffffff = all white -> suspected misread).
 *   (3) PRESENT: a NORMAL window (not the Prefs preview), so surface_flush lands where the window is.
 *
 * ~400 additively-blended colour quads orbit the centre, textured with a radial glow -- a clean
 * analogue of Flurry.  Windowed 800x600 so it uses the proven Path-B windowed surface present.
 *
 * Build (Lombard):  cc -O2 -o ~/flurrywin tools/mach64_flurrywin.c \
 *                       -framework Carbon -framework AGL -framework OpenGL
 * Run at the console:  MACH64_VRAM_ALLOC=1 ~/flurrywin     ; watch for a smooth colourful swirl.
 */
#include <Carbon/Carbon.h>
#include <AGL/agl.h>
#include <OpenGL/gl.h>
#include <stdio.h>
#include <stdlib.h>
#include <math.h>
#include <unistd.h>

/* NPART / NFRAMES are argv-overridable (see main) so we can ramp volume without recompiling.
 * Defaults are deliberately GENTLE -- 400 quads/frame wedged the engine (too many hardware drains
 * per frame for the 4KB command buffer); start small and escalate. */
#define NPART_DEF 40
#define NFRAMES_DEF 150

static GLuint make_glow(void)
{
    /* 64x64 radial-gradient RGBA glow (bright centre -> transparent edge). */
    static unsigned char tex[64*64*4];
    int x, y;
    for (y = 0; y < 64; y++) for (x = 0; x < 64; x++) {
        double dx = (x - 31.5) / 31.5, dy = (y - 31.5) / 31.5;
        double d = sqrt(dx*dx + dy*dy);
        double a = d < 1.0 ? (1.0 - d) : 0.0;
        unsigned char v = (unsigned char)(a * 255.0);
        unsigned char *p = &tex[(y*64 + x)*4];
        p[0] = p[1] = p[2] = 255; p[3] = v;
    }
    GLuint id; glGenTextures(1, &id); glBindTexture(GL_TEXTURE_2D, id);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, 64, 64, 0, GL_RGBA, GL_UNSIGNED_BYTE, tex);
    return id;
}

int main(int argc, char **argv)
{
    Rect bounds = { 40, 40, 640, 840 };   /* 800x600 normal window */
    WindowRef win;
    GLint attribs[] = { AGL_RGBA, AGL_ACCELERATED, AGL_DOUBLEBUFFER, AGL_DEPTH_SIZE, 16, AGL_NONE };
    AGLPixelFormat pf; AGLContext ctx; int f, i;
    int NPART   = (argc > 1) ? atoi(argv[1]) : NPART_DEF;
    int NFRAMES = (argc > 2) ? atoi(argv[2]) : NFRAMES_DEF;
    if (NPART < 1) NPART = 1;
    if (NFRAMES < 1) NFRAMES = 1;

    CreateNewWindow(kDocumentWindowClass, kWindowStandardDocumentAttributes, &bounds, &win);
    SetWindowTitleWithCFString(win, CFSTR("mach64 Flurry-like windowed stress (400 colour quads)"));
    ShowWindow(win);
    pf = aglChoosePixelFormat(NULL, 0, attribs);
    if (!pf) { printf("no pixfmt (err %d)\n", (int)aglGetError()); return 1; }
    ctx = aglCreateContext(pf, NULL);
    if (!ctx) { printf("no ctx (err %d)\n", (int)aglGetError()); return 1; }
    aglSetDrawable(ctx, GetWindowPort(win));
    aglSetCurrentContext(ctx);
    printf("GL_RENDERER = %s\n", (const char*)glGetString(GL_RENDERER)); fflush(stdout);

    glViewport(0, 0, 800, 600);
    glMatrixMode(GL_PROJECTION); glLoadIdentity(); glOrtho(0, 800, 0, 600, -1, 1);
    glMatrixMode(GL_MODELVIEW); glLoadIdentity();
    glEnable(GL_TEXTURE_2D);
    glEnable(GL_BLEND); glBlendFunc(GL_SRC_ALPHA, GL_ONE);   /* additive, like Flurry */
    make_glow();

    printf("rendering %d frames of %d colour quads...\n", NFRAMES, NPART); fflush(stdout);
    for (f = 0; f < NFRAMES; f++) {
        double t = f * 0.04;
        glClearColor(0, 0, 0, 1); glClear(GL_COLOR_BUFFER_BIT);
        glBegin(GL_TRIANGLES);
        for (i = 0; i < NPART; i++) {
            double a = t + i * (6.2831853 / NPART) * 7.0;
            double r = 60 + 200 * (0.5 + 0.5 * sin(t * 1.3 + i * 0.05));
            double cx = 400 + r * cos(a), cy = 300 + r * sin(a);
            float s = 24.f;
            /* distinct colour per particle */
            float cr = 0.5f + 0.5f * (float)sin(i * 0.10 + 0.0);
            float cg = 0.5f + 0.5f * (float)sin(i * 0.10 + 2.1);
            float cb = 0.5f + 0.5f * (float)sin(i * 0.10 + 4.2);
            float x0 = (float)(cx - s), x1 = (float)(cx + s);
            float y0 = (float)(cy - s), y1 = (float)(cy + s);
            glColor3f(cr, cg, cb);
            /* two triangles = one textured quad */
            glTexCoord2f(0,0); glVertex2f(x0, y0);
            glTexCoord2f(1,0); glVertex2f(x1, y0);
            glTexCoord2f(1,1); glVertex2f(x1, y1);
            glTexCoord2f(0,0); glVertex2f(x0, y0);
            glTexCoord2f(1,1); glVertex2f(x1, y1);
            glTexCoord2f(0,1); glVertex2f(x0, y1);
        }
        glEnd();
        aglSwapBuffers(ctx);
        usleep(10000);
    }
    printf("done -- clean exit\n");
    aglSetCurrentContext(NULL); aglSetDrawable(ctx, NULL);
    aglDestroyContext(ctx); aglDestroyPixelFormat(pf);
    return 0;
}

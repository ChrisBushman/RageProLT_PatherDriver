/* tools/ragegl/ycbcr_test.c - B10: what does a GL_APPLE_ycbcr_422 rectangle texture render as?  DVD Player's AltiVec driver uploads each video frame as
 * GL_TEXTURE_RECTANGLE_EXT, format GL_YCBCR_422_APPLE, type GL_UNSIGNED_SHORT_8_8_APPLE and draws a quad; the Rage showed a green screen with grey lines.
 * This draws 8 colour bars (BT.601 YCbCr) as such a texture into an off-screen drawable and prints the RGB at each bar's centre, for the Rage renderer and
 * (argument "soft") the generic software renderer, which is the reference.  Build: cc -o ycbcr_test ycbcr_test.c -framework ApplicationServices -framework OpenGL
 * Usage: ycbcr_test [soft] [rev]   (rev: GL_UNSIGNED_SHORT_8_8_REV_APPLE instead) */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ApplicationServices/ApplicationServices.h>
#include <OpenGL/OpenGL.h>
#include <OpenGL/gl.h>
#define TEX_RECT_ 0x84F5
static unsigned TEX_RECT = TEX_RECT_;   /* GL_TEXTURE_2D with the argument "2d" (texture sizes must then be powers of two: 64x16 is) */
#define YCBCR 0x85B9
#define S88 0x85BA
#define S88R 0x85BB
int main(int argc, char **argv)
{
    int soft = 0, rev = 0, a, rl = 0, cs = 0, sub = 0, twice = 0;
    for (a = 1; a < argc; a++) { if (!strcmp(argv[a], "soft")) soft = 1; if (!strcmp(argv[a], "rev")) rev = 1; if (!strcmp(argv[a], "2d")) TEX_RECT = GL_TEXTURE_2D; if (!strcmp(argv[a], "rl")) rl = 1; if (!strcmp(argv[a], "cs")) cs = 1; if (!strcmp(argv[a], "sub")) sub = 1; if (!strcmp(argv[a], "twice")) twice = 1; }
    CGDirectDisplayID ids[8]; CGDisplayCount n = 0, i; CGOpenGLDisplayMask mask = 0; CGLPixelFormatObj pf = 0; CGLContextObj ctx = 0; long npix = 0; CGLError e;
    CGGetActiveDisplayList(8, ids, &n);
    for (i = 0; i < n; i++) if (CGDisplayIsMain(ids[i])) mask = CGDisplayIDToOpenGLDisplayMask(ids[i]);           /* the Rage is the main display on this rig */
    if (soft) { CGLPixelFormatAttribute at[] = { kCGLPFAOffScreen, kCGLPFAColorSize, (CGLPixelFormatAttribute)32, kCGLPFARendererID, (CGLPixelFormatAttribute)0x00020200, (CGLPixelFormatAttribute)0 }; e = CGLChoosePixelFormat(at, &pf, &npix); }
    else { CGLPixelFormatAttribute at[] = { kCGLPFAOffScreen, kCGLPFAAccelerated, kCGLPFAColorSize, (CGLPixelFormatAttribute)32, kCGLPFADisplayMask, (CGLPixelFormatAttribute)mask, (CGLPixelFormatAttribute)0 }; e = CGLChoosePixelFormat(at, &pf, &npix); }
    if (e || !pf) { printf("CGLChoosePixelFormat error %d\n", (int)e); return 1; }
    if (CGLCreateContext(pf, NULL, &ctx) || !ctx) { printf("CGLCreateContext failed\n"); return 1; }
    int W = 128, H = 64, TW = 64, TH = 16; unsigned char *mem = calloc((size_t)W * H, 4), *rb = calloc((size_t)W * H, 4);
    if (CGLSetOffScreen(ctx, W, H, W * 4, mem)) { printf("CGLSetOffScreen failed\n"); return 1; }
    CGLSetCurrentContext(ctx);
    printf("mode: %s%s%s%s%s%s | GL_RENDERER: %s | extensions has ycbcr: %d rectangle: %d\n", soft ? "software reference" : "Rage", rev ? " (REV)" : "", TEX_RECT == GL_TEXTURE_2D ? " (2D)" : " (rect)", rl ? " rowlen" : "", cs ? " client-storage" : "", sub ? " sub" : "", glGetString(GL_RENDERER),
           strstr((const char *)glGetString(GL_EXTENSIONS), "GL_APPLE_ycbcr_422") != NULL, strstr((const char *)glGetString(GL_EXTENSIONS), "GL_EXT_texture_rectangle") != NULL);
    /* 8 bars, each TW/8 = 8 texels wide (4 macropixels).  Y,Cb,Cr from BT.601 (video range). */
    static const struct { const char *name; int Y, Cb, Cr; unsigned char r, g, b; } bar[8] = {
        { "white",   235, 128, 128, 255, 255, 255 }, { "yellow",  210,  16, 146, 255, 255,   0 }, { "cyan",    170, 166,  16,   0, 255, 255 }, { "green",   145,  54,  34,   0, 255,   0 },
        { "magenta", 106, 202, 222, 255,   0, 255 }, { "red",      81,  90, 240, 255,   0,   0 }, { "blue",     41, 240, 110,   0,   0, 255 }, { "black",    16, 128, 128,   0,   0,   0 } };
    int PT = rl ? TW + 16 : TW;      /* row pitch in texels: "rl" pads each row by 16 texels of 0xAB garbage and sets GL_UNPACK_ROW_LENGTH */
    unsigned char *tex = malloc((size_t)PT * TH * 2), *zero = calloc((size_t)PT * TH, 2); int x, y;
    memset(tex, 0xAB, (size_t)PT * TH * 2);
    for (y = 0; y < TH; y++) for (x = 0; x < TW; x += 2) { int b = x / (TW / 8); unsigned char *p = tex + ((size_t)y * PT + x) * 2;
        if (!rev) { p[0] = (unsigned char)bar[b].Y; p[1] = (unsigned char)bar[b].Cb; p[2] = (unsigned char)bar[b].Y; p[3] = (unsigned char)bar[b].Cr; }      /* 8_8_APPLE: Y0 Cb Y1 Cr (as DVD Player's decoder writes it) */
        else      { p[0] = (unsigned char)bar[b].Cb; p[1] = (unsigned char)bar[b].Y; p[2] = (unsigned char)bar[b].Cr; p[3] = (unsigned char)bar[b].Y; } }  /* 8_8_REV: Cb Y0 Cr Y1 */
    GLuint t; glGenTextures(1, &t); glBindTexture(TEX_RECT, t);
    glPixelStorei(GL_UNPACK_ALIGNMENT, 1);
    if (rl) glPixelStorei(GL_UNPACK_ROW_LENGTH, PT);
    if (cs) glPixelStorei(0x85B2 /* GL_UNPACK_CLIENT_STORAGE_APPLE */, 1);
    glTexParameteri(TEX_RECT, GL_TEXTURE_MIN_FILTER, GL_NEAREST); glTexParameteri(TEX_RECT, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
    glTexParameteri(TEX_RECT, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE); glTexParameteri(TEX_RECT, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    if (cs) glTexParameteri(TEX_RECT, 0x85BC /* TEXTURE_STORAGE_HINT_APPLE */, 0x85BE /* STORAGE_SHARED_APPLE */);
    if (sub) {      /* the AltiVec driver's sequence: create from an empty buffer, then glTexSubImage2D each frame */
        glTexImage2D(TEX_RECT, 0, GL_RGB, TW, TH, 0, YCBCR, rev ? S88R : S88, zero);
        glTexSubImage2D(TEX_RECT, 0, 0, 0, TW, TH, YCBCR, rev ? S88R : S88, tex);
    } else glTexImage2D(TEX_RECT, 0, GL_RGB, TW, TH, 0, YCBCR, rev ? S88R : S88, tex);
    printf("glGetError after upload: 0x%x\n", glGetError());
    glViewport(0, 0, W, H); glMatrixMode(GL_PROJECTION); glLoadIdentity(); glOrtho(0, W, 0, H, -1, 1); glMatrixMode(GL_MODELVIEW); glLoadIdentity();
    glClearColor(0.5f, 0.5f, 0.5f, 1.0f); glClear(GL_COLOR_BUFFER_BIT);
    glEnable(TEX_RECT); glTexEnvi(GL_TEXTURE_ENV, GL_TEXTURE_ENV_MODE, GL_REPLACE); glColor3f(1, 1, 1);
    glBegin(GL_QUADS); { float sx = TEX_RECT == GL_TEXTURE_2D ? 1.0f : (float)TW, sy = TEX_RECT == GL_TEXTURE_2D ? 1.0f : (float)TH; glTexCoord2f(0, 0); glVertex2f(0, 0); glTexCoord2f(sx, 0); glVertex2f(W, 0); glTexCoord2f(sx, sy); glVertex2f(W, H); glTexCoord2f(0, sy); glVertex2f(0, H); } glEnd();
    glFinish(); glReadPixels(0, 0, W, H, GL_RGBA, GL_UNSIGNED_BYTE, rb);
    printf("bar      expected RGB     got RGB (centre of bar)\n");
    for (a = 0; a < 8; a++) { const unsigned char *q = rb + ((size_t)(H / 2) * W + a * (W / 8) + W / 16) * 4;
        printf("%-8s %3d %3d %3d     %3d %3d %3d%s\n", bar[a].name, bar[a].r, bar[a].g, bar[a].b, q[0], q[1], q[2],
               (abs(q[0] - bar[a].r) < 24 && abs(q[1] - bar[a].g) < 24 && abs(q[2] - bar[a].b) < 24) ? "" : "   <-- differs"); }
    { const unsigned char *q = rb + ((size_t)(H / 2) * W + 3) * 4; printf("row sample x=3: %02x %02x %02x %02x\n", q[0], q[1], q[2], q[3]); }
    if (twice) {        /* the video driver's per-frame pattern: update the bound texture with glTexSubImage2D AFTER it has been drawn, draw again.  Bars rotate by one. */
        int ok = 0;
        for (y = 0; y < TH; y++) for (x = 0; x < TW; x += 2) { int b = (x / (TW / 8) + 1) % 8; unsigned char *p = tex + ((size_t)y * PT + x) * 2;
            if (!rev) { p[0] = (unsigned char)bar[b].Y; p[1] = (unsigned char)bar[b].Cb; p[2] = (unsigned char)bar[b].Y; p[3] = (unsigned char)bar[b].Cr; }
            else      { p[0] = (unsigned char)bar[b].Cb; p[1] = (unsigned char)bar[b].Y; p[2] = (unsigned char)bar[b].Cr; p[3] = (unsigned char)bar[b].Y; } }
        glTexSubImage2D(TEX_RECT, 0, 0, 0, TW, TH, YCBCR, rev ? S88R : S88, tex);
        glClear(GL_COLOR_BUFFER_BIT);
        { float sx = TEX_RECT == GL_TEXTURE_2D ? 1.0f : (float)TW, sy = TEX_RECT == GL_TEXTURE_2D ? 1.0f : (float)TH; glBegin(GL_QUADS); glTexCoord2f(0, 0); glVertex2f(0, 0); glTexCoord2f(sx, 0); glVertex2f(W, 0); glTexCoord2f(sx, sy); glVertex2f(W, H); glTexCoord2f(0, sy); glVertex2f(0, H); glEnd(); }
        glFinish(); glReadPixels(0, 0, W, H, GL_RGBA, GL_UNSIGNED_BYTE, rb);
        printf("second draw after glTexSubImage2D (bars rotated by one):\n");
        for (a = 0; a < 8; a++) { const unsigned char *q = rb + ((size_t)(H / 2) * W + a * (W / 8) + W / 16) * 4; int e = (a + 1) % 8; int good = abs(q[0] - bar[e].r) < 24 && abs(q[1] - bar[e].g) < 24 && abs(q[2] - bar[e].b) < 24; ok += good;
            printf("  bar %d expect %-8s %3d %3d %3d  got %3d %3d %3d%s\n", a, bar[e].name, bar[e].r, bar[e].g, bar[e].b, q[0], q[1], q[2], good ? "" : "   <-- stale/differs"); }
        printf("second draw: %d/8 correct\n", ok);
    }
    CGLSetCurrentContext(NULL); CGLDestroyContext(ctx); CGLDestroyPixelFormat(pf); return 0;
}

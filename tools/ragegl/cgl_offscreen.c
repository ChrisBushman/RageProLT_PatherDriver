/* tools/ragegl/cgl_offscreen.c - A6: a GL context on the Rage renderer that is NOT drawn into our scanout (CGL off-screen drawable in system memory).
 * Pixel format = off-screen + accelerated + the Rage display's mask, so CGL loads our bundle; clear and draw a triangle, read the pixels back through glReadPixels and from the
 * drawable's own memory, and check them.  Prints GL_RENDERER so a software fallback is obvious.  Run in the GUI session; wrapper checks the screen did not change.
 * Build: cc -o cgl_offscreen cgl_offscreen.c -framework ApplicationServices -framework OpenGL */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ApplicationServices/ApplicationServices.h>
#include <OpenGL/OpenGL.h>
#include <OpenGL/gl.h>
static int fails;
#define CHECK(name, cond) do { int ok_ = (cond); printf("%s %s\n", ok_ ? "PASS" : "FAIL", name); fflush(stdout); if (!ok_) fails++; } while (0)
static unsigned px(const unsigned char *p, int w, int x, int y) { const unsigned char *q = p + ((size_t)y * w + x) * 4; return (q[0] << 24) | (q[1] << 16) | (q[2] << 8) | q[3]; }
int main(int argc, char **argv)
{
    int soft = argc > 1 && !strcmp(argv[1], "soft");     /* soft: the generic software renderer, as the layout/byte-order REFERENCE for the Rage's flush */
    CGDirectDisplayID ids[8]; CGDisplayCount n = 0, i; CGOpenGLDisplayMask mask = 0; CGLPixelFormatObj pf = 0; CGLContextObj ctx = 0; long npix = 0;
    int W = 128, H = 96; unsigned char *mem, *rb; CGLError e;
    { int a; for (a = 1; a + 1 < argc; a++) if (!strcmp(argv[a], "size")) { W = atoi(argv[a + 1]); H = atoi(argv[a + 2]); } }       /* optional: size W H (default 128 96; the layout dump needs H >= 96) */
    CGGetActiveDisplayList(8, ids, &n);
    for (i = 0; i < n; i++) if ((int)CGDisplayBounds(ids[i]).size.width == 640 && (int)CGDisplayBounds(ids[i]).size.height == 480) mask = CGDisplayIDToOpenGLDisplayMask(ids[i]);
    if (!mask) { printf("no 640x480 Rage display\n"); return 2; }
    if (soft) { CGLPixelFormatAttribute at[] = { kCGLPFAOffScreen, kCGLPFAColorSize, (CGLPixelFormatAttribute)32, kCGLPFADepthSize, (CGLPixelFormatAttribute)16, kCGLPFARendererID, (CGLPixelFormatAttribute)0x00020200 /* kCGLRendererGenericID */, (CGLPixelFormatAttribute)0 };
      e = CGLChoosePixelFormat(at, &pf, &npix); } else
    { CGLPixelFormatAttribute at[] = { kCGLPFAOffScreen, kCGLPFAAccelerated, kCGLPFAColorSize, (CGLPixelFormatAttribute)32, kCGLPFADepthSize, (CGLPixelFormatAttribute)16, kCGLPFADisplayMask, (CGLPixelFormatAttribute)mask, (CGLPixelFormatAttribute)0 };
      e = CGLChoosePixelFormat(at, &pf, &npix); }
    CHECK("CGLChoosePixelFormat (off-screen, accelerated, Rage display mask)", e == 0 && pf);
    if (!pf) { printf("error %d: %s\n", (int)e, CGLErrorString(e)); return 1; }
    CHECK("CGLCreateContext", CGLCreateContext(pf, NULL, &ctx) == 0 && ctx);
    mem = calloc((size_t)W * H, 4); rb = calloc((size_t)W * H, 4);
    CHECK("CGLSetOffScreen (system-memory drawable)", CGLSetOffScreen(ctx, W, H, W * 4, mem) == 0);
    CGLSetCurrentContext(ctx);
    printf("GL_RENDERER: %s\nGL_VERSION: %s\n", glGetString(GL_RENDERER), glGetString(GL_VERSION));
    if (!soft) CHECK("the renderer is the Rage's hardware renderer (not the software one)", strstr((const char *)glGetString(GL_RENDERER), "Rage") != NULL || strstr((const char *)glGetString(GL_RENDERER), "Mach64") != NULL);
    glViewport(0, 0, W, H); glClearColor(1.0f, 0.0f, 0.0f, 1.0f); glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT); glFinish();
    glReadPixels(0, 0, W, H, GL_RGBA, GL_UNSIGNED_BYTE, rb);
    CHECK("clear to red: glReadPixels centre is red", (px(rb, W, W / 2, H / 2) >> 24) > 0xe0 && ((px(rb, W, W / 2, H / 2) >> 16) & 0xff) < 0x20);
    glMatrixMode(GL_PROJECTION); glLoadIdentity(); glOrtho(0, W, 0, H, -1, 1); glMatrixMode(GL_MODELVIEW); glLoadIdentity();
    glColor3f(0.0f, 1.0f, 0.0f); glBegin(GL_TRIANGLES); glVertex2f(10, 10); glVertex2f(118, 10); glVertex2f(64, 86); glEnd(); glFinish();
    glReadPixels(0, 0, W, H, GL_RGBA, GL_UNSIGNED_BYTE, rb);
    CHECK("triangle: glReadPixels inside is green", ((px(rb, W, 64, 30) >> 16) & 0xff) > 0xe0 && (px(rb, W, 64, 30) >> 24) < 0x20);
    CHECK("triangle: glReadPixels outside the triangle is still red", (px(rb, W, 4, 90) >> 24) > 0xe0);
    { int nz = 0; size_t k; CGLFlushDrawable(ctx); for (k = 0; k < (size_t)W * H * 4; k++) if (mem[k]) { nz = 1; break; }
      CHECK("after CGLFlushDrawable the drawable's own memory holds the frame (non-zero)", nz);
      if (nz) { unsigned a = px(mem, W, 64, 30), b = px(mem, W, 64, H - 1 - 30); printf("  memory pixels at (64,30): 0x%08x, flipped row: 0x%08x\n", a, b); } }
    { static const int rows[] = { 2, 8, 15, 30, 60, 80, 90, 94 }; unsigned k;      /* layout/byte-order dump: memory rows (row 0 = first row in memory); GL y = H-1-row if the memory is top-down */
      printf("memory layout (word = bytes b0 b1 b2 b3 in memory order); triangle apex is at GL y=86 (top), base at GL y=10:\n");
      for (k = 0; k < sizeof rows / sizeof rows[0]; k++) { const unsigned char *a = mem + ((size_t)rows[k] * W + 64) * 4, *b = mem + ((size_t)rows[k] * W + 20) * 4;
        printf("  row %2d  x=64: %02x %02x %02x %02x   x=20: %02x %02x %02x %02x\n", rows[k], a[0], a[1], a[2], a[3], b[0], b[1], b[2], b[3]); } }
    CGLSetCurrentContext(NULL); CGLDestroyContext(ctx); CGLDestroyPixelFormat(pf);
    printf("%s (%d failure%s)\n", fails ? "FAILED" : "ALL PASSED", fails, fails == 1 ? "" : "s");
    return fails != 0;
}

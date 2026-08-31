/*
 * test_osmesa.c - T1a proof: render a triangle off-screen with Mesa's OSMesa
 * (software) into a memory buffer and verify pixels. Proves Mesa core builds and
 * renders on Windows 2000 (no hardware, no ICD/window plumbing yet).
 */
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include "GL/gl.h"
#include "GL/osmesa.h"

extern int __cdecl sprintf(char *, const char *, ...);

static HANDLE o;
static void outs(const char *s) { DWORD n; WriteFile(o, s, lstrlenA(s), &n, 0); }

#define W 64
#define H 64
static unsigned char buf[W * H * 4];

int main(void)
{
    char m[256];
    o = GetStdHandle(STD_OUTPUT_HANDLE);

    OSMesaContext ctx = OSMesaCreateContext(OSMESA_RGBA, 0);
    if (!ctx) { outs("OSMesaCreateContext failed\n"); return 2; }
    if (!OSMesaMakeCurrent(ctx, buf, GL_UNSIGNED_BYTE, W, H)) { outs("OSMesaMakeCurrent failed\n"); return 3; }

    glClearColor(0, 0, 0, 0);
    glClear(GL_COLOR_BUFFER_BIT);
    glMatrixMode(GL_PROJECTION); glLoadIdentity(); glOrtho(0, W, 0, H, -1, 1);
    glMatrixMode(GL_MODELVIEW);  glLoadIdentity();
    glColor3f(1.0f, 0.5f, 0.25f);        /* ~ (255,128,64) */
    glBegin(GL_TRIANGLES);
        glVertex2f(8, 8); glVertex2f(56, 8); glVertex2f(8, 56);
    glEnd();
    glFinish();

    unsigned char *pin = &buf[(16 * W + 16) * 4];   /* inside triangle  */
    unsigned char *pcn = &buf[(60 * W + 60) * 4];   /* outside triangle */
    int i, match = 0, bg = 0;
    for (i = 0; i < W * H; i++) {
        unsigned char *p = &buf[i * 4];
        if (p[0] > 200 && p[1] > 90 && p[1] < 180 && p[2] < 110) match++;
        else if (p[0] == 0 && p[1] == 0 && p[2] == 0) bg++;
    }
    sprintf(m, "OSMesa: inside(16,16)=%02x%02x%02x  corner(60,60)=%02x%02x%02x  tri~=%d bg=%d\n",
            pin[0], pin[1], pin[2], pcn[0], pcn[1], pcn[2], match, bg);
    outs(m);

    OSMesaDestroyContext(ctx);
    int pass = (pin[0] > 200 && pcn[0] == 0 && pcn[1] == 0 && pcn[2] == 0 && match > 50 && bg > 50);
    outs(pass ? "RESULT: Mesa OSMesa software render works on Win2000!\n"
              : "RESULT: render mismatch\n");
    return pass ? 0 : 1;
}

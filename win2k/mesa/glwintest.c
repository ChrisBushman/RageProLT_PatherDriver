/*
 * glwintest.c - T1b proof: use our Mesa opengl32.dll via the WGL API to render a
 * triangle and read it back with glReadPixels. Linked against our opengl32 import
 * lib, so merely running proves the DLL loads on Win2000 with correct ABI; a correct
 * readback proves the whole WGL + GL software path works.
 *
 * Headless: renders through GetDC(NULL) (WMesa uses its own DIB backbuffer), so no
 * visible window is needed (works over the service SSH session). Pixel format is set
 * via Mesa's own wglChoosePixelFormat/wglSetPixelFormat (not gdi32's).
 */
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include "GL/gl.h"

extern int __cdecl sprintf(char *, const char *, ...);
static HANDLE o;
static void outs(const char *s) { DWORD n; WriteFile(o, s, lstrlenA(s), &n, 0); }

#define W 64
#define H 64
static unsigned char buf[W * H * 4];

void _mainstart(void)
{
    char m[256];
    o = GetStdHandle(STD_OUTPUT_HANDLE);
    outs("opengl32.dll loaded (imports resolved).\n");

    /* Deterministic headless target: a 64x64 DIB in a memory DC (WMesa takes its
     * size from GetDeviceCaps on the DC). Avoids GetDC(NULL) screen-size ambiguity. */
    HDC screen = GetDC(NULL);
    HDC hdc = CreateCompatibleDC(screen);
    ReleaseDC(NULL, screen);
    if (!hdc) { outs("CreateCompatibleDC failed\n"); ExitProcess(2); }
    BITMAPINFO bmi; ZeroMemory(&bmi, sizeof(bmi));
    bmi.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
    bmi.bmiHeader.biWidth = W; bmi.bmiHeader.biHeight = H;  /* bottom-up */
    bmi.bmiHeader.biPlanes = 1; bmi.bmiHeader.biBitCount = 32; bmi.bmiHeader.biCompression = BI_RGB;
    void *dibits = 0;
    HBITMAP dib = CreateDIBSection(hdc, &bmi, DIB_RGB_COLORS, &dibits, 0, 0);
    if (!dib) { outs("CreateDIBSection failed\n"); ExitProcess(2); }
    SelectObject(hdc, dib);

    PIXELFORMATDESCRIPTOR pfd;
    ZeroMemory(&pfd, sizeof(pfd));
    pfd.nSize = sizeof(pfd); pfd.nVersion = 1;
    pfd.dwFlags = PFD_DRAW_TO_WINDOW | PFD_SUPPORT_OPENGL;
    pfd.iPixelType = PFD_TYPE_RGBA; pfd.cColorBits = 24; pfd.cAlphaBits = 8;

    int pf = wglChoosePixelFormat(hdc, &pfd);
    if (!pf || !wglSetPixelFormat(hdc, pf, &pfd)) { outs("wgl pixel format failed\n"); ExitProcess(3); }

    HGLRC rc = wglCreateContext(hdc);
    if (!rc) { outs("wglCreateContext failed\n"); ExitProcess(4); }
    if (!wglMakeCurrent(hdc, rc)) { outs("wglMakeCurrent failed\n"); ExitProcess(5); }

    glViewport(0, 0, W, H);
    glClearColor(0, 0, 0, 0); glClear(GL_COLOR_BUFFER_BIT);
    glMatrixMode(GL_PROJECTION); glLoadIdentity(); glOrtho(0, W, 0, H, -1, 1);
    glMatrixMode(GL_MODELVIEW);  glLoadIdentity();
    glColor3f(1.0f, 0.5f, 0.25f);
    glBegin(GL_TRIANGLES);
        glVertex2f(8, 8); glVertex2f(56, 8); glVertex2f(8, 56);
    glEnd();
    glFinish();

    glReadPixels(0, 0, W, H, GL_RGBA, GL_UNSIGNED_BYTE, buf);

    unsigned char *pin = &buf[(16 * W + 16) * 4];
    unsigned char *pcn = &buf[(60 * W + 60) * 4];
    int i, match = 0, bg = 0;
    for (i = 0; i < W * H; i++) {
        unsigned char *p = &buf[i * 4];
        if (p[0] > 200 && p[1] > 90 && p[1] < 180 && p[2] < 110) match++;
        else if (p[0] == 0 && p[1] == 0 && p[2] == 0) bg++;
    }
    sprintf(m, "WGL: inside(16,16)=%02x%02x%02x  corner(60,60)=%02x%02x%02x  tri~=%d bg=%d\n",
            pin[0], pin[1], pin[2], pcn[0], pcn[1], pcn[2], match, bg);
    outs(m);

    wglMakeCurrent(NULL, NULL);
    wglDeleteContext(rc);
    int pass = (pin[0] > 200 && pcn[0] == 0 && pcn[1] == 0 && pcn[2] == 0 && match > 50 && bg > 50);
    outs(pass ? "RESULT: Mesa opengl32.dll WGL render works on Win2000!\n"
              : "RESULT: WGL context/DLL loaded, but render mismatch\n");
    ExitProcess(pass ? 0 : 1);
}

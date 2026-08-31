/*
 * wgltest.c - WGL smoke test for our drop-in opengl32.dll on Windows 2000.
 *
 * Proves the whole WGL scaffolding the ICD reuses: create a window, choose+set a
 * pixel format, wglCreateContext / wglMakeCurrent, render (clear + a triangle),
 * SwapBuffers, and verify with glReadPixels. We LoadLibrary("opengl32.dll") and
 * GetProcAddress every entry point so we bind to OUR DLL sitting next to the EXE
 * (app-dir search order) rather than the system stub. CRT-free.
 */
#define WIN32_LEAN_AND_MEAN
#include <windows.h>

void *memset(void *d, int c, size_t n){ unsigned char*a=d; while(n--)*a++=(unsigned char)c; return d; }
__declspec(dllimport) int WINAPIV wsprintfA(LPSTR, LPCSTR, ...);
static HANDLE o; static void outs(const char*s){DWORD n;WriteFile(o,s,lstrlenA(s),&n,0);}
static char bb[256];
#define OUTF(...) do{ wsprintfA(bb,__VA_ARGS__); outs(bb);}while(0)

/* GL/WGL function pointer types we need */
typedef HGLRC (WINAPI *PFNwglCreateContext)(HDC);
typedef BOOL  (WINAPI *PFNwglMakeCurrent)(HDC,HGLRC);
typedef BOOL  (WINAPI *PFNwglDeleteContext)(HGLRC);
typedef int   (WINAPI *PFNwglChoosePixelFormat)(HDC,const PIXELFORMATDESCRIPTOR*);
typedef BOOL  (WINAPI *PFNwglSetPixelFormat)(HDC,int,const PIXELFORMATDESCRIPTOR*);
typedef int   (WINAPI *PFNwglDescribePixelFormat)(HDC,int,UINT,LPPIXELFORMATDESCRIPTOR);
typedef BOOL  (WINAPI *PFNwglSwapBuffers)(HDC);
typedef void  (WINAPI *PFNglClearColor)(float,float,float,float);
typedef void  (WINAPI *PFNglClear)(unsigned);
typedef void  (WINAPI *PFNglReadPixels)(int,int,int,int,unsigned,unsigned,void*);
typedef void  (WINAPI *PFNglFinish)(void);
typedef void  (WINAPI *PFNglViewport)(int,int,int,int);
typedef void  (WINAPI *PFNglBegin)(unsigned);
typedef void  (WINAPI *PFNglEnd)(void);
typedef void  (WINAPI *PFNglColor3f)(float,float,float);
typedef void  (WINAPI *PFNglVertex2f)(float,float);

static PFNwglCreateContext       p_wglCreateContext;
static PFNwglMakeCurrent         p_wglMakeCurrent;
static PFNwglDeleteContext       p_wglDeleteContext;
static PFNwglChoosePixelFormat   p_wglChoosePixelFormat;
static PFNwglSetPixelFormat      p_wglSetPixelFormat;
static PFNwglDescribePixelFormat p_wglDescribePixelFormat;
static PFNwglSwapBuffers         p_wglSwapBuffers;
static PFNglClearColor  p_glClearColor;
static PFNglClear       p_glClear;
static PFNglReadPixels  p_glReadPixels;
static PFNglFinish      p_glFinish;
static PFNglViewport    p_glViewport;
static PFNglBegin       p_glBegin;
static PFNglEnd         p_glEnd;
static PFNglColor3f     p_glColor3f;
static PFNglVertex2f    p_glVertex2f;

#define GL_COLOR_BUFFER_BIT 0x00004000u
#define GL_RGB              0x1907u
#define GL_UNSIGNED_BYTE    0x1401u
#define GL_TRIANGLES        0x0004u

static HMODULE gl;
static void *need(const char *n){ void *p=(void*)GetProcAddress(gl,n); if(!p){ OUTF("missing export: %s\n", n); ExitProcess(4);} return p; }

#define W 64
#define H 64
static unsigned char px[W*H*3];

void _mainstart(void)
{
    o = GetStdHandle(STD_OUTPUT_HANDLE);

    gl = LoadLibraryA("opengl32.dll");
    if (!gl) { outs("LoadLibrary(opengl32.dll) failed\n"); ExitProcess(2); }

    p_wglCreateContext       = need("wglCreateContext");
    p_wglMakeCurrent         = need("wglMakeCurrent");
    p_wglDeleteContext       = need("wglDeleteContext");
    p_wglChoosePixelFormat   = need("wglChoosePixelFormat");
    p_wglSetPixelFormat      = need("wglSetPixelFormat");
    p_wglDescribePixelFormat = need("wglDescribePixelFormat");
    p_wglSwapBuffers         = need("wglSwapBuffers");
    p_glClearColor = need("glClearColor"); p_glClear = need("glClear");
    p_glReadPixels = need("glReadPixels"); p_glFinish = need("glFinish");
    p_glViewport = need("glViewport"); p_glBegin = need("glBegin");
    p_glEnd = need("glEnd"); p_glColor3f = need("glColor3f"); p_glVertex2f = need("glVertex2f");

    /* window (may be invisible under the service window station -- WMesa renders to a
     * DIB, so glReadPixels still verifies the pipeline). */
    WNDCLASSA wc; memset(&wc,0,sizeof(wc));
    wc.lpfnWndProc = DefWindowProcA; wc.hInstance = GetModuleHandleA(0);
    wc.lpszClassName = "wgltestcls"; wc.style = CS_OWNDC;
    RegisterClassA(&wc);
    HWND hwnd = CreateWindowExA(0,"wgltestcls","wgltest",WS_POPUP,0,0,W,H,0,0,wc.hInstance,0);
    if (!hwnd) { outs("CreateWindow failed\n"); ExitProcess(3); }
    HDC hdc = GetDC(hwnd);

    PIXELFORMATDESCRIPTOR pfd; memset(&pfd,0,sizeof(pfd));
    pfd.nSize=sizeof(pfd); pfd.nVersion=1;
    pfd.dwFlags=PFD_DRAW_TO_WINDOW|PFD_SUPPORT_OPENGL|PFD_DOUBLEBUFFER;
    pfd.iPixelType=PFD_TYPE_RGBA; pfd.cColorBits=24; pfd.cDepthBits=16; pfd.iLayerType=PFD_MAIN_PLANE;
    int fmt = p_wglChoosePixelFormat(hdc, &pfd);
    OUTF("wglChoosePixelFormat -> %d\n", fmt);
    if (fmt <= 0) { outs("no pixel format\n"); ExitProcess(5); }
    if (!p_wglSetPixelFormat(hdc, fmt, &pfd)) { outs("SetPixelFormat failed\n"); ExitProcess(6); }

    HGLRC rc = p_wglCreateContext(hdc);
    if (!rc) { outs("wglCreateContext failed\n"); ExitProcess(7); }
    if (!p_wglMakeCurrent(hdc, rc)) { outs("wglMakeCurrent failed\n"); ExitProcess(8); }

    p_glViewport(0,0,W,H);
    p_glClearColor(0.0f, 0.0f, 1.0f, 1.0f);   /* blue */
    p_glClear(GL_COLOR_BUFFER_BIT);
    /* a red triangle covering the centre */
    p_glColor3f(1.0f,0.0f,0.0f);
    p_glBegin(GL_TRIANGLES);
      p_glVertex2f(-0.8f,-0.8f); p_glVertex2f(0.8f,-0.8f); p_glVertex2f(0.0f,0.8f);
    p_glEnd();
    p_glFinish();

    p_glReadPixels(0,0,W,H,GL_RGB,GL_UNSIGNED_BYTE,px);
    p_wglSwapBuffers(hdc);

    /* verify: some blue (clear) at a corner, red (triangle) at the centre */
    #define R(x,y) px[((y)*W+(x))*3+0]
    #define G(x,y) px[((y)*W+(x))*3+1]
    #define B(x,y) px[((y)*W+(x))*3+2]
    unsigned blue=0, red=0, i;
    for (i=0;i<W*H;i++){ unsigned r=px[i*3],g=px[i*3+1],b=px[i*3+2];
        if (b>200 && r<60 && g<60) blue++; else if (r>200 && g<60 && b<60) red++; }
    unsigned cx=W/2, cy=H/2;
    OUTF("readback: center=(%u,%u,%u) corner=(%u,%u,%u) blue=%u red=%u\n",
         R(cx,cy),G(cx,cy),B(cx,cy), R(2,2),G(2,2),B(2,2), blue, red);
    int pass = (B(2,2)>200 && R(2,2)<60) && (R(cx,cy)>200 && B(cx,cy)<60) && blue>200 && red>100;
    outs(pass ? "RESULT: WGL pipeline works through our opengl32.dll (clear+triangle+readback)!\n"
              : "RESULT: wgl mismatch (see counts)\n");

    p_wglMakeCurrent(0,0);
    p_wglDeleteContext(rc);
    ExitProcess(pass?0:1);
}

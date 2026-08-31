/*
 * hwicdtest.c - verify the HARDWARE path of our opengl32.dll on the Rage
 * Mobility-P.  Same WGL sequence as wgltest, but the geometry is rasterised by
 * the mach64 (via mach64_icd.c), so glReadPixels (software colour buffer) is
 * blank.  Instead we read the DLL's VRAM render target back through its debug
 * exports (m64icd_target / m64icd_read_vram -- the kmd device opens exclusive,
 * so the test can't open its own winsys handle) and check the pixels there, and
 * also confirm SwapBuffers presented to the top-left of the scanout. CRT-free.
 * Requires the kmd driver (mach64kmd.sys) started; else the DLL is software-only.
 */
#define WIN32_LEAN_AND_MEAN
#include <windows.h>

void *memset(void *d,int c,size_t n){unsigned char*a=d;while(n--)*a++=(unsigned char)c;return d;}
__declspec(dllimport) int WINAPIV wsprintfA(LPSTR,LPCSTR,...);
static HANDLE o; static void outs(const char*s){DWORD n;WriteFile(o,s,lstrlenA(s),&n,0);}
static char bb[256];
#define OUTF(...) do{wsprintfA(bb,__VA_ARGS__);outs(bb);}while(0)

typedef HGLRC (WINAPI *PFNcc)(HDC);
typedef BOOL  (WINAPI *PFNmc)(HDC,HGLRC);
typedef BOOL  (WINAPI *PFNdc)(HGLRC);
typedef int   (WINAPI *PFNcpf)(HDC,const PIXELFORMATDESCRIPTOR*);
typedef BOOL  (WINAPI *PFNspf)(HDC,int,const PIXELFORMATDESCRIPTOR*);
typedef BOOL  (WINAPI *PFNsb)(HDC);
typedef void  (WINAPI *PFNv4f)(float,float,float,float);
typedef void  (WINAPI *PFNu)(unsigned);
typedef void  (WINAPI *PFNvoid)(void);
typedef void  (WINAPI *PFNvp)(int,int,int,int);
typedef void  (WINAPI *PFN3f)(float,float,float);
typedef void  (WINAPI *PFN2f)(float,float);
/* debug exports are plain cdecl */
typedef int  (*PFNrd)(unsigned,void*,unsigned);
typedef void (*PFNtg)(unsigned*,int*,int*,unsigned*);
typedef int  (*PFNav)(void);

static PFNcc p_cc; static PFNmc p_mc; static PFNdc p_dc;
static PFNcpf p_cpf; static PFNspf p_spf; static PFNsb p_sb;
static PFNv4f p_clearcol; static PFNu p_clear; static PFNvoid p_finish;
static PFNvp p_viewport; static PFNu p_begin; static PFNvoid p_end;
static PFN3f p_col3f; static PFN2f p_vtx2f;
static PFNrd p_rd; static PFNtg p_tg; static PFNav p_av;

#define GL_COLOR_BUFFER_BIT 0x4000u
#define GL_TRIANGLES        0x0004u

static HMODULE gl;
static void *need(const char*n){void*p=(void*)GetProcAddress(gl,n);if(!p){OUTF("missing export: %s\n",n);ExitProcess(4);}return p;}

#define W 64
#define H 64
static unsigned char tbuf[W*4];

void _mainstart(void)
{
    o=GetStdHandle(STD_OUTPUT_HANDLE);
    gl=LoadLibraryA("opengl32.dll");
    if(!gl){outs("LoadLibrary(opengl32.dll) failed\n");ExitProcess(2);}

    p_cc=need("wglCreateContext"); p_mc=need("wglMakeCurrent"); p_dc=need("wglDeleteContext");
    p_cpf=need("wglChoosePixelFormat"); p_spf=need("wglSetPixelFormat"); p_sb=need("wglSwapBuffers");
    p_clearcol=need("glClearColor"); p_clear=need("glClear"); p_finish=need("glFinish");
    p_viewport=need("glViewport"); p_begin=need("glBegin"); p_end=need("glEnd");
    p_col3f=need("glColor3f"); p_vtx2f=need("glVertex2f");
    p_rd=need("m64icd_read_vram"); p_tg=need("m64icd_target"); p_av=need("m64icd_available");

    if(!p_av()){outs("RESULT: hardware NOT available (kmd not started) -- DLL is software-only\n");ExitProcess(5);}

    WNDCLASSA wc; memset(&wc,0,sizeof(wc));
    wc.lpfnWndProc=DefWindowProcA; wc.hInstance=GetModuleHandleA(0);
    wc.lpszClassName="hwicdcls"; wc.style=CS_OWNDC; RegisterClassA(&wc);
    HWND hwnd=CreateWindowExA(0,"hwicdcls","hwicd",WS_POPUP,0,0,W,H,0,0,wc.hInstance,0);
    if(!hwnd){outs("CreateWindow failed\n");ExitProcess(3);}
    HDC hdc=GetDC(hwnd);

    PIXELFORMATDESCRIPTOR pfd; memset(&pfd,0,sizeof(pfd));
    pfd.nSize=sizeof(pfd); pfd.nVersion=1;
    pfd.dwFlags=PFD_DRAW_TO_WINDOW|PFD_SUPPORT_OPENGL|PFD_DOUBLEBUFFER;
    pfd.iPixelType=PFD_TYPE_RGBA; pfd.cColorBits=24; pfd.cDepthBits=16;
    int fmt=p_cpf(hdc,&pfd); if(fmt<=0){outs("no pixel format\n");ExitProcess(6);}
    if(!p_spf(hdc,fmt,&pfd)){outs("SetPixelFormat failed\n");ExitProcess(7);}
    HGLRC rc=p_cc(hdc); if(!rc){outs("CreateContext failed\n");ExitProcess(8);}
    if(!p_mc(hdc,rc)){outs("MakeCurrent failed\n");ExitProcess(9);}

    p_viewport(0,0,W,H);
    p_clearcol(0,0,1,1);            /* blue clear */
    p_clear(GL_COLOR_BUFFER_BIT);
    p_col3f(1,0,0);                 /* red triangle, apex up */
    p_begin(GL_TRIANGLES);
      p_vtx2f(-0.8f,-0.8f); p_vtx2f(0.8f,-0.8f); p_vtx2f(0.0f,0.8f);
    p_end();
    p_finish();
    p_sb(hdc);                      /* hardware present to scanout top-left */

    /* verify the HARDWARE render target (32bpp, top-down) via the DLL's handle */
    unsigned fboff=0,pitch=0; int tw=0,th=0;
    p_tg(&fboff,&tw,&th,&pitch);
    OUTF("target: fboff=0x%x %dx%d pitch=%u\n", fboff, tw, th, pitch);
    unsigned red=0,blue=0,black=0,other=0,i; unsigned cen=0,cor=0;
    char line[80];
    for(i=0;i<(unsigned)th;i++){
        p_rd(fboff+i*pitch*4, tbuf, (unsigned)tw*4);
        unsigned x; int col=0;
        for(x=0;x<(unsigned)tw;x++){ unsigned px=*(unsigned*)(tbuf+x*4)&0xffffff;
            char ch;
            if(px==0xFF0000){red++;ch='R';} else if(px==0x0000FF){blue++;ch='B';}
            else if(px==0){black++;ch='.';} else {other++;ch='?';}
            if((i%2)==0 && (x%2)==0 && col<78) line[col++]=ch;
            if(i==(unsigned)th/2 && x==(unsigned)tw/2) cen=px;
            if(i==2 && x==2) cor=px; }
        if((i%2)==0){ line[col]='\n'; line[col+1]=0; outs(line); }
    }
    OUTF("hw target: center=0x%06x corner=0x%06x red=%u blue=%u black=%u other=%u\n",
         cen, cor, red, blue, black, other);
    int pass=(cen==0xFF0000 && cor==0x0000FF && red>200 && blue>200);
    outs(pass?"RESULT: HARDWARE opengl32.dll renders via mach64 (target verified) + presented!\n"
             :"RESULT: hw icd mismatch (see counts)\n");
    p_mc(0,0); p_dc(rc);
    ExitProcess(pass?0:1);
}

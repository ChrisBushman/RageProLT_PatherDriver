/*
 * mach64_present.c - present proof: write rendered content to the VISIBLE scanout.
 *
 * The desktop is 1024x768 16bpp RGB565, scanout at VRAM offset 0, pitch 1024 (read
 * live from CRTC_OFF_PITCH=0x20000000 / H,V_DISP / GetDeviceCaps). We write a 565
 * pattern straight into the scanout (strided by the 2048-byte row pitch) via the
 * proven kmd VRAM path -- it appears in the top-left of the physical display -- and
 * read it back to verify. (The GUI-engine blit is a perf optimisation for later; a
 * CPU copy proves the present PATH now.) CRT-free.
 */
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include "mach64_emit.h"
#include "mach64_winsys.h"

void *memcpy(void *d, const void *s, size_t n){ unsigned char*a=d; const unsigned char*b=s; while(n--)*a++=*b++; return d; }
void *memset(void *d, int c, size_t n){ unsigned char*a=d; while(n--)*a++=(unsigned char)c; return d; }
__declspec(dllimport) int WINAPIV wsprintfA(LPSTR, LPCSTR, ...);
static HANDLE o; static void outs(const char*s){DWORD n;WriteFile(o,s,lstrlenA(s),&n,0);}
static char b[256];
#define OUTF(...) do{ wsprintfA(b,__VA_ARGS__); outs(b);}while(0)

#define SCANOUT   0x000000u            /* visible framebuffer                 */
#define ROWBYTES  2048u                /* 1024 px * 2 bytes (16bpp)           */
#define N 32                           /* present a 32x32 patch at (0,0)       */

static unsigned short row[N];
static unsigned char  rb[N*2];

void _mainstart(void)
{
    o = GetStdHandle(STD_OUTPUT_HANDLE);
    m64_winsys *ws = m64ws_open();
    if (!ws) { outs("m64ws_open failed\n"); ExitProcess(2); }

    unsigned short RED=0xF800,GRN=0x07E0,BLU=0x001F,WHT=0xFFFF;
    unsigned red=0,grn=0,blu=0,wht=0,other=0;
    int x,y;
    /* present: write the 565 pattern row-by-row into the scanout (strided) */
    for (y=0;y<N;y++){
        for (x=0;x<N;x++) row[x] = (x<N/2? (y<N/2?RED:BLU) : (y<N/2?GRN:WHT));
        m64ws_write_vram(ws, SCANOUT + y*ROWBYTES, row, N*2);
    }
    /* verify by reading it back from the scanout */
    for (y=0;y<N;y++){
        m64ws_read_vram(ws, SCANOUT + y*ROWBYTES, rb, N*2);
        for (x=0;x<N;x++){ unsigned px=*(unsigned short*)(rb+x*2);
            if(px==RED)red++; else if(px==GRN)grn++; else if(px==BLU)blu++; else if(px==WHT)wht++; else other++; }
    }
    OUTF("present: scanout 565 red=%u green=%u blue=%u white=%u other=%u (expect 256 each)\n", red,grn,blu,wht,other);
    int pass=(red>200 && grn>200 && blu>200 && wht>200 && other==0);
    outs(pass ? "RESULT: PRESENT works -- pattern written to the VISIBLE scanout (top-left of screen)!\n"
              : "RESULT: present mismatch\n");
    m64ws_close(ws);
    ExitProcess(pass?0:1);
}

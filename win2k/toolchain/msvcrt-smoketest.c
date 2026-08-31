#define WIN32_LEAN_AND_MEAN
#include <windows.h>
extern void * __cdecl malloc(size_t);
extern void   __cdecl free(void *);
extern double __cdecl sqrt(double);
extern double __cdecl sin(double);
extern int    __cdecl sprintf(char *, const char *, ...);
static HANDLE o; static void outs(const char*s){DWORD n;WriteFile(o,s,lstrlenA(s),&n,0);}
void _mainstart(void){
    o = GetStdHandle(STD_OUTPUT_HANDLE);
    void *p = malloc(1024);
    char b[160];
    sprintf(b, "msvcrt on win2k: malloc=%p  sqrt2*1000=%d  sin1*1000=%d\n",
            p, (int)(sqrt(2.0)*1000.0), (int)(sin(1.0)*1000.0));
    outs(b);
    free(p);
    outs(p ? "RESULT: msvcrt.dll callable on Win2000!\n" : "RESULT: malloc failed\n");
    ExitProcess(0);
}

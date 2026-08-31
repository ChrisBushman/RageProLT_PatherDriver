/*
 * crtdll-test.c - a msvcrt-linked test DLL proving full CRT startup on Win2000:
 * a global constructor runs, DllMain runs and heap-allocates, and an exported
 * function uses msvcrt malloc/sqrt/sprintf. Loaded by dllload.exe.
 */
#define WIN32_LEAN_AND_MEAN
#include <windows.h>

extern void * __cdecl malloc(size_t);
extern void   __cdecl free(void *);
extern double __cdecl sqrt(double);
extern int    __cdecl sprintf(char *, const char *, ...);

static int   g_ctor = 0;
static void  my_ctor(void) __attribute__((constructor));
static void  my_ctor(void) { g_ctor = 0xC0DE; }   /* must run before crt_selftest */

static void *g_attach_alloc = 0;

BOOL WINAPI DllMain(HINSTANCE h, DWORD reason, LPVOID res)
{
    (void)h; (void)res;
    if (reason == DLL_PROCESS_ATTACH)      g_attach_alloc = malloc(256);
    else if (reason == DLL_PROCESS_DETACH) { if (g_attach_alloc) free(g_attach_alloc); }
    return TRUE;
}

__declspec(dllexport) int crt_selftest(char *out, int outlen)
{
    void *p = malloc(1024);
    int ok_heap = (p != 0) && (g_attach_alloc != 0);
    int ok_ctor = (g_ctor == 0xC0DE);
    (void)outlen;
    sprintf(out, "ctor_ran=%s  heap=%s  DllMain_alloc=%p  sqrt2*1000=%d",
            ok_ctor ? "YES" : "no", ok_heap ? "YES" : "no",
            g_attach_alloc, (int)(sqrt(2.0) * 1000.0));
    if (p) free(p);
    return ok_heap && ok_ctor;
}

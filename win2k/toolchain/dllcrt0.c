/*
 * dllcrt0.c - minimal DLL C-runtime startup for Win2000 msvcrt-linked DLLs.
 *
 * The stock mingw dllcrt2.o pulls in the UCRT (api-ms-win-crt-*), which is absent on
 * Win2000. This is a tiny replacement entry point that: runs global constructors
 * (gcc __CTOR_LIST__), calls the DLL's DllMain, and runs destructors on unload.
 * Link it (with crtbegin.o/crtend.o for the .ctors brackets) instead of dllcrt2.o.
 * The C library itself comes from the target's msvcrt.dll (see make-msvcrt-lib.sh).
 *
 * Shared startup for every msvcrt-linked DLL we build (the Mesa ICD, etc.).
 */
#define WIN32_LEAN_AND_MEAN
#include <windows.h>

/* gcc-collected constructor/destructor lists, bracketed by crtbegin.o/crtend.o:
 * __CTOR_LIST__[0] = -1 (or count), [1..n] = ctors, [n+1] = 0. */
extern void (*__CTOR_LIST__[])(void);
extern void (*__DTOR_LIST__[])(void);

static void run_ctors(void)
{
    unsigned long i, n = (unsigned long)__CTOR_LIST__[0];
    if (n == (unsigned long)-1) { for (n = 0; __CTOR_LIST__[n + 1]; n++) ; }
    for (i = n; i >= 1; i--) __CTOR_LIST__[i]();   /* gcc calls ctors in reverse */
}

static void run_dtors(void)
{
    unsigned long i;
    for (i = 1; __DTOR_LIST__[i]; i++) __DTOR_LIST__[i]();
}

/* Default DllMain; a DLL may provide its own (this one is weak). */
BOOL WINAPI DllMain(HINSTANCE, DWORD, LPVOID) __attribute__((weak));
BOOL WINAPI DllMain(HINSTANCE h, DWORD reason, LPVOID res)
{
    (void)h; (void)reason; (void)res;
    return TRUE;
}

BOOL WINAPI DllMainCRTStartup(HINSTANCE h, DWORD reason, LPVOID res)
{
    BOOL r;
    if (reason == DLL_PROCESS_ATTACH) {
        run_ctors();
        r = DllMain(h, reason, res);
    } else if (reason == DLL_PROCESS_DETACH) {
        r = DllMain(h, reason, res);
        run_dtors();
    } else {
        r = DllMain(h, reason, res);
    }
    return r;
}

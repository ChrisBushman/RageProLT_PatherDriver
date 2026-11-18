/*
 * crt0win.cpp - WinMain C-runtime startup for the VQ-cube demo EXE on Win2000.
 *
 * The GUI-app counterpart to win2k/toolchain/crt0exe.c: it runs gcc's global
 * constructors, synthesizes the WinMain arguments the OS loader would normally
 * hand a real CRT, calls the demo's WinMain, and ExitProcess()es with its result.
 *
 * Compiled as C++ (not C) on purpose: the demo's WinMain (RProEx4.cpp) is a plain
 * C++ function, so its symbol carries g++'s C++ mangling. Declaring WinMain here
 * with the identical prototype makes our call resolve to that same mangled symbol
 * (a C file would emit the unmangled _WinMain@16 and fail to link).
 *
 * Link with:  -nostartfiles -Wl,-e,_winmainstart  + crtbegin.o/crtend.o (ctor
 * brackets) + libmsvcrt_win2k.a.
 */
#define WIN32_LEAN_AND_MEAN
#include <windows.h>

extern "C" void (*__CTOR_LIST__[])(void);

/* Same prototype as the demo's definition -> same C++ mangled symbol. */
int PASCAL WinMain(HINSTANCE hInstance, HINSTANCE hPrevInstance,
                   LPSTR lpCmdLine, int nCmdShow);

static void run_ctors(void)
{
    unsigned long i, n = (unsigned long)__CTOR_LIST__[0];
    if (n == (unsigned long)-1) { for (n = 0; __CTOR_LIST__[n + 1]; n++) ; }
    for (i = n; i >= 1; i--) __CTOR_LIST__[i]();
}

extern "C" void _winmainstart(void)
{
    HINSTANCE hInst = GetModuleHandleA(NULL);
    LPSTR     lpCmd = GetCommandLineA();
    int       ret;

    run_ctors();
    ret = WinMain(hInst, (HINSTANCE)NULL, lpCmd, SW_SHOWDEFAULT);
    ExitProcess((UINT)ret);
}

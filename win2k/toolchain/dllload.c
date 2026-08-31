/*
 * dllload.c - CRT-free loader that LoadLibrary's the msvcrt-linked test DLL and
 * calls its exported crt_selftest. Proves a full-CRT DLL loads + initializes on
 * Windows 2000. Build CRT-free (own entry); the DLL under test uses msvcrt.
 */
#define WIN32_LEAN_AND_MEAN
#include <windows.h>

__declspec(dllimport) int WINAPIV wsprintfA(LPSTR, LPCSTR, ...);

static HANDLE o;
static void outs(const char *s) { DWORD n; WriteFile(o, s, lstrlenA(s), &n, 0); }

typedef int (*selftest_t)(char *, int);

void _mainstart(void)
{
    char b[300];
    o = GetStdHandle(STD_OUTPUT_HANDLE);

    HMODULE m = LoadLibraryA("crtdll_test.dll");
    if (!m) { wsprintfA(b, "LoadLibrary(crtdll_test.dll) failed %lu\n", GetLastError()); outs(b); ExitProcess(2); }

    selftest_t f = (selftest_t)GetProcAddress(m, "crt_selftest");
    if (!f) { outs("GetProcAddress(crt_selftest) failed\n"); ExitProcess(3); }

    int r = f(b, sizeof(b));
    outs(b); outs("\n");
    outs(r ? "RESULT: full-CRT DLL works on Win2000 (ctors + heap + DllMain)!\n"
           : "RESULT: CRT DLL only partially working\n");
    FreeLibrary(m);
    ExitProcess(r ? 0 : 1);
}

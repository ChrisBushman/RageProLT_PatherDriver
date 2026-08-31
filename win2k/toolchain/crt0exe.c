/*
 * crt0exe.c - minimal EXE C-runtime startup for Win2000 msvcrt-linked test EXEs.
 * Runs gcc global constructors, calls main(), exits. The EXE counterpart to
 * dllcrt0.c. Link with crtbegin.o/crtend.o (ctor brackets) + the msvcrt import lib.
 * (Test harnesses only; the shipping ICD is a DLL and uses dllcrt0.c.)
 */
#define WIN32_LEAN_AND_MEAN
#include <windows.h>

extern void (*__CTOR_LIST__[])(void);
extern int main(void);

static void run_ctors(void)
{
    unsigned long i, n = (unsigned long)__CTOR_LIST__[0];
    if (n == (unsigned long)-1) { for (n = 0; __CTOR_LIST__[n + 1]; n++) ; }
    for (i = n; i >= 1; i--) __CTOR_LIST__[i]();
}

void _mainstart(void)
{
    run_ctors();
    ExitProcess((UINT)main());
}

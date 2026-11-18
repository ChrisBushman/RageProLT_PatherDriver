/*
 * vqcompat.c - extra Win2000-msvcrt shims specific to the VQ-cube demo, beyond the
 * shared win2k/toolchain/win2kcompat.c (which we are not allowed to modify here).
 *
 * g++ -O2 fuses adjacent sin(x)/cos(x) calls (geom.cpp's rotation matrices) into a
 * single sincosf()/sincos() call, which lives in MinGW's libmingwex, NOT in
 * Windows 2000's msvcrt.dll. Provide those two functions in terms of the plain
 * double sin/cos that msvcrt does export.
 */
extern double sin(double), cos(double);

void sincos(double x, double *s, double *c)  { *s = sin(x);        *c = cos(x); }
void sincosf(float  x, float  *s, float  *c) { *s = (float)sin(x); *c = (float)cos(x); }

/*
 * __mingw_sprintf: libstdc++'s <bits/os_defines.h> UNCONDITIONALLY forces
 * __USE_MINGW_ANSI_STDIO=1 in every C++ TU that pulls a libstdc++ header (our
 * files get it via <math.h> -> <cmath>). That redirect points sprintf() at
 * libmingwex's __mingw_sprintf, which is NOT in Win2000's msvcrt. exutil.cpp has
 * one such sprintf ("%2.2ffps"). Forward it to msvcrt.dll's own vsprintf (which
 * handles %f fine) so the demo imports only msvcrt.dll. This C file is immune to
 * the redirect because it includes no libstdc++ headers.
 */
#include <stdarg.h>
extern int vsprintf(char *, const char *, va_list);

int __mingw_sprintf(char *buf, const char *fmt, ...)
{
    va_list ap;
    int     n;
    va_start(ap, fmt);
    n = vsprintf(buf, fmt, ap);
    va_end(ap);
    return n;
}

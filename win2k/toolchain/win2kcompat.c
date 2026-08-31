/*
 * win2kcompat.c - shims for symbols the modern (UCRT) MinGW headers reference but
 * Windows 2000's msvcrt.dll doesn't provide. Link this into msvcrt-based binaries
 * (Mesa, the ICD) alongside libmsvcrt_win2k.a.
 *
 * Two gaps:
 *  1. float math variants (floorf/sqrtf/...) - old msvcrt has only the double forms.
 *  2. __acrt_iob_func - UCRT's stdin/out/err accessor; the UCRT <stdio.h> emits it
 *     for stderr/stdout. Map it onto msvcrt's __iob_func (array of 32-byte FILEs).
 *
 * We declare the msvcrt entry points ourselves (no UCRT headers here) to avoid
 * pulling more UCRT inlines.
 */

/* --- msvcrt double-precision math (from msvcrt.dll via libmsvcrt_win2k.a) --- */
extern double floor(double), ceil(double), sqrt(double), sin(double), cos(double),
              tan(double), fabs(double), pow(double, double), log(double), exp(double),
              atan2(double, double), asin(double), acos(double), atan(double);

float floorf(float x)          { return (float)floor(x); }
float ceilf (float x)          { return (float)ceil(x); }
float sqrtf (float x)          { return (float)sqrt(x); }
float sinf  (float x)          { return (float)sin(x); }
float cosf  (float x)          { return (float)cos(x); }
float tanf  (float x)          { return (float)tan(x); }
float fabsf (float x)          { return (float)fabs(x); }
float powf  (float x, float y) { return (float)pow(x, y); }
float logf  (float x)          { return (float)log(x); }
float expf  (float x)          { return (float)exp(x); }
float atan2f(float y, float x) { return (float)atan2(y, x); }
float asinf (float x)          { return (float)asin(x); }
float acosf (float x)          { return (float)acos(x); }
float atanf (float x)          { return (float)atan(x); }

/* --- UCRT stdio stream accessor -> msvcrt _iob[] --- */
/* msvcrt.dll exports _iob as DATA (the array of 32-byte FILE structs). Import it
 * as dllimport data and index into it. The UCRT <stdio.h> references the accessor
 * via its dllimport slot (__imp____acrt_iob_func), so provide that slot too. */
__declspec(dllimport) extern char _iob[];
#define MSVCRT_FILE_SIZE 32

void *__acrt_iob_func(unsigned index)
{
    return (void *)(_iob + (unsigned long)index * MSVCRT_FILE_SIZE);
}
/* satisfy the dllimport reference the UCRT headers emit at call sites */
void *__imp____acrt_iob_func __asm__("__imp____acrt_iob_func") = (void *)__acrt_iob_func;

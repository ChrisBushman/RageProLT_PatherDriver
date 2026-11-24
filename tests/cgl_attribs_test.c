/* host test: gcc -I. -o /tmp/cat tests/cgl_attribs_test.c && /tmp/cat */
#include <stdio.h>
#include "common/cgl_attribs.h"
static int fails;
#define T(name, cond) do { if (!(cond)) { printf("FAIL %s\n", name); fails++; } else printf("ok   %s\n", name); } while (0)
int main(void)
{
    int a1[] = { 5, 12, 16, 0 };                 /* DoubleBuffer, DepthSize 16 */
    int a2[] = { 13, 0, 5, 0 };                  /* StencilSize 0 (zero VALUE), then DoubleBuffer: old scan stopped at the 0 */
    int a3[] = { 12, 5, 73, 0 };                 /* DepthSize 5 (value equals the DoubleBuffer token): NOT double-buffered */
    int a4[] = { 73, 54, 0 };                    /* Accelerated, FullScreen: single buffer */
    int a5[] = { 55, 1, 56, 4, 5, 0 };           /* SampleBuffers 1, Samples 4, DoubleBuffer */
    int a6[] = { 7, 0, 8, 32, 11, 8, 14, 0, 5, 0 };
    int nt = 0;
    T("double buffer + depth", cgl_attribs_double_buffer(a1, 48, &nt) == 1 && nt == 2);
    T("zero value does not end the scan", cgl_attribs_double_buffer(a2, 48, &nt) == 1 && nt == 2);
    T("value word 5 is not a DoubleBuffer token", cgl_attribs_double_buffer(a3, 48, &nt) == 0 && nt == 2);
    T("no double buffer", cgl_attribs_double_buffer(a4, 48, &nt) == 0 && nt == 2);
    T("multisample then double buffer", cgl_attribs_double_buffer(a5, 48, &nt) == 1 && nt == 3);
    T("several zero-valued attributes then double buffer", cgl_attribs_double_buffer(a6, 48, &nt) == 1 && nt == 5);
    T("NULL list", cgl_attribs_double_buffer(0, 48, &nt) == 0 && nt == 0);
    T("max bound respected (a2 cut after 2 words)", cgl_attribs_double_buffer(a2, 2, &nt) == 0);
    printf("%s\n", fails ? "FAILED" : "all passed");
    return fails != 0;
}

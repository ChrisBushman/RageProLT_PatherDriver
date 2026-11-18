/* crtdbg.h -- minimal stub for native MinGW (gcc 3.4.5) on Win2000.
 * MinGW ships no <crtdbg.h>; the Knight demo's types.h pulls it in only for
 * MSVC's debug-CRT assert/report macros. In a Release (NDEBUG) build these are
 * no-ops in MSVC too, so stubbing them to nothing matches Release semantics.
 * Added for the native Win2000 rebuild -- CWB 2026-09.
 */
#ifndef _CRTDBG_H_MINGW_STUB
#define _CRTDBG_H_MINGW_STUB

#define _CRT_WARN   0
#define _CRT_ERROR  1
#define _CRT_ASSERT 2

#define _ASSERT(expr)    ((void)0)
#define _ASSERTE(expr)   ((void)0)
#define _RPT0(t,m)               ((void)0)
#define _RPT1(t,m,a)             ((void)0)
#define _RPT2(t,m,a,b)           ((void)0)
#define _RPT3(t,m,a,b,c)         ((void)0)
#define _RPT4(t,m,a,b,c,d)       ((void)0)
#define _RPTF0(t,m)              ((void)0)
#define _RPTF1(t,m,a)            ((void)0)

/* debug-CRT report-hook API (used only to suppress assert MessageBoxes) -> no-ops */
#define _CRTDBG_MODE_DEBUG  0x01
#define _CRTDBG_MODE_FILE   0x02
#define _CRTDBG_MODE_WNDW   0x04
#define _CrtSetReportHook(h)     ((void)0)
#define _CrtSetReportMode(t,m)   ((void)0)
#define _CrtOutputDebugString(s) ((void)0)

#endif /* _CRTDBG_H_MINGW_STUB */

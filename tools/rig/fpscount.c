/* fpscount.dylib: counts CGLFlushDrawable calls per second (frames) and prints them to stderr; load with DYLD_FORCE_FLAT_NAMESPACE=1 DYLD_INSERT_LIBRARIES=... */
#include <OpenGL/OpenGL.h>
#include <dlfcn.h>
#include <stdio.h>
#include <sys/time.h>
static CGLError (*real)(CGLContextObj);
static unsigned n; static double t0;
static double now(void) { struct timeval tv; gettimeofday(&tv, 0); return tv.tv_sec + tv.tv_usec * 1e-6; }
CGLError CGLFlushDrawable(CGLContextObj c)
{
    double t; CGLError r;
    if (!real) real = (CGLError (*)(CGLContextObj))dlsym(RTLD_NEXT, "CGLFlushDrawable");
    r = real ? real(c) : 0;
    n++; t = now(); if (!t0) t0 = t;
    if (t - t0 >= 1.0) { fprintf(stderr, "FPS %.1f\n", n / (t - t0)); n = 0; t0 = t; }
    return r;
}

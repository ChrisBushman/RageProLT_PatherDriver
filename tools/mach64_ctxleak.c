/* mach64_ctxleak.c - prove the gldDestroyContext unmap-leak fix.  Loops aglCreateContext /
 * aglDestroyContext N times; each gldCreateContext maps three VRAM-region apertures into the
 * process, and gldDestroyContext must now IOConnectUnmapMemory them.  Measures the process's
 * virtual size (mach task_info) before/after:
 *   FIXED   -> VM delta ~0 (each context's maps are released on destroy)
 *   LEAKING -> VM grows by (3 regions) per context -> hundreds of MB, may exhaust 32-bit VM
 * Also a teardown-regression check: the loop must COMPLETE without crashing.
 * Build: cc -framework Carbon -framework AGL -framework OpenGL */
#include <Carbon/Carbon.h>
#include <AGL/agl.h>
#include <OpenGL/gl.h>
#include <mach/mach.h>
#include <stdio.h>
#include <unistd.h>

#define N 100

static unsigned long vm_kb(void)
{
    struct task_basic_info info;
    mach_msg_type_number_t cnt = TASK_BASIC_INFO_COUNT;
    if (task_info(mach_task_self(), TASK_BASIC_INFO, (task_info_t)&info, &cnt) != KERN_SUCCESS)
        return 0;
    return (unsigned long)(info.virtual_size / 1024);
}

int main(void)
{
    GDHandle dev = GetMainDevice();
    GLint at[] = { AGL_RGBA, AGL_ACCELERATED, AGL_FULLSCREEN, AGL_NONE };
    AGLPixelFormat pf = aglChoosePixelFormat(&dev, 1, at);
    unsigned long v0, v1;
    int i, made = 0;

    if (!pf) { printf("no pf\n"); return 1; }

    /* one warm-up create/destroy so first-time framework allocations don't skew the delta */
    { AGLContext c = aglCreateContext(pf, NULL); if (c) aglDestroyContext(c); }

    v0 = vm_kb();
    for (i = 0; i < N; i++) {
        AGLContext c = aglCreateContext(pf, NULL);
        if (!c) { printf("aglCreateContext FAILED at i=%d (err=%d) -- VM likely exhausted\n",
                          i, (int)aglGetError()); break; }
        made++;
        aglDestroyContext(c);
    }
    v1 = vm_kb();

    printf("created+destroyed %d contexts\n", made);
    printf("VM virtual: before=%lu KB  after=%lu KB  delta=%ld KB  (%.1f KB/ctx)\n",
           v0, v1, (long)(v1 - v0), made ? (double)((long)(v1 - v0)) / made : 0.0);
    printf("VERDICT: %s\n",
           (made == N && (long)(v1 - v0) < 4096)
             ? "FIXED (VM stable across 100 contexts)"
             : "LEAK/regression -- VM grew or the loop aborted");
    aglDestroyPixelFormat(pf);
    return 0;
}

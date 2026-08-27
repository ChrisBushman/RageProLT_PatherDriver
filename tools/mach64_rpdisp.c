/* mach64_rpdisp.c - identify the glReadPixels dispatch function our HW renderer actually uses.
 *
 * libGL's _glReadPixels tail-calls a per-context function pointer at ctx+0x3b8 (reversed from
 * libGL.dylib): r12=[ctx+0x3b8]; func([ctx+0], x,y,w,h,format,type,pixels).  GLEngine installs one
 * of 3 candidate readback impls there by renderer config; our HW context lands on the no-op one.
 * This probe binds an ACCELERATED context on our driver, grabs the dispatch context via
 * CGLGetCurrentContext(), reads [ctx+0x3b8], and dladdr's it (+ scans the context object for other
 * GLEngine code pointers) so we get the exact function address/offset to disassemble.
 *
 * Pure userspace GL + dladdr -> safe (reads only; no kext calls).  Needs our driver active
 * (accelerated GL_RENDERER) -> run via rpdisp.sh (installs bundle+kext) in the console session.
 * Build: cc -O2 -o mach64_rpdisp mach64_rpdisp.c -framework Carbon -framework AGL -framework OpenGL
 */
#include <Carbon/Carbon.h>
#include <AGL/agl.h>
#include <OpenGL/gl.h>
#include <OpenGL/OpenGL.h>     /* CGLGetCurrentContext */
#include <dlfcn.h>
#include <stdio.h>

static void report(const char *label, void *p)
{
    Dl_info di;
    if (p && dladdr(p, &di))
        printf("  %-34s = %p  [%s +0x%lx] %s\n", label, p,
               strrchr(di.dli_fname, '/') ? strrchr(di.dli_fname, '/') + 1 : di.dli_fname,
               (unsigned long)((char *)p - (char *)di.dli_fbase),
               di.dli_sname ? di.dli_sname : "");
    else
        printf("  %-34s = %p  (dladdr: no match / not a mapped ptr)\n", label, p);
    fflush(stdout);
}

int main(void)
{
    Rect r; r.top=80; r.left=120; r.bottom=380; r.right=520;
    WindowRef win = NULL;
    if (CreateNewWindow(kDocumentWindowClass,
            kWindowStandardDocumentAttributes | kWindowStandardHandlerAttribute, &r, &win) != noErr || !win) {
        printf("CreateNewWindow FAILED\n"); return 1;
    }
    ShowWindow(win);

    GLint attrs[] = { AGL_RGBA, AGL_DOUBLEBUFFER, AGL_ACCELERATED, AGL_NO_RECOVERY, AGL_NONE };
    AGLPixelFormat pf = aglChoosePixelFormat(NULL, 0, attrs);
    if (!pf) { printf("no ACCELERATED pixel format (need our driver active)\n"); return 2; }
    AGLContext ac = aglCreateContext(pf, NULL);
    if (!ac) { printf("aglCreateContext failed %d\n", (int)aglGetError()); return 3; }
    aglSetDrawable(ac, GetWindowPort(win));
    aglSetCurrentContext(ac);
    printf("GL_RENDERER=%s\n", (const char *)glGetString(GL_RENDERER));
    glClearColor(0.2f,0.4f,0.7f,1); glClear(GL_COLOR_BUFFER_BIT); aglSwapBuffers(ac);  /* render */

    void *ctx = (void *)CGLGetCurrentContext();
    printf("CGLGetCurrentContext = %p\n", ctx);
    if (!ctx) { printf("no current CGL context\n"); return 4; }

    /* The reversed glReadPixels dispatch slot (validation front-end at GLEngine+0x2dc4c). */
    report("[ctx+0x3b8] glReadPixels slot", *(void **)((char *)ctx + 0x3b8));
    void *gc = *(void **)ctx;   /* [ctx+0] = the gc the readback fn indexes off */
    printf("  [ctx+0] (gc) = %p\n", gc);
    fflush(stdout);

    /* THE per-renderer readback function pointer: glReadPixels validation tail-calls [gc+0x78b0]
     * with context [gc+0x7898].  For our renderer this is the no-op.  Dump it + neighbours. */
    if (gc) {
        report("[gc+0x78b0] readback FN PTR", *(void **)((char *)gc + 0x78b0));
        printf("  [gc+0x7898] readback ctx = %p\n", *(void **)((char *)gc + 0x7898));
        printf("  [gc+0x789c] fb/attach    = %p\n", *(void **)((char *)gc + 0x789c));
        printf("  --- gc readback region [0x7890..0x78c0] code ptrs ---\n");
        { int o; for (o = 0x7890; o <= 0x78c0; o += 4) {
            void *p = *(void **)((char *)gc + o); Dl_info di;
            if (p && dladdr(p, &di) && strstr(di.dli_fname, "GLEngine"))
                printf("  gc+0x%x -> %p [GLEngine +0x%lx] %s\n", o, p,
                       (unsigned long)((char *)p - (char *)di.dli_fbase), di.dli_sname?di.dli_sname:"");
        } }
    }
    fflush(stdout);

    /* Scan the context object for GLEngine code pointers -> locate the dispatch table + confirm the
     * 0x3b8 slot (bounded to stay inside the object).  dladdr is safe on non-pointers. */
    printf("--- context code-pointer scan (offset : ptr [lib +off] sym) ---\n");
    { int off; int hits = 0;
      for (off = 0; off <= 0xa00; off += 4) {
          void *p = *(void **)((char *)ctx + off);
          Dl_info di;
          if (p && dladdr(p, &di) && strstr(di.dli_fname, "GLEngine")) {
              printf("  +0x%03x -> %p  [GLEngine +0x%lx] %s\n", off, p,
                     (unsigned long)((char *)p - (char *)di.dli_fbase),
                     di.dli_sname ? di.dli_sname : "");
              if (++hits > 80) { printf("  (>80 hits, stop)\n"); break; }
          }
      }
      printf("  (%d GLEngine code pointers in ctx[0..0xa00])\n", hits);
    }
    printf("=== done ===\n");
    return 0;
}

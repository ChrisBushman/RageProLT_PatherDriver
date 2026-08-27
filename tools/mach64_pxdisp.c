/* mach64_pxdisp.c - pin the GLEngine dispatch impls for the pixel WRITE/COPY ops, so we can hook
 * them like glReadPixels (docs/re-notes/drawpixels.md).  libGL tail-calls a per-context fn ptr:
 *   glReadPixels -> [ctx+0x3b8]   glDrawPixels -> [ctx+0x114]
 *   glCopyPixels -> [ctx+0xd4]    glBitmap     -> [ctx+0x1c]
 * dladdr each -> "GLEngine +0xOFFSET", the impl to disassemble to find the per-renderer hook slot
 * (readback's was gc+0x78b0 = our installed dispatch table[0x08]).  Also dumps the gc renderer-hook
 * table region so we can see which slots our renderer fills vs leaves no-op.
 *
 * Pure userspace GL + dladdr (reads only, no kext calls) -> safe.  Needs our driver active
 * (accelerated GL_RENDERER).  Build: cc -O2 -o mach64_pxdisp mach64_pxdisp.c -framework Carbon
 * -framework AGL -framework OpenGL      Run in the console session via pxdisp.sh.
 */
#include <Carbon/Carbon.h>
#include <AGL/agl.h>
#include <OpenGL/gl.h>
#include <OpenGL/OpenGL.h>
#include <dlfcn.h>
#include <stdio.h>

static void report(const char *label, void *p)
{
    Dl_info di;
    if (p && dladdr(p, &di))
        printf("  %-30s = %p  [%s +0x%lx] %s\n", label, p,
               strrchr(di.dli_fname, '/') ? strrchr(di.dli_fname, '/') + 1 : di.dli_fname,
               (unsigned long)((char *)p - (char *)di.dli_fbase),
               di.dli_sname ? di.dli_sname : "");
    else
        printf("  %-30s = %p  (no dladdr match)\n", label, p);
    fflush(stdout);
}

int main(void)
{
    Rect r; r.top=80; r.left=120; r.bottom=380; r.right=520;
    WindowRef win = NULL;
    GLint attrs[] = { AGL_RGBA, AGL_DOUBLEBUFFER, AGL_ACCELERATED, AGL_NO_RECOVERY, AGL_NONE };
    AGLPixelFormat pf;
    AGLContext ac;
    void *ctx, *gc;
    int off;

    if (CreateNewWindow(kDocumentWindowClass,
            kWindowStandardDocumentAttributes | kWindowStandardHandlerAttribute, &r, &win) != noErr || !win) {
        printf("CreateNewWindow FAILED\n"); return 1;
    }
    ShowWindow(win);
    pf = aglChoosePixelFormat(NULL, 0, attrs);
    if (!pf) { printf("no ACCELERATED pixel format (need our driver active)\n"); return 2; }
    ac = aglCreateContext(pf, NULL);
    if (!ac) { printf("aglCreateContext failed %d\n", (int)aglGetError()); return 3; }
    aglSetDrawable(ac, GetWindowPort(win));
    aglSetCurrentContext(ac);
    printf("GL_RENDERER=%s\n", (const char *)glGetString(GL_RENDERER));
    glClearColor(0.2f,0.4f,0.7f,1); glClear(GL_COLOR_BUFFER_BIT); aglSwapBuffers(ac);

    ctx = (void *)CGLGetCurrentContext();
    printf("CGLGetCurrentContext = %p\n", ctx);
    if (!ctx) { printf("no current CGL context\n"); return 4; }
    gc = *(void **)ctx;
    printf("  [ctx+0] (gc) = %p\n\n", gc);

    printf("--- per-context pixel-op dispatch pointers (GLEngine impls) ---\n");
    report("[ctx+0x3b8] glReadPixels", *(void **)((char *)ctx + 0x3b8));
    report("[ctx+0x114] glDrawPixels", *(void **)((char *)ctx + 0x114));
    report("[ctx+0x0d4] glCopyPixels", *(void **)((char *)ctx + 0xd4));
    report("[ctx+0x01c] glBitmap",     *(void **)((char *)ctx + 0x1c));

    /* the per-renderer hook table lives around gc+0x7890 (readback hook @ gc+0x78b0). Dump the
     * region so we can see which slots our renderer fills (GLEngine/our-bundle ptr) vs no-op. */
    printf("\n--- gc renderer-hook table region [0x7880..0x7900] ---\n");
    if (gc) for (off = 0x7880; off <= 0x7900; off += 4) {
        void *p = *(void **)((char *)gc + off); Dl_info di;
        if (p && dladdr(p, &di))
            printf("  gc+0x%x -> %p  [%s +0x%lx] %s\n", off, p,
                   strrchr(di.dli_fname,'/')?strrchr(di.dli_fname,'/')+1:di.dli_fname,
                   (unsigned long)((char *)p-(char *)di.dli_fbase), di.dli_sname?di.dli_sname:"");
        else if (p)
            printf("  gc+0x%x -> %p  (non-lib)\n", off, p);
    }
    printf("=== done ===\n");
    return 0;
}

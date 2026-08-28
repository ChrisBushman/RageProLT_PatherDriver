/* mach64_unpackprobe.c - find the GL_UNPACK_* state offsets in GLEngine's gc, the way the GL_PACK_*
 * offsets were found: set each UNPACK param to a distinctive sentinel via glPixelStorei, then scan
 * the gc pixel-store region (gc+0x6380..0x6400) for those values.  glDrawPixels/glBitmap need these
 * (currently they assume defaults).  Pure userspace reads -> safe; needs our accelerated renderer.
 * Build: cc -O2 -o mach64_unpackprobe mach64_unpackprobe.c -framework Carbon -framework AGL -framework OpenGL */
#include <Carbon/Carbon.h>
#include <AGL/agl.h>
#include <OpenGL/gl.h>
#include <OpenGL/OpenGL.h>
#include <stdio.h>

#ifndef GL_UNPACK_SWAP_BYTES
#define GL_UNPACK_SWAP_BYTES  0x0CF0
#define GL_UNPACK_LSB_FIRST   0x0CF1
#define GL_UNPACK_ROW_LENGTH  0x0CF2
#define GL_UNPACK_SKIP_ROWS   0x0CF3
#define GL_UNPACK_SKIP_PIXELS 0x0CF4
#define GL_UNPACK_ALIGNMENT   0x0CF5
#endif

int main(void)
{
    Rect r; WindowRef win = NULL;
    GLint attrs[] = { AGL_RGBA, AGL_DOUBLEBUFFER, AGL_ACCELERATED, AGL_NO_RECOVERY, AGL_NONE };
    AGLPixelFormat pf;
    AGLContext ac;
    void *ctx, *gc;
    int off;

    r.top=80; r.left=120; r.bottom=380; r.right=520;
    if (CreateNewWindow(kDocumentWindowClass, kWindowStandardDocumentAttributes|kWindowStandardHandlerAttribute,
                        &r, &win) != noErr || !win) { printf("no window\n"); return 1; }
    ShowWindow(win);
    pf = aglChoosePixelFormat(NULL, 0, attrs);
    if (!pf) { printf("no accel pf\n"); return 2; }
    ac = aglCreateContext(pf, NULL);
    aglSetDrawable(ac, GetWindowPort(win));
    aglSetCurrentContext(ac);
    printf("GL_RENDERER=%s\n", (const char *)glGetString(GL_RENDERER));
    glClearColor(0.2f,0.4f,0.7f,1); glClear(GL_COLOR_BUFFER_BIT); aglSwapBuffers(ac);

    ctx = (void *)CGLGetCurrentContext();
    gc  = ctx ? *(void **)ctx : 0;
    printf("ctx=%p gc=%p\n", ctx, gc);
    if (!gc) { printf("no gc\n"); return 3; }

    /* distinctive sentinels (ALIGNMENT must be 1/2/4/8) */
    glPixelStorei(GL_UNPACK_ALIGNMENT,   8);
    glPixelStorei(GL_UNPACK_ROW_LENGTH,  0x1234);
    glPixelStorei(GL_UNPACK_SKIP_PIXELS, 0x0056);
    glPixelStorei(GL_UNPACK_SKIP_ROWS,   0x0078);
    glPixelStorei(GL_UNPACK_SWAP_BYTES,  1);
    glPixelStorei(GL_UNPACK_LSB_FIRST,   1);

    printf("--- gc pixel-store region [0x6380..0x6400], sentinels flagged ---\n");
    for (off = 0x6380; off < 0x6400; off += 4) {
        unsigned v = *(unsigned *)((char *)gc + off);
        const char *tag = "";
        if (v == 0x1234) tag = " <== UNPACK_ROW_LENGTH";
        else if (v == 0x56) tag = " <== UNPACK_SKIP_PIXELS";
        else if (v == 0x78) tag = " <== UNPACK_SKIP_ROWS";
        else if (v == 8) tag = " <== UNPACK_ALIGNMENT (or PACK)";
        printf("  gc+0x%x = 0x%08x%s\n", off, v, tag);
    }
    printf("--- byte scan for SWAP_BYTES / LSB_FIRST (==1) in [0x6380..0x6400] ---\n");
    for (off = 0x6380; off < 0x6400; off++) {
        unsigned char b = *((unsigned char *)gc + off);
        if (b == 1) printf("  byte gc+0x%x = 1  (SWAP_BYTES / LSB_FIRST?)\n", off);
    }
    printf("=== done (known PACK: ROW_LENGTH@0x63c4 SKIP_ROWS@0x63cc SKIP_PIXELS@0x63d0 ALIGN@0x63d8 SWAP@0x63dc) ===\n");
    return 0;
}

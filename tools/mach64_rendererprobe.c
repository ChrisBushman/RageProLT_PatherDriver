/* mach64_rendererprobe.c - which GL renderer does GLEngine pick for each online display?  Creates an ACCELERATED pixel format bound to
 * each display's OpenGL display mask (no window, no drawing), makes the context current on nothing and prints the renderer string.
 * Must run inside the GUI session (WindowServer connection).  Output: stdout.
 * Build: cc -o mach64_rendererprobe mach64_rendererprobe.c -framework ApplicationServices -framework AGL -framework OpenGL */
#include <ApplicationServices/ApplicationServices.h>
#include <AGL/agl.h>
#include <OpenGL/gl.h>
#include <stdio.h>
int main(void)
{
    CGDirectDisplayID ids[8]; CGDisplayCount n = 0, i; CGGetOnlineDisplayList(8, ids, &n);
    for (i = 0; i < n; i++) {
        CGRect b = CGDisplayBounds(ids[i]);
        GLint at[] = { AGL_RGBA, AGL_ACCELERATED, AGL_NO_RECOVERY, AGL_DOUBLEBUFFER, AGL_DEPTH_SIZE, 16, AGL_NONE };
        CGOpenGLDisplayMask m = CGDisplayIDToOpenGLDisplayMask(ids[i]);
        GDHandle gd = NULL; AGLPixelFormat pf; AGLContext c;
        printf("display 0x%x bounds (%d,%d %dx%d) mask 0x%x: ", (unsigned)ids[i], (int)b.origin.x, (int)b.origin.y, (int)b.size.width, (int)b.size.height, (unsigned)m); fflush(stdout);
        if (DMGetGDeviceByDisplayID((DisplayIDType)ids[i], &gd, false) != noErr || !gd) { printf("no GDevice\n"); continue; }
        pf = aglChoosePixelFormat(&gd, 1, at);
        if (!pf) { printf("no accelerated pixel format (agl err %d)\n", (int)aglGetError()); continue; }
        c = aglCreateContext(pf, NULL);
        if (!c) { printf("context failed (agl err %d)\n", (int)aglGetError()); aglDestroyPixelFormat(pf); continue; }
        printf("pixel format+context ok, renderer id: ");
        { GLint rid = 0, vram = 0; aglDescribePixelFormat(pf, AGL_RENDERER_ID, &rid); aglDescribePixelFormat(pf, AGL_VIDEO_MEMORY, &vram); printf("0x%x vram %d MB\n", (int)rid, (int)(vram >> 20)); }
        aglSetCurrentContext(c);
        printf("    GL_RENDERER \"%s\"  GL_VERSION \"%s\"  GL_VENDOR \"%s\"\n", (const char *)glGetString(GL_RENDERER), (const char *)glGetString(GL_VERSION), (const char *)glGetString(GL_VENDOR)); fflush(stdout);
        aglSetCurrentContext(NULL);
        aglDestroyContext(c); aglDestroyPixelFormat(pf);
    }
    return 0;
}

/* cgl_probe: list the displays, and for each the CGL renderers that claim it (does CGL load our bundle for the Rage display?) */
#include <stdio.h>
#include <ApplicationServices/ApplicationServices.h>
#include <OpenGL/OpenGL.h>
#include <mach-o/dyld.h>
#include <string.h>
int main(void)
{
    CGDirectDisplayID ids[8]; CGDisplayCount n = 0, i;
    CGGetActiveDisplayList(8, ids, &n);
    for (i = 0; i < n; i++) {
        CGRect b = CGDisplayBounds(ids[i]); CGOpenGLDisplayMask mask = CGDisplayIDToOpenGLDisplayMask(ids[i]);
        CGLRendererInfoObj info; long nr = 0, j;
        printf("display %u: %dx%d at %d,%d mask 0x%x\n", (unsigned)ids[i], (int)b.size.width, (int)b.size.height, (int)b.origin.x, (int)b.origin.y, (unsigned)mask);
        if (CGLQueryRendererInfo(mask, &info, &nr)) { printf("  CGLQueryRendererInfo failed\n"); continue; }
        for (j = 0; j < nr; j++) {
            long id = 0, acc = 0, vram = 0, win = 0, full = 0;
            CGLDescribeRenderer(info, j, kCGLRPRendererID, &id); CGLDescribeRenderer(info, j, kCGLRPAccelerated, &acc);
            CGLDescribeRenderer(info, j, kCGLRPVideoMemory, &vram); CGLDescribeRenderer(info, j, kCGLRPWindow, &win); CGLDescribeRenderer(info, j, kCGLRPFullScreen, &full);
            printf("  renderer %ld: id 0x%lx accelerated %ld vram %ld window %ld fullscreen %ld\n", j, id, acc, vram, win, full);
        }
        CGLDestroyRendererInfo(info);
    }
    { uint32_t k; for (k = 0; k < _dyld_image_count(); k++) { const char *nm = _dyld_get_image_name(k); if (strstr(nm, "GL") || strstr(nm, "ATI")) printf("image: %s\n", nm); } }
    return 0;
}

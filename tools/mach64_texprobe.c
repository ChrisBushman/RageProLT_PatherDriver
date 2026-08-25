/*
 * mach64_texprobe.c - decode the a6 argument of gldCreateTextureLevel by
 * uploading textures of KNOWN, DELIBERATELY NON-SQUARE sizes and reading the
 * a6 our gld_log prints for each.  a6 was 0x7 for 4x4 and 0xf for 8x8 -- both
 * squares, so w+h-1 / 2*max-1 / mip-mask are indistinguishable.  These sizes
 * separate them:
 *   size  w+h-1  2w-1  2h-1  max/mip
 *   4x8   0xb    0x7   0xf   0xf
 *   8x4   0xb    0xf   0x7   0xf
 *   16x2  0x11   0x1f  0x3   0x1f
 * Each size gets its OWN texture object so there's exactly one
 * gldCreateTextureLevel per glTexImage2D; correlate by ORDER with this list.
 * Build: cc -framework Carbon -framework AGL -framework OpenGL
 */
#include <Carbon/Carbon.h>
#include <AGL/agl.h>
#include <OpenGL/gl.h>
#include <stdio.h>
#include <unistd.h>

static const struct { int w, h; } sizes[] = {
    { 4, 4 },    /* control: expect a6=0x7  */
    { 8, 8 },    /* control: expect a6=0xf  */
    { 4, 8 },
    { 8, 4 },
    { 16, 2 },
    { 2, 16 },
};
#define NSIZES (int)(sizeof(sizes)/sizeof(sizes[0]))

int main(void)
{
    GDHandle dev = GetMainDevice();
    GLint attribs[] = { AGL_RGBA, AGL_ACCELERATED, AGL_FULLSCREEN,
                        AGL_DEPTH_SIZE, 16, AGL_NONE };
    AGLPixelFormat pf = aglChoosePixelFormat(&dev, 1, attribs);
    if (!pf) { printf("no pf\n"); return 1; }
    AGLContext ctx = aglCreateContext(pf, NULL);
    if (!ctx) return 1;
    aglSetFullScreen(ctx, 0, 0, 0, 0);
    aglSetCurrentContext(ctx);
    printf("GL_RENDERER = %s\n", (const char *)glGetString(GL_RENDERER));

    {   static GLubyte px[16 * 16 * 4];   /* big enough for any size above */
        int i;
        printf("=== a6 probe: one glTexImage2D per size, correlate by ORDER ===\n");
        for (i = 0; i < NSIZES; i++) {
            GLuint tex = 0;
            glGenTextures(1, &tex);
            glBindTexture(GL_TEXTURE_2D, tex);
            glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
            glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
            glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, sizes[i].w, sizes[i].h, 0,
                         GL_RGBA, GL_UNSIGNED_BYTE, px);
            printf("  probe[%d] %dx%d  err=0x%x\n",
                   i, sizes[i].w, sizes[i].h, glGetError());
            fflush(stdout);
        }
    }
    glFlush(); glFinish();
    printf("done queries\n"); fflush(stdout);

    aglSetCurrentContext(NULL); aglSetDrawable(ctx, NULL);
    aglDestroyContext(ctx); aglDestroyPixelFormat(pf);
    printf("done\n");
    return 0;
}

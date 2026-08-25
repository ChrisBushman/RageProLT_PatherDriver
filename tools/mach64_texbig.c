/*
 * mach64_texbig.c - large textures uploaded via chunked host-data blits.  Each
 * texture exceeds one command buffer (MACH64_BLIT_MAX=768 texels) and is split
 * into several row-band blits by the driver; a smooth 2D gradient makes any
 * band seam / misplaced chunk show as a visible horizontal discontinuity.
 *
 * Gradient texel(x,y): R = x-ramp (black->red L->R), G = y-ramp (black->green
 * bottom->top), B = 0.  Corners: black / red / green / yellow.
 *   Q0 64x64  (4096 texels -> ~6 chunks, incl. a partial last chunk)
 *   Q1 128x128 (16384 texels -> ~128 chunks)
 * PASS: both smooth gradients, no horizontal seams or repeated/shifted bands.
 * Build: cc -framework Carbon -framework AGL -framework OpenGL
 */
#include <Carbon/Carbon.h>
#include <AGL/agl.h>
#include <OpenGL/gl.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>

static GLuint grad_tex(int n)
{
    GLubyte *px = (GLubyte *)malloc((size_t)n*n*3);
    int x, y; GLuint t = 0;
    for (y=0;y<n;y++) for (x=0;x<n;x++){
        GLubyte *d = &px[(y*n+x)*3];
        d[0]=(GLubyte)(x*255/(n-1)); d[1]=(GLubyte)(y*255/(n-1)); d[2]=0;
    }
    glGenTextures(1,&t); glBindTexture(GL_TEXTURE_2D,t);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGB, n, n, 0, GL_RGB, GL_UNSIGNED_BYTE, px);
    free(px);
    return t;
}

static void quad(GLuint t, int slot)
{
    float x0 = -0.9f + (float)slot * 0.95f, x1 = x0 + 0.8f;
    GLfloat v[12] = { x0,-0.7f, x1,-0.7f, x1,0.7f,  x0,-0.7f, x1,0.7f, x0,0.7f };
    GLfloat tc[12]= { 0,0, 1,0, 1,1,  0,0, 1,1, 0,1 };
    glBindTexture(GL_TEXTURE_2D, t);
    glVertexPointer(2, GL_FLOAT, 0, v);
    glTexCoordPointer(2, GL_FLOAT, 0, tc);
    glDrawArrays(GL_TRIANGLES, 0, 6);
    glFlush(); glFinish();
}

int main(void)
{
    GDHandle dev = GetMainDevice();
    GLint attribs[] = { AGL_RGBA, AGL_ACCELERATED, AGL_FULLSCREEN,
                        AGL_DEPTH_SIZE, 16, AGL_NONE };
    AGLPixelFormat pf = aglChoosePixelFormat(&dev, 1, attribs);
    AGLContext ctx;
    GLuint a, b;
    if (!pf) { printf("no pf\n"); return 1; }
    ctx = aglCreateContext(pf, NULL);
    if (!ctx) return 1;
    aglSetFullScreen(ctx, 0, 0, 0, 0);
    aglSetCurrentContext(ctx);
    printf("GL_RENDERER=%s\n", (const char *)glGetString(GL_RENDERER));
    glViewport(0, 0, 1024, 768);
    glPixelStorei(GL_UNPACK_ALIGNMENT, 1);
    a = grad_tex(64);
    b = grad_tex(128);
    glTexEnvi(GL_TEXTURE_ENV, GL_TEXTURE_ENV_MODE, GL_REPLACE);
    glEnable(GL_TEXTURE_2D);
    glEnableClientState(GL_VERTEX_ARRAY);
    glEnableClientState(GL_TEXTURE_COORD_ARRAY);
    glClearColor(0.f, 0.f, 0.f, 1.f);
    glClear(GL_COLOR_BUFFER_BIT);
    printf("Q0 64x64 gradient, Q1 128x128 gradient (chunked)\n");
    quad(a, 0);
    quad(b, 1);
    printf("drew\n"); fflush(stdout);
    sleep(5);
    aglSetCurrentContext(NULL); aglSetDrawable(ctx, NULL);
    aglDestroyContext(ctx); aglDestroyPixelFormat(pf);
    printf("done\n");
    return 0;
}

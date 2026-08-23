/*
 * mach64_gltest.c - trigger OpenGL.framework renderer discovery and see if it
 * picks OUR driver.  Requests an ACCELERATED CGL pixel format, which makes the
 * framework enumerate hardware renderers -> for ATIRagePro's accelerator it
 * loads /System/Library/Extensions/ATIRageProGLDriver.bundle (our bundle, once
 * installed) and calls our gld* entry points.
 *
 * Definitive signal is /tmp/mach64gld.log (written by our bundle); GL_RENDERER
 * is a bonus.  Build: -framework OpenGL.
 */
#include <OpenGL/OpenGL.h>
#include <OpenGL/gl.h>
#include <stdio.h>

int main(int argc, char **argv)
{
    /* default: request ACCELERATED (our driver).  'sw' arg: drop it -> the
     * framework uses a software renderer (a known-good config-node source we can
     * capture under gdb to learn the exact node format). */
    int sw = (argc > 1 && argv[1][0]=='s');
    CGLPixelFormatAttribute accel_attribs[] = {
        kCGLPFAAccelerated,
        kCGLPFAColorSize, (CGLPixelFormatAttribute)32,
        kCGLPFADepthSize, (CGLPixelFormatAttribute)16,
        (CGLPixelFormatAttribute)0
    };
    CGLPixelFormatAttribute sw_attribs[] = {
        kCGLPFAColorSize, (CGLPixelFormatAttribute)32,
        kCGLPFADepthSize, (CGLPixelFormatAttribute)16,
        (CGLPixelFormatAttribute)0
    };
    CGLPixelFormatAttribute *attribs = sw ? sw_attribs : accel_attribs;
    CGLPixelFormatObj pix = 0; GLint npix = 0;
    CGLContextObj ctx = 0;
    CGLError e;

    e = CGLChoosePixelFormat(attribs, &pix, &npix);
    printf("CGLChoosePixelFormat(accelerated) -> err=%d npix=%d pix=%p\n",
           (int)e, (int)npix, (void*)pix);
    if (e || !pix) {
        printf("=> no accelerated pixel format (framework did not accept a hw renderer)\n");
        return 1;
    }

    e = CGLCreateContext(pix, 0, &ctx);
    printf("CGLCreateContext -> err=%d ctx=%p\n", (int)e, (void*)ctx);
    if (e || !ctx) { CGLDestroyPixelFormat(pix); return 1; }

    CGLSetCurrentContext(ctx);
    printf("GL_VENDOR   = %s\n", (const char*)glGetString(GL_VENDOR));
    printf("GL_RENDERER = %s\n", (const char*)glGetString(GL_RENDERER));
    printf("GL_VERSION  = %s\n", (const char*)glGetString(GL_VERSION));

    /* a trivial draw (no drawable attached -> may no-op, but exercises the path) */
    glClearColor(0.f, 0.f, 0.f, 1.f);
    glClear(GL_COLOR_BUFFER_BIT);
    glBegin(GL_TRIANGLES);
        glColor3f(1,0,0); glVertex2f(-0.5f,-0.5f);
        glColor3f(0,1,0); glVertex2f( 0.5f,-0.5f);
        glColor3f(0,0,1); glVertex2f( 0.0f, 0.5f);
    glEnd();
    glFinish();

    CGLSetCurrentContext(0);
    CGLDestroyContext(ctx);
    CGLDestroyPixelFormat(pix);
    printf("done\n");
    return 0;
}

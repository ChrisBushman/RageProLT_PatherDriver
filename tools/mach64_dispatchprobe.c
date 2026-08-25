/*
 * mach64_dispatchprobe.c - drive every GL primitive type with a UNIQUE vertex
 * count so the driver's per-slot dispatch probe (built with -DGLD_DISPATCH_PROBE)
 * logs "DISP 0x<slot>: r4=.. r5=.. r6=.." for each.  Matching the unique count to
 * the logged args recovers (a) which table slot GLEngine calls per GL primitive
 * and (b) which arg register carries the vertex count (the fan crash implied it
 * varies per slot).  Nothing renders -- the probe stubs only log.
 * Build: cc -framework Carbon -framework AGL -framework OpenGL
 */
#include <Carbon/Carbon.h>
#include <AGL/agl.h>
#include <OpenGL/gl.h>
#include <stdio.h>
#include <unistd.h>

int main(void)
{
    GDHandle dev = GetMainDevice();
    GLint attribs[] = { AGL_RGBA, AGL_ACCELERATED, AGL_FULLSCREEN,
                        AGL_DEPTH_SIZE, 16, AGL_NONE };
    AGLPixelFormat pf = aglChoosePixelFormat(&dev, 1, attribs);
    AGLContext ctx;
    static GLfloat v[16*2];
    int i;
    struct { GLenum mode; const char *name; int n; } prim[] = {
        { GL_POINTS,         "POINTS",         1 },
        { GL_LINES,          "LINES",          2 },
        { GL_LINE_STRIP,     "LINE_STRIP",     5 },
        { GL_LINE_LOOP,      "LINE_LOOP",      7 },
        { GL_TRIANGLES,      "TRIANGLES",      3 },
        { GL_TRIANGLE_STRIP, "TRIANGLE_STRIP", 11 },
        { GL_TRIANGLE_FAN,   "TRIANGLE_FAN",   13 },
        { GL_QUADS,          "QUADS",          8 },
        { GL_QUAD_STRIP,     "QUAD_STRIP",     6 },
        { GL_POLYGON,        "POLYGON",        9 },
    };
    if (!pf) { printf("no pf\n"); return 1; }
    ctx = aglCreateContext(pf, NULL); if (!ctx) return 1;
    aglSetFullScreen(ctx, 0, 0, 0, 0); aglSetCurrentContext(ctx);
    printf("GL_RENDERER=%s\n", (const char *)glGetString(GL_RENDERER));
    glViewport(0, 0, 1024, 768);
    for (i = 0; i < 16; i++) { v[i*2] = -0.5f + 0.06f*i; v[i*2+1] = (i&1)?0.3f:-0.3f; }
    glEnableClientState(GL_VERTEX_ARRAY);
    glVertexPointer(2, GL_FLOAT, 0, v);
    glClearColor(0.f,0.f,0.f,1.f); glClear(GL_COLOR_BUFFER_BIT);

    for (i = 0; i < 10; i++) {
        printf(">>> %s n=%d\n", prim[i].name, prim[i].n); fflush(stdout);
        glDrawArrays(prim[i].mode, 0, prim[i].n);
        glFlush(); glFinish();
    }
    printf("done drawing all primitives\n"); fflush(stdout);
    sleep(2);
    aglSetCurrentContext(NULL); aglSetDrawable(ctx, NULL);
    aglDestroyContext(ctx); aglDestroyPixelFormat(pf);
    printf("done\n");
    return 0;
}

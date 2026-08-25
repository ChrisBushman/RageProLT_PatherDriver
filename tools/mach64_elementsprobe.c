/*
 * mach64_elementsprobe.c - drive glDrawElements for every GL primitive with a
 * UNIQUE index count, against the GLD_DISPATCH_PROBE driver, to discover the
 * INDEXED dispatch path.  The key question: does GLEngine flatten indexed draws
 * back into the same primitive slots (0x18..0x3c) as DrawArrays -- in which case
 * indexed already works -- or route them to a separate element vector (0x40+)
 * with the index buffer handed to us?
 *
 * Indices are REVERSED (15,14,..0) so a flattened path produces a distinct order,
 * and the index-buffer address is printed so it can be matched against the probe's
 * logged r4..r8 pointers (same process => comparable addresses): if a logged
 * pointer == idx, GLEngine passed us the indices; if it's a GLEngine-internal
 * buffer (like DrawArrays' 0x23a2xx), GLEngine pre-gathered/transformed the verts.
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
    static GLfloat  v[16*2];
    static GLushort idx[16];
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
    for (i = 0; i < 16; i++) { v[i*2] = -0.5f + 0.06f*i; v[i*2+1] = (i&1)?0.3f:-0.3f;
                               idx[i] = (GLushort)(15 - i); }
    printf("VBUF=%p IDXBUF=%p type=GL_UNSIGNED_SHORT reversed(15..0)\n",
           (void *)v, (void *)idx);
    glEnableClientState(GL_VERTEX_ARRAY);
    glVertexPointer(2, GL_FLOAT, 0, v);
    glClearColor(0.f,0.f,0.f,1.f); glClear(GL_COLOR_BUFFER_BIT);

    for (i = 0; i < 10; i++) {
        printf(">>> DrawElements %s n=%d\n", prim[i].name, prim[i].n); fflush(stdout);
        glDrawElements(prim[i].mode, prim[i].n, GL_UNSIGNED_SHORT, idx);
        glFlush(); glFinish();
    }
    printf("done drawing all indexed primitives\n"); fflush(stdout);
    sleep(2);
    aglSetCurrentContext(NULL); aglSetDrawable(ctx, NULL);
    aglDestroyContext(ctx); aglDestroyPixelFormat(pf);
    printf("done\n");
    return 0;
}

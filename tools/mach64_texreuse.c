/*
 * mach64_texreuse.c - validate that deleting a texture RECLAIMS its VRAM
 * region (the free-list allocator in gld_texture.c), by two paths:
 *
 *   Phase 1 (top-of-heap reset): create+draw texA, delete it, create+draw texB.
 *     texB should upload to the SAME VRAM offset texA had (freeing the only /
 *     topmost region lowers the bump pointer back).
 *
 *   Phase 2 (free-list reuse): with texB still live, create+draw texP then texQ
 *     (texQ is on top), delete texP (now a hole below the top), then create+draw
 *     texR.  texR should reuse texP's offset out of the FREE LIST (best fit),
 *     NOT climb past texQ.
 *
 * THE SIGNAL IS THE DRIVER LOG, not the screen: each upload prints
 *   "mach64 texture upload: 8x8 ... -> VRAM 0x<off> (key 0x<k>)".
 * Correlate the offsets with the phase markers this test prints to stdout:
 *   PASS  Phase 1 if texB's off == texA's off.
 *   PASS  Phase 2 if texR's off == texP's off (and < texQ's off).
 *   LEAK  if texB/texR climb to a fresh higher offset instead of reusing.
 * The distinct colours are a secondary check: reuse must not corrupt texels, so
 * each quad must still show its OWN colour after the reuse.
 * Build: cc -framework Carbon -framework AGL -framework OpenGL
 */
#include <Carbon/Carbon.h>
#include <AGL/agl.h>
#include <OpenGL/gl.h>
#include <stdio.h>
#include <unistd.h>

static GLuint make_tex(GLubyte r, GLubyte g, GLubyte b)
{
    static GLubyte px[8*8*3];
    GLuint t = 0; int i;
    for (i = 0; i < 8*8; i++) { px[i*3]=r; px[i*3+1]=g; px[i*3+2]=b; }
    glGenTextures(1, &t);
    glBindTexture(GL_TEXTURE_2D, t);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGB, 8, 8, 0, GL_RGB, GL_UNSIGNED_BYTE, px);
    return t;
}

static void quad(float x0, float x1)
{
    GLfloat v[12] = { x0,-0.4f, x1,-0.4f, x1,0.4f,  x0,-0.4f, x1,0.4f, x0,0.4f };
    GLfloat tc[12]= { 0,0, 1,0, 1,1,  0,0, 1,1, 0,1 };
    glVertexPointer(2, GL_FLOAT, 0, v);
    glTexCoordPointer(2, GL_FLOAT, 0, tc);
    glDrawArrays(GL_TRIANGLES, 0, 6);
}

/* Bind t, draw it at slot i, force it through the pipe so the upload happens
 * NOW (mach64_bind_texture drains the pending-upload queue at draw time). */
static void bind_draw(const char *tag, GLuint t, int slot)
{
    float x0 = -0.95f + (float)slot * 0.38f;
    printf(">> %s: bind GL tex %u, draw slot %d\n", tag, t, slot); fflush(stdout);
    glBindTexture(GL_TEXTURE_2D, t);
    quad(x0, x0 + 0.30f);
    glFlush(); glFinish();
}

int main(void)
{
    GDHandle dev = GetMainDevice();
    GLint attribs[] = { AGL_RGBA, AGL_ACCELERATED, AGL_FULLSCREEN,
                        AGL_DEPTH_SIZE, 16, AGL_NONE };
    AGLPixelFormat pf = aglChoosePixelFormat(&dev, 1, attribs);
    AGLContext ctx;
    GLuint texA, texB, texP, texQ, texR;
    if (!pf) { printf("no pf\n"); return 1; }
    ctx = aglCreateContext(pf, NULL);
    if (!ctx) return 1;
    aglSetFullScreen(ctx, 0, 0, 0, 0);
    aglSetCurrentContext(ctx);
    printf("GL_RENDERER=%s\n", (const char *)glGetString(GL_RENDERER));

    glViewport(0, 0, 1024, 768);
    glTexEnvi(GL_TEXTURE_ENV, GL_TEXTURE_ENV_MODE, GL_MODULATE);
    glColor3f(1.f, 1.f, 1.f);
    glEnable(GL_TEXTURE_2D);
    glEnableClientState(GL_VERTEX_ARRAY);
    glEnableClientState(GL_TEXTURE_COORD_ARRAY);
    glClearColor(0.f, 0.f, 0.f, 1.f);
    glClear(GL_COLOR_BUFFER_BIT);

    /* ---- Phase 1: top-of-heap reset ---------------------------------- */
    printf("== Phase 1: delete-then-recreate should REUSE the same offset ==\n");
    texA = make_tex(255, 128, 0);        /* orange */
    bind_draw("texA(orange)", texA, 0);
    printf(">> delete texA (its region should return to the heap)\n"); fflush(stdout);
    glDeleteTextures(1, &texA);
    texB = make_tex(0, 200, 255);        /* cyan */
    bind_draw("texB(cyan)", texB, 1);
    printf("   -> expect texB offset == texA offset (top-of-heap reclaim)\n\n");

    /* ---- Phase 2: free-list hole reuse ------------------------------- */
    printf("== Phase 2: delete a NON-top region, recreate should fill the hole ==\n");
    texP = make_tex(255, 0, 0);          /* red   (allocated above live texB) */
    bind_draw("texP(red)", texP, 2);
    texQ = make_tex(0, 255, 0);          /* green (on top of texP)            */
    bind_draw("texQ(green)", texQ, 3);
    printf(">> delete texP (a hole BELOW texQ -> goes to the free list)\n"); fflush(stdout);
    glDeleteTextures(1, &texP);
    texR = make_tex(80, 80, 255);        /* blue */
    bind_draw("texR(blue)", texR, 4);
    printf("   -> expect texR offset == texP offset (< texQ offset), from free list\n\n");

    printf("all 5 quads drawn: orange-hole/cyan/red/green/blue across the screen\n");
    fflush(stdout);
    sleep(5);

    glDeleteTextures(1, &texB);
    glDeleteTextures(1, &texQ);
    glDeleteTextures(1, &texR);
    aglSetCurrentContext(NULL); aglSetDrawable(ctx, NULL);
    aglDestroyContext(ctx); aglDestroyPixelFormat(pf);
    printf("done\n");
    return 0;
}

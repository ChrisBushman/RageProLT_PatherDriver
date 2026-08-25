/*
 * mach64_texmulti.c - two DISTINCT textures, bound + drawn separately, to
 * validate the texture VRAM allocator (each texture in its own region) AND to
 * reveal how GLEngine signals texture binding to the driver.
 *   texA = solid ORANGE (255,128,0),  texB = solid CYAN (0,200,255)
 *   LEFT quad  drawn with texA bound,  RIGHT quad drawn with texB bound.
 * Expected if the allocator + bind tracking work: left orange, right cyan.
 * If both quads show the SAME colour, binding an already-uploaded texture is not
 * reaching the driver (need to track the bound texture via the gli instead).
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
    GLfloat v[12] = { x0,-0.6f, x1,-0.6f, x1,0.6f,  x0,-0.6f, x1,0.6f, x0,0.6f };
    GLfloat tc[12]= { 0,0, 1,0, 1,1,  0,0, 1,1, 0,1 };
    glVertexPointer(2, GL_FLOAT, 0, v);
    glTexCoordPointer(2, GL_FLOAT, 0, tc);
    glDrawArrays(GL_TRIANGLES, 0, 6);
}

int main(void)
{
    GDHandle dev = GetMainDevice();
    GLint attribs[] = { AGL_RGBA, AGL_ACCELERATED, AGL_FULLSCREEN,
                        AGL_DEPTH_SIZE, 16, AGL_NONE };
    AGLPixelFormat pf = aglChoosePixelFormat(&dev, 1, attribs);
    AGLContext ctx;
    GLuint texA, texB;
    if (!pf) { printf("no pf\n"); return 1; }
    ctx = aglCreateContext(pf, NULL);
    if (!ctx) return 1;
    aglSetFullScreen(ctx, 0, 0, 0, 0);
    aglSetCurrentContext(ctx);
    printf("GL_RENDERER=%s\n", (const char *)glGetString(GL_RENDERER));

    glViewport(0, 0, 1024, 768);
    texA = make_tex(255, 128, 0);      /* orange */
    texB = make_tex(0,   200, 255);    /* cyan   */
    glTexEnvi(GL_TEXTURE_ENV, GL_TEXTURE_ENV_MODE, GL_MODULATE);
    glColor3f(1.f, 1.f, 1.f);
    glEnable(GL_TEXTURE_2D);
    glEnableClientState(GL_VERTEX_ARRAY);
    glEnableClientState(GL_TEXTURE_COORD_ARRAY);

    glClearColor(0.f, 0.f, 0.f, 1.f);
    glClear(GL_COLOR_BUFFER_BIT);

    /* glFinish BETWEEN the draws: if GLEngine batches/defers, this forces draw A
     * to dispatch with texA bound before texB is bound -- so the two draws really
     * have different bindings at draw time. */
    glBindTexture(GL_TEXTURE_2D, texA);  quad(-0.9f, -0.1f);  glFlush(); glFinish();
    glBindTexture(GL_TEXTURE_2D, texB);  quad( 0.1f,  0.9f);  glFlush(); glFinish();

    glFlush(); glFinish();
    printf("drew texA(left) + texB(right)\n"); fflush(stdout);
    sleep(4);
    aglSetCurrentContext(NULL); aglSetDrawable(ctx, NULL);
    aglDestroyContext(ctx); aglDestroyPixelFormat(pf);
    printf("done\n");
    return 0;
}

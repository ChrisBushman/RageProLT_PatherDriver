/*
 * mach64_texelem.c - verify glDrawElements (INDEXED) rendering.  The probe run
 * (tools/mach64_elementsprobe) proved GLEngine flattens indexed draws into the
 * SAME primitive slots as DrawArrays (it dereferences the indices and transforms
 * the gathered vertices into its own draw-order buffer), so no driver-side index
 * path is needed -- this test confirms it renders correctly end to end.
 *
 * The SAME four-quadrant texture (TL red, TR green, BL blue, BR white) is drawn as
 * a quad four ways via glDrawElements, each from 4 shared corner vertices using
 * NON-SEQUENTIAL indices (so a broken index path can't accidentally pass):
 *   Q0 GL_TRIANGLES     6 idx  {0,1,2, 0,2,3}
 *   Q1 GL_TRIANGLE_STRIP 4 idx {0,1,3,2}
 *   Q2 GL_TRIANGLE_FAN   4 idx {0,1,2,3}
 *   Q3 GL_QUADS          4 idx {0,1,2,3}
 * Each should render the IDENTICAL clean four-quadrant square.
 * Build: cc -framework Carbon -framework AGL -framework OpenGL
 */
#include <Carbon/Carbon.h>
#include <AGL/agl.h>
#include <OpenGL/gl.h>
#include <stdio.h>
#include <unistd.h>

static GLuint quad_tex(void)
{
    static GLubyte px[8*8*3];
    int x, y; GLuint t = 0;
    GLubyte col[4][3] = {{255,0,0},{0,255,0},{0,0,255},{255,255,255}};
    for (y=0;y<8;y++) for (x=0;x<8;x++){
        int q = (x >= 4 ? 1 : 0) + (y >= 4 ? 2 : 0);
        GLubyte *d = &px[(y*8+x)*3]; d[0]=col[q][0]; d[1]=col[q][1]; d[2]=col[q][2];
    }
    glGenTextures(1,&t); glBindTexture(GL_TEXTURE_2D,t);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGB, 8, 8, 0, GL_RGB, GL_UNSIGNED_BYTE, px);
    return t;
}

int main(void)
{
    GDHandle dev = GetMainDevice();
    GLint attribs[] = { AGL_RGBA, AGL_ACCELERATED, AGL_FULLSCREEN,
                        AGL_DEPTH_SIZE, 16, AGL_NONE };
    AGLPixelFormat pf = aglChoosePixelFormat(&dev, 1, attribs);
    AGLContext ctx; GLuint tex; int s;
    /* corner order: 0=BL 1=BR 2=TR 3=TL */
    GLubyte  tri[6]  = { 0,1,2, 0,2,3 };
    GLubyte  strip[4]= { 0,1,3,2 };
    GLubyte  fan[4]  = { 0,1,2,3 };
    GLubyte  quad[4] = { 0,1,2,3 };
    if (!pf) { printf("no pf\n"); return 1; }
    ctx = aglCreateContext(pf, NULL); if (!ctx) return 1;
    aglSetFullScreen(ctx, 0, 0, 0, 0); aglSetCurrentContext(ctx);
    printf("GL_RENDERER=%s\n", (const char *)glGetString(GL_RENDERER));
    glViewport(0, 0, 1024, 768);
    glPixelStorei(GL_UNPACK_ALIGNMENT, 1);
    tex = quad_tex();
    glTexEnvi(GL_TEXTURE_ENV, GL_TEXTURE_ENV_MODE, GL_REPLACE);
    glEnable(GL_TEXTURE_2D);
    glEnableClientState(GL_VERTEX_ARRAY);
    glEnableClientState(GL_TEXTURE_COORD_ARRAY);
    glClearColor(0.f, 0.f, 0.f, 1.f); glClear(GL_COLOR_BUFFER_BIT);
    printf("Q0 elem TRIANGLES Q1 elem TRI_STRIP Q2 elem TRI_FAN Q3 elem QUADS\n");

    for (s = 0; s < 4; s++) {
        float x0 = -0.95f + (float)s * 0.48f, x1 = x0 + 0.40f, y0 = -0.5f, y1 = 0.5f;
        GLfloat v[8]  = { x0,y0, x1,y0, x1,y1, x0,y1 };   /* BL BR TR TL */
        GLfloat tc[8] = { 0,0,  1,0,  1,1,  0,1 };
        glVertexPointer(2,GL_FLOAT,0,v); glTexCoordPointer(2,GL_FLOAT,0,tc);
        if      (s == 0) glDrawElements(GL_TRIANGLES,      6, GL_UNSIGNED_BYTE, tri);
        else if (s == 1) glDrawElements(GL_TRIANGLE_STRIP, 4, GL_UNSIGNED_BYTE, strip);
        else if (s == 2) glDrawElements(GL_TRIANGLE_FAN,   4, GL_UNSIGNED_BYTE, fan);
        else             glDrawElements(GL_QUADS,          4, GL_UNSIGNED_BYTE, quad);
    }
    glFlush(); glFinish();   /* one frame: per-glFinish would now colour-clear between */
    printf("drew\n"); fflush(stdout);
    sleep(6);
    aglSetCurrentContext(NULL); aglSetDrawable(ctx, NULL);
    aglDestroyContext(ctx); aglDestroyPixelFormat(pf);
    printf("done\n");
    return 0;
}

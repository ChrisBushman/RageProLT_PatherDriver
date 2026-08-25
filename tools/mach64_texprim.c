/*
 * mach64_texprim.c - verify GL_TRIANGLE_STRIP / GL_TRIANGLE_FAN / GL_QUADS
 * dispatch + decomposition (the dispatch slot labels were found off by one).
 * The SAME 8x8 four-quadrant texture (TL red, TR green, BL blue, BR white) is
 * drawn as a quad four ways; each should render the IDENTICAL clean four-quadrant
 * square (blue/white top, red/green bottom on screen).  A wrong decomposition
 * shows as a malformed quad (missing half, overlap, sheared, or garbage).
 *   Q0 GL_TRIANGLES     (6 verts)  -- the known-good control
 *   Q1 GL_TRIANGLE_STRIP(4 verts)
 *   Q2 GL_TRIANGLE_FAN  (4 verts)
 *   Q3 GL_QUADS         (4 verts)
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
    printf("Q0 TRIANGLES Q1 TRI_STRIP Q2 TRI_FAN Q3 QUADS\n");

    for (s = 0; s < 4; s++) {
        float x0 = -0.95f + (float)s * 0.48f, x1 = x0 + 0.40f, y0 = -0.5f, y1 = 0.5f;
        if (s == 0) {  /* GL_TRIANGLES: 6 verts (BL,BR,TR, BL,TR,TL) */
            GLfloat v[12] = { x0,y0, x1,y0, x1,y1,  x0,y0, x1,y1, x0,y1 };
            GLfloat tc[12]= { 0,0, 1,0, 1,1,  0,0, 1,1, 0,1 };
            glVertexPointer(2,GL_FLOAT,0,v); glTexCoordPointer(2,GL_FLOAT,0,tc);
            glDrawArrays(GL_TRIANGLES, 0, 6);
        } else if (s == 1) {  /* GL_TRIANGLE_STRIP: 4 verts (BL,BR,TL,TR) */
            GLfloat v[8] = { x0,y0, x1,y0, x0,y1, x1,y1 };
            GLfloat tc[8]= { 0,0, 1,0, 0,1, 1,1 };
            glVertexPointer(2,GL_FLOAT,0,v); glTexCoordPointer(2,GL_FLOAT,0,tc);
            glDrawArrays(GL_TRIANGLE_STRIP, 0, 4);
        } else {  /* GL_TRIANGLE_FAN / GL_QUADS: 4 verts (BL,BR,TR,TL) */
            GLfloat v[8] = { x0,y0, x1,y0, x1,y1, x0,y1 };
            GLfloat tc[8]= { 0,0, 1,0, 1,1, 0,1 };
            glVertexPointer(2,GL_FLOAT,0,v); glTexCoordPointer(2,GL_FLOAT,0,tc);
            glDrawArrays(s == 2 ? GL_TRIANGLE_FAN : GL_QUADS, 0, 4);
        }
        glFlush(); glFinish();
    }
    printf("drew\n"); fflush(stdout);
    sleep(6);
    aglSetCurrentContext(NULL); aglSetDrawable(ctx, NULL);
    aglDestroyContext(ctx); aglDestroyPixelFormat(pf);
    printf("done\n");
    return 0;
}

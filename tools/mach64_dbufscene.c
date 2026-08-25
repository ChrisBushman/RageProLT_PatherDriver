/*
 * mach64_dbufscene.c - double-buffer bring-up scene (driver built -DGLD_DBUF_TEST,
 * which renders to a back buffer, clears it to black at frame start, and blits
 * back->front on finish).  Unlike texprim (a glFinish per quad = 4 frames), this
 * draws ALL FOUR quads in ONE frame, then a single glFinish -> one present.
 * PASS = four clean four-quadrant squares on a CLEAN BLACK background (no VRAM
 * garbage), proving render-to-back + back-clear + present all work together.
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

    for (s = 0; s < 4; s++) {   /* four quads, ONE frame (no glFinish between) */
        float x0 = -0.95f + (float)s * 0.48f, x1 = x0 + 0.40f, y0 = -0.5f, y1 = 0.5f;
        GLfloat v[8]  = { x0,y0, x1,y0, x1,y1, x0,y1 };
        GLfloat tc[8] = { 0,0,  1,0,  1,1,  0,1 };
        glVertexPointer(2,GL_FLOAT,0,v); glTexCoordPointer(2,GL_FLOAT,0,tc);
        glDrawArrays(GL_QUADS, 0, 4);
    }
    glFinish();   /* single present of the whole frame */
    printf("drew scene\n"); fflush(stdout);
    sleep(6);
    aglSetCurrentContext(NULL); aglSetDrawable(ctx, NULL);
    aglDestroyContext(ctx); aglDestroyPixelFormat(pf);
    printf("done\n");
    return 0;
}

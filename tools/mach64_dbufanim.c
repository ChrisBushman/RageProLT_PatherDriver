/*
 * mach64_dbufanim.c - end-to-end double-buffer test via the PIXEL FORMAT (no build
 * flag): request AGL_DOUBLEBUFFER, then render two frames with aglSwapBuffers, each
 * clearing and drawing the four-quadrant textured quad at a different position.
 * Uses the known-good textured-array path.  PASS = the pixel format is accepted as
 * double-buffered, and the FINAL frame shows the quad at its last position on a
 * clean black field (no garbage, no leftover from the prior frame) -- proving the
 * driver armed the back buffer from the pixel format and presented via blit.
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

static void draw_quad(float cx)   /* four-quadrant quad centred at NDC x=cx */
{
    float x0 = cx - 0.25f, x1 = cx + 0.25f, y0 = -0.4f, y1 = 0.4f;
    GLfloat v[8]  = { x0,y0, x1,y0, x1,y1, x0,y1 };
    GLfloat tc[8] = { 0,0,  1,0,  1,1,  0,1 };
    glVertexPointer(2,GL_FLOAT,0,v); glTexCoordPointer(2,GL_FLOAT,0,tc);
    glDrawArrays(GL_QUADS, 0, 4);
}

int main(void)
{
    GDHandle dev = GetMainDevice();
    GLint attribs[] = { AGL_RGBA, AGL_ACCELERATED, AGL_FULLSCREEN,
                        AGL_DOUBLEBUFFER, AGL_DEPTH_SIZE, 16, AGL_NONE };
    AGLPixelFormat pf = aglChoosePixelFormat(&dev, 1, attribs);
    AGLContext ctx; GLuint tex; GLint dbl = -1; int f;
    if (!pf) { printf("no DOUBLEBUFFER pixel format!\n"); return 1; }
    aglDescribePixelFormat(pf, AGL_DOUBLEBUFFER, &dbl);
    printf("pf AGL_DOUBLEBUFFER = %d\n", dbl);
    ctx = aglCreateContext(pf, NULL); if (!ctx) { printf("no ctx\n"); return 1; }
    aglSetFullScreen(ctx, 0, 0, 0, 0); aglSetCurrentContext(ctx);
    printf("GL_RENDERER=%s\n", (const char *)glGetString(GL_RENDERER));
    glViewport(0, 0, 1024, 768);
    glPixelStorei(GL_UNPACK_ALIGNMENT, 1);
    tex = quad_tex();
    glTexEnvi(GL_TEXTURE_ENV, GL_TEXTURE_ENV_MODE, GL_REPLACE);
    glEnable(GL_TEXTURE_2D);
    glEnableClientState(GL_VERTEX_ARRAY);
    glEnableClientState(GL_TEXTURE_COORD_ARRAY);

    for (f = 0; f < 3; f++) {
        float cx = -0.6f + 0.6f * (float)f;      /* left, centre, right */
        printf(">>> frame %d cx=%.2f, swap\n", f, cx); fflush(stdout);
        glClearColor(0.f,0.f,0.f,1.f); glClear(GL_COLOR_BUFFER_BIT);
        draw_quad(cx);
        aglSwapBuffers(ctx);                     /* -> gldFlushBuffer -> present */
        sleep(2);
    }
    printf("done (final frame = right)\n"); fflush(stdout);
    sleep(3);
    aglSetCurrentContext(NULL); aglSetDrawable(ctx, NULL);
    aglDestroyContext(ctx); aglDestroyPixelFormat(pf);
    return 0;
}

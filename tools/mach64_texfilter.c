/* mach64_texfilter.c - verify bilinear texture filtering.  A 2x2 texture (red/green/blue/white)
 * drawn magnified to a 200x200 quad; glReadPixels the quad CENTER (texcoord ~0.5,0.5):
 *   center ~ (128,128,128) gray  -> BILINEAR mag filter works (the 4 texels blended)
 *   center = a pure texel colour -> NEAREST (BILINEAR bit had no effect)
 * MIN_FILTER=GL_NEAREST(0x2600), MAG_FILTER=GL_LINEAR(0x2601) are distinctive so the
 * GLD_TEXFILTER_PROBE state dump (in mach64_bind_texture) reveals their gc/texobj offsets.
 * Build: cc -framework Carbon -framework AGL -framework OpenGL */
#include <Carbon/Carbon.h>
#include <AGL/agl.h>
#include <OpenGL/gl.h>
#include <stdio.h>
#include <unistd.h>

static void rd(int x, int y, const char *w)
{
    GLubyte px[4]; px[0]=9;px[1]=9;px[2]=9;px[3]=9;
    glReadPixels(x, y, 1, 1, GL_RGBA, GL_UNSIGNED_BYTE, px);
    printf("READ %-11s (%d,%d) = %3d %3d %3d\n", w, x, y, px[0], px[1], px[2]);
}

int main(void)
{
    GDHandle dev = GetMainDevice();
    GLint at[] = { AGL_RGBA, AGL_ACCELERATED, AGL_FULLSCREEN, AGL_NONE };
    AGLPixelFormat pf = aglChoosePixelFormat(&dev, 1, at);
    AGLContext ctx;
    GLuint tlin, tnear;
    GLubyte tex[2*2*4];
    GLfloat v1[8], v2[8], tc[8];

    /* texel (col,row), row 0 = bottom: (0,0)=red (1,0)=green (0,1)=blue (1,1)=white */
    tex[0]=255;tex[1]=0;tex[2]=0;tex[3]=255;         tex[4]=0;tex[5]=255;tex[6]=0;tex[7]=255;
    tex[8]=0;tex[9]=0;tex[10]=255;tex[11]=255;       tex[12]=255;tex[13]=255;tex[14]=255;tex[15]=255;
    v1[0]=400;v1[1]=300; v1[2]=600;v1[3]=300; v1[4]=600;v1[5]=500; v1[6]=400;v1[7]=500;   /* LINEAR quad */
    v2[0]=400;v2[1]= 40; v2[2]=600;v2[3]= 40; v2[4]=600;v2[5]=240; v2[6]=400;v2[7]=240;   /* NEAREST quad */
    tc[0]=0;tc[1]=0; tc[2]=1;tc[3]=0; tc[4]=1;tc[5]=1; tc[6]=0;tc[7]=1;

    if (!pf) { printf("no pf\n"); return 1; }
    ctx = aglCreateContext(pf, NULL); if (!ctx) return 1;
    aglSetFullScreen(ctx, 0, 0, 0, 0); aglSetCurrentContext(ctx);
    printf("GL_RENDERER=%s\n", (const char *)glGetString(GL_RENDERER));
    glViewport(0, 0, 1024, 768);
    glMatrixMode(GL_PROJECTION); glLoadIdentity(); glOrtho(0, 1024, 0, 768, -1, 1);
    glMatrixMode(GL_MODELVIEW);  glLoadIdentity();
    glClearColor(0, 0, 0, 1); glClear(GL_COLOR_BUFFER_BIT);

    glEnable(GL_TEXTURE_2D);
    glColor3f(1, 1, 1);
    glEnableClientState(GL_VERTEX_ARRAY);
    glEnableClientState(GL_TEXTURE_COORD_ARRAY); glTexCoordPointer(2, GL_FLOAT, 0, tc);

    /* texture 1: MAG=GL_LINEAR (filter read at upload, so set it BEFORE glTexImage2D) */
    glGenTextures(1, &tlin); glBindTexture(GL_TEXTURE_2D, tlin);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);    /* 0x2601 -> bilinear mag */
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, 2, 2, 0, GL_RGBA, GL_UNSIGNED_BYTE, tex);
    glVertexPointer(2, GL_FLOAT, 0, v1);
    glDrawArrays(GL_QUADS, 0, 4);

    /* texture 2: MAG=GL_NEAREST -> no bilinear */
    glGenTextures(1, &tnear); glBindTexture(GL_TEXTURE_2D, tnear);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);   /* 0x2600 */
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, 2, 2, 0, GL_RGBA, GL_UNSIGNED_BYTE, tex);
    glVertexPointer(2, GL_FLOAT, 0, v2);
    glDrawArrays(GL_QUADS, 0, 4);
    glFinish();

    rd(500, 400, "LINEAR ctr");    /* bilinear -> ~128,128,128 gray */
    rd(500, 140, "NEAREST ctr");   /* nearest  -> a pure texel colour */
    printf("LINEAR gray(~128,128,128) + NEAREST pure = conditional bilinear WORKS\n");
    printf("glGetError = 0x%x\n", (unsigned)glGetError());
    fflush(stdout); sleep(3);

    aglSetCurrentContext(NULL); aglSetDrawable(ctx, NULL);
    aglDestroyContext(ctx); aglDestroyPixelFormat(pf);
    return 0;
}

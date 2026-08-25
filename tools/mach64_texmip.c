/*
 * mach64_texmip.c - mipmapped texture test.  A full mip chain (64x64 down to
 * 1x1) with a DISTINCT SOLID COLOUR per level, min filter NEAREST_MIPMAP_NEAREST,
 * drawn at decreasing on-screen sizes to force increasing minification (LOD).
 *   level 0 64x64 RED   1 32x32 GREEN  2 16x16 BLUE  3 8x8 YELLOW
 *   level 4 4x4 MAGENTA 5 2x2 CYAN     6 1x1 WHITE
 *
 * If the driver programs the per-level TEX_n_OFF and clears MIP_MAP_DISABLE, the
 * quads change colour as they shrink (bigger->red, smaller->green/blue/...).
 * Until then only the base uploads, so every quad is RED -- and the driver log's
 * "mip Ln:" lines reveal whether GLEngine even provides the level records.
 * Build: cc -framework Carbon -framework AGL -framework OpenGL
 */
#include <Carbon/Carbon.h>
#include <AGL/agl.h>
#include <OpenGL/gl.h>
#include <stdio.h>
#include <unistd.h>

static void level_color(int lvl, GLubyte *c)
{
    static const GLubyte pal[7][3] = {
        {255,0,0},{0,255,0},{0,0,255},{255,255,0},{255,0,255},{0,255,255},{255,255,255}};
    int i = (lvl < 7) ? lvl : 6;
    c[0]=pal[i][0]; c[1]=pal[i][1]; c[2]=pal[i][2];
}

static GLuint mip_tex(void)
{
    static GLubyte px[64*64*3];
    GLuint t = 0; int lvl, sz, i;
    glGenTextures(1,&t); glBindTexture(GL_TEXTURE_2D,t);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST_MIPMAP_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
    for (lvl=0, sz=64; sz>=1; lvl++, sz/=2) {
        GLubyte c[3]; level_color(lvl, c);
        for (i=0;i<sz*sz;i++){ px[i*3]=c[0]; px[i*3+1]=c[1]; px[i*3+2]=c[2]; }
        glTexImage2D(GL_TEXTURE_2D, lvl, GL_RGB, sz, sz, 0, GL_RGB, GL_UNSIGNED_BYTE, px);
    }
    return t;
}

/* Draw a `pixels`-wide square quad at NDC x-centre cx (texcoords 0..1 so the
 * minification ratio ~ 64/pixels selects the mip level). */
static void quad(float cx, int pixels)
{
    float hw = (float)pixels / 1024.0f;          /* NDC half-width (1024px view) */
    float hh = (float)pixels / 768.0f;
    float x0=cx-hw, x1=cx+hw, y0=-hh, y1=hh;
    GLfloat v[12] = { x0,y0, x1,y0, x1,y1,  x0,y0, x1,y1, x0,y1 };
    GLfloat tc[12]= { 0,0, 1,0, 1,1,  0,0, 1,1, 0,1 };
    glVertexPointer(2, GL_FLOAT, 0, v);
    glTexCoordPointer(2, GL_FLOAT, 0, tc);
    glDrawArrays(GL_TRIANGLES, 0, 6);
    glFlush(); glFinish();
}

int main(void)
{
    GDHandle dev = GetMainDevice();
    GLint attribs[] = { AGL_RGBA, AGL_ACCELERATED, AGL_FULLSCREEN,
                        AGL_DEPTH_SIZE, 16, AGL_NONE };
    AGLPixelFormat pf = aglChoosePixelFormat(&dev, 1, attribs);
    AGLContext ctx;
    GLuint t;
    if (!pf) { printf("no pf\n"); return 1; }
    ctx = aglCreateContext(pf, NULL);
    if (!ctx) return 1;
    aglSetFullScreen(ctx, 0, 0, 0, 0);
    aglSetCurrentContext(ctx);
    printf("GL_RENDERER=%s\n", (const char *)glGetString(GL_RENDERER));
    glViewport(0, 0, 1024, 768);
    glPixelStorei(GL_UNPACK_ALIGNMENT, 1);
    t = mip_tex();
    glTexEnvi(GL_TEXTURE_ENV, GL_TEXTURE_ENV_MODE, GL_REPLACE);
    glEnable(GL_TEXTURE_2D);
    glEnableClientState(GL_VERTEX_ARRAY);
    glEnableClientState(GL_TEXTURE_COORD_ARRAY);
    glClearColor(0.f, 0.f, 0.f, 1.f);
    glClear(GL_COLOR_BUFFER_BIT);
    printf("mip quads: sizes 128,64,32,16,8 px -> expect red,green,blue,yellow,magenta if mips work\n");
    quad(-0.75f, 128);   /* LOD 0 -> level0 red   */
    quad(-0.35f,  64);   /* LOD 1 -> level1 green */
    quad( 0.05f,  32);   /* LOD 2 -> level2 blue  */
    quad( 0.40f,  16);   /* LOD 3 -> level3 yellow*/
    quad( 0.70f,   8);   /* LOD 4 -> level4 magenta */
    printf("drew\n"); fflush(stdout);
    sleep(5);
    aglSetCurrentContext(NULL); aglSetDrawable(ctx, NULL);
    aglDestroyContext(ctx); aglDestroyPixelFormat(pf);
    printf("done\n");
    return 0;
}

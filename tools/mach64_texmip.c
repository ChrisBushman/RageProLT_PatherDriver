/* mach64_texmip.c - validate the mipmapped TEX_BLEND_FCN_LINEAR (trilinear) path, which the driver
 * now enables by DEFAULT for mipmapped textures (nlevels>1).  No probe arg needed.
 *
 * A full mip chain with a DISTINCT colour per level:  L0 16x16=red  L1 8x8=green  L2 4x4=blue
 * L3 2x2=yellow  L4 1x1=white.  Both quads drawn at LOD ~0.4 (texcoords 0..4 over a 48px quad ->
 * 16*4/48 = 1.33 texels/pixel, i.e. between level 0 and level 1):
 *   LEFT  = GL_LINEAR_MIPMAP_LINEAR  -> bit ON  -> TRILINEAR: blends L0(red)+L1(green) => green>0
 *   RIGHT = GL_NEAREST_MIPMAP_NEAREST-> bit OFF -> nearest level: pure L0 red => green~0
 * SAFETY (the primary check): neither quad may read BLACK and the machine must not hang -- black would
 * mean the mip-blend fetched a non-existent level (the single-level failure mode).  EFFECT (secondary):
 * LEFT's green channel high, RIGHT's ~0 -> trilinear confirmed.
 * Build: cc -framework Carbon -framework AGL -framework OpenGL */
#include <Carbon/Carbon.h>
#include <AGL/agl.h>
#include <OpenGL/gl.h>
#include <stdio.h>
#include <unistd.h>

static void fill(GLubyte *p, int n, int r, int g, int b)
{ int i; for (i=0;i<n;i++){ p[i*4]=r; p[i*4+1]=g; p[i*4+2]=b; p[i*4+3]=255; } }

static void rd(int x, int y, const char *w)
{
    GLubyte px[4]; px[0]=px[1]=px[2]=9;
    glReadPixels(x, y, 1, 1, GL_RGBA, GL_UNSIGNED_BYTE, px);
    printf("READ %-16s (%d,%d) = R%3d G%3d B%3d %s\n", w, x, y, px[0], px[1], px[2],
           (px[0]|px[1]|px[2]) ? "" : "  <-- BLACK (mip fetch FAILED)");
}

int main(void)
{
    GDHandle dev = GetMainDevice();
    GLint at[] = { AGL_RGBA, AGL_ACCELERATED, AGL_FULLSCREEN, AGL_NONE };
    AGLPixelFormat pf = aglChoosePixelFormat(&dev, 1, at);
    AGLContext ctx;
    GLuint tl, tn;
    GLubyte l0[16*16*4], l1[8*8*4], l2[4*4*4], l3[2*2*4], l4[1*1*4];
    GLfloat v1[8], v2[8], tc[8];

    fill(l0,256, 255,0,0);   /* L0 red   */
    fill(l1,64,  0,255,0);   /* L1 green */
    fill(l2,16,  0,0,255);   /* L2 blue  */
    fill(l3,4,   255,255,0); /* L3 yellow*/
    fill(l4,1,   255,255,255);/*L4 white */
    v1[0]=140;v1[1]=300; v1[2]=188;v1[3]=300; v1[4]=188;v1[5]=348; v1[6]=140;v1[7]=348;   /* LEFT  */
    v2[0]=540;v2[1]=300; v2[2]=588;v2[3]=300; v2[4]=588;v2[5]=348; v2[6]=540;v2[7]=348;   /* RIGHT */
    tc[0]=0;tc[1]=0; tc[2]=4;tc[3]=0; tc[4]=4;tc[5]=4; tc[6]=0;tc[7]=4;                    /* LOD ~0.4 */

    if (!pf) { printf("no pf\n"); return 1; }
    ctx = aglCreateContext(pf, NULL); if (!ctx) return 1;
    aglSetFullScreen(ctx, 0, 0, 0, 0); aglSetCurrentContext(ctx);
    printf("GL_RENDERER=%s\n", (const char *)glGetString(GL_RENDERER));
    glViewport(0, 0, 1024, 768);
    glMatrixMode(GL_PROJECTION); glLoadIdentity(); glOrtho(0, 1024, 0, 768, -1, 1);
    glMatrixMode(GL_MODELVIEW);  glLoadIdentity();
    glClearColor(0, 0, 0.5f, 1); glClear(GL_COLOR_BUFFER_BIT);
    glEnable(GL_TEXTURE_2D);
    glColor3f(1, 1, 1);
    glEnableClientState(GL_VERTEX_ARRAY);
    glEnableClientState(GL_TEXTURE_COORD_ARRAY); glTexCoordPointer(2, GL_FLOAT, 0, tc);

    /* LEFT: full mip chain, LINEAR_MIPMAP_LINEAR -> trilinear (bit ON via nlevels>1) */
    glGenTextures(1, &tl); glBindTexture(GL_TEXTURE_2D, tl);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR_MIPMAP_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, 16,16, 0, GL_RGBA, GL_UNSIGNED_BYTE, l0);
    glTexImage2D(GL_TEXTURE_2D, 1, GL_RGBA,  8, 8, 0, GL_RGBA, GL_UNSIGNED_BYTE, l1);
    glTexImage2D(GL_TEXTURE_2D, 2, GL_RGBA,  4, 4, 0, GL_RGBA, GL_UNSIGNED_BYTE, l2);
    glTexImage2D(GL_TEXTURE_2D, 3, GL_RGBA,  2, 2, 0, GL_RGBA, GL_UNSIGNED_BYTE, l3);
    glTexImage2D(GL_TEXTURE_2D, 4, GL_RGBA,  1, 1, 0, GL_RGBA, GL_UNSIGNED_BYTE, l4);
    glVertexPointer(2, GL_FLOAT, 0, v1); glDrawArrays(GL_QUADS, 0, 4);

    /* RIGHT: same chain, NEAREST_MIPMAP_NEAREST -> pick nearest level (bit OFF) */
    glGenTextures(1, &tn); glBindTexture(GL_TEXTURE_2D, tn);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST_MIPMAP_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, 16,16, 0, GL_RGBA, GL_UNSIGNED_BYTE, l0);
    glTexImage2D(GL_TEXTURE_2D, 1, GL_RGBA,  8, 8, 0, GL_RGBA, GL_UNSIGNED_BYTE, l1);
    glTexImage2D(GL_TEXTURE_2D, 2, GL_RGBA,  4, 4, 0, GL_RGBA, GL_UNSIGNED_BYTE, l2);
    glTexImage2D(GL_TEXTURE_2D, 3, GL_RGBA,  2, 2, 0, GL_RGBA, GL_UNSIGNED_BYTE, l3);
    glTexImage2D(GL_TEXTURE_2D, 4, GL_RGBA,  1, 1, 0, GL_RGBA, GL_UNSIGNED_BYTE, l4);
    glVertexPointer(2, GL_FLOAT, 0, v2); glDrawArrays(GL_QUADS, 0, 4);
    glFinish();

    rd(164, 324, "LEFT trilinear");   /* centre of left quad  */
    rd(564, 324, "RIGHT nearestmip"); /* centre of right quad */
    printf("SAFE if neither is BLACK.  TRILINEAR if LEFT green > RIGHT green (L0 red + L1 green blend).\n");
    printf("glGetError = 0x%x\n", (unsigned)glGetError());
    fflush(stdout); sleep(8);

    aglSetCurrentContext(NULL); aglSetDrawable(ctx, NULL);
    aglDestroyContext(ctx); aglDestroyPixelFormat(pf);
    return 0;
}

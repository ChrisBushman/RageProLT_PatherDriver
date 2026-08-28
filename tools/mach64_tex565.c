/* mach64_tex565.c - GL_UNSIGNED_SHORT_5_6_5 texture support + type-field probe.
 * Uploads TWO 2x2 GL_RGB textures of the same size: one GL_UNSIGNED_BYTE (8-bit, known-good) and one
 * GL_UNSIGNED_SHORT_5_6_5 (16-bit packed).  With the driver built -DGLD_TEXFMT_PROBE, each upload logs
 * its 20-byte level record ("texrec L0:"); the field that DIFFERS between the 8-bit and 5_6_5 records
 * is the pixel TYPE that mach64_upload must read to convert 5_6_5.  Reads back both so we also see the
 * before/after: the 8-bit quad is correct today; the 5_6_5 quad is garbage until the driver handles it.
 * Build: cc -framework Carbon -framework AGL -framework OpenGL */
#include <Carbon/Carbon.h>
#include <AGL/agl.h>
#include <OpenGL/gl.h>
#include <stdio.h>
#include <unistd.h>

#ifndef GL_UNSIGNED_SHORT_5_6_5
#define GL_UNSIGNED_SHORT_5_6_5 0x8363
#endif

static void rd(int x, int y, const char *w)
{
    GLubyte px[4]; px[0]=px[1]=px[2]=9;
    glReadPixels(x, y, 1, 1, GL_RGBA, GL_UNSIGNED_BYTE, px);
    printf("READ %-14s (%d,%d) = %3d %3d %3d\n", w, x, y, px[0], px[1], px[2]);
}

int main(void)
{
    GDHandle dev = GetMainDevice();
    GLint at[] = { AGL_RGBA, AGL_ACCELERATED, AGL_FULLSCREEN, AGL_NONE };
    AGLPixelFormat pf = aglChoosePixelFormat(&dev, 1, at);
    AGLContext ctx;
    GLuint t8, t565;
    GLubyte  rgb8[12];    /* 2x2 RGB 8-bit: red/green/blue/white */
    GLushort p565[4];     /* 2x2 RGB 5_6_5 */
    GLfloat vL[8], vR[8], tc[8];

    rgb8[0]=255;rgb8[1]=0;rgb8[2]=0;    rgb8[3]=0;rgb8[4]=255;rgb8[5]=0;      /* red, green */
    rgb8[6]=0;rgb8[7]=0;rgb8[8]=255;    rgb8[9]=255;rgb8[10]=255;rgb8[11]=255;/* blue, white */
    p565[0]=0xF800; p565[1]=0x07E0; p565[2]=0x001F; p565[3]=0xFFFF;          /* R G B W */
    vL[0]=250;vL[1]=300; vL[2]=450;vL[3]=300; vL[4]=450;vL[5]=500; vL[6]=250;vL[7]=500;   /* LEFT 8-bit */
    vR[0]=550;vR[1]=300; vR[2]=750;vR[3]=300; vR[4]=750;vR[5]=500; vR[6]=550;vR[7]=500;   /* RIGHT 565 */
    tc[0]=0;tc[1]=0; tc[2]=1;tc[3]=0; tc[4]=1;tc[5]=1; tc[6]=0;tc[7]=1;

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
    glPixelStorei(GL_UNPACK_ALIGNMENT, 1);

    /* LEFT: 8-bit RGB (known-good) -- its level record is the reference */
    glGenTextures(1, &t8); glBindTexture(GL_TEXTURE_2D, t8);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGB, 2, 2, 0, GL_RGB, GL_UNSIGNED_BYTE, rgb8);
    glVertexPointer(2, GL_FLOAT, 0, vL); glDrawArrays(GL_QUADS, 0, 4);

    /* RIGHT: 5_6_5 */
    glGenTextures(1, &t565); glBindTexture(GL_TEXTURE_2D, t565);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGB, 2, 2, 0, GL_RGB, GL_UNSIGNED_SHORT_5_6_5, p565);
    glVertexPointer(2, GL_FLOAT, 0, vR); glDrawArrays(GL_QUADS, 0, 4);
    glFinish();

    printf("-- 8-bit (should be correct) --\n");
    rd(300,350,"8 red"); rd(400,350,"8 green"); rd(300,450,"8 blue"); rd(400,450,"8 white");
    printf("-- 5_6_5 (garbage until handled) --\n");
    rd(600,350,"565 red"); rd(700,350,"565 green"); rd(600,450,"565 blue"); rd(700,450,"565 white");
    printf("PASS(565) if red/green/blue/white read pure.  Compare the two 'texrec L0' log lines.\n");
    printf("glGetError = 0x%x\n", (unsigned)glGetError());
    fflush(stdout); sleep(4);

    aglSetCurrentContext(NULL); aglSetDrawable(ctx, NULL);
    aglDestroyContext(ctx); aglDestroyPixelFormat(pf);
    return 0;
}

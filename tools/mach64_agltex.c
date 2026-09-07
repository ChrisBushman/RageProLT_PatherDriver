/*
 * mach64_agltex.c - windowed AGL TEXTURE test for Path B.
 *
 * Draws a texture-mapped quad (two GL_TRIANGLES) filling a small 320x240 window with a
 * 128x128 RGBA checkerboard (red / yellow, 16px checks).  Exercises the Path-B texture path:
 * glTexImage2D -> gld texture upload (DMA host-data blit into the RELOCATED, Apple-safe texture
 * heap) -> textured triangle (TEX_0_OFF / TEX_CNTL / SCALE_3D_CNTL texture-enable + per-vertex S/T).
 *
 * A crisp red/yellow checkerboard = the texture path works through Apple's kext with the heap in
 * safe VRAM.  Garbage/black/wrong-colours = upload or heap-placement problem.  Small window keeps
 * the texture heap in the collision-free zone below 0x400000.  MUST run AT THE CONSOLE.
 * Build: cc -O2 -o ~/agltex mach64_agltex.c -framework AGL -framework OpenGL -framework Carbon
 */
#include <AGL/agl.h>
#include <OpenGL/gl.h>
#include <Carbon/Carbon.h>
#include <stdio.h>
#include <unistd.h>

int main(void)
{
    Rect bounds = { 80, 80, 320, 400 };              /* 320x240 (keeps texheap < 0x400000) */
    WindowRef win = NULL;
    OSStatus err = CreateNewWindow(kDocumentWindowClass,
                                   kWindowStandardDocumentAttributes, &bounds, &win);
    if (err || !win) { printf("CreateNewWindow err=%d\n", (int)err); return 1; }
    SetWindowTitleWithCFString(win, CFSTR("mach64 Path-B texture"));
    ShowWindow(win);

    GLint attrs[] = { AGL_RGBA, AGL_DOUBLEBUFFER, AGL_ACCELERATED, AGL_NONE };
    AGLPixelFormat pf = aglChoosePixelFormat(NULL, 0, attrs);
    printf("aglChoosePixelFormat(accelerated) -> %p (err %d)\n", (void*)pf, (int)aglGetError());
    if (!pf) { printf("no ACCELERATED pixel format\n"); return 1; }
    AGLContext ctx = aglCreateContext(pf, NULL);
    if (!ctx) { printf("aglCreateContext failed (err %d)\n", (int)aglGetError()); return 1; }
    GLboolean ok = aglSetDrawable(ctx, GetWindowPort(win));
    printf("aglSetDrawable -> %d (err %d)\n", (int)ok, (int)aglGetError());
    aglSetCurrentContext(ctx);

    /* Build a 128x128 red/yellow checkerboard, 16px checks. */
    static unsigned char tex[128*128*4];
    int x, y;
    for (y = 0; y < 128; y++) for (x = 0; x < 128; x++) {
        int c = ((x >> 4) ^ (y >> 4)) & 1;
        unsigned char *p = &tex[(y*128 + x)*4];
        p[0] = 255;                       /* R */
        p[1] = c ? 255 : 0;               /* G: check -> yellow(255) vs red(0) */
        p[2] = 0;                         /* B */
        p[3] = 255;                       /* A */
    }

    GLuint id = 0;
    glGenTextures(1, &id);
    glBindTexture(GL_TEXTURE_2D, id);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, 128, 128, 0, GL_RGBA, GL_UNSIGNED_BYTE, tex);
    glEnable(GL_TEXTURE_2D);
    glTexEnvi(GL_TEXTURE_ENV, GL_TEXTURE_ENV_MODE, GL_REPLACE);   /* show texels directly */
    printf("texture uploaded (glGetError=0x%x)\n", (int)glGetError());

    glViewport(0, 0, 320, 240);
    glMatrixMode(GL_PROJECTION); glLoadIdentity();
    glOrtho(-1.0, 1.0, -1.0, 1.0, -1.0, 1.0);
    glMatrixMode(GL_MODELVIEW);  glLoadIdentity();

    printf("rendering 180 frames (textured quad)...\n"); fflush(stdout);
    int i;
    for (i = 0; i < 180; i++) {
        glClearColor(0.10f, 0.10f, 0.15f, 1.0f);
        glClear(GL_COLOR_BUFFER_BIT);

        /* Textured quad as two triangles, texcoords 0..1, covering most of the window. */
        glBegin(GL_TRIANGLES);
            glTexCoord2f(0.0f, 0.0f); glVertex2f(-0.8f, -0.8f);
            glTexCoord2f(1.0f, 0.0f); glVertex2f( 0.8f, -0.8f);
            glTexCoord2f(1.0f, 1.0f); glVertex2f( 0.8f,  0.8f);

            glTexCoord2f(0.0f, 0.0f); glVertex2f(-0.8f, -0.8f);
            glTexCoord2f(1.0f, 1.0f); glVertex2f( 0.8f,  0.8f);
            glTexCoord2f(0.0f, 1.0f); glVertex2f(-0.8f,  0.8f);
        glEnd();

        aglSwapBuffers(ctx);
        usleep(16000);
    }
    printf("done -- 180 textured frames presented\n");

    aglSetCurrentContext(NULL);
    aglSetDrawable(ctx, NULL);
    aglDestroyContext(ctx);
    aglDestroyPixelFormat(pf);
    DisposeWindow(win);
    return 0;
}

/* srdemo.c - staged minimal reproduction of Sonic R's SEGA splash render path on the Rage (Panther), using the SAME static SDL 2.0.3 as the game.
 * DEMO_STAGE (cumulative):
 *   0  SDL window (RESIZABLE 640x480 centred, depth 24, vsync) + glClear(colour) + swap.
 *   1  + the game's BeginFrame recipe: 4:3 viewport in the drawable, glOrtho(-1,1,-480,480,0,-1), modelview translate/scale,
 *        scissored glClear black, untextured quads drawn with glVertex4f(x*w,y*w,z*w,w) in 640x480 game coordinates.
 *   2  + textured 6-quad grid (256x256 RGBA tiles, NEAREST, CLAMP_TO_EDGE) like RenderLogoQuads.
 *   4  + title frame: depth+colour clear, scissored clear, 32-quad iris at near depth (hole scales with DEMO_FADELEVEL), then logo grid.
 *   3  + two BeginFrame/EndFrame(glFlush) passes per frame, alpha blend + alpha test + depth LEQUAL state, fade-overlay quad.
 * DEMO_FRAMES = number of frames (default 600).
 * Pattern: every pixel of the 4:3 area is covered; expected on screen = 508x381 (at x 66) for a 640x381 window. */
#include <SDL.h>
#include <SDL_opengl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>

static int skip, stage, fullW, fullH, offX, offY, vpW, vpH;
static GLuint tex[6];

static void vtx(float x, float y, float z, float u, float v, float r, float g, float b, float a)
{
    float rhw = (z > 0.f) ? 1.f / z : 1.f, w = 1.f / rhw, normZ = z / (stage >= 4 ? 88064.f : 4096.f);
    glColor4f(r, g, b, a); glTexCoord2f(u, v); glVertex4f(x * w, y * w, normZ * w, w);
}
static void begin_frame(void)
{
    SDL_GL_GetDrawableSize(SDL_GL_GetCurrentWindow(), &fullW, &fullH);
    if (fullW * 3 > fullH * 4) { vpH = fullH; vpW = fullH * 4 / 3; } else { vpW = fullW; vpH = fullW * 3 / 4; }
    offX = (fullW - vpW) / 2; offY = (fullH - vpH) / 2;
    glViewport(offX, offY, vpW, vpH);
    glMatrixMode(GL_PROJECTION); glLoadIdentity(); glOrtho(-1.0, 1.0, -480.0, 480.0, 0, -1);
    glMatrixMode(GL_MODELVIEW); glLoadIdentity();
    glTranslatef(-1.0f, 480.0f, 0.0f); glScalef(2.0f / 640.0f, -960.0f / 480.0f, 1.0f);
    glDisable(GL_CULL_FACE);
    if (stage >= 3) { glEnable(GL_BLEND); glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA); glEnable(GL_ALPHA_TEST); glAlphaFunc(GL_GREATER, 0.0f);
                      if (!(skip & 4)) glEnable(GL_DEPTH_TEST); glDepthFunc(GL_LEQUAL); glDepthMask(GL_TRUE); }
}
static void make_textures(void)
{
    static unsigned char px[256 * 256 * 4]; int t, x, y;
    glGenTextures(6, tex);
    for (t = 0; t < 6; t++) {
        for (y = 0; y < 256; y++) for (x = 0; x < 256; x++) { unsigned char *p = px + (y * 256 + x) * 4;
            int grid = ((x >> 4) ^ (y >> 4)) & 1;
            p[0] = (unsigned char)(40 + t * 35 + (grid ? 60 : 0)); p[1] = (unsigned char)(x); p[2] = (unsigned char)(y); p[3] = 255;
            if (x < 3 || y < 3 || x > 252 || y > 252) { p[0] = 255; p[1] = 255; p[2] = 255; } }
        glBindTexture(GL_TEXTURE_2D, tex[t]);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST); glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE); glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
        glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, 256, 256, 0, GL_RGBA, GL_UNSIGNED_BYTE, px);
    }
}
static void draw_bars(void)   /* stage 1: untextured colour bars covering the whole 640x480 game screen, plus a white frame */
{
    int b; glDisable(GL_TEXTURE_2D);
    for (b = 0; b < 12; b++) { float c = (float)(b + 1) / 12.f, y0 = (float)(b * 40), y1 = y0 + 40.f;
        glBegin(GL_TRIANGLE_FAN); vtx(0, y0, 3000, 0, 0, (b & 1) ? c : .2f, (b & 2) ? 1 - c : .3f, (b & 4) ? c : .6f, 1); vtx(640, y0, 3000, 0, 0, (b & 1) ? c : .2f, (b & 2) ? 1 - c : .3f, (b & 4) ? c : .6f, 1);
        vtx(640, y1, 3000, 0, 0, (b & 1) ? c : .2f, (b & 2) ? 1 - c : .3f, (b & 4) ? c : .6f, 1); vtx(0, y1, 3000, 0, 0, (b & 1) ? c : .2f, (b & 2) ? 1 - c : .3f, (b & 4) ? c : .6f, 1); glEnd(); }
    glBegin(GL_TRIANGLE_FAN); vtx(0, 472, 2990, 0, 0, 1, 1, 1, 1); vtx(640, 472, 2990, 0, 0, 1, 1, 1, 1); vtx(640, 480, 2990, 0, 0, 1, 1, 1, 1); vtx(0, 480, 2990, 0, 0, 1, 1, 1, 1); glEnd();
}
static void draw_logo(void)   /* stage 2: RenderLogoQuads layout: 3 columns (0,256,512..640) x 2 rows (0,256..480), umax/vmax per quad */
{
    static const int gx[4] = { 0, 256, 512, 640 }, gy[3] = { 0, 256, 480 };
    static const float um[6] = { 1, 1, .5f, 1, 1, .5f }, vm[6] = { 1, 1, 1, .875f, .875f, .875f };
    int q; glEnable(GL_TEXTURE_2D);
    for (q = 0; q < 6; q++) { int c = q % 3, r = q / 3; float x0 = (float)gx[c], x1 = (float)gx[c + 1], y0 = (float)gy[r], y1 = (float)gy[r + 1];
        glBindTexture(GL_TEXTURE_2D, tex[q]);
        glBegin(GL_TRIANGLE_FAN); vtx(x0, y0, 3000, 0, 0, 1, 1, 1, 1); vtx(x1, y0, 3000, um[q], 0, 1, 1, 1, 1); vtx(x1, y1, 3000, um[q], vm[q], 1, 1, 1, 1); vtx(x0, y1, 3000, 0, vm[q], 1, 1, 1, 1); glEnd(); }
}
static float irisz(void) { return getenv("DEMO_IRISZ") ? (float)atof(getenv("DEMO_IRISZ")) : 1.1f; }
static void draw_iris(int level, float rot)   /* RenderFadeOverlay: 32 opaque black quads between an inner ring (hole, scales with level) and an outer ring past the screen */
{
    float ri = 330.0f * (float)(level + 256) / 256.0f, ro = getenv("DEMO_RO") ? (float)atoi(getenv("DEMO_RO")) : 800.0f, ix[32], iy[32], ox[32], oy[32]; int k;
    glDisable(GL_TEXTURE_2D);
    for (k = 0; k < 32; k++) { float a = rot + (float)k * 6.2831853f / 32.0f, c = cosf(a), s = sinf(a);
        ix[k] = 320.0f + ri * c * (4.0f / 3.0f); iy[k] = 240.0f - ri * s; ox[k] = 320.0f + ro * c * (4.0f / 3.0f); oy[k] = 240.0f - ro * s; }
    for (k = 0; k < (getenv("DEMO_IRISQ") ? atoi(getenv("DEMO_IRISQ")) : 32); k++) { int n = (k + 1) % 32;
        glBegin(GL_TRIANGLE_FAN); vtx(ix[k], iy[k], irisz(), 0.5f, 0.5f, 0, 0, 0, 1); vtx(ox[k], oy[k], irisz(), 0.51f, 0.5f, 0, 0, 0, 1); vtx(ox[n], oy[n], irisz(), 0.51f, 0.51f, 0, 0, 0, 1); vtx(ix[n], iy[n], irisz(), 0.5f, 0.51f, 0, 0, 0, 1); glEnd(); }
}
int main(int argc, char **argv)
{
    SDL_Window *win; SDL_GLContext gc; int f, nf = getenv("DEMO_FRAMES") ? atoi(getenv("DEMO_FRAMES")) : 600;
    stage = getenv("DEMO_STAGE") ? atoi(getenv("DEMO_STAGE")) : 0; skip = getenv("DEMO_SKIP") ? atoi(getenv("DEMO_SKIP")) : 0;   /* 1 no fade quad, 2 no bars in pass 0, 4 no depth test */
    if (SDL_Init(SDL_INIT_VIDEO) != 0) { fprintf(stderr, "SDL_Init: %s\n", SDL_GetError()); return 1; }
    SDL_StopTextInput();
    SDL_GL_SetAttribute(SDL_GL_DOUBLEBUFFER, 1); SDL_GL_SetAttribute(SDL_GL_DEPTH_SIZE, 24);
    win = SDL_CreateWindow("srdemo", SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED, 640, 480, SDL_WINDOW_RESIZABLE);
    if (!win) { fprintf(stderr, "CreateWindow: %s\n", SDL_GetError()); return 2; }
    SDL_RaiseWindow(win); SDL_ShowCursor(SDL_DISABLE);
    gc = SDL_GL_CreateContext(win); if (!gc) { fprintf(stderr, "CreateContext: %s\n", SDL_GetError()); return 3; }
    SDL_GL_MakeCurrent(win, gc); SDL_GL_SetSwapInterval(1);
    { int w, h, x, y; SDL_GetWindowSize(win, &w, &h); SDL_GetWindowPosition(win, &x, &y); SDL_GL_GetDrawableSize(win, &fullW, &fullH);
      fprintf(stderr, "stage=%d window %dx%d at (%d,%d) drawable %dx%d GL_RENDERER=%s\n", stage, w, h, x, y, fullW, fullH, (const char *)glGetString(GL_RENDERER)); }
    if (stage >= 2) make_textures();
    for (f = 0; f < nf; f++) {
        SDL_Event e; while (SDL_PollEvent(&e)) { if (e.type == SDL_QUIT) f = nf; }
        if (stage >= 4) {   /* title frame: ProcessTpageStates clear, RenderBackground, RenderFadeOverlay, logo/background quads */
            int level = getenv("DEMO_FADELEVEL") ? atoi(getenv("DEMO_FADELEVEL")) : -128;
            if (getenv("DEMO_FADEANIM")) { level = -256 + (f * 6) % 300; if (level > 0) level = 0; }
            begin_frame(); glDisable(GL_SCISSOR_TEST); glDepthMask(GL_TRUE); glClearColor(0, 0, 0, 1); glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
            glEnable(GL_SCISSOR_TEST); glScissor(offX, offY, vpW, vpH);
            begin_frame(); glEnable(GL_SCISSOR_TEST); glScissor(offX, offY, vpW, vpH); glClear(GL_COLOR_BUFFER_BIT); glDisable(GL_SCISSOR_TEST); glFlush();
            if (getenv("DEMO_TESTQ")) {   /* one axis-aligned untextured quad "x0,y0,x1,y1,z,alpha" instead of the iris */
                float x0, y0, x1, y1, z, a; sscanf(getenv("DEMO_TESTQ"), "%f,%f,%f,%f,%f,%f", &x0, &y0, &x1, &y1, &z, &a);
                begin_frame(); glDisable(GL_TEXTURE_2D); { float cr = getenv("DEMO_TESTRED") ? 1.0f : 0.0f; glBegin(GL_TRIANGLE_FAN); vtx(x0, y0, z, 0.5f, 0.5f, cr, 0, 0, a); vtx(x1, y0, z, 0.51f, 0.5f, cr, 0, 0, a); vtx(x1, y1, z, 0.51f, 0.51f, cr, 0, 0, a); vtx(x0, y1, z, 0.5f, 0.51f, cr, 0, 0, a); } glEnd(); if (!getenv("DEMO_SAMESEG")) glFlush();
            } else if (level < 0) { begin_frame(); draw_iris(level, 0.1f * (float)(f & 127)); glFlush(); }
            if (!getenv("DEMO_SAMESEG")) begin_frame();
            if (getenv("DEMO_GREEN")) { glDisable(GL_TEXTURE_2D); glBegin(GL_TRIANGLE_FAN); vtx(0, 0, 3000, 0, 0, 0, 1, 0, 1); vtx(640, 0, 3000, 0, 0, 0, 1, 0, 1); vtx(640, 480, 3000, 0, 0, 0, 1, 0, 1); vtx(0, 480, 3000, 0, 0, 0, 1, 0, 1); glEnd(); }
            else if (!getenv("DEMO_NOLOGO")) draw_logo();
            glFlush();
            SDL_GL_SwapWindow(win); continue;
        }
        if (stage == 0) { glClearColor(0.2f, 0.4f, 0.8f, 1); glClear(GL_COLOR_BUFFER_BIT); }
        else {
            int pass, npass = stage >= 3 ? 2 : 1;
            for (pass = 0; pass < npass; pass++) {
                begin_frame();
                if (pass == 0 || stage < 3) {
                    glEnable(GL_SCISSOR_TEST); glScissor(offX, offY, vpW, vpH); glClearColor(0, 0, 0, 1); glClear(GL_COLOR_BUFFER_BIT | (stage >= 3 ? GL_DEPTH_BUFFER_BIT : 0)); glDisable(GL_SCISSOR_TEST);
                }
                if (stage == 1) draw_bars();
                if (stage >= 2) { if (pass == 0 && stage >= 3) { if (!(skip & 2)) draw_bars(); } else draw_logo(); }
                if (stage >= 3 && pass == 0 && !(skip & 1)) { float a = 0.5f; glDisable(GL_TEXTURE_2D); glBegin(GL_TRIANGLE_FAN); vtx(0, 0, 100, 0, 0, 0, 0, 0, a); vtx(640, 0, 100, 0, 0, 0, 0, 0, a); vtx(640, 480, 100, 0, 0, 0, 0, 0, a); vtx(0, 480, 100, 0, 0, 0, 0, 0, a); glEnd(); }
                glFlush();
            }
        }
        SDL_GL_SwapWindow(win);
    }
    SDL_GL_DeleteContext(gc); SDL_DestroyWindow(win); SDL_Quit();
    return 0;
}

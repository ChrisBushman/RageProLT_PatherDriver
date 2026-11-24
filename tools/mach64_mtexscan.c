/* mtexscan (derived from lodscan): recover GLEngine gli offsets for texture unit 1.  Usage: cc -o mtexscan mach64_mtexscan.c -framework OpenGL */
/* (lodscan usage was: cc -o lodscan mach64_lodscan.c -framework OpenGL ; ./lodscan   (any Panther/Tiger Mac with a display; no Rage needed).  Result: GL_TEXTURE_LOD_BIAS = gli+0x6774.) */
/* lodscan: find GLEngine's gli in an offscreen software context (validated by toggling GL_FOG / GL_DEPTH_TEST, whose gli words are known),
 * then diff it while GL_TEXTURE_LOD_BIAS_EXT changes.  Read-only on our own process memory (vm_read_overwrite cannot fault). */
#include <OpenGL/OpenGL.h>
#include <OpenGL/gl.h>
#include <OpenGL/glext.h>
#include <mach/mach.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static int rd(unsigned long a, void *buf, unsigned n)
{ vm_size_t o = 0; return vm_read_overwrite(mach_task_self(), (vm_address_t)a, n, (vm_address_t)buf, &o) == KERN_SUCCESS && o == n; }
static int rd32(unsigned long a, unsigned *v) { return rd(a, v, 4); }
#define MAXC 40000
static unsigned long cand[MAXC]; static int ncand;
static unsigned fog0[MAXC], dep0[MAXC];
static void addc(unsigned long q) { int i; if (q < 0x10000 || (q & 3) || q > 0xffff0000ul) return; for (i = 0; i < ncand; i++) if (cand[i] == q) return; if (ncand < MAXC) cand[ncand++] = q; }
static void sample(unsigned *fog, unsigned *dep) { int i; for (i = 0; i < ncand; i++) { if (!rd32(cand[i] + 0x6380, &fog[i])) fog[i] = 0xdeadbeef; if (!rd32(cand[i] + 0x6340, &dep[i])) dep[i] = 0xdeadbeef; } }

int main(void)
{
    CGLPixelFormatObj pf; long npf; CGLContextObj c; CGLError e;
    int attr[] = { kCGLPFAOffScreen, kCGLPFAColorSize, 32, kCGLPFADepthSize, 16, 0 };
    static unsigned char fbmem[64 * 64 * 4];
    int i, j, k; unsigned long root;
    e = CGLChoosePixelFormat(attr, &pf, &npf); printf("choose=%d npf=%ld\n", (int)e, npf); if (e || !pf) return 1;
    e = CGLCreateContext(pf, NULL, &c); printf("create=%d ctx=%p\n", (int)e, c); if (e) return 1;
    e = CGLSetOffScreen(c, 64, 64, 64 * 4, fbmem); printf("offscreen=%d\n", (int)e);
    CGLSetCurrentContext(c);
    printf("RENDERER=%s\nEXT lod_bias: %s\n", (const char *)glGetString(GL_RENDERER), strstr((const char *)glGetString(GL_EXTENSIONS), "GL_EXT_texture_lod_bias") ? "yes" : "NO");
    root = (unsigned long)c; addc(root);
    for (i = 0; i < 0x400; i++) { unsigned v; if (!rd32(root + i * 4, &v)) break; addc(v); }
    { int n1 = ncand; for (j = 0; j < n1; j++) for (k = 0; k < 0x200; k++) { unsigned v; if (!rd32(cand[j] + k * 4, &v)) break; addc(v); } }
    printf("candidates: %d\n", ncand);
    { static unsigned f1[MAXC], d1[MAXC], f2[MAXC], d2[MAXC]; int hits = 0;
      glDisable(GL_FOG); glDisable(GL_DEPTH_TEST); glFinish(); sample(fog0, dep0);
      glEnable(GL_FOG); glFinish(); sample(f1, d1);
      glEnable(GL_DEPTH_TEST); glFinish(); sample(f2, d2);
      glDisable(GL_FOG); glDisable(GL_DEPTH_TEST);
      for (i = 0; i < ncand; i++) {
          if (fog0[i] == 0xdeadbeef) continue;
          if ((fog0[i] ^ f1[i]) == 0x100u && (dep0[i] ^ d2[i]) == 0x1000000u) { printf("GLI base candidate 0x%lx\n", cand[i]); hits++; if (hits == 1) root = cand[i]; }
      }
      if (!hits) { printf("no gli found\n"); return 2; } }

    { static unsigned a[0x6000], b[0x6000]; GLuint tex[3]; unsigned char px[8*8*4]; const int N = 0x6000; int n;
      memset(px, 0x80, sizeof px);
      printf("MAX_TEXTURE_UNITS: "); { GLint mu = 0; glGetIntegerv(GL_MAX_TEXTURE_UNITS_ARB, &mu); printf("%d\n", (int)mu); }
      printf("EXT multitexture: %s\n", strstr((const char *)glGetString(GL_EXTENSIONS), "GL_ARB_multitexture") ? "yes" : "NO");
      glGenTextures(3, tex);
      #define SNAP(p) rd(root, p, N * 4)
      #define DIFF(label) { printf("== %s\n", label); rd(root, b, N * 4); for (n = 0; n < N; n++) if (a[n] != b[n]) printf("  gli+0x%04x: 0x%08x -> 0x%08x\n", n * 4, a[n], b[n]); memcpy(a, b, N * 4); }
      glActiveTextureARB(GL_TEXTURE0_ARB); glFinish(); SNAP(a);
      glEnable(GL_TEXTURE_2D); glFinish(); DIFF("unit0: enable TEXTURE_2D");
      glTexEnvi(GL_TEXTURE_ENV, GL_TEXTURE_ENV_MODE, GL_REPLACE); glFinish(); DIFF("unit0: env REPLACE");
      glTexEnvi(GL_TEXTURE_ENV, GL_TEXTURE_ENV_MODE, GL_MODULATE); glFinish(); DIFF("unit0: env MODULATE");
      glBindTexture(GL_TEXTURE_2D, tex[0]); glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, 8, 8, 0, GL_RGBA, GL_UNSIGNED_BYTE, px); glFinish(); DIFF("unit0: bind+upload tex A");
      glActiveTextureARB(GL_TEXTURE1_ARB); glFinish(); DIFF("select unit 1");
      glEnable(GL_TEXTURE_2D); glFinish(); DIFF("unit1: enable TEXTURE_2D");
      glTexEnvi(GL_TEXTURE_ENV, GL_TEXTURE_ENV_MODE, GL_REPLACE); glFinish(); DIFF("unit1: env REPLACE");
      glTexEnvi(GL_TEXTURE_ENV, GL_TEXTURE_ENV_MODE, GL_MODULATE); glFinish(); DIFF("unit1: env MODULATE");
      glBindTexture(GL_TEXTURE_2D, tex[1]); glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, 8, 8, 0, GL_RGBA, GL_UNSIGNED_BYTE, px); glFinish(); DIFF("unit1: bind+upload tex B");
      glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE); glFinish(); DIFF("unit1: wrap S clamp");
      glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST); glFinish(); DIFF("unit1: mag NEAREST");
      glDisable(GL_TEXTURE_2D); glFinish(); DIFF("unit1: disable TEXTURE_2D");
      glActiveTextureARB(GL_TEXTURE0_ARB); glFinish(); DIFF("select unit 0");
      glEnable(GL_LIGHTING); glFinish(); DIFF("enable LIGHTING");
      glLightModeli(GL_LIGHT_MODEL_COLOR_CONTROL, GL_SEPARATE_SPECULAR_COLOR); glFinish(); DIFF("LIGHT_MODEL_COLOR_CONTROL = SEPARATE_SPECULAR");
      glLightModeli(GL_LIGHT_MODEL_COLOR_CONTROL, GL_SINGLE_COLOR); glFinish(); DIFF("LIGHT_MODEL_COLOR_CONTROL = SINGLE");
      glDisable(GL_LIGHTING); glFinish(); DIFF("disable LIGHTING");
      printf("glError=0x%x\n", glGetError()); }

    /* vertex layout: draw one triangle with unique texcoords on both units, then look for those floats in memory reachable from the gli */
    { float u0s = 0.1234f, u0t = 0.2345f, u1s = 0.3456f, u1t = 0.4567f; unsigned t0s, t0t, t1s, t1t; int pass;
      memcpy(&t0s, &u0s, 4); memcpy(&t0t, &u0t, 4); memcpy(&t1s, &u1s, 4); memcpy(&t1t, &u1t, 4);
      glViewport(0, 0, 64, 64); glMatrixMode(GL_PROJECTION); glLoadIdentity(); glMatrixMode(GL_MODELVIEW); glLoadIdentity();
      glActiveTextureARB(GL_TEXTURE0_ARB); glEnable(GL_TEXTURE_2D); glActiveTextureARB(GL_TEXTURE1_ARB); glEnable(GL_TEXTURE_2D);
      glBegin(GL_TRIANGLES);
      glMultiTexCoord2fARB(GL_TEXTURE0_ARB, u0s, u0t); glMultiTexCoord2fARB(GL_TEXTURE1_ARB, u1s, u1t); glVertex2f(-0.5f, -0.5f);
      glMultiTexCoord2fARB(GL_TEXTURE0_ARB, u0s, u0t); glMultiTexCoord2fARB(GL_TEXTURE1_ARB, u1s, u1t); glVertex2f(0.5f, -0.5f);
      glMultiTexCoord2fARB(GL_TEXTURE0_ARB, u0s, u0t); glMultiTexCoord2fARB(GL_TEXTURE1_ARB, u1s, u1t); glVertex2f(0.0f, 0.5f);
      glEnd(); glFinish();
      for (pass = 0; pass < 1; pass++) { int j, k, hits = 0;
        for (j = 0; j < ncand; j++) { static unsigned blk[0x4000]; if (!rd(cand[j], blk, sizeof blk)) continue;
          for (k = 0; k < 0x4000; k++) if (blk[k] == t1s && hits < 40) { int m;
              printf("cand %lx +0x%x: unit1.s found; nearby:", cand[j], k * 4);
              for (m = -64; m <= 64; m++) if (k + m >= 0 && k + m < 0x4000) { unsigned v = blk[k + m]; const char *nm = v == t0s ? "U0S" : v == t0t ? "U0T" : v == t1s ? "U1S" : v == t1t ? "U1T" : 0; if (nm) printf(" %s@%+d", nm, m * 4); }
              printf("\n"); hits++; } } printf("vertex scan hits=%d\n", hits); } }
    return 0;
}

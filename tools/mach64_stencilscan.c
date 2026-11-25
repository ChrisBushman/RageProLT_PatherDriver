/* stencilscan (derived from mtexscan): recover GLEngine gli offsets for stencil + colour mask state.  Usage: cc -o mtexscan mach64_mtexscan.c -framework OpenGL */
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
    int attr[] = { kCGLPFAOffScreen, kCGLPFAColorSize, 32, kCGLPFADepthSize, 16, kCGLPFAStencilSize, 8, 0 };
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
      glFinish(); SNAP(a);
      glEnable(GL_STENCIL_TEST); glFinish(); DIFF("enable STENCIL_TEST");
      glStencilFunc(GL_EQUAL, 1, 0xff); glFinish(); DIFF("StencilFunc EQUAL ref1 mask ff");
      glStencilFunc(GL_ALWAYS, 1, 0xff); glFinish(); DIFF("StencilFunc ALWAYS ref1 mask ff");
      glStencilFunc(GL_EQUAL, 3, 0x0f); glFinish(); DIFF("StencilFunc EQUAL ref3 mask 0f");
      glStencilOp(GL_KEEP, GL_KEEP, GL_REPLACE); glFinish(); DIFF("StencilOp KEEP KEEP REPLACE");
      glStencilOp(GL_KEEP, GL_KEEP, GL_KEEP); glFinish(); DIFF("StencilOp KEEP KEEP KEEP");
      glStencilOp(GL_INCR, GL_DECR, GL_INVERT); glFinish(); DIFF("StencilOp INCR DECR INVERT");
      glStencilMask(0x0f); glFinish(); DIFF("StencilMask 0f");
      glClearStencil(5); glFinish(); DIFF("ClearStencil 5");
      glDisable(GL_STENCIL_TEST); glFinish(); DIFF("disable STENCIL_TEST");
      glColorMask(GL_FALSE, GL_FALSE, GL_FALSE, GL_FALSE); glFinish(); DIFF("ColorMask 0000");
      glColorMask(GL_TRUE, GL_TRUE, GL_TRUE, GL_TRUE); glFinish(); DIFF("ColorMask 1111");
      glDepthMask(GL_FALSE); glFinish(); DIFF("DepthMask off");
      glDepthMask(GL_TRUE); glFinish(); DIFF("DepthMask on");
      glEnable(GL_BLEND); glFinish(); DIFF("enable BLEND");
      glDisable(GL_BLEND); glFinish(); DIFF("disable BLEND");
      printf("glError=0x%x\n", glGetError()); }
    return 0;
}

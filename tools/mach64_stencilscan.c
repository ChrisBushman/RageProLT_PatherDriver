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

    { static unsigned a[0x9000], b[0x9000]; GLuint tex[3]; unsigned char px[8*8*4]; const int N = 0x9000; int n;
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
      glStencilFunc(GL_ALWAYS, 0x55, 0xff); glFinish(); DIFF("StencilFunc ALWAYS ref 0x55 mask ff");
      glStencilFunc(GL_LESS, 0x55, 0xff); glFinish(); DIFF("StencilFunc LESS ref 0x55 mask ff");
      glStencilFunc(GL_NOTEQUAL, 0x55, 0xff); glFinish(); DIFF("StencilFunc NOTEQUAL ref 0x55 mask ff");
      glStencilFunc(GL_NEVER, 0x55, 0xff); glFinish(); DIFF("StencilFunc NEVER");
      glStencilFunc(GL_GEQUAL, 0x55, 0xff); glFinish(); DIFF("StencilFunc GEQUAL");
      glStencilFunc(GL_GREATER, 0x55, 0xff); glFinish(); DIFF("StencilFunc GREATER");
      glStencilFunc(GL_LEQUAL, 0x55, 0xff); glFinish(); DIFF("StencilFunc LEQUAL");
      glStencilOp(GL_ZERO, GL_INCR_WRAP, GL_DECR_WRAP); glFinish(); DIFF("StencilOp ZERO INCR_WRAP DECR_WRAP");
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
      /* where does the stencil REFERENCE live?  snapshot every pointer target reachable from the context at ref 7 and ref 9 */
      { static unsigned *s1[MAXC], *s2[MAXC]; int ci; glEnable(GL_STENCIL_TEST); glStencilFunc(GL_EQUAL, 7, 0xff); glBegin(GL_TRIANGLES); glVertex2f(0,0); glVertex2f(.1f,0); glVertex2f(0,.1f); glEnd(); glFinish();
        for (ci = 0; ci < ncand; ci++) { s1[ci] = malloc(0x2000); if (!rd(cand[ci], s1[ci], 0x2000)) { free(s1[ci]); s1[ci] = 0; } }
        glStencilFunc(GL_EQUAL, 9, 0xff); glBegin(GL_TRIANGLES); glVertex2f(0,0); glVertex2f(.1f,0); glVertex2f(0,.1f); glEnd(); glFinish();
        for (ci = 0; ci < ncand; ci++) { if (!s1[ci]) continue; s2[ci] = malloc(0x2000); if (!rd(cand[ci], s2[ci], 0x2000)) continue;
            for (n = 0; n < 0x800; n++) if (s1[ci][n] != s2[ci][n]) printf("REF 7->9: base 0x%lx +0x%04x: 0x%08x -> 0x%08x%s\n", cand[ci], n * 4, s1[ci][n], s2[ci][n], cand[ci] == root ? "  (gli)" : ""); } }
      /* deeper: every pointer found in the gli (all 0x9000 words) and in the first 0x400 words of those targets */
      { static unsigned long c2[60000]; static unsigned *t1[60000]; int n2 = 0, ci, kk; unsigned gbuf[0x9000];
        rd(root, gbuf, sizeof gbuf);
        for (kk = 0; kk < 0x9000; kk++) { unsigned long q = gbuf[kk]; unsigned tmp; if (q > 0x10000 && !(q & 3) && q < 0xffff0000ul && rd32(q, &tmp) && n2 < 60000) c2[n2++] = q; }
        { int n3 = n2; for (ci = 0; ci < n3; ci++) { unsigned blk[0x400]; if (!rd(c2[ci], blk, sizeof blk)) continue; for (kk = 0; kk < 0x400; kk++) { unsigned long q = blk[kk]; unsigned tmp; if (q > 0x10000 && !(q & 3) && q < 0xffff0000ul && rd32(q, &tmp) && n2 < 60000) c2[n2++] = q; } } }
        glStencilFunc(GL_EQUAL, 7, 0xff); glBegin(GL_TRIANGLES); glVertex2f(0,0); glVertex2f(.1f,0); glVertex2f(0,.1f); glEnd(); glFinish();
        for (ci = 0; ci < n2; ci++) { t1[ci] = malloc(0x400); if (!rd(c2[ci], t1[ci], 0x400)) { free(t1[ci]); t1[ci] = 0; } }
        glStencilFunc(GL_EQUAL, 9, 0xff); glBegin(GL_TRIANGLES); glVertex2f(0,0); glVertex2f(.1f,0); glVertex2f(0,.1f); glEnd(); glFinish();
        for (ci = 0; ci < n2; ci++) { unsigned t2[0x100]; if (!t1[ci] || !rd(c2[ci], t2, 0x400)) continue;
            for (n = 0; n < 0x100; n++) if (t1[ci][n] != t2[n]) { unsigned o = t1[ci][n], w = t2[n];
                if (w == 9 || w == 0x09090909u || w == 0x09000000u || w == 0x00090000u || w == 0x41100000u || w == 0x3d121212u || (o == 7 && w == 9)) printf("REF2 7->9: 0x%lx +0x%x: 0x%08x -> 0x%08x\n", c2[ci], n * 4, o, w); } } }
      printf("glError=0x%x\n", glGetError()); }
    return 0;
}

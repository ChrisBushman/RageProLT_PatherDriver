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
    /* diff the gli (0..0x8000) while changing LOD bias */
    { static unsigned a[0x2000], b[0x2000]; float bias[3] = { 1.0f, 2.5f, -1.5f }; int n;
      glEnable(GL_TEXTURE_2D);
      glTexEnvf(GL_TEXTURE_FILTER_CONTROL_EXT, GL_TEXTURE_LOD_BIAS_EXT, 0.0f); glFinish();
      rd(root, a, sizeof a);
      for (n = 0; n < 3; n++) {
          glTexEnvf(GL_TEXTURE_FILTER_CONTROL_EXT, GL_TEXTURE_LOD_BIAS_EXT, bias[n]); glFinish();
          { GLfloat back = -99; glGetTexEnvfv(GL_TEXTURE_FILTER_CONTROL_EXT, GL_TEXTURE_LOD_BIAS_EXT, &back); printf("bias set %.2f read back %.2f glError=0x%x\n", bias[n], back, glGetError()); }
          rd(root, b, sizeof b);
          for (i = 0; i < 0x2000; i++) if (a[i] != b[i]) { float fa, fb; memcpy(&fa, &a[i], 4); memcpy(&fb, &b[i], 4); printf("  gli+0x%04x: 0x%08x -> 0x%08x (%g -> %g)\n", i * 4, a[i], b[i], fa, fb); }
      } }
    return 0;
}

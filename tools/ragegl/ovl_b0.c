/* tools/ragegl/ovl_b0.c - overlay plan step B0: the single write.  OVERLAY_VIDEO_KEY_CLR <- 0x005A5A5A, read back, restore.  Debug kext only (RAGE_GL_OVL_B0). */
#include "ragegl.h"
int main(void)
{
    ragegl_t c; uint32_t a = 0, b = 0, d = 0; kern_return_t kr;
    if (ragegl_open(&c)) { printf("cannot open the GL client\n"); return 99; }
    kr = RGL_CALL(RAGE_GL_OVL_B0, 0, 3, &a, &b, &d);
    if (kr) { printf("OVL_B0 failed 0x%x (0xe00002e2 = overlay not idle; needs a RAGE_DEBUG_ENGTEST kext)\n", (unsigned)kr); return 1; }
    printf("OVERLAY_VIDEO_KEY_CLR: before 0x%08x, wrote 0x005a5a5a, read back 0x%08x, after restore 0x%08x\n", a, b, d);
    printf("%s\n", (b == 0x005a5a5au && d == a) ? "B0 PASS: block 1 latches writes and the original value is restored" : "B0 FAIL: see values (block 1 may not latch, or the register has fewer implemented bits)");
    ragegl_close(c);
    return (b == 0x005a5a5au && d == a) ? 0 : 2;
}

/* tools/ragegl/ovl_dump.c - step A of docs/re-notes/overlay-scaler-plan.md: READ-ONLY dump of the overlay/scaler registers (register block 1), a few block-0 mode registers and the PCI
 * revision word, through the debug-build selector RAGE_GL_REG_DUMP.  Writes nothing.  Needs a kext built with RAGE_DEBUG_ENGTEST=1 (redeploy.sh). */
#include "ragegl.h"
#include <stdlib.h>
static const char *name1(uint32_t i)
{
    switch (i) { case 0: return "OVERLAY_Y_X_START"; case 1: return "OVERLAY_Y_X_END"; case 2: return "OVERLAY_VIDEO_KEY_CLR"; case 3: return "OVERLAY_VIDEO_KEY_MSK"; case 4: return "OVERLAY_GRAPHICS_KEY_CLR";
        case 5: return "OVERLAY_GRAPHICS_KEY_MSK"; case 6: return "OVERLAY_KEY_CNTL"; case 8: return "OVERLAY_SCALE_INC"; case 9: return "OVERLAY_SCALE_CNTL"; case 0xa: return "SCALER_HEIGHT_WIDTH";
        case 0xb: return "SCALER_TEST"; case 0xc: return "SCALER_THRESHOLD"; case 0xd: return "SCALER_BUF0_OFFSET"; case 0xe: return "SCALER_BUF1_OFFSET"; case 0xf: return "SCALER_BUF_PITCH";
        case 0x10: return "CAPTURE_START_END"; case 0x11: return "CAPTURE_X_WIDTH"; case 0x12: return "VIDEO_FORMAT"; case 0x13: return "VBI_START_END"; case 0x14: return "CAPTURE_CONFIG";
        case 0x54: return "SCALER_COLOUR_CNTL"; case 0x55: return "SCALER_H_COEFF0"; case 0x56: return "SCALER_H_COEFF1"; case 0x57: return "SCALER_H_COEFF2"; case 0x58: return "SCALER_H_COEFF3"; case 0x59: return "SCALER_H_COEFF4"; }
    return "(reserved/unnamed)";
}
static const char *name0(uint32_t o)
{ switch (o) { case 0x00: return "CRTC_H_TOTAL_DISP"; case 0x08: return "CRTC_V_TOTAL_DISP"; case 0x14: return "CRTC_OFF_PITCH"; case 0x1c: return "CRTC_GEN_CNTL"; case 0xb0: return "MEM_CNTL"; case 0xe0: return "CONFIG_CHIP_ID"; } return "?"; }
int main(void)
{
    ragegl_t c; uint32_t w[96], i; kern_return_t kr; uint32_t gen, dw, dh, pitch, bpp, base;
    if (ragegl_open(&c)) { printf("cannot open the GL client\n"); return 99; }
    if (!ragegl_display(c, &gen, &dw, &dh, &pitch, &bpp, &base)) printf("display: %ux%u, %u bytes/pixel, pitch %u px\n", dw, dh, bpp, pitch);
    memset(w, 0, sizeof w);
    kr = RGL_CALL(RAGE_GL_REG_DUMP, 2, 0, (uint32_t)(uintptr_t)w, sizeof w);
    if (kr) { printf("REG_DUMP failed 0x%x (needs a RAGE_DEBUG_ENGTEST kext)\n", (unsigned)kr); return 1; }
    for (i = 0; i < 35; i++) {                       /* 28 block-1 + 6 block-0 + PCI word: the kernel's fixed list */
        uint32_t tag = w[2 * i], v = w[2 * i + 1];
        if (tag == 0xf00) printf("PCI config 0x08 (class<<8 | revision) = 0x%08x   revision 0x%02x\n", v, v & 0xff);
        else if (tag & 0x100) printf("block1[0x%02x] off 0x%03x  %-26s = 0x%08x\n", tag & 0xff, (tag & 0xff) * 4, name1(tag & 0xff), v);
        else printf("block0[0x%02x]            %-26s = 0x%08x\n", tag, name0(tag), v);
    }
    ragegl_close(c);
    return 0;
}

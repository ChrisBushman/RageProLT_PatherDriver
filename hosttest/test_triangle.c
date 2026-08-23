/*
 * test_triangle.c - host-buildable verification of the portable emission core.
 *
 * Builds the state block + one textured triangle exactly as the GLDriver
 * plugin would, then dumps the resulting (reg, value) command stream and
 * checks invariants.  Runs on any host (x86_64/arm64) - no hardware, no
 * IOKit - so the register-encoding and emission logic can be validated
 * before it is ever cross-compiled for PPC/Panther.
 */
#include <stdio.h>
#include <string.h>
#include "../common/mach64_emit.h"
#include "../common/mach64_regs.h"

/* Reverse ADRINDEX() so we can print the original register offset. */
static uint32_t deindex(uint32_t idx)
{
    if (idx & 0x0100)                 /* block1: (r>>2)|0x100 */
        return (idx & 0xff) << 2;
    return (idx << 2) + MACH64_DWMREG0; /* block0 */
}

static const char *regname(uint32_t reg)
{
    switch (reg) {
    case MACH64_Z_OFF_PITCH:    return "Z_OFF_PITCH";
    case MACH64_Z_CNTL:         return "Z_CNTL";
    case MACH64_ALPHA_TST_CNTL: return "ALPHA_TST_CNTL";
    case MACH64_SCALE_3D_CNTL:  return "SCALE_3D_CNTL";
    case MACH64_SETUP_CNTL:     return "SETUP_CNTL";
    case MACH64_TEX_SIZE_PITCH: return "TEX_SIZE_PITCH";
    case MACH64_TEX_CNTL:       return "TEX_CNTL";
    case MACH64_DP_PIX_WIDTH:   return "DP_PIX_WIDTH";
    case MACH64_DST_OFF_PITCH:  return "DST_OFF_PITCH";
    }
    /* vertex slot fields */
    for (int s = 0; s < 3; s++) {
        uint32_t b = MACH64_VERTEX_SLOT(s);
        if (reg == b + MACH64_V_S)    { static char t[16]; snprintf(t,16,"V%d_S",s+1);    return t; }
        if (reg == b + MACH64_V_T)    { static char t[16]; snprintf(t,16,"V%d_T",s+1);    return t; }
        if (reg == b + MACH64_V_W)    { static char t[16]; snprintf(t,16,"V%d_W",s+1);    return t; }
        if (reg == b + MACH64_V_Z)    { static char t[16]; snprintf(t,16,"V%d_Z",s+1);    return t; }
        if (reg == b + MACH64_V_ARGB) { static char t[16]; snprintf(t,16,"V%d_ARGB",s+1); return t; }
        if (reg == b + MACH64_V_X_Y)  { static char t[16]; snprintf(t,16,"V%d_X_Y",s+1);  return t; }
    }
    return "?";
}

int main(void)
{
    uint32_t storage[256];
    m64_cmdbuf cb;
    m64_cb_init(&cb, storage, 256);

    /* A plausible state: smooth-shaded, Z-test LEQUAL, textured, dithered. */
    m64_state st;
    memset(&st, 0, sizeof st);
    st.z_cntl        = MACH64_Z_EN | MACH64_Z_TEST_LEQUAL;
    st.scale_3d_cntl = MACH64_SCALE_PIX_EXPAND_DYNAMIC_RANGE | MACH64_DITHER_EN;
    st.setup_cntl    = 0;                /* smooth shade                    */
    st.dst_off_pitch = (0x0a /*pitch*/ << 22) | 0x00000; /* back buffer     */
    st.z_off_pitch   = (0x0a << 22) | 0x40000;
    st.dp_pix_width  = 0x00060606;       /* 16bpp RGB565 typical            */

    /* Post-transform triangle (values illustrative). */
    m64_vertex v0 = { .x_y=(50<<16)|50,  .z=0, .w=0x10000, .s=0,       .t=0,       .argb=0xffff0000 };
    m64_vertex v1 = { .x_y=(200<<16)|60, .z=0, .w=0x10000, .s=0x10000, .t=0,       .argb=0xff00ff00 };
    m64_vertex v2 = { .x_y=(120<<16)|200,.z=0, .w=0x10000, .s=0x8000,  .t=0x10000, .argb=0xff0000ff };

    m64_emit_state(&cb, &st);
    size_t state_dwords = cb.n;
    m64_emit_triangle(&cb, &v0, &v1, &v2);

    /* Dump the stream. */
    printf("command stream: %zu dwords (%zu register writes)\n\n",
           cb.n, cb.n / 2);
    for (size_t i = 0; i < cb.n; i += 2) {
        uint32_t reg = deindex(cb.buf[i]);
        printf("  [%2zu] ADRINDEX=0x%03x  reg=0x%04x %-14s = 0x%08x\n",
               i/2, cb.buf[i], reg, regname(reg), cb.buf[i+1]);
    }

    /* ---- invariants -------------------------------------------------- */
    int fail = 0;
    if (cb.overflow) { printf("\nFAIL: buffer overflow\n"); fail = 1; }

    /* state = 9 regs, triangle = 3 vtx x 6 regs = 18; total 27 pairs. */
    if (cb.n / 2 != 9 + 18) { printf("\nFAIL: expected 27 writes, got %zu\n", cb.n/2); fail = 1; }
    if (state_dwords / 2 != 9) { printf("\nFAIL: state != 9 writes\n"); fail = 1; }

    /* The very last write MUST be VERTEX_3_X_Y (the draw trigger). */
    uint32_t last_reg = deindex(cb.buf[cb.n - 2]);
    if (last_reg != MACH64_VERTEX_SLOT(2) + MACH64_V_X_Y) {
        printf("\nFAIL: last write is 0x%04x, expected V3_X_Y 0x%04x\n",
               last_reg, MACH64_VERTEX_SLOT(2) + MACH64_V_X_Y);
        fail = 1;
    }

    /* Spot-check the ADRINDEX encoding of a known register. */
    if (MACH64_ADRINDEX(MACH64_SCALE_3D_CNTL) != ((0x05fc - 0x400) >> 2)) {
        printf("\nFAIL: ADRINDEX(SCALE_3D_CNTL) wrong\n"); fail = 1;
    }
    if (MACH64_ADRINDEX(MACH64_SETUP_CNTL) != ((0x0304 >> 2) | 0x0100)) {
        printf("\nFAIL: ADRINDEX(SETUP_CNTL block1) wrong\n"); fail = 1;
    }

    printf("\n%s\n", fail ? "*** TESTS FAILED ***" : "all invariants passed");
    return fail;
}

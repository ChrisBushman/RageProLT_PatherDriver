/*
 * test_triangle.c - host-buildable verification of the portable emission core.
 *
 * Builds the state block + one gouraud triangle exactly as the GLDriver plugin
 * would (mach64_emit.c, aligned to the proven ring path), decodes the resulting
 * register-write PACKET stream, verifies invariants, and checks the little-
 * endian writeout.  Runs on any host (no hardware, no IOKit) so the encoding and
 * emission logic can be validated before it is cross-compiled for PPC/Panther.
 */
#include <stdio.h>
#include <string.h>
#include "../common/mach64_emit.h"
#include "../common/mach64_regs.h"

/* Reverse ADRINDEX() so we can print the original register offset. */
static uint32_t deindex(uint32_t idx)
{
    if (idx & 0x0100)                     /* block1: (r>>2)|0x100 */
        return (idx & 0xff) << 2;
    return (idx << 2) + MACH64_DWMREG0;   /* block0: (r-0x400)>>2 */
}

static const char *regname(uint32_t reg)
{
    switch (reg) {
    case MACH64_SCALE_3D_CNTL:   return "SCALE_3D_CNTL";
    case MACH64_SETUP_CNTL:      return "SETUP_CNTL";
    case MACH64_Z_CNTL:          return "Z_CNTL";
    case MACH64_DP_PIX_WIDTH:    return "DP_PIX_WIDTH";
    case MACH64_DST_OFF_PITCH:   return "DST_OFF_PITCH";
    case MACH64_SC_LEFT_RIGHT:   return "SC_LEFT_RIGHT";
    case MACH64_SC_TOP_BOTTOM:   return "SC_TOP_BOTTOM";
    case MACH64_DP_WRITE_MASK:   return "DP_WRITE_MASK";
    case MACH64_CLR_CMP_CNTL:    return "CLR_CMP_CNTL";
    case MACH64_DP_MIX:          return "DP_MIX";
    case MACH64_DP_SRC:          return "DP_SRC";
    case MACH64_ONE_OVER_AREA_UC:return "ONE_OVER_AREA_UC";
    case MACH64_SRC_CNTL:        return "SRC_CNTL(term)";
    }
    for (int s = 0; s < 3; s++) {
        uint32_t b = MACH64_VERTEX_SLOT(s);
        if (reg == b + MACH64_V_S)         { static char t[16]; snprintf(t,16,"V%d_S",s+1);    return t; }
        if (reg == b + MACH64_V_T)         { static char t[16]; snprintf(t,16,"V%d_T",s+1);    return t; }
        if (reg == b + MACH64_V_W)         { static char t[16]; snprintf(t,16,"V%d_W",s+1);    return t; }
        if (reg == b + MACH64_V_SPEC_ARGB) { static char t[16]; snprintf(t,16,"V%d_SPEC",s+1); return t; }
        if (reg == b + MACH64_V_Z)         { static char t[16]; snprintf(t,16,"V%d_Z",s+1);    return t; }
        if (reg == b + MACH64_V_ARGB)      { static char t[16]; snprintf(t,16,"V%d_ARGB",s+1); return t; }
        if (reg == b + MACH64_V_X_Y)       { static char t[16]; snprintf(t,16,"V%d_X_Y",s+1);  return t; }
    }
    return "?";
}

/* pack an (x,y) pixel coordinate the way the setup engine wants it */
static int32_t xy(int x, int y) { return ((x*4) << 16) | ((y*4) & 0xffff); }

int main(void)
{
    uint32_t storage[256];
    m64_cmdbuf cb;
    m64_state st;
    int npackets = 0, nregs = 0, tri_ok, fail = 0;
    size_t i;

    m64_cb_init(&cb, storage, 256);

    /* The proven opaque untextured gouraud state (3d-triangle.md). */
    memset(&st, 0, sizeof st);
    st.scale_3d_cntl = MACH64_SCALE_3D_CNTL_SOLID;
    st.setup_cntl    = MACH64_SETUP_CNTL_SHADE;      /* 0, not 0x40 */
    st.z_cntl        = 0;
    st.dp_pix_width  = 0x60060666;
    st.dst_off_pitch = ((128/8) << 22) | (0x600000 >> 3);
    st.sc_left_right = 0x1fff0000;
    st.sc_top_bottom = 0x3fff0000;
    st.dp_write_mask = 0xffffffff;
    st.clr_cmp_cntl  = 0;
    st.dp_mix        = MACH64_DP_MIX_SS;
    st.dp_src        = MACH64_DP_SRC_3D;

    /* Post-transform RGB triangle (W=1.0f = 0x3f800000). */
    m64_vertex v0 = { xy(20,16),  0, 0x3f800000, 0,0, 0, 0xffff0000 };
    m64_vertex v1 = { xy(110,44), 0, 0x3f800000, 0,0, 0, 0xff00ff00 };
    m64_vertex v2 = { xy(50,116), 0, 0x3f800000, 0,0, 0, 0xff0000ff };

    m64_emit_state(&cb, &st);
    tri_ok = m64_emit_triangle(&cb, &v0, &v1, &v2);
    m64_emit_terminator(&cb);

    /* Decode the packet stream. */
    printf("command stream: %zu dwords\n\n", cb.n);
    i = 0;
    while (i < cb.n) {
        uint32_t hdr = cb.buf[i++];
        int count = ((hdr >> 16) & 0xff) + 1;
        uint32_t reg = deindex(hdr & 0xffff);
        int k;
        npackets++;
        printf("  packet: ADRINDEX=0x%03x  %d reg(s) from 0x%04x %-16s\n",
               hdr & 0xffff, count, reg, regname(reg));
        for (k = 0; k < count && i < cb.n; k++, i++) {
            printf("      0x%04x %-16s = 0x%08x\n",
                   reg + k*4, regname(reg + k*4), cb.buf[i]);
            nregs++;
        }
    }

    /* ---- invariants -------------------------------------------------- */
    if (cb.overflow) { printf("\nFAIL: overflow\n"); fail = 1; }
    if (!tri_ok)     { printf("\nFAIL: triangle reported degenerate\n"); fail = 1; }

    /* 13 state + 3 vertices(7 each) + 1 trigger + 1 terminator = 18 packets;
     * 13 + 21 + 1 + 1 = 36 registers.  (State gained ALPHA_TST_CNTL 0x550 with the
     * GL_ALPHA_TEST work and TEX_CNTL 0x774 -- cleared for untextured draws, commit 23f5424 --
     * the expectation was stale at 17/35 and failed before the 2D work (Nov 26 fix);
     * DP_FOG_CLR is emitted only when fog is armed, so it does not count here --
     * dp_fog_clr==0 in this fixture.) */
    if (npackets != 18) { printf("\nFAIL: expected 18 packets, got %d\n", npackets); fail = 1; }
    if (nregs   != 36)  { printf("\nFAIL: expected 36 register writes, got %d\n", nregs); fail = 1; }

    /* The terminator MUST be last and target SRC_CNTL=0 (flushes the ring). */
    {
        uint32_t last_hdr = cb.buf[cb.n - 2];
        if (deindex(last_hdr & 0xffff) != MACH64_SRC_CNTL || cb.buf[cb.n-1] != 0) {
            printf("\nFAIL: stream must end with SRC_CNTL=0 terminator\n"); fail = 1;
        }
    }

    /* ADRINDEX spot checks (block0 vs block1). */
    if (MACH64_ADRINDEX(MACH64_SCALE_3D_CNTL) != ((0x05fc - 0x400) >> 2)) {
        printf("\nFAIL: ADRINDEX(SCALE_3D_CNTL) wrong\n"); fail = 1;
    }
    if (MACH64_ADRINDEX(MACH64_VERTEX_SLOT(0)) != ((0x0240 >> 2) | 0x0100)) {
        printf("\nFAIL: ADRINDEX(VERTEX_1 block1) wrong\n"); fail = 1;
    }

    /* Little-endian writeout: first dword of the buffer, byte-reversed. */
    {
        unsigned char le[256*4];
        size_t bytes = m64_cb_write_le(&cb, le);
        uint32_t d0 = cb.buf[0];
        if (bytes != cb.n * 4) { printf("\nFAIL: LE byte count\n"); fail = 1; }
        if (le[0] != (d0 & 0xff) || le[3] != ((d0 >> 24) & 0xff)) {
            printf("\nFAIL: LE writeout not little-endian\n"); fail = 1;
        }
    }

    printf("\n%s\n", fail ? "*** TESTS FAILED ***" : "all invariants passed");
    return fail;
}

/*
 * mach64_emit.c - portable mach64 3D command emission (see mach64_emit.h).
 * Aligned to the proven ring path (mach64_ringtest / mach64_gouraud).
 */
#include "mach64_emit.h"
#include "mach64_regs.h"
#include <string.h>

void m64_cb_init(m64_cmdbuf *cb, uint32_t *storage, size_t cap_dwords)
{
    cb->buf = storage;
    cb->cap = cap_dwords;
    cb->n = 0;
    cb->overflow = 0;
}

void m64_reg(m64_cmdbuf *cb, uint32_t reg, uint32_t val)
{
    if (cb->n + 2 > cb->cap) { cb->overflow = 1; return; }
    cb->buf[cb->n++] = MACH64_ADRINDEX(reg);   /* count 0 -> 1 register */
    cb->buf[cb->n++] = val;
}

void m64_regs(m64_cmdbuf *cb, uint32_t reg, const uint32_t *vals, int n)
{
    int i;
    if (n <= 0) return;
    if (cb->n + 1 + (size_t)n > cb->cap) { cb->overflow = 1; return; }
    cb->buf[cb->n++] = MACH64_ADRINDEX(reg) | ((uint32_t)(n - 1) << 16);
    for (i = 0; i < n; i++) cb->buf[cb->n++] = vals[i];
}

void m64_emit_state(m64_cmdbuf *cb, const m64_state *st)
{
    /* Single-register writes are fine for state (proven in mach64_ringtest).
     * Textured draws override SCALE_3D_CNTL with the FCN_TEXTURE value and emit
     * the three texture registers; untextured draws keep the proven shade path. */
    if (st->textured) {
        /* The base-level texel offset goes into TEX_[maxLog2]_OFF, NOT TEX_0_OFF:
         * the mach64 has one offset register per mip level and the LARGEST level
         * uses the register matching its log2 size.  reg = TEX_0_OFF + maxLog2*4,
         * and maxLog2 lives in TEX_SIZE_PITCH bits [7:4] -> (sp & 0xf0) >> 2 is
         * the byte offset from TEX_0_OFF (ref drm_mach64_state.c mach64_emit_state). */
        if (st->tex_mip) {
            /* One offset register per mip level: TEX_r_OFF for r in [0,maxlog2]. */
            int r;
            for (r = 0; r <= st->tex_maxlog2 && r < 12; r++)
                m64_reg(cb, MACH64_TEX_0_OFF + (uint32_t)r * 4u, st->tex_mip_off[r]);
        } else {
            uint32_t tex_reg = MACH64_TEX_0_OFF + ((st->tex_size_pitch & 0xf0) >> 2);
            m64_reg(cb, tex_reg,           st->tex_0_off);
        }
        m64_reg(cb, MACH64_TEX_SIZE_PITCH, st->tex_size_pitch);
        /* Force a texture-cache flush: write TEX_CNTL with the flush bit CLEAR
         * then SET, so the 0->1 edge fires a fresh fetch every textured draw
         * (a constantly-latched flush bit gives no edge and can serve stale
         * texels).  TEX_CACHE_FLUSH = bit 23. */
        m64_reg(cb, MACH64_TEX_CNTL,       st->tex_cntl & ~(1u << 23));
        m64_reg(cb, MACH64_TEX_CNTL,       st->tex_cntl |  (1u << 23));
        m64_reg(cb, MACH64_SCALE_3D_CNTL,  st->tex_scale_3d);
        /* Route the TEXEL alpha as the pixel "source alpha" that framebuffer
         * blending (SCALE_3D_CNTL ALPHA_BLEND_SRC/DST) uses -- otherwise As is the
         * opaque interpolated vertex alpha and a translucent texture never blends.
         * ALPHA_TEST_ALWAYS | ALPHA_DST_SRCALPHA | ALPHA_TST_SRC_TEXEL (ref default
         * mach64_init_context; test itself stays disabled, EN=0). */
        m64_reg(cb, MACH64_ALPHA_TST_CNTL, (7u << 4) | (4u << 8) | (0u << 12));
    } else {
        m64_reg(cb, MACH64_SCALE_3D_CNTL,  st->scale_3d_cntl);
    }
    m64_reg(cb, MACH64_SETUP_CNTL,     st->setup_cntl);
    if (st->z_off_pitch)               /* only bind a Z buffer when one exists */
        m64_reg(cb, MACH64_Z_OFF_PITCH, st->z_off_pitch);
    m64_reg(cb, MACH64_Z_CNTL,         st->z_cntl);
    m64_reg(cb, MACH64_DP_PIX_WIDTH,   st->dp_pix_width);
    m64_reg(cb, MACH64_DST_OFF_PITCH,  st->dst_off_pitch);
    m64_reg(cb, MACH64_SC_LEFT_RIGHT,  st->sc_left_right);
    m64_reg(cb, MACH64_SC_TOP_BOTTOM,  st->sc_top_bottom);
    m64_reg(cb, MACH64_DP_WRITE_MASK,  st->dp_write_mask);
    m64_reg(cb, MACH64_CLR_CMP_CNTL,   st->clr_cmp_cntl);
    m64_reg(cb, MACH64_DP_MIX,         st->dp_mix);
    m64_reg(cb, MACH64_DP_SRC,         st->dp_src);
}

void m64_emit_vertex(m64_cmdbuf *cb, int slot, const m64_vertex *v)
{
    /* One 7-register packet {S,T,W,SPEC,Z,ARGB,X_Y}.  Writing the whole block
     * as a burst (not individual writes) is what latches per-vertex ARGB into
     * the interpolator -- individual writes give flat/white (3d-triangle.md). */
    uint32_t vals[7];
    vals[0] = (uint32_t)v->s;
    vals[1] = (uint32_t)v->t;
    vals[2] = (uint32_t)v->w;
    vals[3] = v->spec_argb;
    vals[4] = (uint32_t)v->z;
    vals[5] = v->argb;
    vals[6] = (uint32_t)v->x_y;
    m64_regs(cb, MACH64_VERTEX_SLOT(slot), vals, 7);
}

/* unpack a signed *4-fixed coordinate out of a packed X_Y */
static int m64_unpack_x(int32_t xy) { return (int)(int16_t)((xy >> 16) & 0xffff); }
static int m64_unpack_y(int32_t xy) { return (int)(int16_t)(xy & 0xffff); }

int m64_emit_triangle(m64_cmdbuf *cb, const m64_vertex *v0,
                      const m64_vertex *v1, const m64_vertex *v2)
{
    int x0 = m64_unpack_x(v0->x_y), y0 = m64_unpack_y(v0->x_y);
    int x1 = m64_unpack_x(v1->x_y), y1 = m64_unpack_y(v1->x_y);
    int x2 = m64_unpack_x(v2->x_y), y2 = m64_unpack_y(v2->x_y);
    /* coords are *4, so (1/16) converts the cross product back to pixel^2 */
    double area = (1.0/16.0) * ((double)(x0-x2)*(y1-y2) - (double)(y0-y2)*(x1-x2));
    float ooa; uint32_t oob;

    if (area == 0.0) return 0;                 /* degenerate: drop */
    ooa = (float)(1.0 / area);
    memcpy(&oob, &ooa, 4);

    m64_emit_vertex(cb, 0, v0);
    m64_emit_vertex(cb, 1, v1);
    m64_emit_vertex(cb, 2, v2);
    m64_reg(cb, MACH64_ONE_OVER_AREA_UC, oob);  /* fires the setup engine */
    return 1;
}

void m64_emit_terminator(m64_cmdbuf *cb)
{
    m64_reg(cb, MACH64_SRC_CNTL, 0);
}

size_t m64_cb_write_le(const m64_cmdbuf *cb, void *dest)
{
    unsigned char *d = (unsigned char *)dest;
    size_t i;
    for (i = 0; i < cb->n; i++) {
        uint32_t v = cb->buf[i];
        d[i*4 + 0] = (unsigned char)(v      );   /* little-endian byte order */
        d[i*4 + 1] = (unsigned char)(v >>  8);
        d[i*4 + 2] = (unsigned char)(v >> 16);
        d[i*4 + 3] = (unsigned char)(v >> 24);
    }
    return cb->n * 4;
}

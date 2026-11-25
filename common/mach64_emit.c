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
    cb->multitex = 0;
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
         * the byte offset from TEX_0_OFF (ref drm_mach64_state.c mach64_emit_state).
         *
         * Hardware mipmapping: see docs/re-notes/win98-mipmap.md.  The 2026-08-27 attempts (black / hang) lacked SETUP_CNTL.LOG_MAX_INC_ADJ
         * (bit 6); with it set, per-level TEX_r_OFF writes + MIP_MAP_DISABLE clear work (hardware-verified).  The single-register DRI form is
         * used when tex_mip == 0. */
        if (st->tex_mip) {
            /* One offset register per mip level: TEX_r_OFF for r in [0,maxlog2]. */
            int r;
            for (r = 0; r <= 10; r++)                       /* all of TEX_0_OFF..TEX_10_OFF: none left stale */
                m64_reg(cb, MACH64_TEX_0_OFF + (uint32_t)r * 4u, st->tex_mip_off[r]);
        } else {
            uint32_t tex_reg = MACH64_TEX_0_OFF + ((st->tex_size_pitch & 0xf0) >> 2);
            m64_reg(cb, tex_reg,           st->tex_0_off);
        }
        if (st->multitex && st->tex2_size_pitch) m64_reg(cb, MACH64_SECONDARY_TEX_OFF, st->tex2_off);
        m64_reg(cb, MACH64_TEX_SIZE_PITCH, st->tex_size_pitch | (st->multitex ? st->tex2_size_pitch : 0u));
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
        m64_reg(cb, MACH64_ALPHA_TST_CNTL, st->alpha_tst_cntl);
    } else {
        /* TEX_CNTL is only written by textured draws, and a two-texture draw leaves its secondary-unit bits set: an untextured, hardware-blended draw issued
         * afterwards (in this process or the next one that opens the chip) then composites wrongly (found by bisecting gl_probe runs; clearing TEX_CNTL fixes it). */
        m64_reg(cb, MACH64_TEX_CNTL,       0);
        m64_reg(cb, MACH64_SCALE_3D_CNTL,  st->scale_3d_cntl);
        /* Same alpha pipe as the textured path so UNTEXTURED blending works: the
         * blend As is the interpolated vertex alpha (enabled by ALPHA_FOG_EN_ALPHA
         * in scale_3d_cntl).  ALPHA_TEST_ALWAYS keeps the alpha test disabled.
         * (Reference mach64_init_context uses this same value for all draws.) */
        m64_reg(cb, MACH64_ALPHA_TST_CNTL, st->alpha_tst_cntl);
    }
    m64_reg(cb, MACH64_SETUP_CNTL,     st->setup_cntl);
    if (st->z_off_pitch)               /* only bind a Z buffer when one exists */
        m64_reg(cb, MACH64_Z_OFF_PITCH, st->z_off_pitch);
    m64_reg(cb, MACH64_Z_CNTL,         st->z_cntl);
    {   uint32_t dpw = st->dp_pix_width;
        if (st->textured) {
            if (st->tex_fmt)  dpw = (dpw & ~0xf0000000u) | (st->tex_fmt << 28);
            if (st->multitex && st->tex2_fmt) dpw = (dpw & ~0xf0u) | (st->tex2_fmt << 4);
        }
        m64_reg(cb, MACH64_DP_PIX_WIDTH, dpw);
    }
    m64_reg(cb, MACH64_DST_OFF_PITCH,  st->dst_off_pitch);
    m64_reg(cb, MACH64_SC_LEFT_RIGHT,  st->sc_left_right);
    m64_reg(cb, MACH64_SC_TOP_BOTTOM,  st->sc_top_bottom);
    m64_reg(cb, MACH64_DP_WRITE_MASK,  st->dp_write_mask);
    m64_reg(cb, MACH64_CLR_CMP_CNTL,   st->clr_cmp_cntl);
    m64_reg(cb, MACH64_DP_MIX,         st->dp_mix);
    m64_reg(cb, MACH64_DP_SRC,         st->dp_src);
    /* GL_POLYGON_STIPPLE: the 8x8 pattern that DP_SRC's MONO_SRC_PATTERN samples.  Only
     * emitted when armed (dp_src/dp_mix already carry the stipple datapath in that case). */
    if (st->stipple) {
        m64_reg(cb, MACH64_PAT_REG0, st->pat_reg0);
        m64_reg(cb, MACH64_PAT_REG1, st->pat_reg1);
    }
    /* GL_FOG blend colour.  DP_FOG_CLR aliases DP_FRGD_CLR (0x6c4, the 2D-fill
     * foreground), so emit it ONLY when fog is armed -- a plain 3D draw leaves it
     * alone, and the 2D engine re-sets it for its own fills. */
    if (st->dp_fog_clr)
        m64_reg(cb, MACH64_DP_FRGD_CLR, st->dp_fog_clr);
}

/* SECONDARY_S/T/W of vertices 1,2,3 (our slots 0,1,2): not contiguous with the slot blocks */
static const uint32_t m64_sec_reg[3] = { 0x0328u, 0x0334u, 0x02a0u };

void m64_emit_vertex(m64_cmdbuf *cb, int slot, const m64_vertex *v)
{
    /* Secondary texture coordinates first: the X_Y write at the end of the main packet latches the slot (see drm mach64 tris). */
    if (cb->multitex) {
        uint32_t sv[3]; sv[0] = (uint32_t)v->s2; sv[1] = (uint32_t)v->t2; sv[2] = (uint32_t)v->w2;
        m64_regs(cb, m64_sec_reg[slot], sv, 3);
    }
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

/* One vertex's packets written straight into the command stream (no per-vertex call, copy or capacity check -- the caller reserved room):
 * optional SECONDARY_S/T/W packet first, then the 7-register {S,T,W,SPEC,Z,ARGB,X_Y} burst (X_Y last latches the slot). */
static inline uint32_t *m64_put_vertex(uint32_t *d, int slot, const m64_vertex *v, int multitex)
{
    if (multitex) {
        *d++ = MACH64_ADRINDEX(m64_sec_reg[slot]) | (2u << 16);
        *d++ = (uint32_t)v->s2; *d++ = (uint32_t)v->t2; *d++ = (uint32_t)v->w2;
    }
    *d++ = MACH64_ADRINDEX(MACH64_VERTEX_SLOT(slot)) | (6u << 16);
    *d++ = (uint32_t)v->s; *d++ = (uint32_t)v->t; *d++ = (uint32_t)v->w; *d++ = v->spec_argb;
    *d++ = (uint32_t)v->z; *d++ = v->argb; *d++ = (uint32_t)v->x_y;
    return d;
}

int m64_emit_triangle(m64_cmdbuf *cb, const m64_vertex *v0,
                      const m64_vertex *v1, const m64_vertex *v2)
{
    int x0 = m64_unpack_x(v0->x_y), y0 = m64_unpack_y(v0->x_y);
    int x1 = m64_unpack_x(v1->x_y), y1 = m64_unpack_y(v1->x_y);
    int x2 = m64_unpack_x(v2->x_y), y2 = m64_unpack_y(v2->x_y);
    int dxa = x0 - x2, dyb = y1 - y2, dya = y0 - y2, dxb = x1 - x2;
    float ooa; uint32_t oob;
    uint32_t *d; int mt;

    /* Signed area * 16 (coords are *4, so the cross product is 16x the pixel^2 area).  Differences fit 17 bits, so when each is within +-32767 the
     * two products and their difference fit an int: all-integer, and the reciprocal is ONE single-precision divide (the G3's double fdiv is ~2x
     * slower).  Larger spans use a 64-bit integer cross product (no double anywhere). */
    if ((unsigned)(dxa + 32767) <= 65534u && (unsigned)(dyb + 32767) <= 65534u &&
        (unsigned)(dya + 32767) <= 65534u && (unsigned)(dxb + 32767) <= 65534u) {
        int cross = dxa * dyb - dya * dxb;
        if (cross == 0) return 0;                 /* degenerate: drop */
        if (cross < 0) { const m64_vertex *tmp = v1; v1 = v2; v2 = tmp; cross = -cross; }   /* NORMALIZE WINDING (see below) */
        ooa = 16.0f / (float)cross;               /* exact int->float up to 2^24; beyond that one rounding (the engine's reciprocal is a float anyway) */
    } else {                                      /* huge span (off-screen geometry): exact 64-bit cross product, still one single-precision divide */
        long long cross = (long long)dxa * dyb - (long long)dya * dxb;
        if (cross == 0) return 0;                 /* degenerate: drop */
        if (cross < 0) { const m64_vertex *tmp = v1; v1 = v2; v2 = tmp; cross = -cross; }
        ooa = 16.0f / (float)cross;
    }
    /* NORMALIZE WINDING: the mach64 setup engine walks spans by the SIGNED area.  A
     * negative area (opposite winding) rasterizes inside-out -> ZERO coverage (the
     * triangle is invisible, not mis-coloured).  GL culling is done in software upstream
     * (or off), so BOTH windings must draw here: when the area is negative, swap v1/v2 to
     * flip the winding positive.  Safe for Gouraud (each vertex keeps its own colour) and
     * flat (colour already replicated to all three vertices before emit).  Without this,
     * exactly half of all triangles (and any single triangle wound "backwards", like a
     * screen-space CCW app triangle after the framebuffer Y-flip) vanish. */
    memcpy(&oob, &ooa, 4);

    mt = cb->multitex;
    if (cb->n + (size_t)(3 * (mt ? 12 : 8) + 2) > cb->cap) { cb->overflow = 1; return 0; }
    d = cb->buf + cb->n;
    d = m64_put_vertex(d, 0, v0, mt);
    d = m64_put_vertex(d, 1, v1, mt);
    d = m64_put_vertex(d, 2, v2, mt);
    *d++ = MACH64_ADRINDEX(MACH64_ONE_OVER_AREA_UC);   /* fires the setup engine */
    *d++ = oob;
    cb->n = (size_t)(d - cb->buf);
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

/*
 * mach64_emit.c - portable mach64 3D command emission.
 * Ported from the DRI mach64 driver; see mach64_emit.h.
 */
#include "mach64_emit.h"
#include "mach64_regs.h"

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
    /* The stream is (encoded-reg, value).  The kext byte-reverses both to
     * little-endian on submit (stwbrx); here we store host-order so the
     * host test can read them back directly. */
    cb->buf[cb->n++] = MACH64_ADRINDEX(reg);
    cb->buf[cb->n++] = val;
}

void m64_emit_state(m64_cmdbuf *cb, const m64_state *st)
{
    /* Order mirrors drm_mach64_state.c:mach64_emit_state(). */
    m64_reg(cb, MACH64_Z_OFF_PITCH,    st->z_off_pitch);
    m64_reg(cb, MACH64_Z_CNTL,         st->z_cntl);
    m64_reg(cb, MACH64_ALPHA_TST_CNTL, st->alpha_tst_cntl);
    m64_reg(cb, MACH64_SCALE_3D_CNTL,  st->scale_3d_cntl);
    m64_reg(cb, MACH64_SETUP_CNTL,     st->setup_cntl);
    m64_reg(cb, MACH64_TEX_SIZE_PITCH, st->tex_size_pitch);
    m64_reg(cb, MACH64_TEX_CNTL,       st->tex_cntl);
    m64_reg(cb, MACH64_DP_PIX_WIDTH,   st->dp_pix_width);
    m64_reg(cb, MACH64_DST_OFF_PITCH,  st->dst_off_pitch);
}

void m64_emit_vertex(m64_cmdbuf *cb, int slot, const m64_vertex *v)
{
    uint32_t base = MACH64_VERTEX_SLOT(slot);
    /* S,T,W,Z,ARGB first; X_Y LAST (writing X_Y advances the setup engine). */
    m64_reg(cb, base + MACH64_V_S,    (uint32_t)v->s);
    m64_reg(cb, base + MACH64_V_T,    (uint32_t)v->t);
    m64_reg(cb, base + MACH64_V_W,    (uint32_t)v->w);
    m64_reg(cb, base + MACH64_V_Z,    (uint32_t)v->z);
    m64_reg(cb, base + MACH64_V_ARGB, v->argb);
    m64_reg(cb, base + MACH64_V_X_Y,  (uint32_t)v->x_y);
}

void m64_emit_triangle(m64_cmdbuf *cb, const m64_vertex *v0,
                       const m64_vertex *v1, const m64_vertex *v2)
{
    m64_emit_vertex(cb, 0, v0);
    m64_emit_vertex(cb, 1, v1);
    m64_emit_vertex(cb, 2, v2);  /* slot-2 X_Y fires the triangle */
}

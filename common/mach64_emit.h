/*
 * mach64_emit.h - portable command-stream emission for the mach64 3D engine.
 *
 * OS-independent core, aligned to the PROVEN hardware path (mach64_ringtest /
 * mach64_gouraud, docs/re-notes/bus-master-ring.md + 3d-triangle.md).  It builds
 * a stream of register-write packets:
 *
 *     header = ADRINDEX(reg) | ((n_regs-1)<<16),  then n_regs values
 *
 * ended by a SRC_CNTL=0 terminator.  The kext DMAs the bytes RAW (it does NOT
 * byte-reverse), and the mach64 register window is little-endian, so the FINAL
 * bytes in the type-4 command buffer must be little-endian.  We build host-order
 * dwords here (so the host test can read them back), then m64_cb_write_le()
 * packs them little-endian into the shared command buffer -- that is where the
 * big-endian PPC byte-swap happens, NOT in the kext.
 *
 * Used by the GLDriver plugin (to build command buffers) and the host test
 * harness (to verify the logic without hardware).
 */
#ifndef MACH64_EMIT_H
#define MACH64_EMIT_H

#include <stdint.h>
#include <stddef.h>

/* A command buffer being filled with register-write packets (host order). */
typedef struct {
    uint32_t *buf;    /* caller-owned dword storage                        */
    size_t    cap;    /* capacity in dwords                                */
    size_t    n;      /* dwords written so far                             */
    int       overflow;
} m64_cmdbuf;

/* Post-transform vertex (Apple's GLEngine does T&L; we only rasterize).
 * Fields are already in hardware form: x_y packed (x*4<<16 | y*4&0xffff),
 * z/w/s/t as the setup engine expects, argb/spec_argb packed 0xAARRGGBB. */
typedef struct {
    int32_t  x_y;     /* packed screen X,Y (2 sub-pixel bits, signed)      */
    int32_t  z;       /* depth ((int)z)<<15, or 0 for no depth             */
    int32_t  w;       /* perspective W (float bits); 1.0f for 2D           */
    int32_t  s, t;    /* texcoords (float bits)                            */
    uint32_t spec_argb; /* specular colour (0 if unused)                   */
    uint32_t argb;    /* primary colour (per-vertex, gouraud)              */
} m64_vertex;

/* Register shadow: the full untextured-gouraud state we proved on hardware.
 * (Extend with texture regs from mesa_mach64_state.c when adding texturing.) */
typedef struct {
    uint32_t scale_3d_cntl;   /* 0x100c1: shade pipe, opaque, no tex/fog   */
    uint32_t setup_cntl;      /* 0: smooth setup (0x40 breaks gouraud!)    */
    uint32_t z_cntl;          /* depth test/enable (0 = off)               */
    uint32_t z_off_pitch;     /* depth buffer ((pitch/8)<<22)|(off>>3), 0=none */
    uint32_t dp_pix_width;    /* 0x60060666 = ARGB8888 everywhere          */
    uint32_t dst_off_pitch;   /* render target ((pitch/8)<<22)|(off>>3)    */
    uint32_t sc_left_right;   /* scissor                                    */
    uint32_t sc_top_bottom;   /* scissor                                    */
    uint32_t dp_write_mask;   /* 0xffffffff                                 */
    uint32_t clr_cmp_cntl;    /* colour-compare (0 = off)                  */
    uint32_t dp_mix;          /* 0x00070007 = BKGD/FRGD_MIX_S              */
    uint32_t dp_src;          /* 0x0505 gouraud (3D scaler) / 0x0101 flat  */
    uint32_t alpha_tst_cntl;  /* ALPHA_TST_CNTL (0x550): GL_ALPHA_TEST      */
    uint32_t dp_fog_clr;      /* DP_FOG_CLR (0x6c4, aliases DP_FRGD_CLR): GL_FOG
                               * blend target when ALPHA_FOG_EN=FOG (0=don't emit) */
    /* ---- texturing: emitted only when `textured` is set ---------------
     * When textured, scale_3d_cntl is overridden with FCN_TEXTURE (tex_scale_3d)
     * and the three texture registers are emitted; otherwise the untextured
     * shade path above is emitted unchanged (proven gouraud state). */
    uint32_t textured;        /* 1 => emit the texture registers below      */
    uint32_t tex_scale_3d;    /* SCALE_3D_CNTL with FCN_TEXTURE (textured)  */
    uint32_t tex_cntl;        /* TEX_CNTL (0x774)                          */
    uint32_t tex_size_pitch;  /* TEX_SIZE_PITCH (0x770) log2 dims           */
    uint32_t tex_0_off;       /* base texel VRAM offset (single-level path)  */
    /* Mipmapping: when tex_mip, emit TEX_r_OFF = tex_mip_off[r] for r in
     * [0, tex_maxlog2] (one per level) and clear MIP_MAP_DISABLE in tex_scale_3d;
     * otherwise the single base offset above goes to TEX_[maxlog2]_OFF. */
    uint32_t tex_mip_off[12]; /* per-level offset, index = level maxLog2      */
    int      tex_maxlog2;     /* highest TEX_n_OFF register used (base)       */
    int      tex_mip;         /* 1 => a full mip chain is loaded              */
} m64_state;

void m64_cb_init(m64_cmdbuf *cb, uint32_t *storage, size_t cap_dwords);

/* Append one single-register write (packet of 1). */
void m64_reg(m64_cmdbuf *cb, uint32_t reg, uint32_t val);

/* Append an n-register packet: `n` consecutive registers starting at `reg`. */
void m64_regs(m64_cmdbuf *cb, uint32_t reg, const uint32_t *vals, int n);

/* Emit the full state block (order per drm mach64_emit_state + our proven set). */
void m64_emit_state(m64_cmdbuf *cb, const m64_state *st);

/* Emit one vertex into slot n (0,1,2) as a single 7-register packet
 * {S,T,W,SPEC,Z,ARGB,X_Y}; X_Y is last so the setup engine latches the slot. */
void m64_emit_vertex(m64_cmdbuf *cb, int slot, const m64_vertex *v);

/* Emit a triangle: 3 vertices + the ONE_OVER_AREA_UC trigger (float bits of
 * 1/signed_area, computed from the packed X_Y).  Returns 0 if degenerate
 * (zero area) -- caller should drop the triangle.  Winding must give area>0
 * (else it is culled by the setup engine); caller/state decides front-face. */
int m64_emit_triangle(m64_cmdbuf *cb, const m64_vertex *v0,
                      const m64_vertex *v1, const m64_vertex *v2);

/* Append the SRC_CNTL=0 stream terminator (required to flush the bus master). */
void m64_emit_terminator(m64_cmdbuf *cb);

/* Pack the accumulated host-order dwords little-endian into `dest` (the shared
 * type-4 command buffer).  Returns the byte count (== the `size` to pass to
 * new_vertex_buffer).  This is the big-endian->LE boundary. */
size_t m64_cb_write_le(const m64_cmdbuf *cb, void *dest);

#endif /* MACH64_EMIT_H */

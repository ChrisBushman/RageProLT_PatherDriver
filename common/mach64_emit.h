/*
 * mach64_emit.h - portable command-stream emission for the mach64 3D engine.
 *
 * This is the OS-independent core, ported from the DRI mach64 driver
 * (docs/dri-reference/mesa_mach64_state.c, mesa_mach64_tris.c).  It builds a
 * buffer of (ADRINDEX(reg), value) dword pairs - the same stream DMAOUTREG
 * produces - which the kext byte-reverses to little-endian and submits.
 *
 * Used by both the GLDriver plugin (to build command buffers) and the
 * host test harness (to verify the logic without hardware).
 */
#ifndef MACH64_EMIT_H
#define MACH64_EMIT_H

#include <stdint.h>
#include <stddef.h>

/* A command buffer being filled with register-write pairs. */
typedef struct {
    uint32_t *buf;    /* caller-owned dword storage                        */
    size_t    cap;    /* capacity in dwords                                */
    size_t    n;      /* dwords written so far                             */
    int       overflow;
} m64_cmdbuf;

/* Post-transform vertex (Apple's GLEngine does T&L; we only rasterize). */
typedef struct {
    int32_t  x_y;     /* packed screen X,Y (16.16-ish, per HW)            */
    int32_t  z;
    int32_t  w;       /* 1/w                                              */
    int32_t  s, t;    /* texcoords                                        */
    uint32_t argb;    /* packed color                                     */
} m64_vertex;

/* Register shadow (subset; extend from mesa_mach64_state.c mach64UpdateXxx). */
typedef struct {
    uint32_t scale_3d_cntl;
    uint32_t z_cntl;
    uint32_t alpha_tst_cntl;
    uint32_t setup_cntl;
    uint32_t tex_cntl;
    uint32_t tex_size_pitch;
    uint32_t dp_pix_width;
    uint32_t dst_off_pitch;   /* render target                            */
    uint32_t z_off_pitch;     /* depth buffer                             */
} m64_state;

void m64_cb_init(m64_cmdbuf *cb, uint32_t *storage, size_t cap_dwords);

/* Append one register write (== one DMAOUTREG pair). */
void m64_reg(m64_cmdbuf *cb, uint32_t reg, uint32_t val);

/* Emit the dirty-state block (mirrors drm mach64_emit_state ordering). */
void m64_emit_state(m64_cmdbuf *cb, const m64_state *st);

/* Emit one vertex into slot n (0,1,2); X_Y written last. */
void m64_emit_vertex(m64_cmdbuf *cb, int slot, const m64_vertex *v);

/* Emit a triangle: 3 vertices; vertex 2's X_Y fires the setup engine. */
void m64_emit_triangle(m64_cmdbuf *cb, const m64_vertex *v0,
                       const m64_vertex *v1, const m64_vertex *v2);

#endif /* MACH64_EMIT_H */

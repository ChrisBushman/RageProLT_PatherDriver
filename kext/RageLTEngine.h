/* kext/RageLTEngine.h - register-level engine helpers for the kext (kernel only; no floating point).  The 2D recipes are the ones the
 * earlier plugin proved on hardware (fill and blit).  Every wait is bounded: a stuck engine returns -1, it never spins forever. */
#ifndef RAGELTENGINE_H
#define RAGELTENGINE_H

#include "../common/rage_f32.h"
#include <libkern/OSByteOrder.h>
#include <ppc/proc_reg.h>      /* eieio */
#include "rage_types.h"
#include "mach64_regs.h"

#define RAGE_R_CRTC_GEN_CNTL   0x041c
#define RAGE_R_MEM_CNTL        0x04b0
#define RAGE_R_DST_CNTL        0x0530
#define RAGE_R_CRTC_H_TOTAL_DISP 0x0400
#define RAGE_PIXW_555          0x60030363u        /* DP_PIX_WIDTH for ARGB1555 (565 value with the 4s replaced by 3s) */
#define RAGE_R_GEN_TEST_CNTL   0x04d0          /* bit 8 GEN_GUI_RESETB: a high->low transition resets the GUI engine (RRG 4-21) */
#define RAGE_PIXW_565          0x60040464u        /* DP_PIX_WIDTH for RGB565 (hardware-proven) */
#define RAGE_PIXW_8888         0x60060666u        /* DP_PIX_WIDTH for ARGB8888 (the 565 value with the 4s replaced by 6s; 3D prologue value since the first 32 bpp target) */
#define RAGE_WAIT_SPINS        400000u            /* ~0.4 s of register reads: the bound on every wait */

static inline uint32_t eng_rd(volatile uint8_t *regs, uint32_t r) { return OSReadLittleInt32((void *)regs, r); }
static inline void     eng_wr(volatile uint8_t *regs, uint32_t r, uint32_t v) { OSWriteLittleInt32((void *)regs, r, v); eieio(); }

static inline int eng_is_idle(volatile uint8_t *regs)
{
    return !(eng_rd(regs, MACH64_FIFO_STAT) & 0xffffu) && !(eng_rd(regs, MACH64_GUI_STAT) & 1u) && !(eng_rd(regs, MACH64_GUI_STAT) & 1u);
}
/* Idle means: FIFO empty AND GUI_ACTIVE low AND it stays that way.  GUI_ACTIVE takes ~1 us to rise after a trigger write, and the CPU must never
 * touch the VRAM aperture while the engine is starting or running (found Nov 24: a premature idle let the CPU read VRAM mid-fill and wedged the
 * engine).  So: ride out the lag with dummy reads, then require 8 consecutive idle observations.  Bounded: returns -1 if it never settles. */
static inline uint32_t tbl32(void) { uint32_t t; __asm__ volatile("mftb %0" : "=r"(t)); return t; }   /* PROFILE: raw timebase ticks (41.66 MHz on the MDD) */
extern uint32_t pf_user, pf_valid, pf_tex, pf_err;

static inline int eng_wait_idle(volatile uint8_t *regs)
{
    uint32_t i, ok = 0;
    for (i = 0; i < 16; i++) (void)eng_rd(regs, MACH64_GUI_STAT);
    for (i = 0; i < RAGE_WAIT_SPINS; i++) { if (eng_is_idle(regs)) { if (++ok >= 8) return 0; } else ok = 0; }
    return -1;
}

/* Engine reset, from the sequence Linux's mach64 DRM uses (mach64_do_engine_reset; GEN_TEST_CNTL bit 8 per the RRG): stop bus-master transfers,
 * pulse the GUI engine reset, optionally acknowledge FIFO/host error flags (BUS_CNTL 0x00a00000), clear SRC_CNTL (no bus-master source), then put
 * BUS_CNTL back exactly as found (this keeps BUS_EXT_REG_EN).  Returns 0 if the engine is idle afterwards. */
static inline int eng_reset(volatile uint8_t *regs, int ack_errors)
{
    uint32_t bus = eng_rd(regs, MACH64_BUS_CNTL), gt;
    eng_wr(regs, MACH64_BUS_CNTL, bus | (1u << 6));                          /* BUS_MASTER_DIS */
    gt = eng_rd(regs, RAGE_R_GEN_TEST_CNTL);
    eng_wr(regs, RAGE_R_GEN_TEST_CNTL, gt & ~(1u << 8));                     /* GEN_GUI_RESETB high -> low */
    gt = eng_rd(regs, RAGE_R_GEN_TEST_CNTL);
    eng_wr(regs, RAGE_R_GEN_TEST_CNTL, gt | (1u << 8));                      /* released */
    if (ack_errors) eng_wr(regs, MACH64_BUS_CNTL, eng_rd(regs, MACH64_BUS_CNTL) | 0x00a00000u);
    eng_wr(regs, MACH64_SRC_CNTL, 0);
    eng_wr(regs, MACH64_BUS_CNTL, bus);
    return eng_wait_idle(regs);
}

/* Queued write: the command FIFO has 16 slots and a write to a full FIFO is LOST, not stalled (the Linux mach64 DRM waits for a free slot before
 * every write for this reason).  FIFO_STAT low 16 bits are a used-slot mask; one free slot <=> value <= 0x4000.  Bounded. */
static inline void eng_wq(volatile uint8_t *regs, uint32_t r, uint32_t v)
{
    uint32_t i;
    for (i = 0; i < RAGE_WAIT_SPINS; i++) if ((eng_rd(regs, MACH64_FIFO_STAT) & 0xffffu) <= 0x4000u) break;
    eng_wr(regs, r, v);
}

/* Standard engine context, RAGE PRO and Derivatives Programmer's Guide table 5-2 ("Recommended Initialization Values For mach64 Engine").  Our fills
 * assumed whatever state the boot replay left; the first G4 run showed X/Y/height behaving as S.12.2 sub-pixel values (a quarter of the requested
 * size and position).  Trapezoid/Bresenham/source/pattern/host state is put into the known state here, once, after an engine reset. */
static inline void eng_init_context(volatile uint8_t *regs, uint32_t pitch_px)
{
    eng_wq(regs, 0x050c, 0);                 /* DST_Y_X */
    eng_wq(regs, 0x0514, 0);                 /* DST_HEIGHT */
    eng_wq(regs, 0x0524, 0);                 /* DST_BRES_ERR */
    eng_wq(regs, 0x0528, 0);                 /* DST_BRES_INC */
    eng_wq(regs, 0x052c, 0);                 /* DST_BRES_DEC */
    eng_wq(regs, RAGE_R_DST_CNTL, 0x23u);    /* L->R, T->B, last pel */
    eng_wq(regs, 0x0580, (pitch_px / 8u) << 22);   /* SRC_OFF_PITCH */
    eng_wq(regs, 0x058c, 0);                 /* SRC_Y_X */
    eng_wq(regs, 0x0598, 0x00010001u);       /* SRC_HEIGHT1_WIDTH1 */
    eng_wq(regs, 0x05a4, 0);                 /* SRC_Y_X_START */
    eng_wq(regs, 0x05b0, 0x00010001u);       /* SRC_HEIGHT2_WIDTH2 */
    eng_wq(regs, 0x05b4, 0);                 /* SRC_CNTL */
    eng_wq(regs, 0x0640, 0);                 /* HOST_CNTL */
    eng_wq(regs, 0x0680, 0); eng_wq(regs, 0x0684, 0); eng_wq(regs, 0x0688, 0);   /* PAT_REG0/1, PAT_CNTL */
    eng_wq(regs, 0x06c0, 0);                 /* DP_BKGD_CLR */
    eng_wq(regs, 0x0700, 0); eng_wq(regs, 0x0704, 0xffffffffu);                  /* CLR_CMP_CLR, CLR_CMP_MASK */
}

/* Solid fill (pixw = the DP_PIX_WIDTH of the surface; colour in that format), kernel-programmed.  sc_lr / sc_tb are the raw scissor register values (right<<16|left, bottom<<16|top). */
static inline int eng_fill(volatile uint8_t *regs, uint32_t pixw, uint32_t off, uint32_t pitch_px, uint32_t x, uint32_t y, uint32_t w, uint32_t h,
                           uint32_t color, uint32_t sc_lr, uint32_t sc_tb, uint32_t dst_cntl)
{
    if (eng_wait_idle(regs)) return -1;
    eng_wq(regs, 0x0304, 0);                                                 /* SETUP_CNTL: the boot state left sub-pixel (S.12.2) interpretation of DST_X/Y/height on; 0 = plain integers (G4, Nov 24) */
    eng_wq(regs, MACH64_Z_CNTL, 0);
    eng_wq(regs, MACH64_SCALE_3D_CNTL, 0);
    eng_wq(regs, RAGE_R_DST_CNTL, dst_cntl);
    eng_wq(regs, MACH64_DP_PIX_WIDTH, pixw);
    eng_wq(regs, MACH64_DP_FRGD_CLR, color);
    eng_wq(regs, MACH64_DP_WRITE_MASK, 0xffffffffu);
    eng_wq(regs, MACH64_DP_MIX, MACH64_DP_MIX_DS);
    eng_wq(regs, MACH64_DP_SRC, MACH64_DP_SRC_FLAT);
    eng_wq(regs, MACH64_SC_LEFT_RIGHT, sc_lr);
    eng_wq(regs, MACH64_SC_TOP_BOTTOM, sc_tb);
    eng_wq(regs, MACH64_CLR_CMP_CNTL, 0);
    eng_wq(regs, MACH64_DST_OFF_PITCH, ((pitch_px / 8u) << 22) | (off >> 3));
    eng_wq(regs, MACH64_DST_X_Y, (y << 16) | x);                       /* first-named field is the LOW half (RRG 5-13): X low, Y high */
    eng_wq(regs, MACH64_DST_WIDTH_HEIGHT, (h << 16) | w);                    /* trigger */
    return 0;
}

static inline int eng_fill16(volatile uint8_t *regs, uint32_t off, uint32_t pitch_px, uint32_t x, uint32_t y, uint32_t w, uint32_t h,
                             uint32_t color565, uint32_t sc_lr, uint32_t sc_tb, uint32_t dst_cntl)
{ return eng_fill(regs, RAGE_PIXW_565, off, pitch_px, x, y, w, h, color565, sc_lr, sc_tb, dst_cntl); }

/* Blit between two surfaces of the same pixel size (raw copy: DP_PIX_WIDTH is the same for source and destination), own pitch for each. */
static inline int eng_blit(volatile uint8_t *regs, uint32_t pixw, uint32_t src_off, uint32_t src_pitch, uint32_t dst_off, uint32_t dst_pitch,
                           uint32_t sx, uint32_t sy, uint32_t dx, uint32_t dy, uint32_t w, uint32_t h, uint32_t sc_lr, uint32_t sc_tb)
{
    if (eng_wait_idle(regs)) return -1;
    eng_wq(regs, 0x0304, 0);                                                 /* SETUP_CNTL (see eng_fill16) */
    eng_wq(regs, MACH64_Z_CNTL, 0);
    eng_wq(regs, MACH64_SCALE_3D_CNTL, 0);
    eng_wq(regs, RAGE_R_DST_CNTL, (1u << 0) | (1u << 1));
    eng_wq(regs, MACH64_DP_PIX_WIDTH, pixw);
    eng_wq(regs, MACH64_DP_WRITE_MASK, 0xffffffffu);
    eng_wq(regs, MACH64_DP_MIX, MACH64_DP_MIX_DS);
    eng_wq(regs, MACH64_DP_SRC, MACH64_DP_SRC_BLIT);
    eng_wq(regs, MACH64_SC_LEFT_RIGHT, sc_lr);
    eng_wq(regs, MACH64_SC_TOP_BOTTOM, sc_tb);
    eng_wq(regs, MACH64_CLR_CMP_CNTL, 0);
    eng_wq(regs, MACH64_SRC_OFF_PITCH, ((src_pitch / 8u) << 22) | (src_off >> 3));
    eng_wq(regs, MACH64_DST_OFF_PITCH, ((dst_pitch / 8u) << 22) | (dst_off >> 3));
    eng_wq(regs, MACH64_SRC_WIDTH1, w);
    eng_wq(regs, MACH64_SRC_Y_X, (sx << 16) | sy);
    eng_wq(regs, MACH64_DST_Y_X, (dx << 16) | dy);
    eng_wq(regs, MACH64_DST_WIDTH_HEIGHT, (h << 16) | w);                    /* trigger */
    return 0;
}
static inline int eng_blit16(volatile uint8_t *regs, uint32_t src_off, uint32_t dst_off, uint32_t pitch_px, uint32_t sx, uint32_t sy,
                             uint32_t dx, uint32_t dy, uint32_t w, uint32_t h, uint32_t sc_lr, uint32_t sc_tb)
{
    return eng_blit(regs, RAGE_PIXW_565, src_off, pitch_px, dst_off, pitch_px, sx, sy, dx, dy, w, h, sc_lr, sc_tb);
}

/* Flat-colour triangle into a 16 bpp RGB565 surface through the 3D setup engine (recipe: docs/re-notes/3d-triangle.md, SETUP_CNTL 0 as Mesa).
 * Vertices are integer pixels (x*4 sub-pixel encoding is applied here); ooa_bits is the IEEE-float bit pattern of 1/area (area in pixels^2,
 * positive winding), computed by the caller: the kernel has no floating point.  Writing ONE_OVER_AREA_UC triggers the draw. */
static inline int eng_tri16(volatile uint8_t *regs, uint32_t off, uint32_t pitch_px, const int32_t vx[3], const int32_t vy[3], uint32_t color565,
                            uint32_t sc_lr, uint32_t sc_tb, uint32_t ooa_bits)
{
    uint32_t n;
    if (eng_wait_idle(regs)) return -1;
    eng_wq(regs, 0x0304, 0);                                                 /* SETUP_CNTL */
    eng_wq(regs, MACH64_Z_CNTL, 0);
    eng_wq(regs, MACH64_SCALE_3D_CNTL, MACH64_SCALE_3D_CNTL_SOLID);
    eng_wq(regs, RAGE_R_DST_CNTL, 3u);
    eng_wq(regs, MACH64_DP_PIX_WIDTH, RAGE_PIXW_565);
    eng_wq(regs, MACH64_DP_FRGD_CLR, color565);
    eng_wq(regs, MACH64_DP_WRITE_MASK, 0xffffffffu);
    eng_wq(regs, MACH64_DP_MIX, 0x00070007u);
    eng_wq(regs, MACH64_DP_SRC, MACH64_DP_SRC_FLAT);
    eng_wq(regs, MACH64_SC_LEFT_RIGHT, sc_lr);
    eng_wq(regs, MACH64_SC_TOP_BOTTOM, sc_tb);
    eng_wq(regs, MACH64_CLR_CMP_CNTL, 0);
    eng_wq(regs, MACH64_DST_OFF_PITCH, ((pitch_px / 8u) << 22) | (off >> 3));
    for (n = 0; n < 3; n++) {
        uint32_t b = MACH64_VERTEX_SLOT(n);
        eng_wq(regs, b + MACH64_V_S, 0); eng_wq(regs, b + MACH64_V_T, 0); eng_wq(regs, b + MACH64_V_W, 0x3f800000u);
        eng_wq(regs, b + MACH64_V_SPEC_ARGB, 0); eng_wq(regs, b + MACH64_V_Z, 0); eng_wq(regs, b + MACH64_V_ARGB, 0xffffffffu);
        eng_wq(regs, b + MACH64_V_X_Y, (((uint32_t)(vx[n] * 4)) << 16) | (((uint32_t)(vy[n] * 4)) & 0xffffu));
    }
    eng_wq(regs, MACH64_ONE_OVER_AREA_UC, ooa_bits);                         /* trigger */
    return 0;
}


/* Textured quad: the 3D engine draws dst rect (x0,y0)-(x1,y1) (Q2 = quarter pixels, vertex coordinates) from a SQUARE power-of-two texture (2^wl texels per side, row pitch 2^wl)
 * with texel coordinates (u0,v0)-(u1,v1) in Q8 texels.  The destination and the texture format are independent (the engine converts), so this is the 16 -> 32 bpp
 * present and the scaled present (bilinear when scale3d carries the bilinear bits).  Register order = m64_emit_state (textured) + m64_emit_triangle, as proven by
 * tools/ragegl/gldcore_test (textured quad) and gl_suite.  Float encodings are integer-built (rage_f32.h). */
#define RAGE_SCALE3D_TEX_NEAREST 0x0141009bu
#define RAGE_SCALE3D_TEX_BILINEAR (0x0141009bu | 0x02000000u | 0x08000000u)
static inline uint32_t eng_f32_signed(int32_t num, int shift) { return num < 0 ? (0x80000000u | rage_f32_pow2((uint32_t)-num, shift)) : rage_f32_pow2((uint32_t)num, shift); }
static inline void eng_vertex(volatile uint8_t *regs, int slot, int32_t x, int32_t y, uint32_t s, uint32_t t)
{
    uint32_t b = 0x0240u + (uint32_t)slot * 0x20u;
    eng_wq(regs, b + 0x00, s); eng_wq(regs, b + 0x04, t); eng_wq(regs, b + 0x08, 0x3f800000u);   /* W = 1.0 */
    eng_wq(regs, b + 0x0c, 0); eng_wq(regs, b + 0x10, 0); eng_wq(regs, b + 0x14, 0xffffffffu);   /* SPEC, Z, ARGB white (modulate = identity) */
    eng_wq(regs, b + 0x18, (((uint32_t)x & 0xffffu) << 16) | ((uint32_t)y & 0xffffu));          /* X_Y last: latches the slot */
}
static inline int eng_texquad(volatile uint8_t *regs, uint32_t dst_pixw, uint32_t dst_off, uint32_t dst_pitch, uint32_t tex_fmt, uint32_t tex_off, uint32_t wl,
                              int32_t x0, int32_t y0, int32_t x1, int32_t y1, int32_t u0, int32_t v0, int32_t u1, int32_t v1,
                              uint32_t sc_lr, uint32_t sc_tb, uint32_t scale3d)
{
    uint32_t sh = wl + 8u, s0 = eng_f32_signed(u0, (int)sh), s1 = eng_f32_signed(u1, (int)sh), t0 = eng_f32_signed(v0, (int)sh), t1 = eng_f32_signed(v1, (int)sh);
    uint32_t n = (uint32_t)((x1 - x0) / 4) * (uint32_t)((y1 - y0) / 4), oob = rage_f32_recip(n);       /* 16 / cross = 1 / (w * h) */
    uint32_t tc = (1u << 9) | (1u << 23) | (1u << 30) | (1u << 17) | (1u << 18);                       /* as the bundle: cache flush, 4K cache, + clamp S and T */
    if (eng_wait_idle(regs)) return -1;
    eng_wq(regs, RAGE_R_DST_CNTL, 3u);
    eng_wq(regs, 0x0304, 0);                                                                           /* SETUP_CNTL */
    eng_wq(regs, MACH64_Z_CNTL, 0);
    eng_wq(regs, MACH64_TEX_0_OFF + (wl << 2), tex_off);                                               /* the offset register of the largest level (= wl: square) */
    eng_wq(regs, MACH64_TEX_SIZE_PITCH, wl | (wl << 4) | (wl << 8));
    eng_wq(regs, MACH64_TEX_CNTL, tc & ~(1u << 23));
    eng_wq(regs, MACH64_TEX_CNTL, tc);                                                                 /* 0 -> 1 edge flushes the texture cache */
    eng_wq(regs, MACH64_SCALE_3D_CNTL, scale3d);
    eng_wq(regs, MACH64_ALPHA_TST_CNTL, (7u << 4) | (4u << 8));
    eng_wq(regs, MACH64_DP_PIX_WIDTH, (dst_pixw & ~0xf0000000u) | (tex_fmt << 28));
    eng_wq(regs, MACH64_DST_OFF_PITCH, ((dst_pitch / 8u) << 22) | (dst_off >> 3));
    eng_wq(regs, MACH64_SC_LEFT_RIGHT, sc_lr);
    eng_wq(regs, MACH64_SC_TOP_BOTTOM, sc_tb);
    eng_wq(regs, MACH64_DP_WRITE_MASK, 0xffffffffu);
    eng_wq(regs, MACH64_CLR_CMP_CNTL, 0);
    eng_wq(regs, MACH64_DP_MIX, MACH64_DP_MIX_SS);
    eng_wq(regs, MACH64_DP_SRC, MACH64_DP_SRC_3D);
    eng_vertex(regs, 0, x0, y0, s0, t0); eng_vertex(regs, 1, x1, y0, s1, t0); eng_vertex(regs, 2, x0, y1, s0, t1);
    eng_wq(regs, MACH64_ONE_OVER_AREA_UC, oob);
    eng_vertex(regs, 0, x1, y0, s1, t0); eng_vertex(regs, 1, x1, y1, s1, t1); eng_vertex(regs, 2, x0, y1, s0, t1);
    eng_wq(regs, MACH64_ONE_OVER_AREA_UC, oob);
    return 0;
}
#endif

/* kext/RageLTEngine.h - register-level engine helpers for the kext (kernel only; no floating point).  The 2D recipes are the ones the
 * earlier plugin proved on hardware (fill and blit).  Every wait is bounded: a stuck engine returns -1, it never spins forever. */
#ifndef RAGELTENGINE_H
#define RAGELTENGINE_H

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

/* Solid fill, 16 bpp, kernel-programmed.  sc_lr / sc_tb are the raw scissor register values (right<<16|left, bottom<<16|top). */
static inline int eng_fill16(volatile uint8_t *regs, uint32_t off, uint32_t pitch_px, uint32_t x, uint32_t y, uint32_t w, uint32_t h,
                             uint32_t color565, uint32_t sc_lr, uint32_t sc_tb, uint32_t dst_cntl)
{
    if (eng_wait_idle(regs)) return -1;
    eng_wq(regs, 0x0304, 0);                                                 /* SETUP_CNTL: the boot state left sub-pixel (S.12.2) interpretation of DST_X/Y/height on; 0 = plain integers (G4, Nov 24) */
    eng_wq(regs, MACH64_Z_CNTL, 0);
    eng_wq(regs, MACH64_SCALE_3D_CNTL, 0);
    eng_wq(regs, RAGE_R_DST_CNTL, dst_cntl);
    eng_wq(regs, MACH64_DP_PIX_WIDTH, RAGE_PIXW_565);
    eng_wq(regs, MACH64_DP_FRGD_CLR, color565);
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

#endif

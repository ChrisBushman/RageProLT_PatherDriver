/*
 * mach64_regs.h - Rage LT Pro (mach64LB) 3D engine register offsets.
 *
 * Offsets verified against the open-source DRI/DRM mach64 driver
 * (see docs/dri-reference/drm_mach64_drv.h) and the ATI 3D RAGE LT PRO
 * Register Reference (docs/pdfs/..._RRG-G03300_1998.pdf).  The 3D engine
 * registers are the ones ATI stripped from the "Restricted" RRG, recovered
 * here from the DRI sources.
 *
 * This header is self-contained (no X11 CARD32 / no Linux DRM deps) so the
 * portable emission core compiles on any host for testing.
 */
#ifndef MACH64_REGS_H
#define MACH64_REGS_H

#include <stdint.h>

/* ---- Setup / raster state ------------------------------------------- */
#define MACH64_SETUP_CNTL       0x0304  /* flat/smooth shade, solid fill  */
#define MACH64_Z_OFF_PITCH      0x0548  /* depth buffer base+pitch        */
#define MACH64_Z_CNTL           0x054c  /* depth test func/enable         */
#define MACH64_ALPHA_TST_CNTL   0x0550  /* alpha test + dst blend         */
#define MACH64_DST_OFF_PITCH    0x0500  /* render target base+pitch       */
#define MACH64_SCALE_3D_CNTL    0x05fc  /* blend/fog/tex-func/dither      */
#define MACH64_TEX_0_OFF        0x05c0  /* primary texture base           */
#define MACH64_SECONDARY_TEX_OFF 0x0778
#define MACH64_TEX_SIZE_PITCH   0x0770
#define MACH64_TEX_CNTL         0x0774  /* LOD/tiling/clamp               */
#define MACH64_DP_PIX_WIDTH     0x06d0
#define MACH64_DP_WRITE_MASK    0x06c8

/* ---- Register aperture location (PPC/big-endian mach64) -------------
 * VERIFIED on the Lombard: the MMIO registers are NOT at BAR2 (the 4KB
 * auxiliary aperture is optional and was disabled -> reads master-abort and
 * machine-check the machine).  They live in the last 2KB of the little-endian
 * framebuffer aperture (BAR0):
 *     register R  ->  BAR0 + MACH64_REG_WINDOW + R
 * The window is little-endian, so read/write with byte-swap (OSRead/Write
 * LittleInt32).  Confirmed: CONFIG_CHIP_ID reads 0xdc004c49 (type 0x4c49,
 * rev 0xdc), matching the PCI IDs. */
#define MACH64_REG_WINDOW       0x7FF800u   /* BAR0 + 8MB - 2KB */

/* ---- Bus-master data port (submission target) ----------------------- */
#define MACH64_BM_ADDR          0x0648

/* ---- Vertex register slots (fed to the FP setup engine) -------------
 * Three slots, stride 0x20.  Within a slot:
 *   +0x00 S   +0x04 T   +0x08 W   +0x0c SPEC_ARGB
 *   +0x10 Z   +0x14 ARGB          +0x18 X_Y   (X_Y written LAST -> triggers)
 * Writing VERTEX_3_X_Y fires the triangle through the setup engine.
 */
/* ONE_OVER_AREA_UC is the triangle TRIGGER: written last, after all 3 vertices,
 * = IEEE-float bits of 1.0/signed_area.  (NOT VERTEX_3_X_Y.)  See
 * docs/re-notes/3d-triangle.md. */
#define MACH64_ONE_OVER_AREA_UC 0x0300
#define MACH64_SC_LEFT_RIGHT    0x06a8
#define MACH64_SC_TOP_BOTTOM    0x06b4

/* Verified solid/gouraud untextured no-depth triangle state (3d-triangle.md) */
#define MACH64_SCALE_3D_CNTL_SOLID  0x000100c1u  /* shade, opaque, no tex/fog */
#define MACH64_SETUP_CNTL_SMOOTH    0x00000040u  /* LOG_MAX_INC_ADJ, smooth   */

#define MACH64_VERTEX_SLOT(n)   (0x0240 + ((n) * 0x20))  /* n = 0,1,2      */
#define MACH64_V_S              0x00
#define MACH64_V_T              0x04
#define MACH64_V_W              0x08
#define MACH64_V_SPEC_ARGB      0x0c
#define MACH64_V_Z              0x10
#define MACH64_V_ARGB           0x14
#define MACH64_V_X_Y            0x18

/* ---- DMA descriptor flags (turn-9 command ring) --------------------- */
#define MACH64_APERTURE_OFFSET  0x7ff800u
#define MACH64_DMA_HOLD_OFFSET  (1u << 30)
#define MACH64_DMA_EOL          (1u << 31)
#define MACH64_DMA_CHUNKSIZE    0x1000

/*
 * ADRINDEX(): encode an MMIO register offset into the mach64 GUI bus-master
 * command-stream index (from mach64_reg.h).  Registers live in two banks:
 *   block0: 0x400..0x7ff -> (reg-0x400)>>2
 *   block1: 0x000..0x3ff -> (reg>>2) | 0x0100
 */
#define MACH64_DWMREG0          0x0400
#define MACH64_ISREG0(r)        ((r) >= MACH64_DWMREG0)
#define MACH64_ADRINDEX(r) \
    (MACH64_ISREG0(r) ? (((r) - MACH64_DWMREG0) >> 2) \
                      : (((r) >> 2) | 0x0100))

/* ---- A few SCALE_3D_CNTL / Z_CNTL bits used by the state builder ----- */
#define MACH64_Z_TEST_MASK      (7u << 1)
#define MACH64_Z_TEST_LESS      (1u << 1)
#define MACH64_Z_TEST_LEQUAL    (2u << 1)
#define MACH64_Z_TEST_ALWAYS    (7u << 1)
#define MACH64_Z_EN             (1u << 0)

#define MACH64_SCALE_PIX_EXPAND_DYNAMIC_RANGE (1u << 0)
#define MACH64_DITHER_EN        (1u << 8)
#define MACH64_ROUND_EN         (1u << 9)
#define MACH64_TEX_MAP_AEN      (1u << 3)

#endif /* MACH64_REGS_H */

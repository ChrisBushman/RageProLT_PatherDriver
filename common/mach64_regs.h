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

/* Values are plain integer-literal macros -- no typedefs needed.  Only pull in
 * <stdint.h> for host/userspace builds; the kext compiles -nostdinc (KERNEL). */
#if !defined(KERNEL)
#include <stdint.h>
#endif

/* ---- Setup / raster state ------------------------------------------- */
#define MACH64_SETUP_CNTL       0x0304  /* flat/smooth shade, solid fill  */
#define MACH64_Z_OFF_PITCH      0x0548  /* depth buffer base+pitch        */
#define MACH64_Z_CNTL           0x054c  /* depth test func/enable         */
#define MACH64_ALPHA_TST_CNTL   0x0550  /* alpha test + dst blend         */
#define MACH64_DST_OFF_PITCH    0x0500  /* render target base+pitch       */
#define MACH64_CRTC_OFF_PITCH   0x0414  /* live scanout base+pitch: block(0x400)+MM 0x14 */
#define MACH64_CRTC_VLINE_CRNT_VLINE 0x0410 /* [26:16] = current scanline (vblank sync) */
#define MACH64_HW_DEBUG         0x047c  /* RRG 0_1F; bit10=DBL_BUFFER_EN, bit15=BLOCK_DBL_BUF */
#define MACH64_HW_DEBUG_DBL_BUFFER_EN 0x00000400u /* enable CRTC_OFFSET vblank double-buffer */
#define MACH64_HW_DEBUG_BLOCK_DBL_BUF 0x00008000u /* blocks CRTC_OFFSET double-buffering     */
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
/* SETUP_CNTL: use 0 for BOTH flat and gouraud.  The flat triangle happened to
 * work with 0x40 (LOG_MAX_INC_ADJ) because its colour came from DP_FRGD_CLR, but
 * that bit BREAKS the per-vertex colour-gradient setup (gouraud reads back solid
 * white).  0 is mesa's value.  VERIFIED on hardware -- see 3d-triangle.md. */
#define MACH64_SETUP_CNTL_SHADE     0x00000000u
#define MACH64_SETUP_CNTL_SMOOTH    0x00000000u  /* deprecated alias of _SHADE */

/* DP_SRC colour source: flat (from DP_FRGD_CLR) vs 3D scaler (interpolated) */
#define MACH64_DP_SRC               0x06d8
#define MACH64_DP_SRC_FLAT          0x00000101u  /* FRGD/BKGD_SRC_FRGD_CLR    */
#define MACH64_DP_SRC_3D            0x00000505u  /* BKGD/FRGD_SRC_3D (gouraud)*/
#define MACH64_DP_MIX               0x06d4
#define MACH64_DP_MIX_SS            0x00070007u  /* BKGD_MIX_S | FRGD_MIX_S   */
#define MACH64_DP_FRGD_CLR          0x06c4
#define MACH64_CLR_CMP_CNTL         0x0708
/* GUI (2D) solid-fill regs -- used to clear the Z buffer via the engine */
#define MACH64_DST_X_Y              0x06e8
#define MACH64_DST_WIDTH_HEIGHT     0x06ec
#define MACH64_GUI_TRAJ_CNTL        0x0730
#define MACH64_HOST_DATA0           0x0600   /* host-data FIFO 0..15 (0x600-0x63c) */
#define MACH64_DP_SRC_HOST          0x00000200u  /* FRGD_SRC_HOST | MONO_SRC_ONE   */
#define MACH64_DP_MIX_DS            0x00070003u  /* BKGD_MIX_D | FRGD_MIX_S (blit) */
#define MACH64_DP_SRC_BLIT          0x00000300u  /* BKGD_SRC_BKGD_CLR|FRGD_SRC_BLIT|MONO_SRC_ONE */

/* ---- GUI-engine blit SOURCE regs (screen-to-screen copy; offsets + recipe from
 * the DRI reference drm_mach64_{drv.h,state.c} -- same 0x05xx scheme as DST). --- */
#define MACH64_SRC_OFF_PITCH        0x0580  /* blit source base+pitch          */
#define MACH64_SRC_Y_X              0x058c  /* (x<<16)|y source start          */
#define MACH64_SRC_WIDTH1           0x0590  /* source width (blit wrap)        */
#define MACH64_DST_Y_X              0x050c  /* (x<<16)|y dest start            */

/* ---- Bus-master DMA control (VERIFIED ring, bus-master-ring.md) -------
 * The engine bus-master-reads a descriptor ring (BM_GUI_TABLE) and, per
 * descriptor, DMAs a register-write stream to the BM_ADDR data port.  These are
 * the regs the submission path saves/restores + programs. */
#define MACH64_BUS_CNTL             0x04a0
#define MACH64_BUS_MSTR_RESET       (1u << 1)
#define MACH64_BUS_FLUSH_BUF        (1u << 2)
#define MACH64_BUS_MASTER_DIS       (1u << 6)
#define MACH64_BUS_EXT_REG_EN       (1u << 27)
#define MACH64_SRC_CNTL             0x05b4
#define MACH64_SRC_BM_ENABLE        (1u << 8)
#define MACH64_SRC_BM_SYNC          (1u << 9)
#define MACH64_SRC_BM_OP_SYSTEM_TO_REG (3u << 10)
#define MACH64_DST_HEIGHT_WIDTH     0x0518  /* write 0 = kick the transfer    */
#define MACH64_BM_GUI_TABLE_CMD     0x064c  /* ring base | CIRCULAR_BUF_SIZE  */
#define MACH64_BM_GUI_TABLE         0x01b8  /* current descriptor ptr (RO)    */
#define MACH64_CIRCULAR_BUF_SIZE_16KB (0u << 0)  /* the "16KB" encoding is 0  */

/* ---- Engine status (FIFO / idle gating) ----------------------------- */
#define MACH64_FIFO_STAT            0x0710
#define MACH64_FIFO_SLOT_MASK       0x0000ffffu
#define MACH64_GUI_STAT             0x0738
#define MACH64_GUI_ACTIVE           0x00000001u

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

/* ---- Z_CNTL bits (0x54c) -- EXACT layout from the RageLTPro Register
 * Reference (page 5-17) + Mesa mach64_reg.h.  CRITICAL: bit 1 is Z_SRC_2D
 * (new-Z from 2D source), NOT the test field.  Our old (n<<1) encoding set
 * Z_SRC_2D and wedged the bus master.  The test is a 3-bit field at [6:4]. */
#define MACH64_Z_EN             (1u << 0)   /* enable Z testing              */
#define MACH64_Z_SRC_2D         (1u << 1)   /* new-Z from 2D src (leave 0!)  */
#define MACH64_Z_TEST_MASK      (7u << 4)
#define MACH64_Z_TEST_NEVER     (0u << 4)
#define MACH64_Z_TEST_LESS      (1u << 4)
#define MACH64_Z_TEST_LEQUAL    (2u << 4)
#define MACH64_Z_TEST_EQUAL     (3u << 4)
#define MACH64_Z_TEST_GEQUAL    (4u << 4)
#define MACH64_Z_TEST_GREATER   (5u << 4)
#define MACH64_Z_TEST_NOTEQUAL  (6u << 4)
#define MACH64_Z_TEST_ALWAYS    (7u << 4)
#define MACH64_Z_MASK_EN        (1u << 8)   /* enable writing the Z planes   */

#define MACH64_SCALE_PIX_EXPAND_DYNAMIC_RANGE (1u << 0)
#define MACH64_DITHER_EN        (1u << 8)
#define MACH64_ROUND_EN         (1u << 9)
#define MACH64_TEX_MAP_AEN      (1u << 3)

#endif /* MACH64_REGS_H */

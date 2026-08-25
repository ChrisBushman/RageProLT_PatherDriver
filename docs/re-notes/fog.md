# Fog (GL_FOG) — RE findings, not yet working

Attempted 2026-08-25.  The vertex-alpha approach did NOT produce a fog gradient;
the mach64 uses a **screen-space fog PLANE** the driver must set up.  Reverted the
non-working code; findings captured here for a future pass.  Test tool:
`tools/mach64_fogtest.c` / `fogtest.sh` (four red quads at increasing eye-distance
with linear white/coloured fog -> should fade toward the fog colour).

## What GLEngine gives us (recovered, all solid)
- GLEngine does NOT software-fog the vertex colours (fogtest with the current driver
  drew four SOLID red quads -> GLEngine expects the HARDWARE to fog).
- **Per-vertex FOG FACTOR** is in the gli VERTEX at **+0x4c** (float 0..1).  Verified:
  the four quads at z=-0.1/-0.4/-0.7/-0.95 carried 0.9/0.6/0.3/0.05 there (matches
  linear fog f=(end-z)/(end-start)).  (gld-layer.md already listed +0x4c as a
  tex/fog extended attr.)
- **GL_FOG enable** = gli **+0x6354** bit 24 (0x01000000).
- **GL_FOG_COLOR** = gli **+0x6358** RGBA floats.

## mach64 fog hardware
- `DP_FOG_CLR` == `DP_FRGD_CLR` register (0x06c4) sources the solid fog colour.
- `SCALE_3D_CNTL` ALPHA_FOG_EN field (bits 11/12): bit 11 = enable the alpha
  channel (proven: our untextured blend uses 0x800), bit 12 = FOG mode.
- Fog registers: **FOG_X_INC (0_FC), FOG_Y_INC (0_FD), FOG_START (0_FE)** -- a
  screen-space plane: fog(x,y) = FOG_START + FOG_X_INC*x + FOG_Y_INC*y.

## Why the alpha approach failed (HW-tested)
Mesa/Utah-glx set ALPHA_FOG_EN_FOG + SRC_ALPHA/INV_SRC_ALPHA and said "fog factor is
alpha".  On the LT Pro that did NOT read the vertex argb alpha as the fog factor:
- `2<<11` (bit 12 only): uniform FULL fog (SRC_ALPHA≈0 -> the unset fog plane = 0).
- `0x1800` (bit 11+12): uniform NO fog (SRC_ALPHA≈1.0 -> the "assumed 0xFF" alpha).
Neither used the per-vertex +0x4c factor.  So the fog factor comes from the **fog
PLANE (FOG_START/X_INC/Y_INC)**, not the vertex alpha.

## To finish (next pass)
Set up the fog plane per triangle so SRC_ALPHA interpolates the fog factor:
1. Either find the mach64 SETUP-vertex fog slot the setup engine reads to compute
   FOG_START/X_INC/Y_INC automatically (like it does color/Z), and write the +0x4c
   factor there; OR
2. Compute FOG_START/FOG_X_INC/FOG_Y_INC manually per triangle from the 3 vertices'
   screen (x,y) and fog factors (solve the plane), in the fixed-point format those
   registers expect (read the RRG page 6-30/6-31 detail -- it did not extract via
   pdftotext; read the PDF page image).
Then ALPHA_FOG_EN = bit 11 + bit 12, DP_FOG_CLR = gli+0x6358, SRC_ALPHA/INV_SRC_ALPHA.
Mutually exclusive with framebuffer blend (both own the alpha channel).

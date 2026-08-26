# GL_POLYGON_STIPPLE — DONE & HW-VERIFIED (8x8 hardware pattern)

Implemented + HW-verified 2026-08-26 (Picture 84: a stipple-on quad shows fine
horizontal stripes -- white where the pattern bit is set, background showing through
where clear -- next to a solid stipple-off quad).  The reference DRI driver declined
stipple (`ctx->Driver.PolygonStipple = NULL` -> software), so this is HW-only to us.

## Feasibility (proven first)
GLEngine DISPATCHES a stipple-enabled polygon to our HW render slots (it does NOT
software-rasterise it): a stipple-on quad drawn by a driver that ignores stipple came
out SOLID (tools/mach64_stiptest).  So the stipple state is in the gli and we can act
on it -- same as blend/fog/logicop.

## gli state (recovered via GLD_FOGSCAN, tools/mach64_stiptest + stiptest.sh)
- **GL_POLYGON_STIPPLE enable** = gli **+0x66ec bit 24** (0x01000000).
- **32x32 pattern** = gli **+0x665c**, 128 bytes, one 32-bit row per word (row r at
  +0x665c + r*4).

## Hardware (8x8 pattern gates the 3D scaler)
- The mach64 pattern is only **8x8** (PAT_REG0 0x680 = rows 0-3, PAT_REG1 0x684 = rows
  4-7, 8 bits/row).  GL stipple is 32x32, so we extract the top-left 8x8 tile: EXACT
  for 8x8-periodic patterns (halftone / screen-door / the common cases), APPROXIMATE
  for arbitrary 32x32.
- Datapath: `DP_SRC = MONO_SRC_PATTERN(1<<16) | FRGD_SRC_3D(5<<8) | BKGD_SRC_3D(5<<0)`
  (0x10505); `DP_MIX = FRGD_MIX_S | BKGD_MIX_D` (0x70003).  Where the pattern bit is 1
  the pixel takes the 3D scaler colour (FRGD, mix S); where 0 it takes BKGD with
  MIX_D = keep destination -> the framebuffer shows through.  The 3D scaler HONOURS
  MONO_SRC_PATTERN (verified), same datapath finding as the logic ops.
- Mutually exclusive with GL_COLOR_LOGIC_OP (both own DP_SRC/DP_MIX); logic op wins.

## Implementation
- `common/mach64_regs.h`: PAT_REG0/1, DP_SRC_STIPPLE, DP_MIX_STIPPLE.
- `mach64_emit.[ch]`: `stipple`/`pat_reg0`/`pat_reg1` state; PAT_REG0/1 emitted when armed.
- `gld_texture.c` mach64_bind_texture: reads enable+pattern, extracts the 8x8 tile,
  sets the stipple datapath.

## Known caveat (orientation)
Verified with a symmetric horizontal-stripe pattern, which pins the row->row mapping but
NOT the intra-tile bit order (GL default MSB-first, GL origin lower-left vs the mach64's
top-left).  For an ARBITRARY asymmetric 8x8 the tile may be mirrored/phase-shifted.
Since polygon stipple is a screen-aligned repeating pattern this is usually invisible,
but a pixel-exact asymmetric pattern would need an orientation-verification pass (test
with a distinctive 8x8 -- e.g. a single-corner bit -- and adjust the extraction).

## Tooling
`tools/mach64_stiptest.c` + `stiptest.sh` (default build = HW test; build the driver
`-DGLD_FOGSCAN` to re-scan the enable/pattern offsets).

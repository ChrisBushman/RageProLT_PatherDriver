# Fog (GL_FOG) — SOLVED & HW-VERIFIED (specular-alpha fog, bit12-only FOG mode)

Implemented + HW-verified 2026-08-25 (Picture 40/41: four red quads at increasing
depth fade red -> rose -> purple -> fog-blue).  The earlier "screen-space fog plane"
conclusion was WRONG, AND so is the reference DRI recipe.  The mach64 has no fog
table/plane: the setup engine interpolates a per-vertex FOG FACTOR and blends the
fragment against `DP_FOG_CLR` by it:

    pixel = factor*fragment + (1 - factor)*fogcolor

TWO findings diverge from the reference DRI `mach64UpdateFogAttrib` (Utah-glx lineage),
which is **INERT on the LT Pro** — HW-proven: its recipe emitted flawless state
(fog_on=1, sc=0x2c18c1, DP_FOG_CLR correct, per-vertex alpha varying) but drew ZERO
fog (Picture 39, four uniform red quads):

1. **Factor source = SPECULAR alpha** (`VERTEX_SPEC_ARGB[31:24]`), NOT the primary
   vertex ARGB alpha the reference assumes.  Proven by isolation: carrying the factor
   ONLY in spec alpha (primary = colour alpha) still gradients (Picture 41).
2. **FOG mode = `ALPHA_FOG_EN` bit12 ONLY (0x1000)**, not the reference's bit11|bit12
   (0x1800).  0x1800 was HW-proven to give NO fog; bit12-only is the mode that blends.

Kept from the reference: SRC=SRC_ALPHA, DST=INV_SRC_ALPHA, clear TEX_MAP_AEN,
DP_FOG_CLR = GL_FOG_COLOR, mutually exclusive with GL_BLEND (blend wins).

## What GLEngine gives us (all verified via GLD_FOGSCAN, 2026-08-25)
- GLEngine does NOT software-fog the vertex colours — it expects HARDWARE fog and
  hands us a per-vertex fog factor to feed the setup engine.
- **Per-vertex FOG FACTOR** = gli-VERTEX **+0x4c** (float).  Verified: quads at
  z=-0.1/-0.4/-0.7/-0.95 carried 0.900/0.600/0.300/0.050 (linear f=(end-z)/(end-start)).
  Reads **0.0 when GL_FOG is OFF** (so the fog pipe MUST be gated on the enable —
  an always-on pipe would read factor 0 = "fully fogged" and paint everything the
  fog colour).  Colour alpha stays at +0x3c (0x3f800000=1.0 in the test).
- **GL_FOG enable** = gli **+0x6380 bit 8** (0x00000100).  Recovered by diffing the
  gli state cluster across glDisable/glEnable(GL_FOG): the ONLY word that changed was
  `0x6380: 0x855c0000 -> 0x855c0100`.  (0x6354 was DISPROVEN earlier = dither; the
  enable is NOT bit24 like blend/depth — it's bit8 like alpha-test's 0x62e4.)
- **GL_FOG_COLOR** = gli **+0x6358** RGBA floats.

## mach64 programming (SCALE_3D_CNTL 0x5fc) — HW-verified encoding
- `ALPHA_FOG_EN` = FOG: **bit12 ONLY (0x1000)**.  NOT bit11|bit12 (0x1800) — that was
  HW-proven to give NO fog (Picture 39).  The factor source is the SPECULAR alpha
  (see below), so no "alpha-channel on" bit11 is needed.
- **Fog factor = per-vertex SPECULAR alpha** (`VERTEX_SPEC_ARGB[31:24]`), fed from the
  gli-vertex fog factor at +0x4c.  Primary vertex ARGB alpha does NOT drive fog here
  (isolation-proven, Picture 41).
- `ALPHA_BLEND_SRC` [18:16] = SRC_ALPHA (4); `ALPHA_BLEND_DST` [21:19] = INV_SRC_ALPHA (5).
- Clear `TEX_MAP_AEN` (bit30) — fog owns the alpha channel, can't also use texel alpha.
- `DP_FOG_CLR` (0x6c4, **aliases DP_FRGD_CLR** the 2D-fill foreground) = fog colour.
  Emitted only when fog is armed, and re-emitted every fog draw so a clear/fill can't
  leave a stale colour.
- Mutually exclusive with GL_BLEND (both own the alpha channel) — blend wins.

## Implementation
- `gld_texture.c`: `mach64_fog_active()` (enable bit set AND blend off),
  `mach64_apply_fog()` (overlays FOG mode bit12 + SRC/DST onto SCALE_3D_CNTL, latches
  DP_FOG_CLR) — applied on both the untextured and textured state paths.
- `gld_prims.c` `mach64_read_vertex`: packs +0x4c into the vertex **specular** alpha
  (`o->spec_argb`) when `mach64_fog_on` (set per-draw by `mach64_bind_texture`).
- `mach64_emit.[ch]`: `dp_fog_clr` state field, gated DP_FRGD_CLR emit.

## Scan tooling (for re-RE)
`tools/mach64_fogscan.c` + `fogscan.sh` (build driver `-DGLD_FOGSCAN`): draws a
fog-off then fog-on frame changing nothing else; the driver prints the gli state-
cluster delta + per-vertex fog factors.  NOTE the delta detail lines are
`  gli+0x....` (no "FOGSCAN" prefix) — read the full /tmp/mach64gld.log, don't
`grep FOGSCAN`.

## Verify
`tools/mach64_fogtest` (fogtest.sh): four red quads at increasing eye-distance with
linear fog toward fc=(0.2,0.5,0.8) — should fade red (near) -> blue-grey (far).

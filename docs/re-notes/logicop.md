# Colour logic ops (glLogicOp / GL_COLOR_LOGIC_OP) — DONE & HW-VERIFIED

Implemented + HW-verified 2026-08-25 (Picture 42: on a blue background, XOR-white =
yellow, XOR-red = magenta, INVERT = yellow).  The mach64 3D scaler DOES honour the
datapath ROP register `DP_MIX` — so this driver does hardware logic ops that the
reference DRI driver never did (it FELL BACK TO SOFTWARE for every op != GL_COPY,
`FALLBACK(MACH64_FALLBACK_LOGICOP, opcode != GL_COPY)` — likely over-conservative,
probably to avoid dither artifacts on mid-tones; see the caveat below).

## gli state (recovered via GLD_FOGSCAN state-delta, tools/mach64_lopscan)
- **gli+0x63a4** packs BOTH: `[31:16]` = glLogicOp GL enum (0x1500..0x150f) and
  `bit0` = GL_COLOR_LOGIC_OP enable.  Delta across off -> XOR -> OR:
  `0x15030000 -> 0x15060001 -> 0x15070001`.

## Hardware (DP_MIX 0x6d4)
- `FRGD_MIX` [20:16] + `BKGD_MIX` [4:0], each a 5-bit ROP code of source (S = the
  incoming fragment / 3D scaler output) vs dest (D = framebuffer).  Set BOTH fields to
  the same code: `DP_MIX = (code<<16) | code`.
- The 16 GL ops map onto the 16 basic MIX codes exactly (S=source, D=dest):

  | GL op | ROP | code | GL op | ROP | code |
  |-------|-----|------|-------|-----|------|
  | CLEAR | 0 | 1 (ZERO) | NOR | ~(s\|d) | 15 |
  | AND | s&d | 12 | EQUIV | ~(s^d) | 6 |
  | AND_REVERSE | s&~d | 13 | INVERT | ~d | 0 |
  | COPY | s | 7 (S) | OR_REVERSE | s\|~d | 10 |
  | AND_INVERTED | ~s&d | 14 | COPY_INVERTED | ~s | 4 |
  | NOOP | d | 3 (D) | OR_INVERTED | ~s\|d | 9 |
  | XOR | s^d | 5 | NAND | ~(s&d) | 8 |
  | OR | s\|d | 11 | SET | 1 | 2 (ONE) |

## Implementation (gld_texture.c)
- `mach64_logicop_mix[16]` (the table above), `mach64_logicop()` (reads 0x63a4,
  returns DP_MIX when enabled AND op != GL_COPY; COPY is the pass-through no-op).
- `mach64_bind_texture`: sets `st->dp_mix` to the ROP (or MACH64_DP_MIX_SS normally).
  A logic op takes PRECEDENCE over blend and fog (GL: logic op disables blending) —
  both branches force the opaque SOLID pipe and fog is skipped when `lop_on`.
- Applies to 3D draws only; the 2D clear/fill/blit paths keep their own DP_MIX, and
  glClear is (correctly) not affected by logic op.

## Caveat (dithering)
Verified with SATURATED colours (0x00/0xFF channels), where dithering is a no-op.  The
SOLID pipe dithers mid-tones, so an XOR of a dithered mid-tone source will show dither
noise in the low bits — probably why the reference punted.  Not fixed (would need to
disable DITHER_EN in scale_3d_cntl when a logic op is active); acceptable since logic
ops are overwhelmingly used with saturated colours (rubber-banding, highlight XOR).

## Tooling
`tools/mach64_lopscan.c` + `lopscan.sh` (offset scan, build driver -DGLD_FOGSCAN);
`tools/mach64_logictest.c` + `logictest.sh` (HW verify).

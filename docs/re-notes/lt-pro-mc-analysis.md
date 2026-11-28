# Rage LT Pro motion compensation - static analysis (M2 prerequisite)

Sources: Win98 ATIMCRAE/ATID32AE evidence (win98re/evidence/mc12.c, mc-scaler-pipe-cmds.c: FUN_b0103f60 init, FUN_b00c6a40 per-MB emitter),
3D RAGE LT PRO Register Reference (only the register LISTING; the Scaler Pipe chapter text is NOT in this document - descriptions live in the
separate 3D register guide we do not have), Rage Pro Programmer's Guide (no MC section; says MC "RAGE PRO only", LT-PRO "adds TV/LVDS/low power").
Tags: E established from code, I inferred, N not established.

## Hardware path (E)
MC is done by the **front-end scaler pipe** (a bilinear 2-source texture/scale engine), not a dedicated MC block as on the Rage 128 Pro.
Per-MB the Win98 driver writes register index/value pairs (BM list, header 0x7fffe48):
- Init (0x26 dwords, E): SCALE_OFF(0x70)=0, SECONDARY_SCALE_OFF(0xde)=0, SCALE_3D_CNTL(0x7f)=0x98a40, ALPHA_TST_CNTL(0x54)=0x8000,
  DP_PIX_WIDTH(0xb4)=0x80080888 (8bpp), DST_CNTL(0x4c)=0x8e023, TEX_CNTL(0xdd)=0x40800180, CLR_CMP_CNTL(0xc2)=0, DP_FOG_CLR(0xb1)=0,
  0x77/0x78 = frame width*2 / height, SCALE_X_INC(0x7c)=SCALE_Y_INC(0x7d)=SECONDARY X/Y_INC(0xe7/0xf4)=0x10000 (1:1, so the "scale" is pure
  translation + bilinear half-pel), SC_LEFT_RIGHT(0xaa), SC_TOP_BOTTOM(0xad), DP_SRC(0xb6)=0x505, DP_MIX(0xb5)=0x70003.
- Per prediction block: DST_OFF_PITCH(0x40) = plane_offset>>3 | pitch<<20 (E); SCALE_PITCH_BOTH(0xe0) = (pitch<<16|pitch) (x2 for luma frame
  access: field access doubles the pitch) (E); SECONDARY_SCALE_OFF_ACC(0xe1) / SCALE_OFF_ACC(0xe2) = start address of the reference block in
  that plane + integer position, with the HALF-PEL flags in bit 28 (x) and bit 30 (y) (E, from `(frac_y<<2 | frac_x)<<0x1c`); the odd-line/second
  field select is added as one extra pitch row ((flags & 0x2000)>>13 etc.) (E);
  SCALE_DST_Y_X(0xe3) = (x<<16 | y) (E); trigger reg 0xe4 = (height<<16|width) of the block: 0x100010 luma 16x16, 0x100008 luma 16x8,
  0x80008 chroma 8x8, 0x80004 chroma 8x4 (E). So 0xE4 is the SCALE_DST_HEIGHT_WIDTH-style trigger (name still N). Y, U and V are three
  separate 8-bit-plane passes (DST_OFF_PITCH re-pointed each time) (E).
- Bidirectional: the second reference goes through the SECONDARY source (0xe1), the first through 0xe2, both in one trigger; SCALE_3D_CNTL
  0x98a40 selects blend ONE/ONE with TEX_CACHE_SPLIT (E for the value, I for the meaning: the two bilinear-filtered samples are averaged
  by the blender).
- Two-vector (field/16x8) cases are simply two triggers with different offsets/destinations (E).

## Residual (E/N)
Chip has NO hardware IDCT (ICD family 4: MC=1, IDCT absent) (E). Win98 does the IDCT in software and copies the residual into a Y8 plane
(FUN_b00c5340), then the SAME scale pipe adds it: "signed dst clamp" is in SCALE_3D_CNTL 0x98a40 (I). The exact order (prediction written to
frame then residual blended on, or residual as the secondary source with the prediction as primary) is N.

## Not established (needs hardware experiment or the missing 3D register guide)
1. Rounding of the bilinear half-pel and of the ONE/ONE average vs MPEG-2 (a+1)>>1 (bit exactness - a bit-exactness claim must never be
   assumed; MPEG-2 drift accumulates over a GOP).
2. How signed residuals are added (offset 128 encoding? SCALE_3D_CNTL signed clamp bits).
3. Whether 8-bit plane passes at 1:1 are exact for full-pel vectors (expected yes).
4. Completion/fence: how the CPU knows the pass is done (engine idle poll, DST_...?).
5. The driver (kext) path: our standalone Rage driver already has a PM4-less register/bus-master path for the 2D engine (see
   docs/path-b-driver-spec.md); an MC submit user-client must write the same register list through it.

## Consequence for M2
Bit-exactness is the crux. Plan (needs agreement before any live write, per the project rules): build a tiny user-space experiment that, using
only the register writes listed above (all fully decoded; no unexplained fields), runs ONE 16x16 luma half-pel prediction of a known plane,
reads the destination back and compares with the CPU reference in recon.c for all 4 half-pel phases and for the bidirectional average. The
result decides whether MC can be used for exact reconstruction or only approximate (display-quality) assist.

## Update: deeper Win98 trace (ATID32AE.c FUN_b00c60a0 / b00c66d0 / b00c6900, ATIMCRAE.c)
Established (E):
- The per-MB command stream is BM_ADDR/BM_DATA format (LT Pro guide 6.2.2): index dword with GUIREG_COUNTER in bits 23:16 (n-1 consecutive regs,
  auto-increment), then n data dwords. So `0x7f,0x98a40` = write SCALE_3D_CNTL; `0xc00001ef,<v>` brackets (v=0x100 start/end, 0x40 + 0x192
  writes of `0xb2 = DP_WRITE_MASK <- 0xffffffff` = padding, v = 0x10|0x2/0x4/0x5 per-MB flags from MB +2: 0x300/0x20/0xc0 bits); `0x80000000` ends a block.
- EVERY MB (intra via FUN_b00c66d0, inter via FUN_b00c6900) ends with the residual feed: per block (6: Y0-3,U,V) repeated packets
  `0xa00201e0, d0, d1, d2` = three dwords to register index 0x1e0 (+0x1e1,+0x1e2), taken straight from the framework's coefficient buffer
  at ((MB+8)&0xffffff)*4, the per-block counts being MB bytes +10..+15 (= our MbRec +0x16..+0x1b), consumed 4 counts per packet, then `0x80000000`.
  0x1e0.. are block-1 registers absent from the LT Pro guide (cf. R128 IDCT_RUNS 0x1f80/IDCT_LEVELS 0x1f84): I: IDCT run/level feed.
- Intra MBs use SCALE_3D_CNTL=0x8240 (no blend: write mode) with trigger 0x100010 + 2x 0x80008 (Y, U, V; the first pass writes the whole MB then
  chroma), inter use 0x98a40 (blend add). Two-way/one-way variants differ only in sources.
- MC12 sets IDCTEngineUsed=1 at surface creation (FUN_b01041f0) and has host IDCT code ("IDCT not enabled / not supported" box at ATIMCRAE ~5819 when the
  engine is off; INI UseIDCT=1, IDCTinAGPenable/emulate, One/TwoWayIDCT counters).
Open (N), the decisive one for M2: does 4c49 take THIS stream (0x1e0 coefficient feed to a hardware IDCT), or does ICD family 4 / id 0x4c49 route to a host-IDCT
mode whose result is fed some other way? ICD says "IDCT absent" for 4c49 yet MC12 families {5,6,8,9} are all emitted by the same function; the mapping HAL/ICD
family -> MC family (DAT_b0122e50) and the meaning of ctx+0x220 byte +0x13 (selects FUN_b00c60a0's two branches) must be traced next, plus the host IDCT's output
format at ATIMCRAE ~5790. Until then the "no hardware IDCT on LT Pro" statement is E only from the ICD capability key, not from MC register behaviour.

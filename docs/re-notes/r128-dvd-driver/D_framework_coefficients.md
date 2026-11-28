# DVDPlayback slice decoder -> driver: what the framework hands to DVDDriverDecode (static)

Source: DVDPlayback.framework (av/dvdpb.bin, base 0x8a640000), class DVDSliceDecoder (Ravisent), error string
"DVDSliceDecoder::SetupDVDVideoIdctMB: illegal macroblockType 0x%x" (refs at 0x8a71e5a4, 0x8a71f0e0, 0x8a71f420: one per picture-structure variant).

## MbRec fill (0x8a71e4fc, ESTABLISHED)
- flags (+0x15) = (this[0x4a0] >> 6) & 1, i.e. one bit of the macroblock-type word. mc_type (+0x14) is derived from this[0x20] (picture structure 1/2/3) and
  this[0x4a0] bits 0/2/3 (intra / motion forward / backward), +4 when this[0x4ac]==2. MVs copied from this+0x526.., field selects from this+0x4b7...
- This resolves MbRec.flags: it is a macroblock-type bit from the parser (exact MPEG-2 flag still to be named).

## Coefficient writer (0x8a71e794.., ESTABLISHED)
- VLC decode by table lookup; per coefficient writes a 4-byte entry: u8 run (stb 0(r29)), s16 level (sth 2(r29)); escape-overflow writes run=0x3f-based padding with level 0.
- Levels are DEQUANTISED IN THE FRAMEWORK: intra: level * W[scan[pos]] * quantiser_scale >> 4 (0x8a71e914-0x8a71e91c); non-intra: (2*level+sign) * W * qs >> 5
  (0x8a71e950-0x8a71e964); clipped to [-2048, 2047] (0x7ff/-0x800 at 0x8a71e968-0x8a71e9a8). r25 = weight matrix table, r26 = quantiser scale, r22 = scan table.
- Consequence: the driver (and hardware IDCT on R128) receives dequantised run/level data; no matrices or scale are passed to the driver. A host IDCT on the LT Pro
  needs only: inverse zig-zag (or alternate scan - check which scan the framework uses via the coefficient position, run is a delta), IDCT, add to prediction.
- Intra DC (0x8a71eb10-0x8a71eb68, ESTABLISHED): entry {run 0, level = (dc_pred + dc_diff) << (3 - this[0x14])} (this[0x14] = intra_dc_precision; predictor kept at this+0x4e8..). AC entries follow; run = zeros skipped since previous coefficient (position r30 accumulates, 0x8a71e8b8), so position = cumulative run in scan order.
- Per-block trailer (0x8a71e9f8-0x8a71ea68): only when this[0x604] != 0, builds an 8-byte column-occupancy mask (max row per column, 0x8a71e9d0-0x8a71e9f4) and appends it to a side list at this[0x5bc] (sparsity hint for an IDCT; not part of the driver's 4-byte entries).
- MISMATCH CONTROL: SETTLED for design purposes. The framework's coefficient writer (all three writer paths: normal AC 0x8a71e8e0-0x8a71e9b4, escape 0x8a71e8c4-0x8a71e8dc, intra DC 0x8a71eb10-0x8a71eb68) accumulates NO coefficient sum and applies NO parity/LSB toggle; saturation to [-2048,2047] only. The R128 bundle also sends no mismatch flag/field. Therefore whatever applies ISO 13818-2 mismatch control (sum of all 64 dequantised coefficients even => toggle LSB of F[7][7], after saturation, all block types) is downstream of the framework: on R128 the chip's IDCT (not inspectable), on the LT Pro OUR host IDCT. Decision: implement it per spec in the host IDCT (we are the only IDCT; the spec defines correctness; bit-exactness vs Apple's chip is not required).

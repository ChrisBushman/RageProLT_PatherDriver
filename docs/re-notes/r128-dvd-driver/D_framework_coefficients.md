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
- NOT established: where MPEG-2 mismatch control (parity of coefficient sum) is applied (not seen in this writer; maybe in the chip's IDCT, then the host IDCT must do it);
  whether run is delta-since-last-coefficient (r30 accumulates position, 0x8a71e8b8) in scan order.

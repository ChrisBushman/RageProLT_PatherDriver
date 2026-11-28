# LT Pro motion-compensation experiment (M2 prerequisite) - plan and stages

Basis: docs/re-notes/lt-pro-mc-analysis.md (Win98 MC12 HAL, static).  Rules: no register or value that is not established from the Win98 code or the register guide; the kernel
builds the sequence (the caller picks geometry only); debug kext only (RAGE_DEBUG_ENGTEST, selector RAGE_GL_MC_EXP); engine state saved and restored; first run on a calibration that
needs no interpretation of the result.

## Stage 1 (this document's first run): prediction only, luma, one source
Question answered: does the LT Pro scaler pipe, driven with the Win98 sequence, reproduce MPEG-2 half-pel prediction bit-exactly (rounding of (a+b+1)>>1 and (a+b+c+d+2)>>2)?
- Planes: two 64x64 8-bit planes in the caller's own VRAM blocks; source = pseudo-random bytes (maximum sensitivity to rounding), destination pre-filled 0xA5.
- Calibration (no interpretation): integer-pel shifts (0,0), (+1,0), (0,+1) must give an exact copy; run for the four aperture swap modes (VRAM_WRITE/READ flags 0..3), the first that is exact
  is used afterwards.  If none is exact the experiment stops (wrong register semantics or addressing) and nothing further is sent.
- Measurement: 4 half-pel phases (full, h, v, hv) x 4 positions (odd and even X/Y, positive and negative integer part of the vector); per phase: number of differing pixels versus MPEG-2
  rounding and versus truncation, and the histogram of (hardware - MPEG-2).  Also counts bytes changed outside the 16x16 destination block (must be 0).
- Registers (all values from the Win98 init stream FUN_b0103f60 / one-way frame case of FUN_b00c6a40): see kext/RageLTMode.cpp RageLTDevice::mcExp.
- Failure handling: engine not idle within the bound -> eng_reset, status 1, tool aborts.  Crash/hang is acceptable investigation cost (G4, reboot 2-3 min).
Outputs decide: exact -> hardware prediction is usable for exact reconstruction; off-by-one pattern -> characterise (possible compensation); not exact and not characterisable -> approximate-only.

## Later stages (each needs its own go)
2. chroma (8x8, half-pel on half-chroma grid), 3. two-way average (SECONDARY source, ONE/ONE blend) rounding, 4. residual: the 0x1e0 feed (3-dword layout variants), 5. completion/fence behaviour, 6. field/16x8 cases.

## Stage 1 live results (G4 MDD, 2026-11-27)

Debug selector RAGE_GL_MC_EXP (RAGE_DEBUG_ENGTEST only); tool `tools/ragegl/mc_exp` (`cal`, `one`, `x` = constant-source x constant-destination transfer table).

* Panther IOConnectMethodScalarIScalarO allows 6 arguments in total. A 6-in/2-out selector crashed the kernel (nothing was written to the engine).
* Win98 init stream plus per-block MC registers, zero vector: the output is the destination plus ~+-3 of dither-like noise. The source plane is NOT copied. Nothing is written outside the 16x16 block. The source plane reads back intact.
* The documented front-end-scaler blit (Programmer's Guide 8.9.2 values, `doc` mode) gives the same result. Constant-source table: d=00 -> 00, d=80 -> 80 for s=00/40/80; s=ff -> +3. The pass behaves as dst + roughly src/85 or src read as a low-weight channel.
* So the 0xe0..0xe4 sequence itself works (it touches exactly the block) but the SOURCE FETCH or PIXEL FORMAT/weight is wrong. Suspects: SCALE_3D_CNTL colour-source/format bits, DP_PIX_WIDTH scale field, TEX_CNTL, the SCALE_OFF address units.
* Engine hang: about every second run the pass never idles (FIFO_STAT 0x1000, GUI_ACTIVE set); eng_reset recovers. Resetting before each run does not prevent it.

### Stage 1, further live results (same day)
* Sweep of SCALE_3D_CNTL (single-bit variants, 0x0, 0x8240, 0x240, bits 0-24 on top of 0x40; `mc_exp v`): output unchanged in every case (dst + 0, +3 for source 0xff only). Colour-expand, dither, cache-disable and blend bits make no difference.
* Finer source-constant table (`mc_exp x doc`): sources 0x10..0xfc add 0; only 0xff adds 3. A 0x00..0xff ramp source adds 0 everywhere. A texture-palette ramp (TEX_PALETTE_INDEX 0x740 / TEX_PALETTE 0x77c, three encodings) changes nothing. So the pass is not seeing the source plane at all (the "+3 at 0xff" is a saturation artefact, not a transfer function).
* Register dump of the engine before the pass (`mc_exp d`, `MCDUMP` lines in the kernel log): many 3D/scaler registers hold leftover 0x..0d9f0d-style values, so the 3D/scaler block is not in a defined state after eng_reset; 0x5dc/0x5e0/0x5ec/0x750/0x768 = 0x40 (probably our own earlier run, restored).
* Open hypotheses: (1) source fetch address/pitch go through TEX_SIZE_PITCH (0x770) / TEX_0_OFF, not the registers written; (2) the scaler needs a full 3D context (Win98 loads it via the bus-master stream, not MMIO); (3) the pipe's source comes from the SECONDARY unit only.

### Stage 1 RESULT (root cause found, same day)
**Cause of "source not fetched": DP_WRITE_MASK (0x6c8) was left at 0x97385b03 by the 3D driver, so only the low 2 bits of every written byte changed** (all earlier "+3 max" outputs). The kernel now writes DP_WRITE_MASK=0xffffffff before the pass (and restores it). The wrong-register 0x6cc write is harmless. The earlier SCALE_3D_CNTL/palette/unit sweeps were all masked by this. TEX_PALETTE (0x77c) is read-only.

With the mask fixed (destination pre-filled with 0, because the Win98 inter setup ADDS source to destination: blend ONE/ONE):
* integer-pel (swap mode 0): exact copy of all 256 bytes, nothing outside the block. Swap modes 1-3 break half-pel only; use mode 0.
* horizontal half-pel (2,0): 0 of 1024 samples differ from MPEG-2 `(a+b+1)>>1`.
* vertical half-pel (0,2): 0 of 1024 differ from MPEG-2.
* **diagonal half-pel (2,2): 362 of 1024 are +1 above MPEG-2 `(a+b+c+d+2)>>2`** (hardware does two chained rounded bilinear stages). Needs a host correction or a different decomposition for hv blocks (about 35% of their samples).
* Zero bytes changed outside the block in all runs.
Remaining stage-1 gaps: engine hang every ~2nd run (eng_reset path), run on 16x8 and chroma sizes (stage 2).

### Stage 2 RESULT (2026-11-27): block sizes and two-way
* **hv half-pel model fixed:** hardware = chained rounding `((a+b+1)>>1 + (c+d+1)>>1 + 1)>>1`, 0 of 1024 differ from that model. It differs from MPEG-2 `(a+b+c+d+2)>>2` by exactly +1 in ~35% of samples (never -1).
* **All four block sizes** (16x16 0x100010, 16x8 0x100008, 8x8 0x80008, 8x4 0x80004; `mc_exp z`): full/h/v = MPEG-2 exact, hv = chained model exact, 0 bytes outside the block, over all positions tried.
* **Two-way:** needs **bit 31 set on SCALE_OFF_ACC (0x788, the primary)**; Win98's two-reference path always sets it through its `((y|~1)<<2 | x)<<28` expression. Secondary reference address + flags go to SECONDARY_SCALE_OFF_ACC (0x784) before 0x788. With that, all 16 phase pairs x {16x16, 8x8}: 0 of 1536/384 samples differ from `(p1+p2+1)>>1` where p1,p2 use the hardware's own chained hv. Without bit 31 the secondary is ignored. TEX_CNTL variants make no difference.
* Consequence for M2: prediction is bit-exact vs MPEG-2 except hv half-pel (and two-way averages involving an hv half), which drift by +1 in ~35% of samples. Decision needed: accept (ATI's own MC had the same property), or route hv blocks through the CPU path of M1.
Remaining: residual (0x1e0 feed or host IDCT + blender add), completion/fence, engine hang every ~2nd run, field-pitch (doubled) cases.

## M2 bundle results (Nov 27)

- Bundle default = Rage MC (Win98 method); `DVDSTUB_CPU=1` or Info.plist `RageDVDMode=cpu` selects the bit-exact CPU path for whole pictures (not per-block hybrid). CPU path still equals M1 (disc 14: 150 frames, maxdiff 3).
- PAL failure root cause: command list was flushed mid-emission (cap reached) so the engine ran before the residual upload and the upload then overwrote its output. Fix: grow the list, flush once after upload (chunks of RAGE_MC_MAX_CMDS).
- Verification: `DVDSTUB_BOTH=1` runs HW and CPU on every Decode and compares visible samples. Discs 8-15 (352x240 .. 720x576, frame/field, progressive/interlaced): worst diff 1 (disc 15: 2), 0 samples off by >3 in all but a few disc-15 decodes. Differences are the known hv chained-rounding.
- Timing (disc 8, 83 decodes, ms): host IDCT+staging 504, upload 497, engine 1004, fix-ups 4030, dump readback 4517. Fix-ups (excess >127 residual, 8-byte vread/vwrite) dominate: M4 target.
- Open: Show via overlay not implemented; vectors outside picture skipped; per-run dump-vs-ref comparison under HW shows timing-dependent mismatches (dump taken after later decode) — use BOTH mode for accuracy.

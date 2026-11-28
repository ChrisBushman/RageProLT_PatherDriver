# Rage LT Pro DVD hardware-assist bundle - design proposal (for agreement; nothing built yet)

Goal: `ATIRageProDVDDriver.bundle` implementing the `DVDDriver*` interface (see docs/re-notes/r128-dvd-driver/), loaded by DVD.framework on machines
without VSP/AltiVec (G3 Lombard) via the accelerator property `IODVDBundleName`. Host does bitstream parse + dequantisation (framework); bundle does IDCT; Rage does motion compensation (+ residual add if provable).

## Constraints (user, Nov 27)
- Target G3: compile -mcpu=750, no AltiVec; integer math and table lookups; NO double (avoid float too); minimal threads (default: zero extra threads, decode runs in the framework's caller thread; GPU is the parallelism).
- No CPU-speed simulation; correctness via bundle-loading simulation; performance = make the bundle as fast as possible.

## Pipeline per picture (DVDDriverDecode)
1. Read MbRec array + coefficient stream (spec A). Coefficients arrive dequantised (spec D).
2. IDCT (host): 8x8 integer IDCT incl. ISO 13818-2 mismatch control (framework does none; see spec D) (fixed point, 16-bit intermediates, row/column with precomputed constant table; sparse-block shortcuts: DC-only, run-limited rows) -> residual blocks.
3. Prediction + residual:
   - M1 (reference path, correctness oracle): CPU does MC + residual into the destination frame buffer (half-pel integer interpolation, rounding per MPEG-2 spec). Also used as fallback.
   - M2 (Rage MC): emit MC via our kext (LT Pro scaler regs 0xE0-0xE4, SCALE_3D_CNTL blend ONE/ONE with signed dst clamp as in Win98 init - the likely residual-add mechanism: IDCT residual uploaded as a signed plane, then blended on top of / before MC). Compare frame by frame against M1.
4. Show: ShowMPBuffer/EnableMP/SetMPRects/GetKeyColor via our kext overlay (the R128 bundle programs no overlay registers; kext owns overlay).
5. Sub-picture: port the software DCSQ/SP blend (spec C) - on the host or via our overlay path; decide after M1.

## Kext interface (to define)
Own user-client selectors modelled on the R128 set (open/close, ring map, show/enable MP, rects, SP upload, sync). Ring/command format is ours; R128's type-3 0x31 packet is NOT reusable (LT Pro mach64 registers). Must address cache coherency (CPU-written residual/reference planes: dcbf/dcbst before GPU reads; dcbf before CPU reads of GPU-written frames - the earlier B10 lesson).

## Milestones (each verified before the next; static-first, live only to verify implemented code)
- M0 stub bundle: all exports, returns success, no output; loaded by patched-framework rig (private DVD.framework copy, VSP probe string changed, DYLD_FRAMEWORK_PATH) -> proves selection/loading.
- M1 CPU-only reconstruction, then compare against the stock VA path output (reference frames) for correctness.
- M2 Rage MC + host residual; M3 SP/buttons; M4 timing/optimisation (profile per phase before optimising); M5 B-frames/field/dual-prime coverage.

## Open questions (must be resolved statically before M2)
Residual-add mechanism and rounding on LT Pro; MC register semantics for 0xE4 trigger/size codes; dual-prime handling (R128 has no distinct handler, so framework likely converts); what DVD.framework does when no bundle loads on a G3.

## M0 result (Nov 28, MDD, live): PASS
Rig: tools/dvd/m0_rig.sh (backs up DVD.framework's binary, patches the VSP probe symbol 'VSPMaximumThreads'->'VSPMaximumThreadX' => probe fails like on a VSP-less G3; installs dvdbundle/ATIRageProDVDDriver.bundle; `off` restores byte-identical). Kext personality IODVDBundleName = ATIRageProDVDDriver.
DVD.framework selected and loaded the stub via the hardware-assist path and called: InitializeLibrary(services, mask, cb3, cb4) -> GetVersion -> OpenDevice(out, maxSize, display, a4..a6, flagsOut, _, &nMP, &nSP) -> SetMPRects -> ClearMP/GetKeyColor/EnableButton/EnableSP/GetSPBuffer/ClearSP/ShowSPBuffer/SetSPPalette -> EnableMP(1) -> SetMVLevel -> then per frame Decode(ctx, params, rect) + ShowMPBuffer(idx 0..3 cycling). 490 Decode + 485 ShowMPBuffer calls in 15 s; DVDPlay ran with no errors (no video, as expected for a stub). Trace: docs/re-notes/r128-dvd-driver/M0_call_trace.txt. Stub has no AltiVec instructions (-mcpu=750).
Findings: Decode receives (ctx, params, rect) as predicted; ShowMPBuffer cycles 4 buffers (OpenDevice reported 5); 720x480 content, 16bpp main display => GetKeyColor 0x40.

## M1 result (Nov 28, MDD + host reference): PASS for frame pictures
dvdbundle/recon.c: integer Chen-Wang IDCT (IEEE 1180 accuracy test passes: dvdbundle/test_idct.c), ISO 13818-2 mismatch control, half-pel MC for all frame-picture types, field DCT, residual add; run inside the stub through the M0 rig; decode-order frames dumped (DVDSTUB_YUV/DVDSTUB_FRAMES/DVDSTUB_DUMPDEC=1) and compared with ffmpeg's decode of the same VOB (tools/dvd/m1/cmp2.c matches each dumped frame to the best reference frame; ShowMPBuffer is time-driven so the framework drops/repeats frames).
| stream (ffmpeg authored, 720x480) | coverage | result |
|---|---|---|
| TESTDVD (I,P progressive) | intra, forward | 150 frames, min PSNR 70.0, maxdiff 2 |
| v1 (-bf 2) | + backward, bidirectional (B) | 150 frames, min PSNR 72.2, maxdiff 2 |
| v2 (interlaced, ildct/ilme, bf 2) | + field-pred types 5/6 (fwd/bwd), field DCT (flags!=0) | 133 frames, min PSNR 72.2, maxdiff 2 |
| v3 (-alternate_scan 1, bf 2) | alternate scan | 147 frames, min PSNR 72.2, maxdiff 2 |
Established by the runs: params +6/+7/+8 = dest/forward/backward buffer index; params +4/+5 = alternate_scan flag per pass; MbRec.flags != 0 = field DCT (frame pictures); mc types 1/2/3/5/6 semantics as in recon.c; intra DC needs +1024 (framework predictor starts at 0); mv units = half-sample, chroma = trunc(mv/2).
NOT covered yet (no ffmpeg-authorable content; need another encoder or real discs): field pictures (st 1/2, incl. P second field referencing the first), 16x8 MC (types 5-7 in field pictures), type 7 (bidirectional field-pred), dual prime, odd picture sizes, 625-line PAL.

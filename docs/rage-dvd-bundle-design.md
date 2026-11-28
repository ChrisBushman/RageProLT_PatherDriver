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
### M1 extension: field pictures, 16x8, dual prime (Nov 28)
ffmpeg's mpeg2video cannot write field pictures or dual prime, so the test streams come from mjpegtools 2.2.1 `mpeg2enc -I 2` after fixing its field-picture mode
(tools/dvd/m1/mpeg2enc/mpeg2enc-2.2.1-fieldpic-fixes.patch; reproduce with tools/dvd/m1/make_field_discs.sh). Stream quality is irrelevant: ffmpeg's decode of the same stream is the reference, our stub's decode is compared to it.
Upstream bugs fixed: (1) the second field's reference to 'the same frame' used a blank per-Picture reconstruction buffer (fields now share one frame buffer), (2) intra `clearblock` used the frame stride instead of the field stride (reconstruction garbage), (3) a sequence header was written between the two fields of frame 0 (illegal, desyncs decoders), (4) NULL previous-reference at stream start (crash; first frame's second field is now coded I).
| stream | coverage (macroblock counts from the framework records) | result |
|---|---|---|
| disc4 (-I 2 -R 0) | field pictures, I/P, field MC (type 1), 16x8 two-vector (type 5, ~7300 MBs), P second field referencing the first field of the same frame | 91 dumps; all frames identical to ffmpeg (PSNR>=70dB) except the last two (see below) |
| disc5 (-I 2 -R 2) | + B field pictures: fwd/bwd/bi 16x16 (types 1-3), 16x8 fwd/bwd (5/6, ~35000 MBs), bidirectional 16x8 (type 7, 13 MBs) | all frames identical except (a) the last dump (single-field Decode call) and (b) 3 reference frames with 1-2 wrong 16x8 lower-half MBs, see below |
| disc6 (--dualprime-mpeg2) | 44 dual-prime MBs in 90 P field pictures | identical except at the dual-prime MBs; error then propagates through following P pictures (drift) |
Findings:
* A Decode call normally carries BOTH fields (structure 1,2); the framework passes `ptype[2]`, `structure[2]`, aux[2] (alternate_scan per field) and we continue from saved MB/coefficient pointers for the second pass.
* **Dual prime is not representable**: the framework's MbRec for a dual-prime MB is a plain type-1 (forward) record with a single vector (the derived opposite-parity vector and the averaging are not present in the record); decoded output therefore differs from the reference at exactly the encoder's dual-prime MBs (verified by logging the encoder's dual-prime positions). Any record-based hardware path (including Apple's own R128 driver) shares this limit. Dual prime requires a stream without B pictures (low-delay profile); it is not expected on DVD-Video content. Nothing to fix in the bundle.
* Lower-half 16x8 vectors in B field pictures: in 3 macroblocks the vector in the framework record (e.g. (-52,16) half-pel, bottom field, type 6 lower half) differs from what ffmpeg decodes; our output is an exact copy from the reference frame at the framework's vector (verified by exhaustive source search), so the difference is in the framework's motion-vector prediction (PMV) for 16x8 in field pictures versus ffmpeg, upstream of the bundle. Not DVD-relevant in practice (needs 16x8 field MC in B fields).
* The last Decode calls of a title can carry a single field (`st=1,0` then `st=2,0`, different destination buffers). rc_decode now skips the second pass when the second structure is not 1/2 (previously it would have read past the macroblock array). Those two pictures are half-decoded by nature (not matched by the reference).
### M1 size coverage (Nov 28): PASS
All DVD-legal sizes decode identically to ffmpeg (min PSNR >= 69 dB, max pixel difference <= 3), tools/dvd/m1/make_size_discs.sh + run_sz.sh:
| disc | system / size | structure | frames compared | result |
|---|---|---|---|---|
| 8 | NTSC 704x480 | progressive, B | 145 | pass |
| 9 | NTSC 352x480 | interlaced (field MC + field DCT) | 80 | pass |
| 10 | NTSC 352x240 | progressive, B | 150 | pass (maxdiff 3) |
| 11 | PAL 720x576 | interlaced | 56 | pass |
| 12 | PAL 704x576 | progressive, B | 119 | pass |
| 13 | PAL 352x576 | interlaced | 61 | pass |
| 14 | PAL 352x288 | progressive, B | 150 | pass (maxdiff 3) |
| 15 | PAL 720x576 | field pictures, B (mpeg2enc) | 56 | pass except the final single-field Decode call (end-of-title artifact) |
M1 coverage is therefore complete for everything the encoders can produce. Remaining untested: real-disc content, and the two framework-limited cases above (dual prime; lower-half 16x8 in B fields).

### GeForce / Apple AltiVec decoder as an oracle (Nov 28, partial)
Method (works): play a disc on the GeForce through the Tiger GL stack wrapper (`/Library/TGL/tgl-run`, sets `DYLD_FRAMEWORK_PATH=/Library/TGL/Frameworks`; player `tools/dvd/dvd_play_nv.c`), with `tools/dvd/gl_yuv_tap.c` inserted (`DYLD_FORCE_FLAT_NAMESPACE=1 DYLD_INSERT_LIBRARIES=...`; real GL entry points must be resolved with NSAddImage on `OpenGL.framework/Libraries/libGL.dylib` + NSLookupSymbolInImage, dlsym/RTLD_NEXT find the wrapper again and recurse). It dumps the AltiVec driver's client-storage YCbCr 4:2:2 texture (GL_YCBCR_422_APPLE, 720x480, `GL_UNPACK_ROW_LENGTH` 736, luma = byte 1 of each 16-bit pair, 4 rotating buffers; snapshot taken one upload later). The rig must be OFF (original DVD.framework).
Findings: the top field of every captured picture (I, P and B, disc5) matches ffmpeg (mean |d| 0.05-0.1). The bottom field matches no ffmpeg field (the framework calls the driver's `DVDVideoDeinterlace` on the display path, bottom lines are roughly the average of their neighbours), so the 16x8 lower-half B-field question (anomaly is in the 2nd, bottom field) cannot be decided from this output. A bottom-field-first stream (disc7, `-z b`) moves our stub's mismatches to the top field, i.e. the anomaly follows the SECOND field, not the parity; the GeForce capture of disc7 is too noisy to attribute (the player shows ~65 of 90 pictures, overlay digits make neighbouring pictures ambiguous), so it is NOT a verification either way.
Open: find how DVDPlayback decides to deinterlace / avoid it, or tap the AltiVec driver's decode buffer before `DVDVideoDeinterlace`.

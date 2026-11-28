# Planar YUV 4:2:0 through the hardware overlay scaler (Nov 28)

Why: the DVD bundle's pictures are planar YUV 4:2:0; the LT Pro scaler reads that format directly, so showing a frame costs no CPU time and no 3D-engine time (no YUV->RGB step anywhere).
The existing `RAGE_GL_OVL_ON` only takes RGB because it was written for the GL present path; that is an API gap, not a chip limit.

## Step 1: what the sources say (all static)
- [PRG 8.4 / 8.6] scaler source formats: RGB555/565/8888, YUV9 planar, YUV12 planar, packed 4:2:2. YUV12: U and V are exactly half the Y size in both directions; "the Y/U/V pitches are specified in terms of pixels, the Y/U/V offsets are byte offsets". YUV (not RGB) sources get bilinear interpolation.
- [RRG] SCALER_BUF0_OFFSET_U/V = block-1 idx 0x75/0x76, SCALER_BUF1_OFFSET_U/V = 0x77/0x78 (window offset 0x1d4, 0x1d8, 0x1dc, 0x1e0) - present on the LT Pro (the register reference lists them), xf86-video-mach64 marks them "GTPro".
- [W98] `overlay-hal.c` FUN_b00bcc8c (writes 0x1d4-0x1e0): for YUV12 the first chroma plane follows the luma plane and the second follows that (each luma_size/4); in the default (YV12) case the FIRST chroma plane goes to BUF_V (0x1d8/0x1e0) and the second to BUF_U (0x1d4/0x1dc); the IYUV case swaps them. FUN_b00b3940 region: VIDEO_FORMAT.SCALER_IN = 0xa for YV12/IYUV; BUF_PITCH = luma pitch in BYTES (packed formats: bytes/2); everything else (increments, coefficients, colour control, key) as the RGB path. Flip rewrites BUF0/BUF1 offsets (and U/V) - same as RGB.
- NOT established: the chroma pitch register (there is only one pitch register; Win98's layout has chroma pitch = luma pitch / 2, so that is required), whether SCALE_CNTL bit 7 matters (not written, as for RGB), the exact colour-conversion matrix/range (SCALER_COLOUR_CNTL neutral 0x00101000, Win98 default).

## Step 2: difference from the RGB path
Same registers as OVL_ON in the same order, plus BUF0/1_OFFSET_U/V (4 writes), SCALER_IN = 0xa, BUF_PITCH in bytes. 25 register stores in total (verified in the disassembly: 25 stwbrx; the offsets 0x1d4/0x1d8/0x1dc/0x1e0 and 0x34/0x38/0x3c are the immediate loads before them).

## Step 3: kernel interface
`RAGE_GL_OVL_ON_YUV` (in 1: user pointer to `RageOvlYuv`), `RAGE_GL_OVL_FLIP_YUV` (in 3: offY, offU, offV), OVL_OFF shared. Kernel checks: owner owns Y (pitch*srcH), U and V (pitch/2 * srcH/2); offsets multiples of 8; pitch multiple of 16; srcW <= 720, even size; one overlay; destination inside the display; overlay idle. Freeing a U/V block, closing the client or a mode change disables the overlay like the RGB case. The RGB `OVL_FLIP` refuses a YUV overlay.

## Step 4: test
`tools/ragegl/ovl_yuv`: 704x480 BT.601 colour bars + grey ramp + diagonal/grid in three planes (pitch 768/384), shown on the 1024x768 CRT, flipped to a second set after the hold. Results: see below.

## Result (Nov 28, MDD, 1024x768 16 bpp CRT) - user's observation of `ovl_yuv`
Eight bars in the right order (white, yellow, cyan, green, magenta, red, blue, black), reversed correctly after the flip (`RAGE_GL_OVL_FLIP_YUV`); bottom quarter: smooth black-to-white ramp, then white grid on black with the diagonal line from upper left towards lower right (about the left three quarters of that panel, as drawn: 2 px per row); clean image, no distortion. So: U/V plane assignment (first-chroma-plane = V register, as Win98) is right with planes given as Y, Cb, Cr; the one-pitch-register assumption (chroma pitch = luma pitch / 2) holds; YUV12 is displayed with no CPU or engine conversion.
`ovlDisableLocked` now also returns the whole block-1 overlay state (geometry, increments, buffers, colour, coefficients, planar U/V offsets, VIDEO_FORMAT.SCALER_IN) to the idle all-zero values seen before any overlay use.

## Integration in the bundle (done in the tree, hardware verification BLOCKED)
`rch_show()` (recon_hw.c): hardware mode points the overlay at the picture buffer already in VRAM; CPU mode uploads the host planes to a two-set VRAM ring first; destination = picture fitted into the display (square pixels, centred, integer math); `ShowMPBuffer` calls it unless `DVDSTUB_NOSHOW` is set (the dump/compare scripts set it); `tools/dvd/m1/run_show.sh N secs [cpu]` plays a disc with the frames shown.
BLOCKER found while testing: since about 00:29 on Nov 28 the framework's open-device callback (`cb4`, AVA) returns error 1000 on the MDD for every run with the rig on - including the bundle from commit 155ce42 that passed all discs earlier, with the original mcprod1 kext, and after restoring the idle overlay registers. Rig off (software VSP path) and plain GL (cgl_probe) work. Not caused by the overlay selectors or the bundle; cause not yet found.

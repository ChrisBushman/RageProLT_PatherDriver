# Step 4: hardware overlay scaler behind `RAGE_FEAT_OVERLAY` - static analysis and plan (Nov 25)

Status: **analysis only; nothing written to hardware.** Every hardware step below needs the user's "proceed" after the exact write list is shown.
Sources (label in brackets): [RRG] register reference index (block-1 indices); [ATI-X] xf86-video-mach64 `atiregs.h` (field masks); [PRG] Rage PRO Programmer's Guide ch. 8; [W98] Win98 ATI 4.13.1.2655 HAL decompile (`win98-overlay-scaler.md`, evidence `win98re/evidence/overlay-hal.c`); [OBS] observed on our hardware.

## 1. Why it is worth doing
The stretched/converted present costs the 3D engine **13-14 ms per 1024x768 frame** (measured, `fs_test`), i.e. most of a 60 Hz frame, every frame, for every full-screen context smaller than the screen. The overlay scaler reads the render target **directly from VRAM at scan-out time**: zero engine time, zero copy, bilinear in both directions [PRG 8.4.4], and the 16 bpp target needs no 16->32 conversion. For a 640x480 game on a 1024x768 CRT that turns "engine busy 83% presenting" into "engine free". Fallback when the overlay is unavailable or refused: the existing textured-quad present (unchanged).

Limits that decide when it applies:
* One overlay only (`dwMaxVisibleOverlays = 1`) [W98].
* **Source width <= 720 px on this silicon** (384 on rev A VT/GT; 720 on rev B and later) [PRG 8.4.2]; Win98 uses 768 for family < 5 and 720 for family >= 5 [W98]. The chip's PCI revision must be read to choose (a config-space read, no hardware change). Larger targets (800x600, 1024x768 sources) must keep using the quad.
* Source offset and pitch must be **quadword (8-byte) aligned** [PRG 8.4.2]. Our targets are page-aligned with pitch in multiples of 8 B: satisfied.
* Source formats we can supply: 15 bpp (SCALER_IN 3), 16 bpp (4), 32 bpp (6) [ATI-X, W98]. Our engine-written targets are in exactly the pixel format the CRTC scans, so the same raw bytes are the scaler's input (to be confirmed once on hardware).
* LCD (Lombard): the chip has its own panel expansion; the Win98 driver recomputes H_INC for it (`FUN_b00bdf20`, **not traced**). On the Lombard the overlay is probably only useful for sources that panel expansion does not already cover, so its value there is **unestablished**; the G4 CRT rig is the case that gains.

## 2. Registers (block 1 = byte offset index*4 from the register window base 0x7FF800 [PRG 8.x address equation]; the kext's `fRegs` window already covers it: block 1 at 0x000-0x3FF, block 0 at 0x400-0x7FF)
Index/offset from [RRG]; fields from [ATI-X]; values from [W98] unless noted. "Doc" = every bit we would write is named in a source; "UNEXPLAINED" = bits we will NOT write (or only write after a read-only observation).

| Reg (block-1 idx, off) | We would write | Basis |
|---|---|---|
| OVERLAY_Y_X_START (00, 0x000) | `x<<16 | y`, **LOCK bit 31 = 0** | Doc: Y[9:0], X[25:16]; bit 31 = LOCK (we never set it) |
| OVERLAY_Y_X_END (01, 0x004) | `((x2-1)<<16) | (y2-1)` | Doc (same fields) |
| VIDEO_KEY_CLR/MSK (02/03), GRAPHICS_KEY_CLR/MSK (04/05) | 0 / 0 (unused for full cover) | Doc |
| OVERLAY_KEY_CNTL (06, 0x018) | **0x00000011** = VIDEO_FN[2:0] 1 (TRUE) \| GRAPHICS_FN[6:4] 1 (TRUE), CMP_MIX bit 8 = 0 (OR): overlay always shown | Doc fields only. Win98's "no key" shadow value 0x8000211 has bits 9 and 27 that are **UNEXPLAINED** (it is a driver *state* slot, `puVar5[0xd]`, not proven to be the raw register): we do not write them. Idle/hide value 0x110 [W98] (FALSE AND TRUE = never video) is the safe "off" state |
| OVERLAY_SCALE_INC (08, 0x020) | `(H_INC<<16) | V_INC`, `INC = (src << 12) / dst`, **truncated** | Doc [PRG 8.8.1], integer math (G3 rule). 640->1024: 0x0a00 |
| OVERLAY_SCALE_CNTL (09, 0x024) | `SCALE_EN (31) | OVERLAY_EN (30)` = 0xC0000000, bits 26/27 (BANDWIDTH/DIS_LIMIT) left 0 as Win98 does on family > 2 | Doc. **Bit 7 UNEXPLAINED**: Win98 sets it when PDEV+0x248 bit0 == 0, clears it otherwise, family 7 only; what 0x248 means (output-device state, LCD vs CRT?) is not traced. We write bit 7 = 0 only after the read-only step shows the idle value |
| SCALER_HEIGHT_WIDTH (0A, 0x028) | `(srcW<<16) | srcH` | Doc |
| SCALER_BUF0_OFFSET / BUF1_OFFSET / BUF_PITCH (0D/0E/0F) | target VRAM byte offset (both), pitch in **pixels** (bytes/2 for 16 bpp, /4 for 32 bpp) | Doc [W98]; alignment [PRG] |
| VIDEO_FORMAT (12, 0x048) | SCALER_IN [19:16] = 3 / 4 / 6; other bits preserved | Doc |
| CAPTURE_CONFIG (14) | SCALER_BUF select = buffer 0; **OVL_BUF_MODE/OVL_BUF_NEXT (28/29) semantics UNEXPLAINED** -> not used; flip by rewriting BUF0 offset as Win98 does | PRG 8.8.1 2b; W98 Flip |
| SCALER_COLOUR_CNTL (54, 0x150) | 0x00101000 (neutral: Usat/Vsat 0x10, brightness 0) | Doc [W98] |
| SCALER_H_COEFF0-4 (55-59, 0x154-0x164) | 0x00002000, 0x0d06200d, 0x0d0a1c0d, 0x0c0e1a0c, 0x0c14140c | [W98] family > 2 defaults (PRG: "a function programs these to acceptable defaults") |

Not touched: CRTC, PLL, DSP/FIFO registers, engine registers, anything in block 0.

## 3. Design (follows the established serial kernel-owned pattern: design 11.16, dma-async-measurement.md; no threads)
* New kernel selector `RAGE_GL_OVERLAY` (ABI add): `{target offset, pitch, w, h, format}` + `{dx, dy, dw, dh}` + enable/disable. The **kernel** validates (offset inside a live allocation owned by that client, 8-byte alignment, w <= revision limit, dst inside the current display, format in {15,16,32}) and computes the increments in integer math; the client never writes block 1 (the command guard covers block 0 only). `RAGE_FEAT_OVERLAY=0` removes the selector.
* One owner at a time; programming is done with the engine idle (`idleWait`), in a fixed order: hide (KEY_CNTL 0x110) -> geometry/format/buffers/increments/coefficients -> colour -> key -> SCALE_CNTL enable last. Disable = KEY_CNTL 0x110 then SCALE_CNTL 0.
* Flip: rewrite BUF0 offset (and keep BUF1 equal) when the GL context swaps; tearing is accepted for the first version (hardware double-buffer bits unexplained).
* Ownership/cleanup (risks named in the existing design): the overlay is disabled when the owning client closes or dies (otherwise the scaler keeps reading freed VRAM), on `will-change` of the display (mode generation), before the target is freed or reallocated, and on sleep. A VRAM block in use as a scaler source cannot be freed by the client while the overlay is enabled.
* Bundle policy (replaces the quad when `fs_scale` and w <= 720): render target = scaler source; swap -> rewrite BUF0; fall back to the quad if the selector refuses.
* Capabilities reported through `RAGE_GL_INFO` feature bits (existing `RAGE_FBIT_OVERLAY`).

## 4. Risks / unknowns (all would be observed, not guessed)
1. **Bandwidth / display FIFO**: scaler fetch + CRTC fetch from the same 8 MB; an underrun shows as wobble/garbage. The 32 bpp DSP_ON discrepancy is already open. First test at 15 bpp 640x480 -> 1024x768 only.
2. KEY_CNTL "always video" assumption (TRUE|TRUE under OR shows the video): inferred from the consistent Win98 values 0x110 (hidden), 0x114 (source key), 0x50 (dest key, cross-checked with the X driver); verify by the first picture. Fallback is the X driver's destination-key mode 0x50 with the key colour painted into the scanout.
3. Bit 7 of SCALE_CNTL and the LCD path as above; mouse cursor interaction (hardware cursor over/under the overlay) untested.
4. Source width limit on this revision (read PCI revision first; probe 720 vs 768 only if agreed).
5. The overlay is composited after the CRTC, so scanout readbacks (`ragegl_scanout_read`, screenshots) will not show it; tests need a camera/monitor or a different readback.

## 5. Proposed hardware steps (each needs "proceed")
**A. Read-only dump (no writes).** Read block 1 indices 0x00-0x15 and 0x54-0x59 (about 28 registers) while the display shows an ordinary 640x480 15 bpp console, plus PCI revision (config read). Purpose: see idle values (notably SCALE_CNTL bit 7, SCALER_TEST, whether the overlay is already enabled by the BIOS) before writing anything. Implementation: a debug-build-only selector (`RAGE_DEBUG_ENGTEST`) or `stubctl` read command - no release code.
**B. Static overlay, 640x480 -> 1024x768.** Write list = section 2 table, in the section 3 order, for a buffer that a tool first fills with a known test pattern (colour bars + a grid via the existing engine fill, in a private allocator block, 15 bpp, pitch 1024 px). Hold 25 s, then disable (KEY_CNTL 0x110, SCALE_CNTL 0) and restore the console. You judge the picture on the monitor.
**C. Bundle integration**: `RAGE_GL_OVERLAY` selector, policy in `present_rects`, gates (gl_suite 291/291, gl_probe 128/1, fs_test with an overlay phase), cleanup-on-death test, then a timed comparison against the quad.
**D. 16/32 bpp sources and 720 vs 768 limits** only if A-C are clean and you want them.

## 6. Step A result (Nov 25 22:3x, G4, 640x480 15 bpp console, nothing written to the chip)
Tool: `tools/ragegl/ovl_dump` through the debug-only, fixed-list, read-only selector `RAGE_GL_REG_DUMP` (kernel `RageLTDevice::regDump`; absent from release builds).
* **All 28 block-1 registers read 0** (OVERLAY_*, SCALER_*, VIDEO_FORMAT, CAPTURE_CONFIG, SCALER_COLOUR_CNTL, SCALER_H_COEFF0-4): the overlay is idle and the BIOS did not enable it. The idle SCALE_CNTL bit 7 is 0, so it gives **no** information about the unexplained bit-7 meaning, and all-zero cannot yet tell "idle" from "does not latch": step B starts with one harmless write+readback.
* Block 0 (sanity): CRTC_H_TOTAL_DISP 0x004f0063 (640 wide), CRTC_V_TOTAL_DISP 0x01df020c (480 high), CRTC_OFF_PITCH 0x14000000, CRTC_GEN_CNTL 0x4b0b0300 (pixel-width field 3 = 15 bpp), MEM_CNTL 0x14751a7b, CONFIG_CHIP_ID 0xdc004c49.
* PCI config 0x08 = 0x030000dc: class 0x030000 (VGA), revision 0xdc. The Win98 HAL classes 0x4c49 as family 7, whose source limit is 720 px (the PRG's 384 is for rev-A VT/GT).
* **First attempt crashed the G4 (my bug, no hardware involvement).** The kernel-stack array in `selRegDump` was `uint32_t pairs[48]` (48 words) but the dump returns 35 pairs = 70 words: the overflow ran past the frame and overwrote the saved return address with the word at index 58 = the tag of block-0 entry 29 = 0x08. The panic shows exactly that: "0x400 Inst access, PC=0x8, LR=0x8", no module in the backtrace, from a user syscall. Fixed: `pairs[2 * kDumpPairs]` and a count check; verified in the disassembly (frame 0x1e0, array ends at +0x1c0 where `n` begins). Lesson recorded: size buffers in the unit the callee counts (pairs vs words), and read the disassembly of any kernel stack buffer before the first live call.

Step B therefore begins with sub-step B0: write one value to OVERLAY_VIDEO_KEY_CLR (0x008, a colour-key register with no visible effect while the overlay is disabled), read it back, restore 0.

## 7. Step B0 result (Nov 25, G4)
`tools/ragegl/ovl_b0` (debug-only selector `RAGE_GL_OVL_B0`; disassembly checked first: the only stores are two to window offset 0x008). OVERLAY_VIDEO_KEY_CLR: before 0x00000000, wrote 0x005a5a5a, **read back 0x005a5a5a**, restored to 0x00000000 and confirmed by a final readback (and by a following full dump). **Block 1 latches writes**, so the all-zero state in step A is the real idle state, not "unreadable". Display unaffected (overlay disabled). Next: step B (static overlay, needs the user's "proceed" on the section 2 write list).

## 8. Step B result (Nov 25, G4, 1024x768 15 bpp CRT, `ovl_b 30`)
Debug-only selectors `RAGE_GL_OVL_ON/OFF` (kernel validates: owner-owned source block, 8-byte offset, pitch multiple of 4 px, width <= 720, destination inside the display, integer truncated increments; auto-disable when the owning client closes). The 21 register stores were read from the disassembly and matched the approved list in order before the run. Readback after the run: Y_X_END 0x03ff02ff, SCALE_INC 0x0a000a00, HEIGHT_WIDTH 0x028001e0, BUF0 0x00180000, BUF_PITCH 0x400, VIDEO_FORMAT 0x00030000, COLOUR_CNTL 0x00101000, final SCALE_CNTL 0 / KEY_CNTL 0x110 (disabled).
**Observed by the user on the monitor: a stable picture, no wobble or flicker (so no visible display-FIFO underrun at 15 bpp 1024x768 with the scaler fetching a 640x480 source), light/dark grid lines as drawn, and the console returned normally after the overlay was disabled.** KEY_CNTL 0x011 (documented bits only) does show the video unconditionally; SCALE_CNTL bit 7 and Win98's 0x8000211 were not needed. Not yet checked: bar order/colour correctness in detail (only "looked right"), smoothness vs blockiness, behaviour at 32 bpp/800x600 sources, mouse cursor over the overlay, mode change while enabled.

## 9. Step B2 result (Nov 25 22:43-22:45, G4, 1024x768 15 bpp CRT, 150 s hold, questions asked while the picture was up)
First B2 attempt: `OVL_ON` refused (0xe00002e2): my idle check demanded KEY_CNTL == 0 but our own disable leaves the hidden value 0x110. No register written (the guard fired first). Fixed: idle = SCALE_CNTL 0 and KEY_CNTL in {0, 0x110}; write list unchanged (21 stores, re-read from the disassembly). A second run was missed by the user because the tool's stdout was buffered over ssh (fixed: `setvbuf`), so the third run announced the picture live.
User's observations: (1) bar order correct; (2) edges clean (suggestion: add a diagonal shape to judge stair-stepping next time); (3) grid good; (4) **mouse pointer not visible**; (5) image covers the whole screen; (6) stable for the whole hold. Registers read back while the overlay was live: Y_X_END 0x03ff02ff, SCALE_INC 0x0a000a00, HEIGHT_WIDTH 0x028001e0, BUF0/1 0x00180000, PITCH 0x400, VIDEO_FORMAT 0x00030000, KEY_CNTL 0x11, SCALE_CNTL 0xc0000000.
Pointer: RageStubFB has no cursor code, so the generic IOFramebuffer software cursor is used, which draws into the scanout memory *under* the overlay; KEY_CNTL 0x11 shows video everywhere, so the pointer is hidden by design. Option if a pointer is wanted over the overlay: destination key (KEY_CNTL 0x50, documented/cross-checked with the X driver) with the key colour painted into the scanout; on a Lombard the Apple NDRV hardware cursor may composite differently (untested, cannot be tested on the rig).

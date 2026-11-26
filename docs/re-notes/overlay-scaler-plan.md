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

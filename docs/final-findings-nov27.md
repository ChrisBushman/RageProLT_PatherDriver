# Experiment findings and recommendations (Nov 27) -- for review

Rig: G4 MDD, real Rage LT Pro (640x480, 60 Hz, 16 bpp 5-5-5 / 32 bpp), RageStubFB, Panther 10.3.9. "Measured" = run on the rig this session; "static" = read from code/disassembly; "not established" = no evidence. Raw tools: `tools/rig/*`, `tools/ragegl/*`.

## 1. Gains (measured)

| Item | Result | Verdict |
|---|---|---|
| Hardware 2D client (B1b), real WindowServer, scrolling a TextEdit window (240 wheel events) | WindowServer CPU 2.06-2.15 s (CPU path) vs 1.29-1.38 s (hardware copies): **about 36% less**; one engine copy per event; pixels correct | keep (already built) |
| Hardware 2D client, window moves (240 AppleScript moves of a 540x380 Finder window) | 2.26-2.62 s vs 2.0-2.4 s: at most 10%, within noise | keep; gain is mostly scrolling/copies |
| Real Quartz op mix (Finder view switches, resizes, navigation) | almost no engine ops (3 copies, 0 fills in the whole run), 0 refused. Most of the desktop is Quartz drawing into the backing store plus a CPU flush | 2D engine helps scroll/copy, little else |
| Write-combine for WindowServer's framebuffer map (B1g-5) | user-space `memcpy` into the **default** (inhibited) VRAM mapping already runs at 122 MB/s; write-combine also 122 MB/s (PCI ceiling). Paced flush benchmark (100 ms/frame, 480 KB): 5.7-6.1 ms per frame with write-combine, 5.7-9.2 ms without. **No measurable gain.** The 3.9x seen earlier was for 4-byte stores only (31 -> 121 MB/s) | leave OFF (default) |
| **Write-combine mapping for the kext's own texture upload (`vramCopyIn`)** | 256 KB upload: 8.55 -> **2.65 ms**; 1 MB: 34.3 -> **10.6 ms**; end-to-end 2.76 ms against 7.5 ms for the DMA path; `xfer_test` (byte-exact readbacks, 4 concurrent clients) and `gl_suite` 291/0 pass. Kernel `bcopy` is *slower* (15.7 MB/s) so the gain needs the write-combine mapping + word stores | **recommend: make it default** (`-DRAGE_VRAM_WC=1` today, off). Then B8 (DMA texture upload) is not worth building further: DMA's call costs 2.1 ms of CPU and its transfer takes 7.5 ms; write-combine CPU stores take 2.65 ms and finish |
| Corrected CPU-path figure | WindowServer's flush uses wide stores: ~120 MB/s writes (not the 44 MB/s measured with 4-byte stores). Reads from VRAM stay ~10 MB/s, which is why hardware copies (210-235 MB/s) win scrolling, and fills (250-280 MB/s) win by about 2x | doc corrected |

## 2. v-sync (B9, measured; kext debug switch, default OFF)
- Present today is an immediate engine blit (no v-sync): 2.4 ms for 640x480x16 (~250 MB/s), 412 fps unthrottled. Display refresh is **60 Hz** (frame 16.7 ms; vblank ~1.4 ms).
- A blit started at the beginning of vblank runs ~5x faster than the beam reads, so it stays ahead of the beam: **should not tear** (static reasoning; the visual check is queued for you).
- Polled wait on `CRTC_VLINE_CRNT_VLINE`: locks presents to 59.8 fps but **burns 100% of a CPU**. A single read of that register sometimes returns an out-of-range value (seen 0x1ff, ~7% of waits ended early) so the poll needs a second confirming read (implemented).
- `IOSleep(1)` on Panther takes 1.009 ms (max 1.06 ms): sleep-then-spin works. 59.8 fps kept, CPU **27%** with a 2.5 ms spin margin (of which 2.4 ms is waiting for the previous blit), 32% at 3.5 ms, 40% at 4.5 ms. The lazy fence idea does not apply: the standalone driver does not page-flip.
- Recommendation: implement v-sync as sleep-then-spin (margin 2.5 ms) behind a per-context default decided after your eyes-on check of tearing with it off/on. Interrupt-driven wait is not justified (remaining CPU is the blit wait).

## 3. Robustness and soak (measured)
- 2D client: 5 x `kill -9` of a process in the middle of a stream: 0 leaked clients, no panic. 4 mode changes (800x600x16, 640x480x32, 800x600x32, 640x480x16) with a looping 2D stream: no panic, no leak, `twod_test` and `gl_suite` (291/0) pass afterwards.
- **D3 soak** (Apple FIFO formula, the default): 24 min = Flurry 240 s, Atlantis 240 s (128 presents/s), Sonic R 240 s, 15 process cycles. No panic; IOMalloc +176 bytes in total, instance counts flat. PASS.
- **D5**: the stub offers only 16 bpp (5-5-5) and 32 bpp (static: `getPixelFormats`; `cgmode ... 8` reports no exact mode). 16 bpp 5-5-5 was already tested in the 7 rig modes. Nothing more to test on this rig.
- **Test artefact**: unloading/reloading the kext under a running WindowServer leaves WindowServer spinning at ~85-100% CPU and, after one such, a stuck WindowServer that survived `killall loginwindow` (needed `kill -9`). Not a product path (the kext loads before WindowServer), but any rig script that reloads must restart the session.

## 4. Bug found: A6 off-display GL context (measured) -- needs your decision
`tools/ragegl/cgl_offscreen.c` (CGL off-screen pixel format + Rage display mask, system-memory drawable). Hardware renderer is selected ("ATI Rage LT Pro (mach64)"), clear and triangle read back correctly through `glReadPixels`, **but** (a) the context's frames appear **on the real Rage screen** (a screenshot after the test shows a full red screen with the green triangle) and (b) the drawable's own memory is never written (CGLFlushDrawable leaves it zero). So an off-screen context corrupts the visible display and CGL off-screen apps would get no image. Not yet fixed: the fix is in the bundle's present/flush path (off-screen contexts must render to a private target and copy back to the drawable memory instead of presenting). Recommend: fix, test stays in `tools/rig/offscreen_test.sh`.

## 5. Read-only evidence for B2 (measured)
- The Rage has a **PCI power-management capability** (at config 0x5c, version 1, D1 and D2 supported, state D0). So the PCI side can really be put to sleep by IOPCIFamily on the MDD; whether the MDD cuts slot power is still not established.
- Still impossible here: the real NDRV event order on a Lombard (list for the contributor in sleep-wake-plan.md section 7 tier D).

## 6. What each B2 item is, and my recommendation
Background: stock handles sleep exactly like a mode change and keeps no chip state; our kext only handles the "info == 0" events (as mode change), ignores real system-sleep events, and has no guard stopping client calls from touching the chip while it is suspended (a read of a suspended chip hangs the machine).
- **B2a** `RageLTPower.cpp`: act on the system-sleep/wake events, add a "chip may be touched" gate, verify PCI config before touching the chip on wake. Small; the real safety fix. **Agree.**
- **B2e** put the same gate at the top of the functions that touch the chip but are ungated today (`vramCopyIn/Out`, DMA, overlay, register dump...). Also fixes a latent mode-change gap. **Agree (do with B2a).**
- **B2c** host state-machine test and a kext `sleep-sim` test of B2a/B2e: proves the gate without hardware risk. **Agree.**
- **B2f** guard the MEM_CNTL aperture-endian bits (byte-order-reconcile item) in the wake verify. Tiny. **Agree.**
- **B2b** make the G4 stub emit the same power events as the Lombard's NDRV, so B2a runs on the G4 with software events only and no chip writes. Moderate; improves rig parity. **Agree (optional but cheap).**
- **B2d** real G4 system sleep, staged, you present to wake it; it can hang the G4 and tells us only about the MDD slot, not the Lombard. **Trim / defer** to the very end, only if you want the rig to survive sleep.

## 7. Recommendations
1. Make the write-combine upload mapping the default; drop B8 (DMA upload) as a feature.
2. Leave write-combine for WindowServer OFF; close B1g-3 (DMA window flush) as agreed.
3. Fix the A6 off-screen context bug.
4. Implement sleep-then-spin v-sync after your tearing check.
5. B2: agree a, c, e, f (+b); trim d.
6. Quartz Extreme last, as you said.
7. Remaining untested without you: overlay/FS_SCALE/DVD/TV-out visuals, tearing check, B2d.

## Addendum: flush-heavy scroll test for write-combine (WindowServer CPU)

`tools/rig/flushscroll.sh` scrolls a 520x8000 busy image in Preview (Actual Size, 150 wheel jumps of 40 lines, each larger than the window, so every jump is a full redraw; the engine handled only 3 ops per run, i.e. the work is CPU flush). Session restarted per write-combine setting. WindowServer CPU per 150 jumps, 8 runs each:

| | runs (s) | mean | median |
|---|---|---|---|
| write-combine off | 0.45 1.62 1.00 0.44 0.79 1.21 0.78 1.00 | 0.91 | 0.90 |
| write-combine on  | 0.15 0.94 1.07 0.21 1.51 0.68 0.80 0.86 | 0.78 | 0.83 |

About 10% lower with write-combine on, but the run-to-run spread (0.15-1.62 s) is far larger than that difference, so it is **not significant**. The first run after a session restart is lowest in both settings. Conclusion unchanged: no demonstrated CPU benefit for WindowServer; leave write-combine off for it.

## Addendum: tearing check (user's eyes, Nov 27, blind order)

`tools/ragegl/tear_test.c`: four 40 px white bars moving at 1800 px/s (time-based, same motion at any frame rate) on a 640x480 scanout, 15 s per mode, mode order randomised and hidden from the observer.

| Run | Mode (revealed after) | fps | Observer |
|---|---|---|---|
| A | v-sync poll | 59.8 | no tearing |
| B | v-sync **off** | 192.5 | **tearing** |
| C | v-sync sleep-then-spin (2.5 ms margin) | 59.8 | no tearing |

Confirms the static reasoning: with the wait the blit stays ahead of the beam; without it, tearing is visible. Recommendation: make sleep-then-spin (27% CPU) the default for presents to the scanout; `RageVsyncTimeouts` was 0.

## Addendum: v-sync sleep-then-spin is now the kext default (user agreed Nov 27)

`config/features.h` `RAGE_VSYNC_DEFAULT` = 3 (0 off, 1 poll, 3 sleep-then-spin, margin 2.5 ms); `RAGE_GL_CONFIG` key 9/10 still override at run time. It applies to the same-format raw-copy present only; the textured-quad and overlay presents do not wait. Verified on the release kext after a fresh load: `default_vsync` (no config call) 59.8 fps; gl_suite 291/0; layout_exercise (Atlantis, Flurry, Chess, Sonic R, 7 min): 6251 waits, 0 timeouts, 26893 sleeps, no new crash logs, panic.log unchanged. The test tools `present_bench` and `tear_test` now restore the default (3) on exit rather than forcing it off.
Future iteration (user): event-driven or overlapped waiting so the CPU is free for other work during the wait.

## Addendum: write-combine upload is now the default; B8 dropped (user agreed Nov 27)

`RAGE_VRAM_WC` = 1 in `config/features.h`: `vramCopyIn` stores through a second, write-combined VRAM mapping (registers stay on the inhibited map). With it the CPU upload path is the default (`fXferUse = !RAGE_VRAM_WC`); DMA upload remains available with `RAGE_GL_CONFIG` key 2 = 1. Release kext, G4: `xfer_test time` 256 KB: CPU path 3.7 ms per upload+fence, DMA path 7.6 ms; 1 MB: 14.5 ms vs 31.8 ms (the CPU path was 8.55 / 34.3 ms before write-combine). Gates: `xfer_test` ALL PASSED (byte-exact, 4 concurrent clients), `gl_suite` 291/0, layout_exercise (Atlantis, Flurry, Chess, Sonic R) with no new crash logs and panic.log unchanged. B8 (further DMA upload work) is dropped.

## Addendum: A6 fixed - CGL off-screen contexts render into the app's memory (user agreed Nov 27)

Cause: GLEngine attaches an off-screen context with type **0x35** and a descriptor of {width, height, rowbytes, base address, ...}; the bundle did not know the type and treated it as a full-screen drawable (scanout size, present to the scanout), so frames appeared on the real Rage screen and the app's memory stayed empty.
Fix (bundle only, no kernel change): type 0x35 marks the context off-screen (`mach64_priv.offscreen`, validated descriptor or the attach is refused so GLEngine uses another renderer); the target is ARGB8888 (16 bpp fallback) and never overlay/quad presented; `glFinish` / `CGLFlushDrawable` copy it into the app's memory (`mach64_offscreen_flush`, gld_pixels.c). `glFlush` does not copy. Memory layout copied from the generic software renderer as measured on the G4 with `cgl_offscreen soft`: rows top-down, ARGB8888 words in memory byte order A,R,G,B, alpha byte 0 for a format without alpha.
Verified on the G4: `cgl_offscreen` ALL PASSED at 128x96, 300x200, 160x120 and 640x480 with a layout dump identical to the software renderer's; the Rage screen shows the normal desktop afterwards (screenshot); `gl_suite` 291/0; layout_exercise (Atlantis, Flurry, Chess, Sonic R) with no new crash logs and panic.log unchanged. Debug aid added: `RAGE_ATTACH_LOG=1` prints every attach (type, flags, first descriptor words).

## Addendum: B2 sleep/wake gate (Nov 27)
- Host: `hosttest/pm_test.c` 72,085 checks, 4 mutants caught. Kext: `tools/ragegl/sleep_sim` 19/19 on the debug kext (closed gate refuses all 9 chip-touching calls and new clients; every event order reopens correctly; upload byte-exact after two cycles). Release kext: gl_suite 291/0, xfer_test, layout_exercise pass. MEM_CNTL LOWER_APER_ENDIAN is 00 on the G4 (B2f).
- B2b stub emitter: registering power management at stub `start()` with `PMinit()` + `registerPowerDriver` + `joinPMtree` **panicked the G4 at boot, even in safe mode** (needed single-user to remove the kext). Apple's `IONDRVFramebuffer::initForPM` (disassembled) calls only `registerPowerDriver`; IOFramebuffer already did the rest. The stub now registers on demand (`stubctl pm`, property `rage-pm`) with `registerPowerDriver` only: no panic.
- Display sleep (Energy Saver `dim`) reaches the framebuffer only as `setAttributeForConnection('powr', 0 / 0x80000003)`; the framebuffer power-state events (3,1)/(4,1) and (5..8) arise only on real system sleep (B2d, still deferred).
- **B2d RESULT (Nov 27, user-attended, 3 runs): this MDD does not wake from system sleep with the Rage card installed -- including with none of our kexts loaded.** Runs: (1) stub with PM registered + release kext; (2) stub without PM + release kext; (3) no RageStubFB/ATIRageProLT at all. Each: sleep via Finder AppleScript, fans spin up on key press, BOTH displays (GeForce and Rage) stay off, hard restart needed; no panic.log written; no kernel log lines after the sleep (kernel messages after syslogd stops reading are lost on reset). So the failed wake is not caused by our PM registration, stub or kext. Open: whether it is the Rage card (PCI slot power) or the machine itself (untested: needs the card physically removed).
- Consequence: the wake path (verify PCI first, stay gated) cannot be exercised on this rig; it stays verified by pm_test (72,085 checks) and sleep_sim (19/19) only.

## Addendum: B10 test disc (Nov 27)
- Authored `tools/dvd/author.sh`: 30 s NTSC MPEG-2 + AC-3, burned-in frame counter, 3 chapters. dvdauthor 0.7.2+ built from source (libxml2 2.12.9 in a private prefix; needed a configure patch: AM_ICONV macro replaced, FreeType made optional; spumux does not link, not needed). Verified on the host with libdvdnav (ffmpeg `-f dvdvideo`): title, chapter, video and audio read, 5 s decoded at 241x.
- On the G4 (10.3.9, DVD Player), the VIDEO_TS folder and a mounted HFS+ image of it (`~/Movies/TestDVD`, `~/Movies/TESTDVD.dmg`) were NOT opened by DVD Player: the Open panel's Open button stays disabled, Return closes it, the controller stays at `--:--:--`. Cause not established (no error shown; the G4 has two ATAPI DVD drives but no disc). Next: user opens it by hand or burns/inserts a disc.

## Addendum: B10 DVD Player on the Rage (Nov 27)
- **Why the viewer would not go to the Rage:** `DVDIsSupportedDisplay` (DVDPlayback.framework) returned -70013 for the Rage and 0/supported for the GeForce. DVDPlayback enumerates accelerator services and reads `IODVDBundleName` (and `AAPL,phandle`); the GeForce's NVKernel publishes `GeForceVADriver`, Apple's ATIRage128 personality `ATIRage128DVDDriver`; Rage Pro has none. Adding `IODVDBundleName = AppleAltiVecDVDDriver` to our kext personality makes the Rage "supported" and DVD Player opens its viewer on it. (Established live; the default-name branch of the loader is not decoded.) Tools: `tools/dvd/dvd_display_probe.c`, `accel_probe.c`.
- **The AltiVec driver draws through OpenGL**, not an overlay: it imports CGLSetSurface/CGSOrderSurface and uploads each frame as a GL_TEXTURE_RECTANGLE texture, format GL_YCBCR_422_APPLE, type GL_UNSIGNED_SHORT_8_8_APPLE (subpicture/OSD: BGRA, 8_8_8_8_REV). Our GL driver handles that format (hardware datatype 11/12).
- `tools/ragegl/ycbcr_test.c` (off-screen, 8 BT.601 colour bars): **the Rage renders YCbCr 4:2:2 rectangle textures correctly in both byte orders** (white 246, others within 8 of expected). `GL_APPLE_ycbcr_422` is NOT in our GL_EXTENSIONS string though it works. The software reference lacks rectangle textures (0x501), so only the Rage column is informative.
- User saw (Rage monitor, DVD Player playing the test disc): bright green with grey lines drifting down, like a CRT raster. Not explained: the texture path works, so the frames themselves (decoder output, or the stripe/pitch of the uploads) are suspect; DVD Player sat in state U at 33-40% CPU and the time display stayed `--:--:--`.
- A second process cannot DVDInitialize while DVD Player runs (-70002). `tools/dvd/dvd_play_test.c` (drives DVDPlayback directly) deadlocks in DVDOpenMediaFile when no event loop is pumped (main thread on a semaphore, another on a mutex, 0% CPU); needs restructuring around a Carbon event loop.
- The G3 question (Lombard): the AltiVec driver ships three `mp2decvbin` variants; which one DVDPlayback picks on a G3 is not decoded.
- **Direct playback works** (`tools/dvd/dvd_play_test.c`, Carbon event loop, port set before device): state Playing, elapsed time advances in step with wall clock, ~35-40% of one CPU, no DVD Player GUI needed. The picture is the same as in DVD Player: solid green (all-zero YCbCr) with thin white lines, slanting slightly downward every ~46 px, and only ~640 of the 720 columns drawn. So the fault is in the frame data or its upload pitch, not in DVD Player. To keep Terminal (opened by rungui) from covering the window, move it to the GeForce display first (`osascript ... set bounds of front window`).
- **B10 green screen: three GL driver bugs, all fixed** (found with `tools/ragegl/ycbcr_test.c` modes `rl cs sub twice`, and static reading of the AltiVec driver's upload code at 0x5c5c-0x6250: UNPACK_ALIGNMENT 1, UNPACK_ROW_LENGTH = rowbytes/2, UNPACK_CLIENT_STORAGE_APPLE, storage hint SHARED, five rectangle textures created from an empty buffer, then glTexSubImage2D per frame):
  1. **glTexSubImage2D after the first draw was ignored.** GLEngine reports it only as `gldModifyTextureLevel(ctx, t, 0, level, 0,0,0)`, which was a stub; `gldModifyTexture(0x80)` is not called again. Any app updating a texture per frame saw it frozen. Fix: that call now queues the texture for re-upload (`gld_entry.c`). This was the cause of the solid green (the decoder's initial fill, Cb=Cr~0).
  2. **Client-storage + GL_UNPACK_ROW_LENGTH:** with client storage GLEngine keeps the application's padded buffer (level record: byte +0x07 bit 0 = client storage, halfword +0x08 = row length in texels); we read it as tight rows, which sheared the picture into slanting lines. Fix: `mach64_upload` repacks such levels.
  3. **GL_UNSIGNED_SHORT_8_8_APPLE byte order was backwards:** the decoder writes memory `Y Cb Y Cr` for 8_8_APPLE (so hardware datatype 11) and `Cb Y Cr Y` for _REV (datatype 12). Red bars had come out green. Fixed in `gld_texture.c`; `ycbcr_test` layouts updated.
- Result: `dvd_play_test` now shows the test pattern correctly (bars, timecode, frame counter, moving ball, checkerboard) on the Rage; GL suite stays 291/0. Playback CPU about 35-40% of one CPU at 720x480.
- **DVD playback is choppy (user, 11:05): upload bandwidth.** Profile of DVD Player (`sample`, 6 s): the decoder is small; our upload path dominated (`__memcpy`, `tex_texel_hw16`, `mach64_upload`, `tex_upload_image`, `tex_hash`). Removed the staging buffer + per-texel loop for YCbCr (raw source bytes uploaded directly; profile share about 40% -> 28% of one CPU). Timing the upload with a temporary log (removed): **each 720x480 frame (691 KB) takes about 45 ms in `rgb_upload2` and frames arrive about 11 per second**, i.e. the video runs at roughly 11 fps against 29.97 (about 15 MB/s). This matches `docs/re-notes/dma-upload-design.md` (kext memcpy path about 20 MB/s, CPU stores into the aperture 32 MB/s at best; 30 fps needs 21 MB/s). So smooth DVD needs the bus-master (system transfer) upload (measured 45 MB/s with the CPU free), which is designed there but not integrated. The audio stutter is probably the same cause (long blocking uploads hold the accelerator lock); not yet shown.
- **DVD playback now runs at the disc's full rate (29.97 fps; 11 fps before).** Measured per frame with temporary timing (removed): the kernel's write-combine upload was NOT the problem (about 80-90 MB/s); the DMA system transfer was not needed (it stays off by default; the Nov 27 kext note that CPU upload is faster than DMA holds). The 45 ms per frame was: **30 ms in a per-texel padding loop** (the chip needs power-of-two textures, so a 720x480 rectangle is padded to 1024x512; the loop did a 2-byte memcpy plus 64-bit index maths per texel), 12 ms upload, 2.8 ms row repack, 1.7 ms content hash.
  Fixes (`gld_texture.c`, kext): (1) edge-clamp padding copies whole rows and fills typed (46 -> 21 ms); (2) a client-storage YCbCr frame is not repacked or hashed (a video frame is never unchanged); (3) **new kext selector `RAGE_GL_VRAM_WRITE_ROWS`** (ABI 26): copies rows straight from the application's buffer to VRAM at the texture's pitch and pads only what a bilinear fetch inside the picture can reach (the last macropixel of each row and one extra row) -- no padded copy, 691 KB instead of 1 MB, no user pages touched under the device lock (rows staged in 32 KB batches); input validated (sizes, pitches, flags, whole destination span owned by the caller); falls back to a padded copy if the kernel refuses. Result 45 -> 7.6 ms per upload; `RAGE_STATS`: 30 frames/s steady.
  Remaining per-frame cost is the decoder (about 16 ms, serial with rendering) and `present` (vsync wait plus the chip drawing the quad). Not yet checked by ear: whether the audio stutter is gone.
- `tools/ragegl/ycbcr_test` modes now: `rl` (row length), `cs` (client storage), `sub` (create empty, then TexSubImage), `twice` (update after a draw), `npot` (48x12, takes the strided path), `2d`, `rev`. `tools/ragegl/vsyncset N` sets the kext's vsync mode (device-global; 3 is the default).
- **Thin horizontal lines in DVD playback = a race with the decoder, not a kernel write fault (found by the user, Nov 27).** Symptom: short one-row lines of the previous frame where the shapes had been (e.g. an orange segment inside the pink part of the diagonal on frame 898; the source frame has none). Method: a temporary env-gated check snapshotted the application's frame before each upload and compared VRAM after: 1-40 rows per frame differed, and every differing row was also a row where the application's buffer had changed during our copy, with 0 rows where the buffer was stable but VRAM differed -- so the copy was faithful and the *source moved*. Cause: the upload runs at the next draw, but the AltiVec decoder reuses the client-storage buffer as soon as glTexSubImage2D returns; the 30 fps speed-up (earlier work) narrowed the gap and exposed it. Fix: `gldModifyTextureLevel` (called synchronously inside glTexSubImage2D) copies the frame into a private tight buffer (`mach64_texture_snapshot`, up to 16 textures, about 1 ms) and the deferred upload reads that copy; respecifying the image (`gldModifyTexture` with the dirty bit) invalidates it; `gldDeleteTexture` frees it. The check, rerun on the fixed bundle, showed 0 changed rows and 0 mismatches.

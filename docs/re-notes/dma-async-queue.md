# Two-buffer DMA submit and one-call flush (Nov 25, MDD G4)

Pattern followed (established, see `standalone-driver-design.md` 3.3 / 11.16, `dma-async-measurement.md`, and the SonicR plugin's `gld_engine.c`): kernel-owned buffers, validated batches, serial completion check, **no worker thread**, no interrupt needed.

## Why not a worker thread (my first attempt, dropped)
A userspace thread that decodes and submits batches was written and discarded unbuilt. `gld_engine.c` (SonicR plugin) already records the reason: on one CPU the worker's wait competes with the main thread for the CPU and the scheduler, not the driver, picks the order; the kernel's `dmaWait` is a spin, not a sleep, so the thread would burn the CPU it is meant to free. Design 3.3 allows one kernel work loop for interrupt events only.

## What the plugin's engine does and what we copied
Two staging buffers, each with its own 16 KB-aligned descriptor table in the ring; batch N is kicked, batch N+1 is BUILT in the other buffer while the engine draws N, the engine is polled for completion (`BM_GUI_TABLE == table end` and idle) only just before N+1 is kicked, and anything else that touches the accelerator fences first.
`RageLTDma.cpp` now does the same: `fDmaCmd`/`fDmaCmd2`, ring block 48 KB (two tables at +0 and +16 KB after physical alignment), `dmaSubmit` = build into the free buffer -> `dmaWait()` for the stream in flight -> `eng_wait_idle` -> kick. `fDmaEnd` holds the table end of the stream in flight. Every other engine use still drains through `idleWait()` (unchanged), so ordering against fills, Z clears, uploads, frees and presents is the same as in the serial model. Without a second buffer (allocation failed) the old strict order is used.

## One kernel call per flush
Measured profile (Sonic R demo race): ~13 kernel calls per flush: 11 `TEX_n_OFF` writes plus format calls, each its own IOKit round trip, then the submit. `RAGE_GL_SUBMIT_EX` (17) carries the texture records and formats with the pairs; the kernel applies them through the same `setTexture` checks. `rgb_flush` also keeps one staging buffer instead of malloc/free per flush.

## Findings
* The texture-register hazard (the engine reads texture registers while earlier triangles still draw; Chess board bug, `gld_core.c` sampler-change split) means a second stream cannot be chained behind the first without an engine-idle point. So the design deliberately waits for N before kicking N+1; the overlap gained is the CPU build of the stream (and the caller's own batch preparation), not chip-side chaining.
* Interrupt completion (O6) stays open. Static facts gathered: the Rage node has one level interrupt (`interrupts = <1>`, specifier 0x3a on the OpenPIC controller) and its only other client on this rig is our own `RageStubFB`. The chip has `BUSMASTER_EOL_INT_EN/INT/AK` in `CRTC_INT_CNTL` (bits 24/25); the Programmer's Guide (8.10.3) documents it for the system-to-framebuffer transfer, NOT for the GUI-master list we use, so it had to be tested on hardware (below); and EOL would signal end of the descriptor list, not 3D idle, so it could not by itself lift the texture-hazard wait.
* Verification: gl_suite 290/290 and gl_probe 128 pass / 1 fail (ARB fragment program, known) on the new kext; 4169 submits alternating both buffers, zero engine errors or timeouts in the event log.

## Inline gli reader (the plugin's established caching fix)
`mach64_gli_u32` is now an inline fast path in `gld.h` (cached gli base, back-pointer re-validated), slow path out of line; `glidiff` reads its env var once. Same as the SonicR plugin's `gld.h`.

## Measurement after all of the above (Sonic R demo race, windowed on the Rage display, one 8 s `sample` + `RAGE_STATS=1`)
* Main thread still ~60% blocked in kernel calls (before 60%): submit calls ~0.47 s of every second (~190 us per flush, ~0.36 us per register pair), 11-19 presents per second. `getenv` and `gli_find` dropped out of the profile (~3% of the main thread); the remaining CPU time is spread thin (bind_texture 2.6%, emit_tri_rv 1.9%, vertex read/pack ~3%, track/grid draw loops).
* Reading: the wait scales with pairs (work) rather than with flush count, so it is mostly the chip drawing (or the fixed per-stream cost inside the kernel); the two-buffer submit only overlaps the stream BUILD, which is small. Not yet separated: chip time vs per-stream kernel cost (`RAGE_STATS=2` serialises each submit with a fence to measure it).

## O6 answered: no completion interrupt exists for the GUI-master list (Nov 25, MDD G4, stage A, polling only)
`ragelt engtest 0x10000002` (debug kext; `dmaMinimalTest` variant bit 1): enable `BUSMASTER_EOL_INT_EN` (CRTC_INT_CNTL 0x418 bit 24), acknowledge a stale status (bit 25), kick the proven two-register stream, read the register right after the walk pointer moved and again after the engine went idle. Writes keep only the enable bits plus the one acknowledge bit, since status and acknowledge share bit positions.
Three runs, same result: found 0x80000154 / 0x80000115, after enable+ack 0x81000154 (EN set, bit 25 clear), after the walk 0x81000154, after idle 0x81000174 (only the VLINE_SYNC bit toggled): **bit 25 never set**. The Programmer's Guide (8.10.3) documents BUSMASTER_EOL_INT for the system-to-framebuffer transfer (BM_SYSTEM_TABLE) only; the GUI-master list we use does not raise it. The remaining CRTC_INT_CNTL sources (vblank, vline, snapshot, capture, overlay, GP I/O) say nothing about the engine. So there is no interrupt for GUI-master completion or engine idle: completion stays polled (BM_GUI_TABLE at the table end and idle, 8 stable reads), and stage B (registering the IRQ) is moot. Register map and probe stay in the debug kext only.

## Per-primitive bind memo (Nov 25)
`mach64_bind_texture` (gld_texture.c) re-decoded ~100 gli words and the bound slots on every primitive, and its per-level register reset made `mach64_tex_select_lod` re-dirty the state each time (an extra 96-dword state block per primitive). It is now a memo around `bind_texture_full`: valid while the gli regions it reads (0x62e0-0x6800 and the bound-texobj words 0x1573c-0x15780), the texture epoch (bumped by slot alloc/evict, upload, delete, invalidate, cpu_free), the batch generation, the target and the context are unchanged and no upload is pending. `RAGE_NO_MEMO=1` turns it off; `RAGE_VERIFY_MEMO=1` runs the full decode on every hit and compares `p->hw` (minus the per-triangle LOD registers, which select_lod owns), the globals and the result.
Evidence: Sonic R demo run in verify mode: ~1.87M hits, 217K misses, 0 mismatches; gl_suite 290/290 and gl_probe 128/1 with verify on and off.

## CPU cuts, round 2 (Nov 25)
- `rage_f2byte`: 32-bit round (`(p>>sh) + ((p>>(sh-1))&1)`), forced inline; identical to the 64-bit version over all 2^32 inputs (this removed `__lshrdi3`/`__ashldi3` calls from the vertex path).
- `mach64_read_vertex`: the four colour words compared with the previous vertex's; a hit reuses the packed colour.
- `rgb_flush`: per-register routing through a one-load class table instead of up to ten compares per word.
- Bind memo compares only the gli ranges the bind reads (107 words instead of 328); verify mode on Sonic R: 1.89M hits, 0 mismatches.
- Measured (demo race, one sample): main thread kernel wait 58%, our driver CPU 22.6% (was 23.9%), game+system 19%; 12.5 presents/s. Remaining driver CPU: emit_tri_rv 3.2%, read_vertex 1.8%, rgb_flush 1.6%, m64_emit_triangle 1.4%, pack_vertex 1.4%, tex_select_lod 1.2%.
- Plugin driver baseline: it draws wrong textures on the G4 in both title and race, so it is not a usable performance baseline.

## Kernel wait profile and kernel-side CPU cuts (Nov 25)
Kext counters (raw mftb ticks, logged as marks 0xd0060000 build/wait-prev/idle-before-kick, 0xd0061000 idleWait from other calls, 0xd0062000 userIn/batch_ok/tex-records, 0xd0063000 starved/total) over the Sonic R title + demo race (~118K submits, ~125 s):
- wait for the previous stream (in dmaSubmit): 24.1 s = 203 us/submit; wait for engine idle in clear-Z/fill/present (idleWait, 25.7K calls): 21.9 s (0.85 ms each; clearZ ~1.7 ms, fill ~1.7 ms, present ~2 ms per call); build 1.8 s (15 us); idle before the kick 1.9 s (16 us).
- **72% of submits found the previous stream already finished** (the chip was starved by the CPU); the 24 s of waiting is concentrated in the minority of big batches. So reducing CPU work does help, and the remaining wait is chip work, not a protocol stall.
- Kernel CPU per submit before: userIn 29 us (IOMemoryDescriptor + page wiring every call), rage_batch_ok 27 us (linear rule scan per pair), tex-records 21 us (one lock + ctxFor per record).
- Fixes: `copyin()` on the caller's own thread when `fTask == current_task()` (falls back to the descriptor path otherwise); a per-register table in `rage_reg_ok` (first-matching-rule-is-plain; exact scan for everything else; `tests/guard_test.c` compares both for all registers and 10 values); `setTextures()` applies all records and both formats under one lock.
- Result: userIn 7.5 us, tex-records 1 us, batch_ok 14 us per submit; presents/s 14.5 -> 15.8 in the same race segment (same 125 s window, RAGE_STATS=1). gl_suite 290/290, gl_probe 128/1, guard_test 63/0.
- Open: the 21.9 s of engine-idle waiting in clear-Z/fill/present. The CPU could build the next frame's batches during that drain if those operations were queued as stream ops instead of idle-wait + PIO. Hazard to check first: DST_OFF_PITCH/scissor changes while earlier triangles still draw.

## Fills, Z clears and presents no longer wait for completion (Nov 25)
Static check first: clearZ/fillRect/present each call `idleWait()` (drain), program the engine over PIO, then `waitFence`. Every later engine use already drains first (the next `dmaSubmit` waits for idle before its kick; CPU reads/uploads and mode changes call `idleWait`; FENCE drains), so the trailing `waitFence` only blocked the CPU while the engine ran the op. It is now skipped (`RAGE_OP_DONE`; `-DRAGE_SYNC_OPS` restores it). Queueing the ops as DMA streams would add nothing: a stream also waits for the previous one before its kick, exactly like the PIO path.
Result (same 125 s race window, RAGE_STATS=1): 15.8 -> 16.3 presents/s; present time in the kernel ~2 ms -> ~0 ms; clearZ/fill still ~1.1 ms per call (their drain of the draw stream, which is chip work). gl_suite 290/290, gl_probe 128/1, no engine errors in the event log. Session total from the start of the CPU work: 14.5 -> 16.3 (+12%).
What remains in the kernel wait is chip execution (previous stream, drain before clear/fill), plus per-call overhead; reducing it further means less chip work per frame (fewer state changes/passes), not a protocol change.

## CPU trimming, round 3 (Nov 25)
- Fresh profile after the kernel changes (demo race, one 8 s sample): kernel wait ~69-71%, game+system ~16%, our driver CPU 13-15%, a flat list: read_vertex 1.2-2.2%, rgb_flush 1.0-1.7%, emit_tri_rv 1.0-1.1%, pack_vertex 0.8-1.0%, m64_emit_triangle 0.4-1.0%, hw_tri 0.6%, bind 0.3-0.6%. No single remaining item is above ~2%.
- The bind memo's gli compare had become the largest single driver item (memo_a_same 2.2%). It is now branch-free (OR of XORs, unrolled by 4 words; ranges widened to multiples of 16 bytes): 16 -> 3 samples (0.4%). Verify mode: suite 290/290, probes 128/1, Sonic R 2.06M hits, 0 mismatches.
- No cheap "state changed" word was found for the memo (GLEngine's dirty tracking would have to be reverse-engineered; 0.4% left to gain). Presents/s varies by several fps between runs of the same demo (scene timing), so a 1-2% CPU saving is not visible in it; use samples for CPU changes.

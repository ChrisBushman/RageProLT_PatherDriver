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
* Interrupt completion (O6) stays open. Static facts gathered: the Rage node has one level interrupt (`interrupts = <1>`, specifier 0x3a on the OpenPIC controller) and its only other client on this rig is our own `RageStubFB`. The chip has `BUSMASTER_EOL_INT_EN/INT/AK` in `CRTC_INT_CNTL` (bits 24/25); the Programmer's Guide (8.10.3) documents it for the system-to-framebuffer transfer, NOT for the GUI-master list we use, so it must be tested on hardware before relying on it; and EOL would signal end of the descriptor list, not 3D idle, so it cannot by itself lift the texture-hazard wait.
* Verification: gl_suite 290/290 and gl_probe 128 pass / 1 fail (ARB fragment program, known) on the new kext; 4169 submits alternating both buffers, zero engine errors or timeouts in the event log.

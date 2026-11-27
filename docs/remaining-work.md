# Remaining work: agreed checklist (Nov 26)

Scope rule (user, Nov 26): no Lombard is available, so nothing here depends on
testing on a Lombard. Other contributors may test it. Verification is on the G4
rig (real Rage LT Pro, no panel, composite TV-out connector present).

## Closed / out of scope
- A1 L5b screen-saver wedge: not reproduced; closed. A recurrence is filed as a new bug.
- A3 bus-master wedge: closed unless a reason to disable bus mastering appears.
- A4 Chess 32 bpp on the Lombard; D1 Lombard package; D2 Lombard mode-change events: out of scope.
- B9 interrupt completion (O6): chip raises no completion interrupt; vertical-blank interrupt researched, documented in re-notes/vblank.md (193b14f); polling stays unless a measured need appears.
- D4 hardware cursor visual check: done; the only remainder (cursor over the overlay) is part of B4's test.

## Investigations (static first, results written to docs/re-notes)
- A2 / L9 / O4: reconcile "byte-reversed texels on the G4 stub rig" with design 11.3 (resolved from documents). Stock binaries + docs.
- A5 per-client 3D state (user decision Nov 26): build the 2D client (B1) and windowed-GL protocol (B3) WITHOUT save/restore first; measure the cost of save/restore, and test for cross-client state leaks. If they leak, save/restore is required; if the measured cost is negligible, use the robust save/restore anyway.
- B2 sleep/wake: investigate stock hooks, propose implementation and test plan; find how far the G4 can go.
- C3 NDRV panel leftovers: finish decode; report which behaviour is G4-testable vs unverifiable.
- C1 O1 (classes/properties/selectors that ATIRageProGA and GLEngine look up); C2 O2 (IOAccelerator base usable by a third-party kext).

## Standing authorizations (user, Nov 26)
G4 reboots/recovery and panics acceptable; kext build/load on the G4 (not stock kexts); GUI control and screencapture; host tooling in a container for DVD content; up to 3 agents; commit+push. Chip register experiments are LOWEST priority (likely to crash; recovery needs the user). Overlay register experiments never unattended. Work needing the user's eyes is queued, not blocking.

## Added Nov 26
- V-sync wait: measure time in waitFlip (Atlantis, Sonic R, Chess); first try the lazy fence (wait at the first draw/clear needing the retiring buffer); then sleep-then-spin; interrupt only if measurement justifies it.

## Tests to add
- A6 off-display GL context (not drawn into our scanout): add to the test suite.
- D3 long FIFO soak with the Apple formula (default).
- D5 remaining depths on the stub (16 bpp = 5-5-5, no 24 bpp).
- D7 TV-out via the composite port (G4-testable). Everything else panel-side is untestable here.
- D6 full regression pass (T3, performance, GL suite, soak): before the next release, after the whole checklist is complete.

## Features (agreed to include)
- B1a (user agreed Nov 26) Quartz acceleration study: what WindowServer/CoreGraphics require before using Quartz Extreme (GPU compositing) on a GPU, whether the Rage LT Pro can meet it, and whether CG's surface flush path passes through our type-0 client (so the CPU copy could move to blitter/DMA). If VRAM (8 MB) is the limit, discuss lowering requirements (QE only at lower resolution/bpp, downscaled assets, other memory users). Static first.
- B1b 2D client (stock type 2): CORRECTED Nov 26 -- consumed by CoreGraphics (Quartz 2D fills and screen-to-screen copies via IOPSAllocateBlitEngine / MPHW*) and X11, so it IS the broad 2D win (quartz-acceleration-study.md). Software window flush (memory-to-screen) stays a CPU copy; stock plugin declines mode 0x2000. Sub-items proposed (not yet agreed): class named ATIRagePro, IOCFPlugInTypes, 2D validator, DP_PIX_WIDTH reconcile, replay-from-shadow context owner, rig Info.plist patch.
- Proposed from B1a (awaiting agreement): B1d fix gld_renderer_info layout (_pad[0x14] puts video memory at +0x34, affects CGL-reported VRAM today; host test); B1e Quartz Extreme enable experiment only if 2D is not enough (plist GLCompositorRequiredClasses/MinimumVRAM, pixel-format nodes, HW rectangle textures, 16 bpp only; QE at 32 bpp does not fit 8 MB); B1f read /var/log/windowserver.log as root (needs user); B1g decode IOFBBlitMemorySurfaceCopy / plugin mode 0x2000 for a DMA-backed memory-to-screen blit.
- B1g RESULT (47369a7, memory-to-screen-blit.md): feasible only for a subset, not worth building now (CG requests a HW memory blit only with CGS debug bit 0x400000; needs our own user-space plugin; byte order blocks zero-copy at 16/32 bpp; ~3x less CPU per byte, no elapsed-time win). Follow-ups APPROVED by the user Nov 26 (B1g-3 only if B1g-2 says the CPU flush matters; B1g-2 after B1b works): B1g-1 static decode of window backing alignment + debug-bit table; B1g-2 measure real WindowServer flush cost after B1b works (go/no-go); B1g-3 hardware experiments + own-plugin prototype only if B1g-2 says it matters; B1g-4 record aperture cache mode / check write-combining (faster CPU flush may beat DMA).
- Proposed from the B2 study (awaiting agreement): B2a RageLTPower.cpp (chipOK gate, info!=0 events, wake verify); B2b stub PM/event emitter; B2c host + ENGTEST sleep-sim; B2d staged user-attended G4 system sleep; B2e gate ungated chip-touching functions; B2f MEM_CNTL LOWER/UPPER_APER_ENDIAN guard.
- B3 windowed-GL kernel protocol (type-1 stock layout, window rect / visible regions / surface id)
- B4 video overlay and scaler (Y3/V1 tests need explicit go-ahead per run; include cursor-over-overlay check)
- B5 RAGE_FEAT_FS_SCALE (hardware-scaler upscale for fullscreen 32 bpp; verify quality)
- B6 GL: front-end scaler blit, paletted textures, stencil
- B7 ARB fragment programs
- B8 texture upload by DMA
- B10 hardware DVD/MPEG2 assist; author test DVD content (video + audio) ourselves

- B1g-1/B1g-4 RESULTS (this commit): backing rowbytes always a multiple of 64 and page-aligned shmem, per-rect source pointer alignment depends on x; debug bit 0x400000 settable only via CGSSetDebugOptions (Quartz Debug is the only shipped caller, constants not decoded); WindowServer maps VRAM with default (inhibit) cache because our stub sets no cache flags; write-combine measured 3.7x store speed (117 vs 31 MB/s) but CoreGraphics cannot request it. PROPOSED, awaiting agreement: B1g-5 = stub returns an IODeviceMemory subclass whose map() forces write-combine (see memory-to-screen-blit.md).

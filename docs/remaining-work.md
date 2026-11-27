# Remaining work: agreed checklist (Nov 26)

Scope rule (user, Nov 26): no Lombard is available, so nothing here depends on
testing on a Lombard. Other contributors may test it. Verification is on the G4
rig (real Rage LT Pro, no panel, composite TV-out connector present).

## Closed / out of scope
- A1 L5b screen-saver wedge: not reproduced; closed. A recurrence is filed as a new bug.
- A3 bus-master wedge: closed unless a reason to disable bus mastering appears.
- A4 Chess 32 bpp on the Lombard; D1 Lombard package; D2 Lombard mode-change events: out of scope.
- B9 interrupt completion (O6): answered, chip raises no completion interrupt; nothing to do.
- D4 hardware cursor visual check: done; the only remainder (cursor over the overlay) is part of B4's test.

## Investigations (static first, results written to docs/re-notes)
- A2 / L9 / O4: reconcile "byte-reversed texels on the G4 stub rig" with design 11.3 (resolved from documents). Stock binaries + docs.
- A5 per-client 3D state is not saved per context: explain risk, then decide (see chat Nov 26).
- B2 sleep/wake: investigate stock hooks, propose implementation and test plan; find how far the G4 can go.
- C3 NDRV panel leftovers: finish decode; report which behaviour is G4-testable vs unverifiable.
- C1 O1 (classes/properties/selectors that ATIRageProGA and GLEngine look up); C2 O2 (IOAccelerator base usable by a third-party kext).

## Tests to add
- A6 off-display GL context (not drawn into our scanout): add to the test suite.
- D3 long FIFO soak with the Apple formula (default).
- D5 remaining depths on the stub (16 bpp = 5-5-5, no 24 bpp).
- D7 TV-out via the composite port (G4-testable). Everything else panel-side is untestable here.
- D6 full regression pass (T3, performance, GL suite, soak): before the next release, after the whole checklist is complete.

## Features (agreed to include)
- B1 2D acceleration client (stock type 2, 16 selectors, IOCFPlugInTypes for ATIRageProGA.plugin)
- B3 windowed-GL kernel protocol (type-1 stock layout, window rect / visible regions / surface id)
- B4 video overlay and scaler (Y3/V1 tests need explicit go-ahead per run; include cursor-over-overlay check)
- B5 RAGE_FEAT_FS_SCALE (hardware-scaler upscale for fullscreen 32 bpp; verify quality)
- B6 GL: front-end scaler blit, paletted textures, stencil
- B7 ARB fragment programs
- B8 texture upload by DMA
- B10 hardware DVD/MPEG2 assist; author test DVD content (video + audio) ourselves

# Hardware ICD + Laser Arena: diagnosis (2026-08-31)

The software Mesa `opengl32.dll` runs Laser Arena to the menu (headline goal, met).
The hardware backend (`mach64_icd.c`) renders + presents correctly in isolation
(`hwicdtest` passes). But `LA_GL.exe` on the hardware DLL flickers "readable vs
garbage" and crashes at the menu. Instrumented `m64icd_swap` to sample the
hardware render target each frame (C:\m64icd.log). Findings over 40 frames:

```
MC W=640 H=400 pitch=640 fboff=0x400000
SW#1  cen=030404 tl=0000ff nz=192602
SW#2..8  cen=030404 tl=050505 nz=256000
SW#9  cen=094ed0 ...
SW#10..40 cen=050505 tl=050505 nz=256000 (a few ~255960)
```

Interpretation:
- **nz == 640*400 every frame** -> the target is fully written each frame; NOT
  flickering to black/garbage. Frame content is stable.
- **cen/tl are a near-uniform dark grey (0x050505)** -> the target is essentially
  just the glClear colour. The menu's actual pixels are NOT arriving through our
  tnl `Render.Triangle`/`Render.Quad` hooks.

Conclusion: Laser Arena's menu is drawn through a GL path we don't intercept --
almost certainly `glDrawPixels`/`glBitmap` 2D blits (idTech menus use `Draw_Pic`
image blits), which go through `ctx->Driver.DrawPixels/Bitmap` + swrast, NOT the
T&L triangle pipeline. So:
- The game's 2D menu renders into WMesa's DIB via swrast (uncleared by us, since
  our hw_clear only clears the VRAM target).
- Our `m64icd_swap` then OVERWRITES the whole DIB with the near-empty hardware
  target -> the menu is stomped. Alternating with frames where the game's own
  drawing shows through == the "readable vs garbage" flicker.
- The crash is most likely engine coexistence: the kmd quiesces + re-programs
  ATI's BUS_CNTL/SRC_CNTL on EVERY batch at DISPATCH_LEVEL, time-sharing the one
  2D/3D engine with ATI's live desktop driver; many batches/frame -> hang/fault.

## Two hard problems for a full hardware ICD here
1. **Mixed 2D/3D compositing.** Geometry goes to a VRAM target; 2D blits go to a
   system-memory DIB. Presenting correctly needs a compositor: clear BOTH, keep
   the clear key, and copy only the hardware target's non-background pixels over
   the swrast DIB before BitBlt (so 2D UI shows where no geometry was drawn, and
   the 3D world shows where it was). Also requires intercepting DrawPixels/Bitmap
   or accepting swrast for them.
2. **Engine coexistence/perf.** Per-batch bus-master takeover vs ATI's live 2D is
   slow and fragility-prone. Would want per-frame (not per-batch) engine handoff,
   or fewer/larger submits, and a robust quiesce/restore.

Neither is a quick fix; both are multi-session with more on-hardware test runs.
The software ICD already delivers the user-visible goal (game runs, no ATI crash).

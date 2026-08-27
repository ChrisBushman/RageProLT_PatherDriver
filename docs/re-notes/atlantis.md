# atlantis — first real GL app on the driver, and the software-T&L ceiling

Ran Apple's unmodified GLUT `atlantis` demo (lit/depth-tested/double-buffered swimming
sharks) fullscreen through our accelerated renderer.  `GL_RENDERER = "ATI Rage LT Pro
(mach64)"`.  It renders correctly — this is the first real, unmodified GL app on the driver.
Tools: `tools/atlantis.sh` (installs driver+kext, patches atlantis to fullscreen game mode +
16-bit depth, forces a 1024x768 viewport, frame-pacing with an 'f' cap toggle, and an
in-session `tools/restore_desktop` teardown).

## What it took to render at all
- **Renderer selection**: atlantis requests a DOUBLE-buffered context.  Our default build did
  not advertise a DB pixel-format config, so GLEngine picked the software "Generic" renderer.
  Build the bundle `-DGLD_DBUF` and it advertises DB -> our driver is selected.
- **DB-gate for type 0x50**: a real GLUT app attaches its DB drawable as gldAttachDrawable
  `type == 0x50` (our AGL test tools use 0x36).  The "has back buffer -> dispatch primitives
  normally" gate (GLEngine object word0 |= 2, ctx+0x1e4=1, caps 0x20 clear) was keyed on
  type==0x36, so 0x50 was skipped and GLEngine fell back to the stub **slot 0x60** and drew
  nothing.  Gate on `pv->dbuf` (the DB context flag) instead of the drawable type.
- **CRTC dims for type 0x50**: 0x50's descriptor size is garbage (441x258 / 188x258), so take
  the fullscreen dims from the CRTC hardware (like 0x36/0x5a), not the descriptor.
- **Slot 0x60 is GLEngine's per-frame BUFFER-SWAP hook**, not the geometry path.  aglSwapBuffers
  routes here once/frame (fixed control-block args, identical every call).  The reference
  no-ops it and lets the WindowServer page-flip; our kext can't, so the DB PRESENT cycle
  (wait_flip + flush + page-flip + clears) lives in `mach64_dispatch_render_ctl` now.  It USED
  to run in gldUpdateDispatch on the assumption gUD was the frame boundary -- true for our
  single-state test tools (~1 gUD/frame) but a real app re-installs the dispatch table per
  state change (~29-48 gUD/frame), so we page-flipped + vblank-waited dozens of times a frame
  and it crawled at <1fps.

## Performance: ~3fps, and it is NOT the driver
Profiled with `-DGLD_PERF` (per-frame split, printed to stderr).  A ~330ms frame:

    OUR emit (read_vertex + emit_triangle, per triangle) ~2ms    (0.7%)
    OUR present (page-flip + Z/colour clears + DMA submit)  ~30ms   (9%; HW rasterize ~19ms, uncached copy ~10ms)
    GLEngine T&L + lighting                                ~290ms  (90%)

**90% of every frame is GLEngine's SOFTWARE transform + lighting**, which runs on the CPU
before it ever dispatches a triangle to us.  The Rage LT Pro is a rasterizer -- it has NO
hardware T&L unit (nothing consumer did until the GeForce 256, *after* this chip), so software
T&L is correct and unavoidable, exactly what the original ATI driver did.  The Lombard's G3
(750) has no AltiVec, so that T&L is scalar-FP-bound.  If GLEngine's T&L were free this
geometry would run ~32fps (1000/31ms) on our path -- our rasterization (19ms) and packing
(2ms) are already fast.  **~3fps for a lit scene is the hardware+stack ceiling, not a driver
defect, and there is nothing left to optimize on our side** (strip vertex-reuse would shave
~1ms off 330ms).  To DEMONSTRATE the hardware's value, benchmark a **fill-bound** workload
(big textured/blended quads, heavy overdraw, 2D blits) where HW rasterization crushes the
software renderer -- atlantis (thousands of tiny lit triangles) is our worst case.

## Things that did NOT move perf (but were kept -- they're correct)
- **DMA descriptor chaining** (kext `submitCommandBuffer`): collapse a batch's submit into one
  chained kick (many <=4092-byte descriptors, HOLD_OFFSET makes the split transparent to the
  GUI parser) instead of ~200 sub-4KB kicks/frame.  Correct and reduces submit count, but
  submit overhead was never the bottleneck (GPU/DMA is ~19ms/frame), so fps was unchanged.
- **gld_log open-once**: the log did fopen+vfprintf+fclose PER CALL; at ~300 lines/frame that
  cost ~450ms/frame by itself on the IDE disk.  Now opens once, buffered.  (MACH64_GLD_NOLOG=1
  disables logging entirely for perf runs.)

## Two operational gotchas
- **Frozen-frame-on-exit**: we page-flip CRTC_OFF_PITCH directly; on app exit nothing restored
  it, and NOT EVEN `killall WindowServer` recovered the desktop (Apple's framebuffer driver
  caches its own CRTC value and won't rewrite the register it thinks is already correct).
  FIX: the kext saves the desktop CRTC_OFF_PITCH at the first flip and restores it in stop()
  (a direct HW write on kextunload).  atlantis.sh unloads the kext on teardown, so the desktop
  returns.  `tools/restore_desktop` (in-session CGDisplayCapture/Release) is the belt-and-
  suspenders.
- **Intermittent HARD LOCK (black screen -> reboot)**: the kext bus-masters the engine while
  Apple's ATIRagePro driver is still live, with NO shared hardware lock (submitCommandBuffer
  save/restore is racy -- "acceptable headless").  With the WindowServer active, a collision
  can wedge the machine.  It's a race (usually doesn't fire; re-run).  The real fix is full
  device takeover (a HW lock or fully displacing Apple's driver) -- a deliberate future effort.

# OpenGL.framework renderer discovery — REVERSED

How the framework finds and loads a vendor GL renderer, so a stock GL app lands
on our driver (vs us driving the lifecycle via CFBundle ourselves, as in first
light).

## The mechanism (from GLEngine + the ATI kext Info.plists)

1. Every GL app loads `libGL` -> `GLEngine.bundle/GLEngine` (Apple's shared GL
   engine, /System/Library/Frameworks/OpenGL.framework/Resources/).
2. GLEngine enumerates IOKit services of category **`IOAccelerator`**.
3. For each accelerator it reads the string property **`IOGLBundleName`**.
4. It builds the path **`/System/Library/Extensions/<IOGLBundleName>.bundle`**
   (both string fragments confirmed present in GLEngine) and CFBundle-loads it.
5. It looks up **~53 `gld*` entry points by name** (the full list is in GLEngine's
   string table) and calls them for enumeration + the context lifecycle.

`gldrvmond` (the "GL driver monitor daemon") also reads IOGLBundleName -- it is
the Profiler's monitor, not the loader; GLEngine is the loader.

## The empty slot is already ours

The accelerator kext declares which bundle to load:

```
ATIRage128.kext  IOGLBundleName = "ATIRage128GLDriver"  -> loads .../ATIRage128GLDriver.bundle  (ships)
ATIRagePro.kext  IOGLBundleName = "ATIRageProGLDriver"  -> loads .../ATIRageProGLDriver.bundle  (MISSING!)
```

**Apple's own ATIRagePro.kext already points at `ATIRageProGLDriver.bundle`, which
was never shipped** -- that's exactly why the Rage Pro has no hardware GL. The
framework is already looking for our bundle by name; the slot is empty.

=> Integration option A (simplest): install our bundle as
`/System/Library/Extensions/ATIRageProGLDriver.bundle`.  GLEngine, enumerating
ATIRagePro's IOAccelerator, will load it.  Our gld* bodies open our own
Mach64Accelerator kext (mach64_match_service), so the bundle transparently uses
our engine even though the framework loaded it "for" ATIRagePro's accelerator.
UNCERTAINTY to test: whether the framework passes ATIRagePro's accelerator
context to gldCreateContext expecting us to use it (we open our own instead).

Option B: our Mach64Accelerator kext itself publishes IOGLBundleName + presents
as an IOAccelerator GLEngine enumerates.  Cleaner ownership, more kext work
(category/probe vs ATIRagePro).  Decide after testing A.

## Entry-point gap: we export 13 of the ~53 GLEngine looks up

HAVE (13): gldInitializeLibrary, gldTerminateLibrary, gldGetRendererInfo,
gldChoosePixelFormat, gldDestroyPixelFormat, gldCreateContext, gldDestroyContext,
gldUpdateDispatch, gldAllocVertexBuffer, gldCompleteVertexBuffer,
gldFreeVertexBuffer, gldFlush, gldFinish.

CORE still needed for a windowed triangle:
- **gldAttachDrawable** -- bind the app's window surface as the render target
  (today we hardcode an off-screen VRAM offset; this is how the real drawable +
  pitch/offset arrive).  Most important missing piece.
- **gldGetString** -- GL_VENDOR / GL_RENDERER / GL_VERSION / extensions.
- **gldGetInteger / gldSetInteger** -- renderer limits / params.
- **gldGetError** -- error propagation.
- **gldInitDispatch** -- dispatch-table init (pairs with gldUpdateDispatch).
- **gldFlushBuffer** -- swap/present the drawable (double-buffer flip).

OPTIONAL (stub safely so lookups are non-NULL; only called if the app uses the
feature): textures (Create/Modify/Delete/Get TextureLevel, IsTextureResident),
pipeline programs = shaders (Create/Modify/Destroy/Relate/GetInfo), queries
(Create/Destroy/GetInfo), fences (Create/Destroy), buffers (Create/Destroy/
FlushBuffer), vertex arrays (Create/Modify/Flush/Destroy -- Apple's Rage128 STUBS
these, return 0), shared (Create/Destroy), memory-plugin-data
(Get/Set/Destroy/Finish/Test), FinishObject/TestObject.

## Plan for the next phase

1. Implement the CORE set above (gldAttachDrawable is the real work -- recover its
   signature + how the drawable/pitch arrive, from the reference bundle at
   0x4f38).  Stub ALL remaining gld* with safe returns so no lookup is NULL.
2. gldGetRendererInfo: advertise conservative caps (accelerated, RGBA8888, 16-bit
   depth once implemented, no stencil/accum) so the framework accepts us.
3. Install as /System/Library/Extensions/ATIRageProGLDriver.bundle; keep
   Mach64Accelerator kextload'd; run a trivial GL app.  RISK: the bundle loads
   for ALL GL contexts system-wide -- a bad cap -> software fallback (safe), a
   crash in a called-but-unimplemented gld* -> that GL app crashes.  Add logging
   in gldInitializeLibrary/gldGetRendererInfo to confirm the framework loads us.
4. Iterate: watch which gld* the framework actually calls, implement/​fix, until a
   glClear+triangle app renders through us in a window.

# win2k/driver — Mesa mach64 backend + winsys (the OpenGL ICD guts)

This is the software half of the port: the full OpenGL front-end (Mesa) plus the
mach64 render backend, routed to the proven `kmd` DMA path. The hardware is done
(see `../kmd/*.md`); this is the remaining large piece.

## Layered design

```
opengl32.dll (Windows)
  -> ../icd/  DrvXxx ICD shim  (DrvCreateContext/SetContext/SwapBuffers/
              Describe+SetPixelFormat/GetProcAddress/...)
  -> Mesa classic core         (main + tnl T&L + swrast fallback)  <- full GL
  -> mach64 render backend      (triangle setup / texture / state; from
              docs/dri-reference/mesa_mach64_*.c, cross-checked vs our re-notes)
  -> this winsys: common/mach64_emit.c  ->  IOCTL_MACH64_DMASUBMIT (../kmd)  <- HW
```

We keep Mesa's device-**independent** halves (GL front-end + the mach64 render
algorithms) and replace its **DRI/DRM device layer** (which needs Linux/X11/DRM)
with our winsys: build register-write streams with `common/mach64_emit`, submit via
the `kmd` bus-master DMA ring. No DRI, no DRM, no X.

## Bring-up ladder (mirrors the hardware bring-up discipline)

**T0 — CRT startup shim (toolchain). DONE (2026-08-31).** Win2000 `msvcrt.dll` is
linkable (`../toolchain/`), AND `dllcrt0.c` gives a real DLL proper startup: global
constructors + `DllMain` + msvcrt heap/math, proven by `LoadLibrary` on the Armada
(`ctor_ran=YES heap=YES DllMain_alloc=... sqrt2*1000=1414`). DLL link recipe is in
`../toolchain/README.md`. (Deferred: `atexit`/`_cexit` on detach; C99 fn shims — add
when Mesa needs them.)

**T1 — ICD load path (swrast first). STARTED (2026-08-31).** Mesa **7.0.4** vendored
(`../mesa/`, `fetch-mesa.sh`) — it ships OSMesa, the Windows `DrvXxx` ICD shim
(`drivers/windows/icd/icd.c`), and the full mach64 driver in-tree. Custom build
(compile the `src/mesa/sources` groups with our toolchain, link legacy-msvcrt +
dllcrt0). Compile recipe established (imports.c builds; `-Dffs=__builtin_ffs` etc.).
  - **T1a (in progress):** build OSMesa into a test EXE that renders a triangle
    off-screen + verifies pixels, run on the Armada — proves Mesa core on Win2000.
  - **T1b:** the `DrvXxx` ICD shim as an `opengl32`-loadable DLL, registered so a real
    app selects our software pixelformat, before hardware. (ATI's stock ICD
    atioglxx.dll is what we replace — it NULL-derefs; see the Laser Arena finding.)

**T2 — wire the mach64 backend. IN PROGRESS (2026-08-31).**

Port model (Mesa's mach64 DRI driver is the blueprint; it uses the standard classic
hw-driver pattern: `_swrast/_tnl/_swsetup_CreateContext` + `mach64InitTriFuncs` /
`mach64DDInitStateFuncs` / `mach64DDInitSpanFuncs` / `mach64DDInitState`). We keep the
device-independent render logic and swap the DRI device layer for our kmd winsys:

| Mesa `drivers/dri/mach64/` file | T2 disposition |
|---|---|
| mach64_tris.c, mach64_vb.c, mach64_native_vb.c (+ *tmp.h) | KEEP — vertex build + tnl render hooks (emit to a buffer) |
| mach64_state.c, mach64_tex*.c, mach64_dd.c | KEEP — GL state -> mach64 registers (cross-check re-notes) |
| mach64_ioctl.c (DRM DMA submit) | REPLACE with `mach64_winsys.c` (`m64ws_submit` -> kmd bus-master DMA) |
| mach64_span.c (framebuffer read/write) | REPLACE with `m64ws_read_vram/m64ws_write_vram` |
| mach64_screen.c, mach64_lock.c, DRI context init | REPLACE with a WGL/kmd context (no DRI/DRM/SAREA/X) |

**DONE:** `mach64_winsys.{c,h}` — the user-mode device layer (the kmd client) the
ported driver calls: open `\\.\Mach64Kmd`, map FB+regs, `m64ws_submit` (bus-master
DMA a `common/mach64_emit` stream), reg + VRAM read/write. Proven by
`mach64_winsys_test` on the Armada (triangle: 276 px, inside=ff8040) -- the same
hardware triangle, now via the reusable library. Build: `./build-winsys.sh`.

**FIRST HW TRIANGLE THROUGH MESA -- DONE (2026-08-31):** `mach64_gl.c` reuses the
OSMesa context for the GL front-end + T&L, then overrides `tnl->Driver.Render.
{Start,Finish,Triangle,BuildVertices}` to emit transformed triangles via
`common/mach64_emit` -> `m64ws_submit`. A GL triangle (glBegin/glVertex/glColor ->
Mesa transform+viewport -> our hooks -> mach64 setup engine -> VRAM) renders on the
Armada: 276 color px, inside=ff8040, corner=0 (readback-verified). Build:
`../mesa/build-mach64gl.sh`. The hook re-validation problem is dodged by validating
state once (warm-up glFinish) then overriding hooks with no further state changes.

**NEXT:** move these hooks into a proper mach64 Mesa context (survives state changes
via the DDInitState/InvalidateState path); wire `Driver.Clear` to a hw fill; add
depth/texture state (cross-check re-notes); then T3 present (WGL SwapBuffers ->
m64ws blit/flip to the visible framebuffer) + Laser Arena.

---
_Original T2 note:_ **T2 — wire the mach64 backend.** Replace swrast with the mach64 render backend →
`common/mach64_emit` → `IOCTL_MACH64_DMASUBMIT`. Off-screen before on-screen. Apply
the `docs/re-notes/` fixes (Z_CNTL, SETUP_CNTL gouraud, texture-blend audit) as each
path lights up — most are already encoded in `common/`.

**T3 — present + double buffer.** `DrvSwapBuffers` → the verified present/flip path;
add a double-buffered DMA ring. Then the target apps (Laser Arena, QuakeGL).

## Open decisions

1. **Mesa version to vendor.** The mach64 driver tracks a classic-era Mesa
   (pre-Gallium). Candidate: **Mesa 6.5.x / 7.0.x** core (main+tnl+swrast) + the
   mach64 tnl driver files (already mirrored in `docs/dri-reference/mesa_mach64_*`).
   We need the core + tnl + swrast + the mach64 driver API, NOT the DRI loader.
   **Confirm before vendoring** (it's a large import into `mesa/`).
2. **>4KB block I/O fix** (`kmd` RDBLK/WRBLK via METHOD_OUT_DIRECT/MDL) — needed for
   textures and larger surfaces (see `../kmd/engine-proof.md`).
3. **Coexistence** for sustained rendering vs ATI's `ati2mpab` (quiesce is fine for
   off-screen tests; the visible-scanout ICD may need firmer engine ownership).

## Reuse

- `../../common/mach64_emit.c` + `mach64_regs.h` — shared, proven on Mobility-P.
- `../kmd/` — the kernel DMA submission path (`IOCTL_MACH64_DMASUBMIT`).
- `docs/re-notes/*` — the chip-level fixes that beat stock ICDs.
- `docs/dri-reference/mesa_mach64_*` — the mach64 render algorithms to lift.

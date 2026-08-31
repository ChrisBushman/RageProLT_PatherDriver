# Rage Mobility P — Windows 2000 OpenGL ICD (Mesa-based)

A hardware **OpenGL ICD (Installable Client Driver) for the ATI Rage Mobility P**
(mach64, Rage Pro-class 3D core) on **Windows 2000 Professional** — targeting the
Compaq Armada e500. Goal: a capable, correct OpenGL that beats ATI's stock ICD
(which mishandles Quake-engine games such as the QuakeGL-modded *Laser Arena*).

This tree holds **only** the Windows-specific work. The proven, OS-independent
mach64 command-emission core is shared, unchanged, from the repo's `common/`
directory (see "Shared with the OS X driver" below). Nothing here depends on the
OS X `gldriver/` or `kext/` trees.

## Why "Mesa ICD" is not just "compile the DRI driver"

The important reality up front: the OS X side of this repo is a **backend only** —
Apple's `GLEngine` owns the OpenGL state machine, T&L, and clipping, and our
plugin only rasterizes. **Windows has no shared GL engine.** A Win2000 ICD *is*
the entire OpenGL: every `glXxx` entry point, all transform/lighting/clipping/state.

Mesa supplies exactly that missing front-end. But Mesa's mach64 driver
(`docs/dri-reference/mesa_mach64_*.c`) is a **DRI** driver — its device access
goes through DRM ioctls, the SAREA, and X server integration, none of which exist
on Win2000. So "port the Mesa driver" really means:

```
  Mesa classic core (mesa main + tnl T&L + swrast fallback)   <- full GL front-end
    + mach64 tnl driver logic (triangle setup / texture / state)
        (from mesa_mach64_*.c, cross-checked against our re-notes)
    + a Win2000 winsys  =  common/mach64_emit.c  +  kernel helper (MMIO+DMA)
    + a DrvXxx ICD shim  (opengl32.dll  <->  Mesa)             <- replaces GLX/DRI loader
```

We keep Mesa's device-**independent** halves (the front-end and the mach64 render
algorithms) and **replace the DRI/DRM device layer** with our own winsys built on
the already-verified `common/` emit core plus a small Win2000 kernel helper.

## Directory layout

```
win2k/
  README.md    this file
  icd/         DrvXxx ICD shim: opengl32.dll <-> Mesa dispatch (DrvCreateContext,
               DrvSetContext, DrvSwapBuffers, DrvDescribe/SetPixelFormat, ...)
  driver/      the Win2000 winsys: binds Mesa's mach64 render backend to
               common/mach64_emit.c and to the kernel helper (submit / present /
               fence). This is the analog of gldriver/gld_render.c's *hardware*
               half, minus the Apple gld ABI.
  kmd/         kernel-mode helper (the kext analog): maps the register BAR,
               allocates contiguous DMA memory, kicks the bus master. Reached from
               user mode via a video-miniport escape or a small WDM .sys.
  mesa/        vendored Mesa subset + our patches (see "Open decisions").
  build/       build scripts (MinGW for the DLL, Win2000 DDK for the .sys).
```

## Shared with the OS X driver (do not fork)

Used verbatim from `../common/`:

- `common/mach64_emit.{c,h}` — register-write packet format, vertex/triangle
  emission, the proven state block. **Simplification for x86:** the whole
  big-endian byte-swap boundary (`m64_cb_write_le`, the PPC swap) collapses — x86
  is little-endian and the mach64 register window is already little-endian, so
  host-order dwords can be memcpy'd straight into the command buffer. We keep the
  function for source compatibility; on x86 it degenerates to a copy.
- `common/mach64_regs.h` — register offsets and bit layouts (Rage Pro core; same
  3D engine as Rage Mobility P — verify the exact device ID and any Mobility
  deltas, see Phase 0).

**Not** shared: `common/mach64_abi.h` is the OS X plugin↔kext contract and is
irrelevant here; the Win2000 winsys defines its own user↔kernel contract in `kmd/`.

## The re-notes are the differentiator

`../docs/re-notes/` records chip-level truths that stock ICDs (and stock Mesa)
get wrong — these are the bugs that make ATI's ICD flaky, and we already know the
fixes. Port these into the mach64 render backend as we go:

- `3d-triangle.md` — SETUP_CNTL flat-vs-gouraud (0x40 breaks gouraud), the
  ONE_OVER_AREA trigger, winding/area sign.
- `mach64_regs.h` Z_CNTL note — Z_SRC_2D (bit 1) vs the [6:4] test field; the
  classic wedge-the-bus-master bug.
- `texture-blend-audit.md`, `fog.md`, `logicop.md`, `scissor.md`, `stipple.md`,
  `double-buffer.md`, `drawpixels.md`, `readpixels.md` — verified state recipes.
- `bus-master-ring.md` — the proven descriptor-ring submission sequence (the
  hardware sequence is identical on Win2000; only the memory allocation changes).

## Phased plan

**Phase 0 — Target intel (no code).** Confirm on the e500: exact PCI device ID /
mach64 chip type, framebuffer + register aperture layout, VRAM size, whether ATI's
stock driver must stay resident for modeset/2D (coexistence) or we own the device.
Confirm Win2000 (vs 98SE), RAM, and DDK availability.

**Phase 1 — Kernel helper (`kmd/`).** The kext analog: map the register BAR,
read CONFIG_CHIP_ID round-trip (first light), scratch-register write round-trip,
allocate contiguous DMA memory, expose an escape/IOCTL surface. Mirrors the
proven OS X `kext/` bring-up, in Win2000 XPDM terms.

**Phase 2 — MMIO-mode first triangle.** Skip DMA: from a tiny user-mode test,
push `common/mach64_emit.c` output straight to `BM_ADDR` via the helper and read
back an off-screen flat/gouraud triangle. This revalidates the whole `common/`
core on the new silicon before Mesa is involved.

**Phase 3 — Mesa winsys + ICD skeleton.** Stand up Mesa classic core building
under MinGW as an `opengl32.dll` ICD; implement the `DrvXxx` shim (`icd/`) with a
swrast backend first (correct pixels, no hardware) to prove the ICD load path and
pixel-format negotiation against a real app.

**Phase 4 — Wire the mach64 backend.** Replace swrast with the mach64 render
backend routed through `driver/` → `common/mach64_emit.c` → `kmd/`. Off-screen
before on-screen. Apply the re-notes fixes as each state path lights up.

**Phase 5 — Present + double buffer, then the target apps.** Hook `DrvSwapBuffers`
to the verified present/flip path (`double-buffer.md`), then bring up Laser Arena
and other QuakeGL games; fix by observed failure.

## Open decisions (confirm before vendoring Mesa)

1. **Mesa version.** The mach64 driver tracks a classic-era Mesa API (~6.4–7.x,
   pre-Gallium). Recommend pinning a specific classic Mesa (candidate: 7.0.x) that
   still has both the mach64 driver API and the `drivers/windows` ICD scaffolding,
   then vendoring a minimal subset under `mesa/`. **Needs confirmation** — I'll
   verify the exact last-good version before importing.
2. **Toolchain.** Recommend **MinGW (gcc)** for the user-mode `opengl32.dll` ICD
   (Mesa builds cleanly under MinGW; cross-buildable from a modern host) and the
   **Windows 2000 DDK build environment** for the kernel `.sys` in `kmd/`.
3. **Kernel path chosen:** a **custom kernel helper** (Phase 1), coexisting with
   ATI's stock driver for modeset/2D rather than depending on ATI escapes.

## Status

| Layer | State |
|---|---|
| Architecture + reuse boundary | ✅ this doc |
| Phase 0 target intel (e500 chip ID / apertures) | ✅ `phase0-target-intel.md` — DEV_4C4D "LM" Rage Mobility-P; FB 16MB@0x80000000, MMIO 4KB@0x81000000, IO 0x3000, IRQ11 |
| `kmd/` kernel helper (MMIO first light) | ✅ 2026-08-31 — `mach64kmd.sys` (MinGW cross-built, loads unsigned on Win2000) read `CONFIG_CHIP_ID=0x64004c4d` ("LM", rev 0x64) via `MmMapIoSpace(0x81000000)` |
| MMIO-mode first triangle on Rage Mobility P | ⬜ |
| Mesa core building as opengl32 ICD (swrast) | ⬜ |
| mach64 hardware backend wired | ⬜ |
| Present / double buffer | ⬜ |
| Laser Arena / QuakeGL games | ⬜ |

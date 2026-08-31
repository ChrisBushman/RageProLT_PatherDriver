# win2k/mesa — vendored Mesa 7.0.4 (the GL front-end)

Mesa 7.0.4 is the classic-era GL implementation we build the ICD on. The source tree
is large/third-party, so it is **not committed** — run `./fetch-mesa.sh` to download
+ extract `Mesa-7.0.4/` here (from archive.mesa3d.org). This dir commits only our
fetch script, build files, and patches.

## Why 7.0.4 (confirmed a great fit)

It ships all three pieces we need, in one tree:
- `src/mesa/drivers/osmesa/osmesa.c` — off-screen software driver (T1a: prove Mesa
  core renders correct pixels on Win2000, decoupled from ICD/window plumbing).
- `src/mesa/drivers/windows/icd/icd.c` (+ `mesa.def`) — the **DrvXxx ICD shim** for
  Windows (T1b), and `drivers/windows/gdi/wmesa.c` (GDI software driver) for reference.
- `src/mesa/drivers/dri/mach64/` — the **full mach64 driver** in-tree
  (mach64_context/dd/ioctl/native_vb/...), matching `docs/dri-reference/mesa_mach64_*`
  (T2: the hardware render backend, its device layer swapped for our kmd DMA winsys).

## Build approach (custom, not Mesa's autotools)

We compile the needed `.c` files with our cross toolchain and link with the legacy
msvcrt CRT (`../toolchain/`) — Mesa's own build system doesn't know our UCRT->msvcrt
+ dllcrt0 setup. Source groups are in `src/mesa/sources` (MAIN_/GLAPI_/MATH_/SWRAST_/
SWRAST_SETUP_/TNL_/VBO_/SHADER_ ... _SOURCES).

**Compile recipe (established — imports.c compiles clean with this):**
```
i686-w64-mingw32-gcc -c -O2 -D_WIN32_WINNT=0x0500 -Dffs=__builtin_ffs \
  -Iinclude -Isrc/mesa -Isrc/mesa/main -Isrc/mesa/glapi -Isrc/mesa/shader \
  <file>.c -o <file>.o
```
(`ffs` is a Unix-ism absent on Windows -> map to the gcc builtin. Expect a handful
more small platform shims like this across the file set — that's the T1a shakeout.)

Link (software OSMesa test EXE, or later the ICD DLL) uses the msvcrt CRT + dllcrt0:
see `../toolchain/README.md`.

## T1 ladder

- **T1a** — build OSMesa (MAIN+GLAPI+MATH+SWRAST+SWRAST_SETUP+TNL+VBO+SHADER + osmesa)
  into a test EXE that renders a triangle off-screen to a memory buffer and verifies
  pixels; run on the Armada. Proves Mesa-core-on-Win2000. **(in progress)**
- **T1b** — the `DrvXxx` ICD shim (`icd.c`) as `opengl32`-loadable DLL (msvcrt +
  dllcrt0), registered so a real GL app selects our software pixelformat.
- **T2** — swap swrast for the mach64 backend -> `common/mach64_emit` ->
  `IOCTL_MACH64_DMASUBMIT` (`../kmd`). See `../driver/README.md`.

## Status

- ✅ Mesa 7.0.4 fetched; structure mapped; compile recipe established (imports.c OK).
- ⬜ T1a full OSMesa build + off-screen render proof on Win2000 (next).

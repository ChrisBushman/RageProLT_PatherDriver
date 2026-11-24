# RageProLT_PatherDriver - `standalone-driver` branch

A single standalone driver for the ATI Rage LT Pro (mach64, PCI 1002:4c49) on Mac OS X 10.3.9 Panther, aimed at the PowerBook G3
"Lombard" (PowerPC 750, one core). It replaces Apple's `ATIRagePro.kext` plus the earlier patched-kext and helper-kext approaches
(those live on branch `sonicr-driver-import`; they are deleted here).

Status: **Phase 1** (skeleton). Nothing here has been loaded on hardware yet. Read `docs/standalone-driver-design.md` first (goal, constraints,
architecture, bug ledger, phases; section 11 is the running log).

Layout
- `config/features.h` - build-time feature switches
- `common/` - portable, host-tested code shared by kext and tests: register definitions, command emission, `rage_guard.h` (what a client may make the chip do), `rage_vram.h` (the VRAM allocator)
- `kext/` - `ATIRageProLT.kext` (builds on a Panther PPC host; `make -C kext kext checkfp`)
- `rig/stubfb/` - `RageStubFB`, the boot-time chip init used on the G4 test rig only
- `tests/`, `hosttest/` - host tests (`make`); `tests/suite-reference/` is the old suite, kept as a list of checks to port (not built)
- `tools/` - driver-agnostic GL test apps and rig helpers
- `docs/` - design, memory map, reverse-engineering notes, bug history (`docs/history/README-SONICR.md`)
- `win2k/` - separate earlier work, unrelated to this driver

Constraints: G3 target, integer math first (never `double`), no FPU in the kernel (`make checkfp`), single CPU (no worker threads for
rendering), 2D acceleration always on.

# Installing the Rage LT Pro OpenGL driver

A permanent, boot-time install for the from-scratch Rage LT Pro (mach64 'LI', PCI 0x1002:0x4c49)
OpenGL/accelerator driver on **Mac OS X 10.3 Panther (PPC)**.  This replaces the per-run test
install the `tools/*.sh` runners do.

## Install / uninstall

Run on the target (the PowerBook), as root:

```
sudo sh install.sh     # build + install permanently, load now, load at every boot
sudo sh uninstall.sh   # remove and restore Apple's stock configuration
```

`install.sh` builds the release driver and is idempotent (safe to re-run to upgrade).

## What gets installed, and why it works

Two pieces go into `/System/Library/Extensions` (`/S/L/E`):

1. **`Mach64Accelerator.kext`** — the kernel accelerator (MMIO register access, bus-master DMA ring,
   command-buffer submission, present).  Its `Info.plist` declares an `IOKitPersonalities` entry that
   auto-matches the hardware at boot:
   - `IOPCIMatch = 0x4c491002 0x4c421002 0x4c501002` — ATI vendor 0x1002 × Rage LT Pro device ids
     (`LI`/`LB`/`LP`).  The Lombard's board is `0x4c49` rev 0xdc, verified via `ioreg`.
   - `IOMatchCategory = Mach64GLAccelerator` — a category **distinct** from Apple's `ATIRagePro`
     accelerator category, so IOKit starts our kext **alongside** Apple's on the same `IOPCIDevice`:
     Apple keeps 2D/scanout (the desktop stays up); we own the GL/3D engine.
   - `IOUserClientClass = Mach64GLUserClient` — the user client our GL bundle opens.

2. **`ATIRageProGLDriver.bundle`** — the userspace GLEngine renderer plugin.  GLEngine discovers
   renderers by scanning `/S/L/E/*GLDriver.bundle` (the same convention as `ATIRadeonGLDriver.bundle`,
   `ATIRage128GLDriver.bundle`, …); ours **is** the Rage Pro renderer and advertises renderer id
   `0x2540` (distinct from Apple's `0x2513`).

The installer also **disables Apple's never-finished Rage Pro GL plugin** (`ATIRageProGA.plugin`, moved
aside to `….off`).  Apple built the Rage Pro *surface/present* protocol but left the 3D command-submit
path stubbed (see `rageprolt-pathb-commit` / `docs/re-notes/path-b-findings.md`), and two accelerators
contending for the one device hard-locks the machine — so GLEngine must route the Rage Pro to us, not
to Apple's stub.

## Boot behavior

`touch /S/L/E` (done by the installer) invalidates the extensions cache so the kernel rebuilds
`Extensions.mkext` on the next boot and includes our kext; `kextcache -e` rebuilds it immediately when
available.  The GL bundle is `dlopen`'d by GLEngine at runtime (not cached by the kernel), so it just
needs to be present with `root:wheel 755`.

## Notes / limitations

- **Panther only** (10.3, PPC, gcc 3.3 kernel ABI).  The kext links against the 10.3 `Kernel.framework`
  and uses the 10.3-era `OSBundleLibraries` names, not the 10.4+ `com.apple.kpi.*` ones.
- Hardware limits are documented in `docs/re-notes/` and the `rageprolt-hw-limits` memory (no HW
  mipmapping, ~3 fps software-T&L ceiling, etc.) — these are silicon limits, not install problems.
- No code signing (Panther kexts are unsigned).  A double-clickable `.pkg` (PackageMaker) could wrap
  these scripts later; the scripts are the substance.

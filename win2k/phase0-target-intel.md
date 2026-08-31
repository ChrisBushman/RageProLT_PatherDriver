# Phase 0 — Target intel: Compaq Armada e500 (Windows 2000)

Gathered live over SSH (`ssh armada`) from the running machine on 2026-08-31, by
exporting `HKLM\SYSTEM\CurrentControlSet\Enum\PCI` with `regedit /e` and decoding
the device's `BootConfig` resource list. `SYSTEMROOT=C:\WINNT` (confirms Win2000).

## GPU inventory

Three ATI devices are enumerated; only the first is ours:

| Device | PCI ID | Location | DeviceDesc | Driver service |
|---|---|---|---|---|
| **TARGET** | `VEN_1002 & DEV_4C4D & SUBSYS_B1600E11 & REV_64` | bus 1, dev 0, fn 0 (onboard) | **RAGE MOBILITY-P AGP** | `ati2mpab` |
| dock | `VEN_1002 & DEV_5960` | bus 4, dev 10, fn 0 | RADEON 9250 | `ati2mtag` |
| dock | `VEN_1002 & DEV_5940` | bus 4, dev 10, fn 1 | RADEON 9250 - Secondary | `ati2mtag` |

- `DEV_4C4D` = ASCII **"LM"** = ATI **Rage Mobility-P** (mach64, Rage Pro-class 3D
  core). `SUBSYS ...0E11` = Compaq. This is the onboard chip and our port target.
- The two Radeon 9250 (RV280, R200-era — **not** mach64) devices are on **PCI bus 4
  = the docking station**, present only when docked, and irrelevant to this project.
  Driver `ati2mtag` vs the Mobility's `ati2mpab`.

**Testing note:** run undocked so the Rage Mobility-P is the active display head and
the Radeon doesn't grab primary.

## Rage Mobility-P resource map (decoded from BootConfig CM_RESOURCE_LIST)

PCI bus 1, 4 resources:

| Resource | Address | Size | Role |
|---|---|---|---|
| Memory (BAR0) | `0x80000000` | 16 MB (`0x01000000`) | linear framebuffer / VRAM aperture |
| I/O ports | `0x3000` | 256 B (`0x100`) | mach64 I/O register block |
| Memory (MMIO) | `0x81000000` | 4 KB (`0x1000`) | memory-mapped register file |
| Interrupt | IRQ 11 | — | shared, affinity 0xFFFFFFFF |

### Implication for the driver vs the Lombard (PPC)

On the Lombard the auxiliary MMIO aperture was disabled, so `common/mach64_regs.h`
reaches registers via the little-endian framebuffer window at `BAR0 + 0x7FF800`
(`MACH64_REG_WINDOW`). **On the Armada a dedicated 4 KB MMIO register aperture
exists at `0x81000000`** — the `kmd/` kernel helper should map that for register
access, and map the 16 MB window at `0x80000000` for VRAM (command buffers,
textures, render targets). This matches the mach64 DRI driver's x86 model and is
cleaner than the PPC path. (The registers are also still available at the top of
the framebuffer aperture if needed as a fallback.)

- **Endianness:** x86 is little-endian and the mach64 register window is
  little-endian, so the PPC byte-swap boundary in `common/` collapses to a copy.
- **Chip delta:** Lombard = `0x4C49` "LI" (Rage LT Pro); target = `0x4C4D` "LM"
  (Rage Mobility-P). Both mach64 Rage Pro-class; register set expected to carry
  over. Verify `CONFIG_CHIP_ID` and memory size once the helper can read registers.

## Register access: user-mode is a dead end (verified 2026-08-31)

`win2k/tools/mach64_peek.c` (CRT-free MinGW build, maps `\Device\PhysicalMemory`
from user mode — `Doc` is a local Administrator) proved:

- Low physical addresses in installed RAM (`0x00100000`, `0x000A0000`) map fine.
- The device MMIO apertures **`0x80000000` (FB) and `0x81000000` (regs) both fail
  `NtMapViewOfSection` with `0xC00000F4` = STATUS_INVALID_PARAMETER_6** (the
  SectionOffset). Win2000's `\Device\PhysicalMemory` section is capped at top of
  RAM and cannot map MMIO above RAM.

**Conclusion:** register/DMA access requires a kernel-mode helper (`kmd/`) using
`MmMapIoSpace()`, which has no such restriction. Win2000 loads **unsigned** drivers,
and a minimal legacy NT driver can be cross-built from the Mac with MinGW-w64
(declare DDK prototypes, link ntoskrnl/hal import libs, NATIVE subsystem) — no MSVC
DDK required. Two entry-point-not-found lessons from the user tool also apply to any
Win2000 binary: build **CRT-free** (`-nostartfiles -nodefaultlibs`, own entry) so it
doesn't import `EncodePointer`/`DecodePointer` (KERNEL32, XP SP2+).

## Still open (later Phase 0 / Phase 1)

- VRAM size (aperture is 16 MB; actual RAM likely 8 MB — read `MEM_CNTL`/config
  once register reads work, or from `ati2mpab` settings).
- Whether the stock `ati2mpab` display driver must stay resident for modeset/2D
  (coexistence design for `kmd/`), or we take the device.
- Confirm no built-in firewall blocks a debug port (Win2000 has none by default).

## Access notes (how this was gathered)

- `ssh armada` = key auth, passwordless. Non-interactive commands run under **`sh`**
  (Cygwin ash), not cmd — use `cmd /c <builtin>` for Windows built-ins, and
  `regedit /e <file> <key>` to export registry branches, then `scp` them back.
  `.reg` exports are UTF-16LE; `iconv -f UTF-16 -t UTF-8` to parse.

# kmd first light — Rage Mobility-P register access on Win2000 (2026-08-31)

The `win2k/kmd` kernel helper (`mach64kmd.sys`) — cross-built from macOS with
MinGW-w64, loaded **unsigned** on Windows 2000 via the SCM — successfully maps the
MMIO register aperture and reads/writes mach64 registers. This is the Win2000 analog
of the OS X kext's "first light" and clears the register-access hurdle for the whole
ICD port.

## What was proven

1. **The toolchain works.** A MinGW-cross-built native-subsystem `.sys` loads on
   Win2000 (unsigned, `SERVICE_KERNEL_DRIVER`, `StartService`). No MSVC, no DDK
   install, no signing.
2. **Register READ** via `MmMapIoSpace(0x81000000, 0x1000, MmNonCached)` +
   `READ_REGISTER_ULONG` — no `\Device\PhysicalMemory` "above RAM" restriction.
3. **Register WRITE** via `WRITE_REGISTER_ULONG` — scratch round-trip verified.
4. **`common/mach64_regs.h` offsets apply to the "LM" chip** — CONFIG_CHIP_ID at
   0x04e0 read the expected value.

## Register snapshot (aperture base 0x81000000, register R at base+R)

```
CONFIG_CHIP_ID [0x04e0] = 0x64004c4d   <- type 0x4c4d ("LM" Rage Mobility-P), rev 0x64
CONFIG_CNTL    [0x04dc] = 0x00002002
CONFIG_STAT0   [0x04e4] = 0x00c00094
MEM_CNTL       [0x04b0] = 0x10c57a3b
EXT_MEM_CNTL   [0x04ac] = 0xe0000c81
SCRATCH_REG0   [0x0480] = 0x04100400   (ATI driver's value; preserved)
```

All reads are live hardware values (not 0xFFFFFFFF), confirming the aperture and the
linear `base + regoffset` mapping (same scheme as the Lombard, minus the PPC byte
swap — x86 + LE register window means direct dword access).

## Scratch write round-trip (SCRATCH_REG0 @ 0x0480)

```
orig      = 0x04100400
wr DEADBEEF -> rd 0xdeadbeef   OK
wr A5A5F00F -> rd 0xa5a5f00f   OK
restore     -> rd 0x04100400   OK (original restored)
```

## The driver (`mach64kmd.c`) — v1 scope

Legacy NT driver, device `\\.\Mach64Kmd`, buffered IOCTLs:
`MAP {phys,size}` (MmMapIoSpace), `UNMAP`, `RD32 {off}->val`, `WR32 {off,val}`.
Bounds-checked against the current mapping. No DMA yet.

## Build / run

```
cd win2k/kmd && make            # -> mach64kmd.sys + kmdtest.exe (MinGW cross)
make deploy                      # scp to armada (C:\mach64kmd.sys, ~/kmdtest.exe)
make test                        # install + start + read CONFIG_CHIP_ID
# on the box: kmdtest install|start|stop|remove|peek|rd <off>|wr <off> <val>|auto <sys>
```

## Win2000 build gotchas (apply to every binary here)

- **User tools must be CRT-free** (`-nostartfiles -nodefaultlibs -e _mainstart`),
  else modern MinGW's CRT imports `EncodePointer`/`DecodePointer` (KERNEL32, XP SP2+)
  and you get "entry point not found" on load.
- **Driver:** `-nostdlib -D_X86_ -I<mingw>/i686-w64-mingw32/include/ddk` (for the
  `<wdm.h>` that `ntddk.h` pulls in), link `-lntoskrnl -lhal`, subsystem `native`,
  entry `_DriverEntry@8`. Dispatch/unload/DriverEntry must be `NTAPI` (stdcall).

## Open

- Decode VRAM size (CONFIG_MEMSIZE / MEM_CNTL fields; aperture is 16 MB, chip
  likely 8 MB).
- Coexistence: `ati2mpab` is resident and owns the same MMIO — reads/writes so far
  don't disturb it, but the DMA/engine path will need a coexistence or takeover
  decision.

## Next rung

Map the framebuffer aperture (0x80000000) too, and do an **off-screen** engine
proof (solid fill via `common/mach64_emit`), readback-verified — before anything
touches the visible screen.

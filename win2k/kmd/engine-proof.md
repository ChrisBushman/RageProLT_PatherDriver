# Off-screen engine proof — Rage Mobility-P, Win2000 (2026-08-31)

The mach64 2D drawing engine executes our command sequence on the Armada's Rage
Mobility-P, rendering to off-screen VRAM, readback-verified — coexisting with ATI's
live `ati2mpab` driver. Same milestone as the OS X side's off-screen solid fill.

## Path

`kmdtest fill` (user) → `mach64kmd.sys` IOCTLs (kernel) → MMIO registers. No DMA
ring yet — registers written directly (MMIO), the way the Lombard bring-up started.

Prereqs proven first (sub-rung 1, `kmdtest vram`):
- **Combined FB+regs mapping**: one `MmMapIoSpace(0x80000000, 0x1001000)` covers the
  16 MB framebuffer *and* the 4 KB register aperture (they're physically adjacent:
  FB ends at 0x81000000 = regs base). Register R is at map offset `0x1000000 + R`;
  VRAM byte B at offset B. Confirmed by reading CONFIG_CHIP_ID (0x64004c4d) through
  the combined map.
- **Block VRAM I/O** (`RDBLK`/`WRBLK`): write pattern / read back verified at
  0x400000, 0x600000, 0x700000, 0x780000, 0x7F0000 → **VRAM is 8 MB**; 0x400000 is a
  safe off-screen scratch region (desktop lives below it).

## The fill (sub-rung 2)

1. Pre-clear the 32x32 target at VRAM 0x400000 (so a pass proves *our* fill).
2. Wait engine idle (poll GUI_STAT bit0).
3. Batch the **exact 14-register solid-fill sequence** from
   `gld_render.c:mach64_fill_rect` via one `WRREGS` IOCTL, applied at DISPATCH_LEVEL
   so ATI's live driver can't interleave writes mid-sequence (uniprocessor box):
   `Z_CNTL=0, SCALE_3D_CNTL=0, GUI_TRAJ_CNTL=0x3, DP_PIX_WIDTH=0x60060666,
    DP_FRGD_CLR=color, DP_WRITE_MASK=0xffffffff, DP_MIX=0x00070003,
    DP_SRC=0x00000101, SC_LEFT_RIGHT=0x1fff0000, SC_TOP_BOTTOM=0x1fff0000,
    CLR_CMP_CNTL=0, DST_OFF_PITCH=(pitch/8<<22)|(off>>3), DST_X_Y=0,
    DST_WIDTH_HEIGHT=(h<<16)|w`  (last write triggers).
4. Wait idle, read back 32x32 (4 KB) and verify.

Result: **1024/1024 pixels = 0x00c0ffee.**

## What this establishes

- The mach64 3D/2D **register semantics carry from Rage LT Pro to Rage Mobility-P**
  unchanged — the Lombard's proven fill state works verbatim.
- MMIO-mode command submission works on Win2000 via the kernel helper.
- Off-screen rendering **coexists with ATI's driver** (idle-sync + DISPATCH burst;
  no observed desktop disturbance, no hang). The coexistence-vs-takeover decision is
  only forced when we need the *visible* scanout buffer.

## Coexistence caveat

The engine is shared with `ati2mpab`. We sync to idle and burst at DISPATCH_LEVEL,
which is safe for occasional off-screen tests on this single-CPU box. Sustained
rendering (the real ICD) will need a firmer arrangement — either a cooperative
engine-lock or taking the device — decided when we drive the visible framebuffer.

## Next rung

Off-screen **3D triangle** via `common/mach64_emit` (flat, then gouraud),
readback-verified — reusing the exact emission core the OS X side proved. Then the
bus-master DMA ring (replacing MMIO submission), then Mesa winsys integration.

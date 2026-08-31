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

## Off-screen 3D triangle — via the shared common/mach64_emit (2026-08-31)

The mach64 **3D setup engine** rasterizes a triangle built by the *shared*
`common/mach64_emit.c` (compiled into `kmdtest.exe` — the same code the OS X driver
uses), submitted via **DRI-style pseudo-DMA**: decode the ADRINDEX packet stream
back to consecutive register writes (`mmselect`) and feed them with FIFO flow
control (drain every 16 writes) through `IOCTL_MACH64_SUBMIT`.

`kmdtest tri`: 32x32 gouraud (single-colour) triangle at VRAM 0x400000, pixels
(4,4)-(28,4)-(4,28), colour 0xFF8040. Result:
```
emit: 52 dwords -> 35 register writes (via common/mach64_emit)
tri: 276 px=color  748 px=bg  inside(10,10)=0xff8040  corner(28,28)=0x000000
```
276 interior + 748 background = full 1024 px: a real triangle, interior = vertex
colour, exterior = clean background. **common/ validated end-to-end on Mobility-P.**

Proven here: (1) the shared emission core ports unchanged; (2) pseudo-DMA MMIO
submission with FIFO/idle sync works; (3) ADRINDEX<->register decode (`mmselect`)
matches the DRI reference.

## Known limitation to fix

`RDBLK`/`WRBLK` (METHOD_BUFFERED block VRAM I/O) **fault above ~4 KB** — the 64x64
(16 KB) triangle target crashed in the preclear; 32x32 (4 KB) works. Fine for these
proofs but must be fixed for textures/larger surfaces (switch big transfers to
`METHOD_OUT_DIRECT`/MDL, or chunk at 4 KB). Tracked for the DMA/texture phase.

## Next rung

Bus-master **DMA ring** (replacing pseudo-DMA MMIO submission) — the real path
`common/mach64_emit` + `m64_cb_write_le` are designed for; needs contiguous DMA
memory (`MmAllocateContiguousMemory`) + the descriptor ring
(`docs/re-notes/bus-master-ring.md`). Then Mesa winsys integration.

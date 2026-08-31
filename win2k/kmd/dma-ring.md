# Bus-master DMA ring — Rage Mobility-P, Win2000 (2026-08-31)

The mach64 bus-master DMA engine fetches a descriptor ring + command buffer from
system RAM and executes the register-write stream — the real submission path the
ICD will use (pseudo-DMA MMIO was the bring-up shortcut). Command buffers are built
by the shared `common/mach64_emit.c` + `m64_cb_write_le`.

## Two proofs (`kmdtest dma`, `kmdtest tridma`)

1. **PAT ring test** (the DRI/Lombard canonical proof): seed PAT_REG0/1 = 0x11111111
   via MMIO, DMA-submit a stream writing 0x22222222 / 0xaaaaaaaa, read back:
   ```
   before DMA: PAT0=0x11111111 PAT1=0x11111111
   after  DMA: PAT0=0x22222222 PAT1=0xaaaaaaaa
   ```
   (SYSTEM_TO_REG reaches the GUI register block — same regs the Lombard used.)
2. **3D triangle via DMA**: 52-dword stream (208 bytes) from `common/mach64_emit`,
   bus-mastered → `276 px=color, 748 px=bg, inside=0xff8040, corner=0x000000` —
   identical to the pseudo-DMA triangle, now via real DMA.

## Recipe (mirrors docs/re-notes/bus-master-ring.md; x86 simplifications noted)

Kernel `IOCTL_MACH64_DMASUBMIT` (mach64kmd.c):

1. **DMA memory**: `MmAllocateContiguousMemory(64KB, hi=0xFFFFFFFF)` (lazy, once).
   Carve a **16 KB-aligned** descriptor ring + a command buffer after it. Physical
   via `MmGetPhysicalAddress`. **x86: cached is fine** — bus-master DMA is
   cache-coherent (snooped), so NO uncached mapping and NO dcache flush (unlike the
   PPC/Lombard path). A `KeMemoryBarrier()` before the kick orders the WB stores.
2. **Copy** the LE command stream into the cmdbuf; **descriptor** (4 dwords) =
   `{0x7ffe48 (APERTURE_OFFSET+BM_ADDR), cmd_phys, len|DMA_HOLD_OFFSET|DMA_EOL, 0}`.
3. **Activate + kick** at DISPATCH_LEVEL (uniprocessor: blocks ATI interleave),
   after quiescing (FIFO empty + GUI idle):
   - save `BUS_CNTL`,`SRC_CNTL`
   - `BUS_CNTL = (saved & ~BUS_MASTER_DIS) | BUS_EXT_REG_EN`
   - `BM_GUI_TABLE_CMD = ring_phys` (CIRCULAR_BUF_SIZE_16KB = 0)
   - `SRC_CNTL = SRC_BM_ENABLE | SRC_BM_SYNC | SRC_BM_OP_SYSTEM_TO_REG`
   - `DST_HEIGHT_WIDTH = 0`  ← kicks
   - wait FIFO empty + GUI idle
   - **restore** `SRC_CNTL`,`BUS_CNTL` (so ATI's next 2D op doesn't read our ring)

## Constraints / gotchas carried from the LT-Pro

- **12-bit byte-count**: descriptor `len` field truncates to `len & 0xfff` — cap
  streams to ≤4092 bytes; large geometry must split across submits (as
  `gld_render.c` does at 960 dwords) or chain descriptors. Enforced in the IOCTL.
- SYSTEM_TO_REG only reaches the **GUI/drawing** register block, not scratch regs
  (why the ring test targets PAT, not SCRATCH).
- Coexistence with `ati2mpab` handled by quiesce + save/restore at DISPATCH; fine
  headless. Sustained ICD rendering may want a firmer engine-ownership scheme.

## Still open

- `RDBLK`/`WRBLK` >~4 KB fault (block VRAM I/O) — fix with METHOD_OUT_DIRECT/MDL for
  textures/larger surfaces.
- Double-buffered/async ring (split cmdbuf, retire by polling `BM_GUI_TABLE`) — perf,
  not correctness — for the ICD.

## Next

Mesa winsys integration: route Mesa's mach64 driver output through
`common/mach64_emit` → `IOCTL_MACH64_DMASUBMIT`, add the `DrvXxx` ICD shim. This
begins `win2k/driver/` + `win2k/icd/` (the OpenGL front is the remaining large piece).

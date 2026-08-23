# Bus-master ring / gouraud — findings and plan

## Why gouraud needs the ring

Per-vertex color does not latch when vertices are written as individual MMIO
register writes (flat color via `DP_FRGD_CLR` works; `DP_SRC=3D` gouraud reads
back `0xffffffff`).

Tried the shortcut of submitting the vertex **packets** (`(count<<16)|ADRINDEX
(reg)`, values) straight to the `BM_ADDR` data port via MMIO ("pseudo-DMA",
no ring).  Result on hardware: **0 pixels** — the engine ignores `BM_ADDR`
writes unless bus-mastering is actually activated (`BUS_CNTL` bus-master enable
+ `BM_GUI_TABLE` pointing at a descriptor ring + `SRC_CNTL` system-to-reg).

So the auto-incrementing vertex-packet path — the thing that latches per-vertex
color — requires the real bus-master setup.

## What the ring needs (kext work)

1. **DMA memory** — allocate the 16KB descriptor ring + command buffers as
   physically-contiguous, DMA-coherent memory; get their **bus addresses**
   (`IOBufferMemoryDescriptor` + physical segment; the Lombard predates DART so
   bus addr ≈ physical, but verify).
2. **Cache coherency (PPC)** — the CPU writes command buffers, the engine reads
   them; flush/uncached so the engine sees the data.
3. **Activate** — `BUS_CNTL &= ~BUS_MASTER_DIS | BUS_EXT_REG_EN`;
   `BM_GUI_TABLE_CMD = ring_bus_addr | CIRCULAR_BUF_SIZE_16KB`.
4. **Descriptors** — 4 dwords each: `{APERTURE_OFFSET+BM_ADDR, buf_bus_addr,
   count|HOLD|EOL, 0}`; EOL-chain to append; kick via `SRC_CNTL` +
   `DST_HEIGHT_WIDTH` (turn-9 analysis, drm_mach64_dma.c).
5. **Completion** — poll ring head / GUI idle.

## The new risk: DMA coexistence with ATIRagePro

Unlike register pokes (synchronous — we save/restore around them), the
**bus-master engine is a shared, asynchronous, stateful resource**.  ATIRagePro
likely uses bus-master DMA for 2D/WindowServer accel with its OWN
`BM_GUI_TABLE`.  If we point `BM_GUI_TABLE` at our ring, ATIRagePro's next async
DMA reads our ring → corruption/hang, and it can fire between our save and
restore (no lock).  This is a genuinely harder coexistence problem than
anything so far, and a bad DMA (wrong bus address) can panic/corrupt rather
than just stall.

Options: (a) proper DRI-style hardware lock shared with ATIRagePro (large);
(b) quiesce/stop ATIRagePro's accel while we own the engine (we work over SSH,
so a frozen display is acceptable during tests); (c) take over the device
entirely (replace ATIRagePro).

## VERIFIED WORKING RECIPE (mach64_dmatest, quiesce approach)

The bus-master DMA ring is **proven on hardware** (Lombard, Rage LT Pro).  The
engine bus-master-read a command buffer from RAM and executed a register-write
stream:

```
PAT before: PAT_REG0=0x11111111 PAT_REG1=0x11111111
PAT after:  PAT_REG0=0x22222222 PAT_REG1=0xaaaaaaaa   <- DMA-written
BM_GUI_TABLE advanced ring base -> base+0x10 (engine processed one descriptor)
```

Exact working sequence (engine idle / quiesce; save+restore ATIRagePro's
BUS_CNTL/SRC_CNTL/BM_GUI_TABLE around it):

1. **DMA memory** (kext): ring + command buffer as `IOBufferMemoryDescriptor`
   with `kIOMemoryPhysicallyContiguous | kIOMemoryKernelUserShared`.  MUST call
   `prepare()` to wire the pages before `getPhysicalSegment()` — without it the
   bus address is garbage (~0xd0320000 > RAM) and activating DMA **panics**.
   Ring MUST be **16KB-aligned** (`withOptions(..., 0x4000)`) or the engine
   won't walk it.  Map to userspace **uncached** (`kIOMapInhibitCache`).
2. **`clientMemoryForType` must `retain()`** the returned descriptor — IOKit
   releases it after mapping; without the retain the cached fCmdBuf/fRing get
   over-released → use-after-free panic on kextunload.
3. **Command buffer** = register-write stream, each entry:
   `DMAREG(reg) | ((n_regs-1)<<16)`, then n_regs values.  `DMAREG(r)` =
   `r>=0x400 ? (r-0x400)>>2 : (r>>2)|0x100`.  End the buffer with a
   `DMAREG(SRC_CNTL) | (0<<16), 0` terminator (the DRI does; harmless).
4. **Descriptor** (4 dwords, LE): `{APERTURE_OFFSET+BM_ADDR (0x7ffe48),
   cmdbuf_bus, byte_count|DMA_HOLD_OFFSET|DMA_EOL, 0}`.  `byte_count` is the
   command buffer size in **bytes** (count*4), not dwords.
5. **`mb()` (`sync; eieio`)** after writing the buffers, before the kick.
6. **Activate + kick** (register window, LE):
   - `BUS_CNTL = (saved & ~BUS_MASTER_DIS) | BUS_EXT_REG_EN`
   - `BM_GUI_TABLE_CMD = ring_bus` ( `| CIRCULAR_BUF_SIZE_16KB` which is `0<<0`)
   - `SRC_CNTL = SRC_BM_ENABLE | SRC_BM_SYNC | SRC_BM_OP_SYSTEM_TO_REG`
   - `DST_HEIGHT_WIDTH = 0`  ← kicks the transfer
   - wait for GUI idle; `BM_GUI_TABLE` advances base→base+0x10 per descriptor.

**KEY GOTCHA that cost us several runs:** the bus-master `SYSTEM_TO_REG` path
routes writes through the **GUI engine's register decode**, which reaches the
drawing-engine block (PAT/DST/SRC/vertex/`DP_*` regs) but **NOT** the
general-purpose scratch registers.  Testing against `SCRATCH_REG1` (0x484)
showed "engine walks the ring but the register never changes" — a red herring.
The DRI deliberately tests `PAT_REG0/PAT_REG1` (0x680/0x684).  Since gouraud
writes GUI vertex regs, this path reaches exactly what we need.

## Status

**Ring PROVEN.**  Flat 3D triangle (MMIO) + bus-master DMA ring both work.
Next: build the gouraud command buffer (auto-incrementing vertex packets that
latch per-vertex color) and submit it via this ring instead of MMIO.  The
DMA-coexistence risk (async collision with ATIRagePro) is handled for now by
the quiesce+save/restore approach — acceptable headless over SSH.

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

## Status

Deferred as a dedicated effort.  The flat-shaded 3D triangle works and is the
proven milestone; gouraud is a well-scoped enhancement gated on this ring +
the DMA-coexistence decision above.

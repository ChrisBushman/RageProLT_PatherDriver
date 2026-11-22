# Bus-mastered texture/pixel upload: measurements and coexistence design (Nov 20)

Goal: cut CPU time spent pushing texels to VRAM (the Lombard is CPU bound). Status: measured on the MDD rig, design below, **not integrated**.

## 1. Measurements (MDD G4, headless Rage, 112 KB image = 256x112 ARGB8888)

| Operation | CPU time per image | Notes |
|---|---|---|
| CPU stores straight into the VRAM aperture | 3.45 ms (32 MB/s) | the floor for any CPU-driven upload |
| Kext `new_texture` memcpy upload (today's path) | about 5.6 ms (about 20 MB/s) | derived from the suite's `uploadbw`, includes call overhead |
| Fill an **uncached** DMA staging buffer | 0.49 ms | what the kext hands us (`kIOMapInhibitCache`) |
| memcpy + `dcbf` into a **cached** staging buffer | 0.22 ms | lower bound if the kext gave us cached memory |
| Engine time for a **system transfer** | 1-2 ms, CPU free (about 100%) | 45 MB/s, 0 of 28,672 dwords wrong |
| Engine time for the **GUI-path** transfer (`mach64_bmupload`) | 2.05 ms | 41 MB/s, 6% packet overhead, uses the draw engine |

So per upload the CPU cost drops from 3.5-5.6 ms to about 0.5 ms, roughly 7-11x less, and the transfer overlaps with other work.
(The "CPU free" figure is limited by how often the test polls; it measures about 100% when polling sparsely.)

## 2. The primitive to use: system transfer, not the GUI path

Programmer's Guide 8.10.3 / RRG 6-10..6-11. Descriptor table of `{frame buffer offset, system physical address, byte count <= 4096 | HOLD(30) | EOL(31), 0}`; writing `table_phys | SYSTEM_TRIGGER(0)` to `BM_SYSTEM_TABLE` copies memory to VRAM. Facts established on hardware (`tools/mach64_bmsys.c`):

* Register map: the RRG `1_xx` prefix is the **low** register block (`BM_GUI_TABLE` 1_6E = 0x1b8). So `BM_SYSTEM_TABLE` = **0x1bc**, `BM_FRAME_BUF_OFFSET` 0x180, `BM_SYSTEM_MEM_ADDR` 0x184, `BM_COMMAND` 0x188, `BM_STATUS` 0x18c. (My first attempt wrote 0x5bc by misreading the prefix; that is an unidentified block-1 register, nothing visible changed, but it was a wrong write.)
* Completion: `BM_SYSTEM_TABLE` reads back the descriptor pointer and ends at `table_phys + 16 * ndesc`; `BM_COMMAND` reads 0x80000000 (end-of-list). Before a transfer both show the previous run's final state.
* It does **not** use the draw engine or the register FIFO. 20 of 20 rounds of "2D fill through the FIFO while a transfer streams" came out with the image and the fill both intact.
* `BUS_CNTL` on this rig already has bus mastering enabled (`BUS_MASTER_DIS` clear) and `BUS_EXT_REG_EN` set (0x7b330101), so no `BUS_CNTL` write is needed; check it at run time and fall back if not.
* Texels go straight to their final VRAM offset: no packet headers, any destination offset, the frame buffer offset increments per 4 KB descriptor.

The GUI path (`mach64_bmupload`) needs the draw engine, no other client may touch registers while it runs, and it carries packet overhead. It is superseded.

## 3. What Apple's kext does (static analysis of the stock ATIRagePro, md5 8fc97891...)

* `ATIRagePro::submit_buffer` (0x12730) takes **no lock**. It writes `{reg, val}` pairs with `stwbrx`, waits for FIFO space (`FIFO_STAT`, `thread_block` yield loop), and before a write to `SCALE_3D_CNTL` (reg 0x1fc in its offset space) waits for `GUI_STAT` idle.
* `ATIR3GLContext::submit_context_buffer` (0x139c4) copies our context buffer into an accelerator-owned buffer and calls `submit_buffer`; no lock either.
* The lock is one level up: **both** `IOATIRProGLContext::clientMemoryForType` (the call that triggers our drain) and `IOATIRPro2DContext::clientMemoryForType` (WindowServer's 2D path) take `mutex_lock(accel[0x544])` (load `ctx[0xb0]` / `ctx[0x94]`, then `0x544(accel)`). It is the same per-accelerator mutex, so GL drains and 2D drains are serialized against each other. (It is also the mutex our texture-allocation kext patch takes.)

Consequences:
1. Anything we put in the drain is serialized with WindowServer's 2D drains by Apple's own mutex. Our register writes are as safe as any other 3D draw.
2. A DMA that was started and must be waited for **inside** a drain can use the `SCALE_3D_CNTL` write as a completion fence (it waits for idle), but that blocks the calling thread (yield loop).
3. A GUI-path DMA must not outlive the drain (another client would program registers mid-stream). A system transfer can: it does not use the register FIFO, so it can finish after the mutex is released.

## 4. Design

Primitive: system transfer, kick = one write of `BM_SYSTEM_TABLE`.

* **Where the kick is written.** Preferred: through the existing drain as a `{0x1bc - 0x400 wrapped, value}` pair (block-0 registers below 0x400 already reach the hardware this way, see `path-b-driver-spec.md`), so it runs under Apple's accelerator mutex. The register range check (`MACH64_DRI_REG_MAX`) already admits it; the register-audit test would be changed to expect exactly this one bus-master register. Alternative: write the register directly through the CPU register mapping (no Apple involvement; still safe because the transfer does not touch the FIFO). The drain route needs no new mapping and I recommend it.
* **Staging memory.** Needs physical addresses: only the Path A kext (`Mach64Accelerator`) provides them (128 KB command buffer + 16 KB ring, physically contiguous, uncached). The plugin would open a second connection to it, used for memory only. Precondition to verify on the Lombard: Path A loads alongside Apple's kext there (`kext/coexist-test.sh`).
* **Upload flow.** Convert texels into the staging buffer (the conversion already exists), build the descriptor table (one 4 KB descriptor per page, EOL on the last; textures over 128 KB go in chunks), memory barrier, kick, return. No wait.
* **Fencing (correctness).** The engine must not sample a texture that is still arriving, and the staging buffer/table must not be rewritten while in flight. So before (a) the next upload and (b) any submit that binds a texture uploaded by DMA, read `BM_COMMAND == 0x80000000 && BM_SYSTEM_TABLE == table_end` (or just wait for the transfer, a few ms). Eviction/re-upload and `glTexSubImage` go through the same fence.
* **Multiple processes.** Path A's buffers are global to the kext, not per connection, so two GL processes would overwrite each other's staging data. Needs a cross-process lock around (fill, kick, fence) (a lock file), or a per-connection buffer in the kext.
* **Default on (changed Nov 20) and fallback.** `MACH64_DMA_UPLOAD=0` disables (was opt-in `=1`). Fall back to the existing kext memcpy path when Path A is missing, `BUS_CNTL` is not in the expected state, a transfer times out, or a texture is larger than the staging buffer.
* **What it does not change.** Conversion cost on the CPU, the 1 MB heap and LRU eviction, and the register-audit rule that the plugin never writes the GUI-path bus-master registers (`BM_GUI_TABLE`, `SRC_CNTL` bus-master bits).

## 5. Risks

* Path A kext on the Lombard (unverified; the Lombard is also the display, so WindowServer is a live client).
* A wrong physical address or table makes the chip DMA into/over RAM (a panic or corruption, not just a failed upload). The tool refuses addresses outside RAM and non-16-byte-aligned tables; the integrated version must do the same on every transfer.
* The kick through the drain is untested; direct MMIO is what was measured.
* Numbers are from a G4 on the MDD's PCI slot; the Lombard's PCI bridge and G3 will differ (the CPU saving should be larger there).

## 6. Implemented (Nov 20; now default on, `MACH64_DMA_UPLOAD=0` disables; `gldriver/gld_texture.c`)

Synchronous version 1: fill the Path A staging buffer (second connection), write the descriptor table, kick `BM_SYSTEM_TABLE` through the connection's BAR map (not through Apple's drain. see section 7: only the first drain of a frame is consumed by Apple's kext), wait with `usleep` until the table pointer reaches the table end and `BM_COMMAND` shows EOL, all under a `flock` on `/var/tmp/mach64_dma.lock`. Safety: addresses checked against `hw.physmem`, destination checked with `mach64_vram_span_ok`, `BUS_CNTL` must already have bus mastering and extended registers on (never written), any failure disables the path and the kext memcpy upload is used. Uploads under 4 KB use the kext. Fast converters for RGBA8/RGB8/RGB565.

Results on the MDD: suite 183/183 with the env var set; `dmaupload` test (RGBA8 16/128/256 KB, RGB565, Y8) passes, 3 processes concurrently all pass; `uploadbw` 12.0 -> 18.2 MB/s, kernel (sys) CPU time 0.46 s -> 0.14 s. Still to do: async overlap (return before completion, fence before draw), Lombard run.

## 7. Settled (Nov 20): only the first drain in a frame takes effect

Earlier text asserted "a second drain in a frame is dropped" without evidence (corrected in c57c90c). Measured on the MDD with `PB_SEQ=<ops> mach64_pathb_suite seq` (T triangle, F fill_rect, B explicit batch flush, E endframe; each op draws at its own spot) and `MACH64_DRAIN_TRACE=1` (logs the type-2 command-buffer header before/after each type-1 map):

| ops in one frame | result |
|---|---|
| T, F, TT, TTT (one batch) | all present |
| TF, FT, FF, TBT, TBF, FBT, TFT, FFF, FTFTF | op 0 present; every later op missing (a 2nd fill at the SAME spot with another colour leaves the 1st colour) |
| TETET (one drain per frame) | every frame's drain lands (last frame visible) |

Header trace: the first drain of a frame returns with `flags` SUBMIT bit (0x1) cleared = consumed. Every later drain in the frame returns `kr=0` (map reports success, same address 0x245000) but `flags` is still 0x1 after the call = **Apple's kext did not drain it**; the engine is idle afterwards, so nothing was merely slow. The next frame's first drain is consumed again.
**Mechanism (established: static analysis + runtime state + a predicted-and-confirmed experiment).** The type-1 map runs `IOATIRProGLContext::clientMemoryForType` case 1 (kext 0x5750). Virtual calls on `this` use `vptr + offset` with no +8 header (verified against the ATIR3GLContext vtable): `+0x5b0` = `submit_context_buffer`, `+0x5b4` = `process_command_buffer`, `+0x5b8` = `discard_command_buffer`. At 0x59fc:

```
if (accel[0x74] != this) {            // accel[0x74] = the context the accelerator last submitted for
    this->submit_context_buffer();    // ATIR3GLContext::submit_context_buffer (0x139c4): if ctxbuf.flags & 1, copy the context buffer into
                                      //   the accelerator's ring and CLEAR flags bit 0  <- the "consumed" bit we observe
    accel[0x4b4]++;  accel[0x74] = this;
}
```

So a context's buffer is submitted **only when it becomes the accelerator's current context**. A second type-1 map by the same context finds `accel[0x74] == this`, skips the submit, and returns success. Observed kernel state matches exactly: drains #1 and #3 (each frame's first) show `accel[0x4b4]` +1, `accel[0x74]` 0 -> ctx, stamps advance, SUBMIT flag cleared; drains #2 and #4 change nothing (map+unmap about 70 us against 2.3 ms for a real drain). `accel[0x74]` is reset to 0 by `IOATIRProSurface::flush_surface` (0xc5d4, the present step), `set_surface`, `ATIR3Surface::build_swap`, `prune_buffers`, and the discard path, which is why each frame's first drain works again; and set to the 2D context by `IOATIRPro2DContext::clientMemoryForType` (0x84fc), so a WindowServer 2D drain also re-arms ours.
**Confirming experiment** (`PB_DS=1 mach64_pathb_suite drainswitch`, two contexts): A drains, B draws and ends its frame, A drains again in the same frame, then A's 2nd drain LANDS (all 3 triangles present, SUBMIT flag cleared); the control without B in between loses it. Apple's userspace driver therefore never relies on a second same-context drain per frame; Path B's 4 KB-body multi-drain model and the mid-frame fill/blit/Z-clear drains do.
Not needed for the DMA path, which does not use a drain.

Consequence beyond DMA: any frame that forces more than one drain (a 2D fill/blit/Z clear mid-frame, an explicit flush, or a batch overflow of the 16 K-dword body) silently loses everything after the first drain unless the frame boundary re-arms it. The suite avoids this with `endframe()` after fills. I have not yet checked whether a heavy 3D frame that overflows the body (the `vol` test, 800 triangles) loses geometry this way; the `vol` check only requires some pixels drawn.

# Memory-to-screen blit: can DMA replace Quartz's CPU window flush? (checklist B1g)

Static analysis only (Nov 26). Nothing was run on the G4 and no kext was loaded. Sources: the Panther 10.3.9 binaries pulled earlier
(`scratchpad/qe-study/cg.bin` CoreGraphics, `iokit.bin` IOKit, `scratchpad/stock/ga/ATIRageProGA` stock 2D plugin) disassembled with the
Capstone tools of `quartz-acceleration-study.md`; chip documents (RRG, Programmer's Guide); the repo's measured DMA notes. Addresses are link
addresses inside the named binary. Anything marked "not established" was not proven. Hardware items are proposals, not run.

Related: `quartz-acceleration-study.md` section 4 (the path this note decodes further), `ga-2d-client-decode.md` (type-2 client, plugin
interface), `dma-upload-design.md`, `texture-dma-upload-standalone.md`, `dma-async-measurement.md`, `bus-master-ring.md`, `byte-order-reconcile.md`,
design 11.3.

## 0. Verdict

**Feasible only for a subset, and not worth building now.** In order of weight:

1. **Stock CoreGraphics does not take this path by default, with or without a plugin that supports it.** The only caller that can issue a
   memory-source `MPHWCopyPixels` for ordinary window backing stores is `__CGXFlushSurfacesInRegion` (0x938c669c). It passes the source kind
   `surface+0x1c`, and `_CGXNextSurface` (0x936ac478) sets that field to **-1 (memory) only when the CGS debug option bit 0x400000 is set,
   else 0** (0x936acd4c-0x936acd58 and the twin at 0x936ad048-0x936ad068). With 0, `_CGXFlushSurfacesInRegion` skips the hardware call (0x938c6c28)
   and does the CPU copy `_CGBlt_copyBytes` (0x938c6dec). So "the stock plugin declines the memory mode" (section 3) is the second gate, not the first.
2. A hardware memory blit needs **our own `GetBlitter(type 4, source 0x3000)`**, which the stock `ATIRageProGA.plugin` refuses
   (0x2c7 at 0x28ac). That means a plugin of our own (user-space bundle, referenced from `IOCFPlugInTypes`), not just the kernel client.
3. **Zero-copy is blocked by byte order** at 16 and 32 bpp: Quartz's memory is big-endian (`kIO32ARGBPixelFormat`, `kIO16BE555PixelFormat`,
   section 2.4), VRAM scan-out is little-endian, the system-transfer DMA copies bytes as they are (probable, H1), and the user pages are read-only to us. Only 8 bpp
   needs no swap. The existing design (copy into a kernel stage with `RAGE_XFER_SWAP16/32`, then DMA) still costs a full CPU copy.
4. **No elapsed-time win is expected**, only CPU offload: the measured DMA rate (35-45 MB/s) is not above the measured CPU store rate (about 33 MB/s)
   by enough to matter, and the true WindowServer CPU flush rate (AltiVec stores) was never measured. Section 5.

What would make it worthwhile: measuring (H2) that the CPU flush is a real share of WindowServer time on a Lombard, proving (H1/H3) that the swap can be
done without a CPU pass, and a decision to ship our own 2D plugin anyway (which has other benefits, section 6). Until then: **do not implement.**

## 1. The IOKit/CoreGraphics side: who asks for what

### 1.1 The blitter interface (the plugin ABI)
`IOGraphicsAcceleratorInterface` (`stock/hdr/IOGraphicsInterface.h`), vtable offsets derived from the header and checked against the stock
plugin's table at 0x3004: Reset +0x20, Flush +0x2c, Synchronize +0x34, AllocateSurface +0x3c (0x2928), FreeSurface +0x40 (0x2a30),
SetDestination +0x50 (0xf38), GetBlitter +0x54 (0x27e4), WaitComplete +0x58 (0x28bc); **+0x5c is 0 in the stock plugin** (read from the binary).
Blitter signature: `IOReturn (*IOBlitterPtr)(this, options, type, sourceType, IOBlitOperation*, void *source)`.

### 1.2 `IOPSAllocateBlitEngine` (IOKit 0x905cc5b8)
Creates the plugin interface (UUIDs of the study), calls vtable +0x50 `SetDestination(0, NULL)` (framebuffer) if present, then four `GetBlitter(options -1, type, source)`:
(1,0)->`blit+4`, (0,0x6000)->`+8`, (4,0x1000)->`+0x78` (copy region; failure tolerated, slot cleared), (1,0x2000)->`+0xc` (failure tolerated).
No consumer of `+0xc` was found among `IOFBBlitVRAMCopy` (0x905d8af4), `IOPSBlitFill` (0x905bd780), `IOFBBlitSurfaceSurfaceCopy` (0x905d8bdc);
**the memory blit does not use it**: it asks for its own blitter (1.3).

### 1.3 `IOFBBlitMemorySurfaceCopy` (CoreGraphics 0x936fc468, a CG-private helper, not IOKit)
Reconstructed prototype (arguments by register, checked against the two callers):

`(blit r3, op r4, pixelFormat r5, srcPtr r6, srcSize r7, rowBytes r8, dstCGSSurface r9, IOAccelDeviceRegion *region r10, [stack] deltaX, deltaY)`

- `op` 2 or 0x80 (0x936fc4b0-0x936fc4b8): **release** every cached pinned source (`lastMemory`, a list of up to 16 entries of 0x74 bytes, table allocated
  0x780 bytes: 0x936fc7c4-0x936fc83c). Each is freed with FreeSurface (+0x40), or with vtable **+0x5c** (called with
  `(this, 0, &surface)`) when op is 0x80 and the slot is non-null (0x936fc570-0x936fc594). Returns 0. (op 0x80 is what `MPHWSyncPixels` sends, 0x938d5570.)
- Otherwise: null blit -> 0x2c7; null source/rowbytes/region -> 0x2c2; length `rowBytes * region.bounds.h` above 16 MB -> 0x2db (0x936fc640);
  the length used is `max(srcSize, rowBytes*h)` capped at 16 MB.
- `GetBlitter(this, -1, type 4 = kIOBlitTypeCopyRegion, source 0x3000 = kIOBlitSourceOOLMemory, &proc)` once per blit ref (globals `lastBlitterRef`, `lastBlitterProc`,
  0x936fc664-0x936fc6bc). Not found -> proc 0 -> returns **0x2c7** (0x936fc6c8-0x936fc6d0). This is the point where the stock plugin declines.
- **Pinning protocol.** The source's page range (page-rounded using `vm_page_size`, 0x936fc4e0-0x936fc504) is looked up in the `lastMemory` list (same blit ref, range covered,
  same pixel format, 0x936fc6ec-0x936fc720). On a miss an entry is built: an `IOBlitSurface` with `memory.bytes = page-aligned start`, `pixelFormat = r5`,
  `size = {0,0,w,h}`, `rowBytes`, and **`AllocateSurface(options 8 = kIOBlitReferenceSource, surface, cgsID 0)`** (+0x3c, 0x936fc8c8-0x936fc8f8). The plugin is
  expected to take a reference (wire) on the user pages there. Past 16 entries the head is evicted with FreeSurface (0x936fc858-0x936fc87c). A failed `AllocateSurface`
  returns its error.
- **Destination.** If the cached destination CGS surface (`blit+0x74`) differs from `r9`: `AllocateSurface(options 1 = kIOBlitHasCGSSurface, blit+0x10, cgsID r9)` then
  `SetDestination(options 1, surface)`; for `r9 == 0`: `SetDestination(0, NULL)` (framebuffer) (0x936fc940-0x936fc9bc). The blit ref (`r3`) is an object whose `+0` is the
  interface pointer, `+0x10` a destination `IOBlitSurface`, `+0x74` the cached destination id.
- **The blit.** The source surface's pointer is set to the real (unaligned) `srcPtr`, the region is compacted (its first rect copied over its bounds,
  0x936fca0c-0x936fca1c) and an `IOBlitCopyRegion` (0x64 bytes: `IOBlitOperation` 0x58 zeroed + deltaX, deltaY, region) is built; then
  `proc(this, op, 4, 0x3000, &operation, &sourceSurface)` (0x936fca2c-0x936fca58) and, on success, `Flush(this, 0)` (+0x2c). Beam sync arrives as `options` bit 0 (`kIOBlitBeamSync`).

### 1.4 `MPHWCopyPixels` (0x938d55e8) and `MPHWSyncPixels` (0x938d54ec)
`MPHWCopyPixels(dev, dx, dy, w, h, srcKind, srcPtr, srcSize, [stack] rowBytes, srcHeight, deltaX, deltaY, dstSurface)`. `dev+0x80` = per-display private (`priv`), `dev+8` = display record.
- Pre-gates: `priv+0x178` (hardware-2D pixel flag: set when the display's pixel format code is >= 2; cleared by debug bit 0x100) and `priv+0x168` (blit
  engine allocated). Either zero -> return 0 (0x938d5618-0x938d5650).
- `srcKind == -1` = **memory**. Gates (0x938d57ac-0x938d57d4): display flag word `dev8+0xac` (read byte-reversed) bit **0x40**; **`srcPtr & 0xfff == 0` (page aligned)**;
  **`rowBytes & 0x3f == 0`**. Failing any gate returns 0 and CoreGraphics does the CPU copy. Screen format code `priv+0xa4` 2/3/4 maps to pixel format **8 / 0x10 / 0x20** (0x938d5808-0x938d5850),
  i.e. 8 bpp indexed, `kIO16BE555PixelFormat`, `kIO32ARGBPixelFormat` (the source has the **screen's pixel format in Mac big-endian layout**; there is no conversion request).
- **Cache management is done by CoreGraphics**: `dcbst` over every source row of the rectangle, `sync`, `dcbf`, `lwbrx`, `isync` (0x938d58a0-0x938d593c) before the call. So the contract expects the
  source in memory, not only in cache, when the blitter starts. Whether a Lombard needs it (PCI snooping) is not established; doing it is cheap.
- The region built for `IOFBBlitMemorySurfaceCopy` (0x938d5880-0x938d5898): `num_rects = 1`, bounds `{0, 0, rowBytes/bytesPerPixel, srcHeight}` (the whole source extent), rect `{dx, dy, w, h}` (destination
  rectangle), `deltaX/deltaY` = source origin offset.
- After success: `priv+0xc8 |= 3` (memory blit pending, hardware touched) and returns 1 (0x938d59b0-0x938d59c8). On **0xe00002c7** it clears bit 0x40 of `dev8+0xac` permanently, logs `"unsupported"`
  and returns 0 (0x938d59d0-0x938d5a3c): after one refusal CoreGraphics never asks again in that session. Errors 0x2c2 / 0x2be / 0x2db / 0x2f0 are silent fallbacks; others log.
- `MPHWSyncPixels` (0x938d54ec): if `priv+0xc8 & 2`, calls `IOFBBlitMemorySurfaceCopy(op 0x80)` (unpin) and clears the bit, then `IOFBSynchronize(blit, x, y, w, h, 2 = kIOBlitSynchronizeFlushHostWrites)`
  (IOKit 0x905d89c0 = vtable +0x34 `Synchronize`). So **Synchronize must not return before the DMA has landed.**

### 1.5 When is the memory flag set, and who sets the source kind?
- `dev8+0xac` is computed in `__MPSetDisplayFlags` (0x9378ac18): if the hardware-2D bit `priv+0xa0 & 0x20` is set (it stays set when `IOPSAllocateBlitEngine` succeeded, 0x93789e0c-0x93789e48),
  it ORs `0x58` (= **0x40 memory**, 0x10 surface-surface, 0x08), plus bits 2/4 from VRAM size tests and 0x80 from a registry number property > 1. So with a plugin loaded the memory flag is on by default.
- **`surface+0x1c` is the real switch.** `_CGXNextSurface` for a window's own backing store (case 1, 0x936accf0): `andis. r0,flags,0x40` then `li r0,-1` only when set (0x936acd4c-0x936acd58); the result is stored at
  `surface+0x1c` (0x936acd58). The twin for the other backing flavour (0x936ad048-0x936ad068) is identical. The flag word is `_CGSDebugOptionFlags_` (0xa36812d0), set through
  `CGSSetDebugOptions` (0x938f6cd8 -> server `__CGSSetDebugOptions`). **Bit 0x400000 is not enabled by default** (the table of debug bits is not decoded: name/meaning of the bit not established; whether
  any shipped tool sets it: not established).
- For accelerated GL-window surfaces (case 2, 0x936ac7c0): `surface+0x1c = surface id` (`stw r2,0x1c(r30)`, 0x936acc1c), a **surface-to-surface** copy through `IOFBBlitSurfaceSurfaceCopy` (IOKit 0x905d8bdc),
  which reuses the copy-region blitter `blit+0x78` (type 4, source 0x1000 from `IOPSAllocateBlitEngine`) with source type **0x7000** (CGS surface) (0x905d8cc0-0x905d8cd0). That is the B1b surface path, not memory.
- Other callers of `CGXHWCopyPixels` (0x938d2a98): `_layer_blit_stack` (0x9379431c, two sites 0x937949a8 / 0x93794ac0) - both require a surface id (not 0, not -1; 0x937948f4-0x93794900,
  0x93794a38-0x93794a3c). So the memory source kind is requested **only** by `_CGXFlushSurfacesInRegion` under the debug bit.

## 2. What CoreGraphics passes, in detail

### 2.1 Memory and page state
`srcPtr` is the window backing store returned by `_CGXWindowBackingGet` (0x936a8a88; fields at `surface+0x20` pointer, `+0x28` rowbytes, `+0x30` pixel format byte 8/0x10/0x20, `+0x3c` size). User pages of the WindowServer
(or shared memory it maps), not wired by CoreGraphics. Alignment is enforced by the gates of 1.4; whether WindowServer's backings are in fact page aligned with 64-byte-multiple rowbytes: **not established**
(`_CGXWindowBackingGet` not decoded; a window whose rowbytes is, for example, `width*4` with an odd width is not a multiple of 64 and would never use the blitter).
Pinning is **the plugin's job** (`AllocateSurface(8)`), kept across calls (up to 16 regions of up to 16 MB), released at `MPHWSyncPixels`/`op 2`.

### 2.2 Clip
The clip is a CGS region enumerated rect by rect (`CGSRegionEnumerator`, 0x938c6a44-0x938c6df0); **one `CGXHWCopyPixels` per rectangle** of the dirty/visible region. A complicated visible shape means many small blits.

### 2.3 Destination
`dstSurface` is 0 here (the caller stores 0 at 0x938c6c88): the destination is always the **framebuffer** (VRAM scan-out). The CPU path writes `dev+0xc` (mapped framebuffer base) plus offsets (0x938c6d74-0x938c6de8).

### 2.4 Pixel format and byte order
Source formats: 8 indexed (0x08), 16-bit big-endian 1555 (0x10), 32-bit ARGB big-endian (0x20). The chip's scan-out is little-endian (word `0xAARRGGBB` as bytes `BB GG RR AA`; byte-order-reconcile.md section 3).
What each path does with it:

| path | byte order work |
|---|---|
| CoreGraphics CPU copy into the mapped framebuffer | none in software: WindowServer stores through the **upper (swapped) aperture** (UPPER_APER_ENDIAN 01 at 16 bpp, 10 at 32 bpp, set by the NDRV, `apple-ndrv-framebuffer.md`), the aperture swaps on the bus |
| system-transfer DMA (`BM_SYSTEM_TABLE`) | copies bytes as in memory; no endian field in the descriptor or in `BM_COMMAND`. Whether aperture swapping applies to bus-master writes: **not established** (it is an aperture-decode property, so probably not) |
| existing kernel upload (`xferKick`, RageLTDma.cpp:124) | CPU swap in the kernel stage (`rage_xfer_inplace`, `RAGE_XFER_SWAP16/32`) before the kick; the source is the copy-in staging buffer, which is ours to modify |
| draw-engine host-data path | `HOST_BIG_ENDIAN_EN` (RRG 0_90 bit b; GUI_TRAJ_CNTL bit aa) swaps 15/16 bpp bytes within a word and reverses 32 bpp dwords (RRG 5-33/5-34): exactly the needed conversions, but **never used or measured by us** (section 4, option C) |

Because the source pages are the client's and read-only for us, an in-place swap is not allowed; a swapped copy is a second full CPU pass.

## 3. Why the stock plugin declines (proved)
`GetBlitter` (plugin 0x27e4): `mode & 0x7FFFF000`; table: (1, 0 or 0x1000) -> rProCopy; (4, 0x1000) -> rProCopyRegion; (kind&~0xF000 == 0, 0x6000) or (0x100, 0x5000) -> rProFill; (2, 0x6000), (3, 0x6000) lines/scanlines.
(4, 0x3000) is none of these and falls to `0x2c7` (0x28a0-0x28b4). `AllocateSurface` (0x2928) supports only `options & 1` (CGS surface); any other options value, including 8, returns 0x2c7 (0x29f8) even if
GetBlitter had succeeded. The +0x5c slot is empty. So the stock plugin implements no part of the memory protocol. (Why Apple left it out: not established.)

## 4. What the kernel side would need

Nothing below exists yet; B1b (the type-2 client) does not exist either. Design pattern to follow: kernel-owned memory, validated requests, serial completion check, **no threads, no interrupts** (`dma-async-queue.md`, `texture-dma-upload-standalone.md`).

### Option A: copy-in + swap + DMA (reuses the proven machinery)
1. Our plugin's `AllocateSurface(8)` records `{ptr, len, rowBytes, format}` (no wiring needed; a user-space bookkeeping object).
2. `blitter(4, 0x3000)` calls a new type-2 selector `blit_memory(dst rect/pitch/format, srcPtr, srcRowBytes, rects...)` (struct in, validated in the kernel: rect inside the display, source range registered, format matches the current mode).
3. Kernel: `copyin` rows (own task: `copyin()` on the caller's thread when `fTask == current_task()`, the established fast path) into the 1 MB stage, byte swap in place (cached stores), `dmaFlush` (dcbf),
   write one descriptor per source row piece into the table (explicit destination offset per descriptor; one descriptor per row when the destination pitch differs from the source row, else merged 4 KB pieces),
   kick `BM_SYSTEM_TABLE`, return. `Synchronize`/`WaitComplete`/the next engine use call the existing `dmaWait`/`xferWait` (`fDmaPending`; completion = table end and EOL plus last-word readback).
4. Table: the current table is one 4 KB page (256 descriptors); per-row descriptors of a tall window need more (1024x768: 768 descriptors, 12 KB): enlarge the table or chunk per stage fill.
5. Limits: count per descriptor <= 4096 (system descriptors of 4096 are exercised by `xfer_test`; the 12-bit truncation at 4095 bytes proven for the GUI path is separate). The source address bits 1:0 give the byte alignment (Programmer's Guide 8.10.2), so unaligned rows are legal per the document; not tested.

CPU cost is the copy-in + swap; the chip does the PCI write. It is the existing 3.4x CPU saving per byte relative to aperture stores (section 5), not zero.

### Option B: zero-copy for 8 bpp only (no swap)
`IOMemoryDescriptor::withAddress(addr, len, kIODirectionOut, task)` with the owning task (the 2D client knows its owner), `prepare()` (wire; **mandatory**: an unwired `getPhysicalSegment` gave garbage and a panic, `bus-master-ring.md`), `getPhysicalSegment` per page (physical pages are not contiguous: at most one descriptor per 4 KB page), the RAM range check of the existing path, then table and kick; `complete()` after the transfer.
Page-aligned base and 64-byte rowbytes (CoreGraphics gates) make rows natural. Value: low (8 bpp desktops are rare; the Lombard runs 15/16/32 bpp).
Costs not established: wiring time for 384-768 pages (the only measured wiring cost is `userIn` 29 us for a small descriptor, `dma-async-queue.md`), and VM interaction with a process that exits mid-transfer
(existing design: the table is ours, the next `dmaWait` finishes it; with user pages the descriptor retains them until `complete()`).

### Option C: zero-copy with hardware swap via the draw engine (research)
Stream the user pages through `BM_HOSTDATA` (RRG 0_91, "Bus Master host data register") into a host-data blit with `HOST_BIG_ENDIAN_EN`, destination pitch and clip handled by the engine. The GUI-path stream
(`mach64_bmupload`) already moves pixel data this way but through a register-write packet stream (headers every 16 dwords: 6% overhead, 41 MB/s, `dma-upload-design.md` section 1), which is **not** zero-copy.
Pointing HOLD descriptors straight at the pixel pages (no headers) would be; whether BM_HOSTDATA accepts that, whether the swap applies, and whether it combines with our engine state: **not established**.
It occupies the draw engine during the transfer (acceptable in the serial kernel-owned model).

### Common requirements
- Coherency: CoreGraphics already `dcbst`/`dcbf`s the source rows. The kernel should not rely on that alone (a flush of the wired range for Option B, `dmaFlush` of the stage for Option A).
- Security: the kernel validates every request: source range registered by this client, destination inside the display's scan-out for the current mode, pitch/format match; cap the wired total (CoreGraphics may keep 16 regions of up to 16 MB pinned, 1.3) and refuse beyond it with 0x2c2/0x2db (silent CPU fallback in CoreGraphics, 1.4).
- Fencing against our own users: `idleWait()` before the kick (as `xferKick` does). With a GL stream running this wait is the dominant fixed cost (`dma-async-queue.md`: idle waits 0.85 ms average per call in the Sonic R run).
- Failure policy: on any anomaly return 0x2c7 once: CoreGraphics then permanently stops using the path (1.4), the existing safe fallback.

## 5. Benefit estimate for a 1024x768 window update

Measured numbers used (MDD G4 with the headless Rage; Lombard/G3 untested):
- CPU stores into the VRAM aperture: 3.45 ms per 112 KB = **about 32-33 MB/s** (`dma-upload-design.md` section 1; kernel uncached map, word stores).
- System transfer engine time: 1-2 ms per 112 KB = **45 MB/s** (same table); 256 KB in 7.6 ms (35 MB/s) and 1 MB in 31 ms (about 34 MB/s) back to back including the copy and fence (`texture-dma-upload-standalone.md`).
- CPU time inside the upload call (copy-in + optional swap + dcbf + kick): 16 KB 0.14 ms, 256 KB 2.2 ms, **1 MB 9.9 ms** (about 10 ms/MB) vs 34 ms/MB with CPU stores (same document, `xfer_test`).
- Fixed costs: IOKit round trip 3.7 us; per-submit kernel work 7-14 us; `idleWait` drains up to ms when a GL stream runs.

| update | bytes | CPU stores to aperture at ~33 MB/s | DMA engine at 35-45 MB/s | CPU time, Option A (about 10 ms/MB; extrapolated beyond 1 MB) |
|---|---|---|---|---|
| 1024x768 @ 16 bpp (full) | 1,572,864 | about 48 ms | about 35-45 ms | about 15 ms |
| 1024x768 @ 32 bpp (full) | 3,145,728 | about 95 ms | about 70-90 ms | about 30 ms |
| 400x300 @ 32 bpp (typical dirty rect) | 480,000 | about 15 ms | about 11-14 ms | about 4.6 ms |

Reading:
- **Latency to pixels is about the same** (DMA 35-45 MB/s vs CPU stores about 33 MB/s) and Option A adds a CPU pass before the DMA starts, so elapsed time is no better and can be worse for a single rectangle (the single 1 MB stage allows no overlap of copy-in n+1 with transfer n).
- **CPU time saved is real but partial**: about 3x per byte (95 -> 30 ms at 32 bpp full screen). On a Lombard where WindowServer is CPU bound that is the only benefit, and only for large updates; small ones are dominated by per-call costs and the existing design already sends anything under 8 KB through the CPU path.
- **The CPU baseline is unmeasured where it matters.** CoreGraphics' own copy (`_CGBlt_copyBytes`, through the swapped upper aperture) may run faster or slower than the kernel's 33 MB/s word stores; the cache mode of the mapping handed to WindowServer decides (not established; the NDRV sets inhibited for its own use, `apple-ndrv-framebuffer.md`). If the real rate were 60-80 MB/s, DMA would be slower in elapsed time and equal in CPU only with zero-copy.
- Zero-copy (Options B/C) would cut CPU time to table build plus wiring (expected tens to hundreds of microseconds; **not established**), but it is blocked at 16/32 bpp by the swap question (H1/H3).

## 6. Risks
1. **Wrong physical address or table -> DMA over RAM** (panic or silent corruption): same class as `dma-upload-design.md` section 5; Option B adds user pages (wiring discipline).
2. **Wired memory exhaustion** from CoreGraphics' pinned cache (16 x up to 16 MB, held until a sync): cap and refuse.
3. **A user-space plugin of our own in WindowServer's address space**: a crash there is a WindowServer crash. The stock plugin is already loaded there; ours would replace it. Side benefit: our own plugin has no "must conform to class ATIRagePro" gate (`ga-2d-client-decode.md` section 1), which simplifies B1b.
4. **CPU and DMA ordering**: a CPU draw after a DMA blit needs `Synchronize` to block until the transfer is done. CoreGraphics does call `CGXSynchronizeDisplayDevice` before its CPU path after a hardware op (0x938c6d04-0x938c6d18), so the protocol exists; ours must implement it (the stock `Synchronize` returns 0, `ga-2d-client-decode.md` section 1).
5. **Debug-flag dependency**: the path exists only behind an internal CGS debug bit; it is not a supported interface and may differ in other CoreGraphics builds.
6. **Tearing**: `kIOBlitBeamSync` is requested by CoreGraphics (`r23`); `GetBeamPosition` is unsupported in the stock plugin; ignoring it matches stock.
7. The `lastBlitterRef/lastBlitterProc` cache is a single global (0x936fc668-0x936fc6bc): a second display re-queries each time; harmless.

## 7. What must be proven on hardware (proposals; none run)
- **H1 endian of DMA vs apertures.** With an `xfer_test`-style kick, DMA a known 32-bit pattern and read it back through the lower view and through the swapped upper view (stub rig has UPPER 01). Decides whether any zero-copy scheme needs a swap pass at all.
- **H2 the real CPU flush cost.** Read-only first: `sample WindowServer` during a window drag on the G4 for the share in `CGBlt_copyBytes` / `_CGXFlushSurfacesInRegion`. Then a user-space micro-benchmark storing 1.5/3 MiB with 16-byte AltiVec stores through the mapping WindowServer uses.
  This sets the real CPU baseline; the go/no-go hinges on it.
- **H3 BM_HOSTDATA with `HOST_BIG_ENDIAN_EN`** (Option C): host-data blit from a pattern buffer with HOLD descriptors at BM_HOSTDATA; verify pixels and the swap in VRAM through the lower view. Touches engine registers: a supervised chip-register experiment.
- **H4 wiring cost**: time `IOMemoryDescriptor::withAddress` + `prepare()` + `getPhysicalSegment` over 384/768 pages in a user task, and with the owning task exiting mid-transfer.
- **H5 gate test**: with a prototype plugin that returns success for (4, 0x3000), set the CGS debug bit 0x400000 from a tool (`CGSSetDebugOptions`) and log which flushes arrive, their alignment (page, rowbytes multiple of 64) and rect counts. Proves the whole chain (and the alignment question of 2.1) before any kernel work.

## 8. Proposed checklist items (to agree; not added to `remaining-work.md`)
- B1g-1 (static, cheap): decode `_CGXWindowBackingGet` (0x936a8a88): page-aligned base? rowbytes multiple of 64? Decode the CGS debug-bit table (bit 0x400000) and who can set it.
- B1g-2: H2 baseline (read-only `sample`, then the micro-benchmark) after B1b works; go/no-go gate for everything below.
- B1g-3 (only if B1g-2 shows the CPU flush is a significant share): H1, H4, then an own-plugin prototype with H5; Option A first (reuses proven code), Option B only for 8 bpp.
- B1g-4 (independent, small): record the cache mode of the aperture mapping the framebuffer hands to WindowServer and whether a write-combining/gathering mode is available (could speed the CPU flush with no DMA at all).

## 9. Not established
- Whether any shipped Panther component sets CGS debug bit 0x400000; its documented meaning.
- Page alignment and rowbytes of WindowServer's window backing stores (the gates of 1.4 decide how often a blit would qualify).
- Whether bus-master DMA writes pass through the aperture endian swap; whether `BM_HOSTDATA` + `HOST_BIG_ENDIAN_EN` works as a header-less pixel path.
- The WindowServer CPU flush rate and the cache mode of its framebuffer mapping.
- Wiring cost per page; behaviour when the owning task dies mid-transfer.
- Behaviour of `kIOBlitBeamSync`; meaning of `dev8+0xac` bit 0x08 and of the registry number property that sets bit 0x80.
- Why the stock plugin never implemented the memory protocol; whether the Lombard's PCI path is cache-coherent for DMA reads of user memory.

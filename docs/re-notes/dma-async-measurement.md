# How much time is the CPU blocked in the synchronous DMA paths? (Nov 20, MDD G4, `mach64_pathb_suite dmabench`)

Question: is making the DMA paths asynchronous (return after the kick, fence before dependent use) worth the added risk? Method: timers around the exact blocking calls (`gld_render.c` vdma append/submit, `gld_texture.c` dma_fill / transfer wait loop) against wall time per frame. Run: `PB_BENCH_TRI=n PB_BENCH_FRAMES=n PB_BENCH_TEX=w ./mach64_pathb_suite dmabench`.

## Results (as the plugin is today)
Geometry (n triangles per frame, 60 frames): wall 500 / 986 / 1961 ms per frame for 1500 / 3000 / 6000 triangles. Time in the DMA submit call 4.1 / 8.1 / 16.2 ms (0.8% of the frame); append calls 0.6 / 1.3 / 2.6 ms.
Texture (create + upload + draw + end frame + delete, 60 iterations; the fixed cost of an iteration is about 10 ms): 

| texture | wall, DMA on | blocked in transfer wait | CPU texel conversion | wall, DMA off (kext copy) |
|---|---|---|---|---|
| 64x64 (16 KB) | 10.7 ms | 0.17 ms | 0.05 ms | 11.1 ms |
| 128x128 (64 KB) | 11.3 | 0.55 | 0.21 | 13.0 |
| 256x256 (256 KB) | 13.6 | 2.12 | 0.85 | 20.6 |
| 512x512 (1 MB) | 23.9 | 8.45 | 4.42 | 51.8 |

## The geometry frame time is NOT the DMA
A CPU profile (`sample`) of the geometry run: ~91% of all samples are in `mach64_gli`, called from `mach64_bind_texture`, `mach64_fog_active`, `mach64_logicop`, `mach64_alpha_tst` on every triangle; ~5% is `gld_log` (an fsync'd write per call). `mach64_gli` (gld_render.c) re-locates the GL context by scanning backwards from `ctx+0x10` up to 0x8000 bytes (8192 words) for the word that points back at `ctx`, on every call. About 0.33 ms per triangle, linear in triangle count, user-mode CPU (19.1 s user of 19.97 s real for 60000 triangles).
Experiment (not committed; now committed, see below): cache the located base per ctx, validated by the same back-pointer test: 300 / 1500 / 3000 / 6000 triangles take 19 / 50 / 86 / 160 ms per frame (before: 109 / 500 / 986 / 1961), i.e. about 12x faster per triangle. With that, the DMA submit is 8.1% / 9.4% / 10.1% of the frame (4.1 / 8.1 / 16.2 ms).

## What async could gain (upper bounds, not measured gains)
* Geometry: at most the blocked share. Today <1% (so nothing); after the `mach64_gli` fix about 8-10% of a heavy frame, and only if the CPU has other useful work to overlap (building the next batch) and the engine does not become the limit.
* Textures: at most the wait column (15% of a 256x256 iteration, 35% of a 512x512 one), but uploads happen at texture creation/change, not every frame, so the effect is on load-time stalls rather than steady-state frame time. The CPU texel conversion (0.85 / 4.4 ms) could overlap the previous transfer only with a second staging buffer (today one 128 KB buffer).
* Risk of async: ownership of staging/ring memory when a process dies mid-transfer, fence placement before every dependent draw, present and buffer reuse.

## After the fix (commit "Per-triangle CPU cost", Nov 20): `mach64_gli` base cached per context + per-primitive trace lines rate-limited
Geometry frame time (60 frames): 300 triangles 109 -> 14.0 ms; 1500: 500 -> 22.3 ms; 3000: 986 -> 32.5 ms; 6000: 1961 -> 53.1 ms (37x). Per-triangle CPU is now about 0.007 ms. The profile before the fix was 91% `mach64_gli` + 5% `gld_log`; the user/sys split of a 6000-triangle run is now 0.84 s user + 0.48 s sys for 20 frames.
Blocked in the DMA submit (4.1 / 8.1 / 16.2 ms) is now 18% / 25% / 30% of a 1500 / 3000 / 6000-triangle frame. Of the rest of a frame, about 14 ms is fixed per frame (state setup, clear, end-of-frame PIO submit + present, the 300-triangle figure) and the remainder is plugin CPU (about 5 ms per 1000 triangles incl. vertex-DMA appends of 0.5 ms per 1500).
Texture numbers are unchanged (they do not go through the changed code): wait is 5% / 16% / 37% of a 128 / 256 / 512-pixel-square iteration (DMA off is 1.2x / 1.5x / 2.2x slower).
Correctness after the change: suite 184/184 (incl. dmabench), heavy 1500 and 6000 = 0 missing, multictx + soak pass, texops 31/31.
So the blocked share is now material for heavy frames (up to ~30%), which changes the cost/benefit of async vertex DMA from "nothing" to "up to about a third of a heavy frame, if the CPU has independent work to overlap and the engine is not the limit". That upper bound is NOT a measured gain: the 16 ms includes the kernel building the descriptor chain and the engine's own drawing time, which async cannot remove, only overlap.

## Build vs engine split of the vertex-DMA submit (Nov 20; `MACH64_VDMA_SPLIT=1 ... dmabench`, user-space only, no kext change)
Method: before each real flush the same stream goes through a DRY submit (op 2 arg bit0: the kernel validates and builds the descriptor table, touches no hardware); real submit minus dry submit = the DMA run (engine + wait + register save/restore); an empty op-7 call gives the IOKit round trip.
Per frame: 1500 triangles: real 4.10 ms = dry 0.50 + run 3.59 (4 flushes); 3000: 8.12 = 1.13 + 6.99 (9 flushes); 6000: 16.18 = 2.38 + 13.80 (19 flushes). Empty IOKit round trip 3.7 us. So ~85% of the submit is the DMA run, ~15% is kernel validation + descriptor build (about 0.12 ms per 8140-dword flush); round-trip overhead is negligible. About 0.73 ms of run time per flush, ~2.3 us per (tiny, 5x5 px) triangle.
Not separated: how much of the "run" is the engine drawing versus the fixed per-flush register save/restore and idle waits in `dma_run` (PIO to uncached registers). Separating needs timestamps inside the kernel handler or a one-triangle flush.
Upper bound for async on a 6000-triangle frame (57 ms): the run (13.8 ms, 24%) could overlap with CPU work (about 41 ms of non-blocked CPU incl. ~12 ms of harness), but the single ring serialises consecutive flushes and the end-of-frame present waits for idle, so the realistic gain is below that.

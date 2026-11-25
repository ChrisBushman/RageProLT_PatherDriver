# Standalone driver: design (Phase 0 draft)

Status: **DRAFT for review. No hardware has been touched and no driver code has been written.**
Branch `standalone-driver`, based on `sonicr-driver-import` (e0296f4). Date: 2026-11-24.

## 1. Goal

One new kernel extension, `ATIRageProLT.kext`, plus a rewritten GL bundle, that replace **everything** this repo and Apple use for the Rage LT Pro GL path today:

| Replaced | Why |
|---|---|
| Apple `ATIRagePro.kext` (stock or binary-patched) | Patching it is fragile: arbitrary kernel read (sel 14 peek), sel 10 multiplexing, an Apple stack-descriptor bug that panicked ~10% of processes |
| `Mach64Accelerator.kext` ("Path A") | Second accelerator on the same chip; kernel code was written around the patched Apple kext |
| Path-A / Path-B code in `kext/`, `kext-patch/`, `install-*.sh`, the old `gldriver/` | Deleted on this branch at the start of Phase 1. History stays on `sonicr-driver-import`. |

Product bar: **"stock+"**. Everything stock `ATIRagePro.kext` does for the Lombard (2D acceleration for Quartz, surfaces, swap, mode change, sleep/wake) keeps working, and the GL driver Apple never shipped is added.

Kept: `common/` (register definitions, command emission, host-tested), the register recipes in `docs/re-notes/`, the GL feature logic in `gldriver/` (rewritten to the new ABI), the test suite in `tools/`.

## 2. Constraints (user, Nov 24)

1. **Target is the Lombard G3 (PPC 750, one core).**
2. **Arithmetic order of preference:** integer; then lookup table plus interpolation; then fixed point; single-precision float only if the precision loss is too great. **Never `double`.** Kernel code uses no floating point at all (Mac OS X kernel code must not touch the FPU).
3. **One CPU:** extra threads only for genuinely asynchronous waiting (interrupts, hardware events). No worker-thread rendering engine.
4. **No Lombard access for me.** Another contributor tests builds and files issues. All Lombard-only features are built and unit-tested, never claimed verified from G4 results.
5. **Optional features compile out** with build flags.
6. **Test apps (on the G4 MDD):** Sonic R, Chess, Flurry, Atlantis.
7. Attach alongside Apple's `ATY_*` framebuffer; do not take over display or modesets.

### 2.1 How the no-double rule will be enforced

- `-mcpu=750 -msoft-float` for the kext (no FP instructions at all, and a link-time/objdump check that none appear).
- Plugin: `-mcpu=750 -fsingle-precision-constant -Wdouble-promotion -Wfloat-conversion`; a `make checkfp` target disassembles every object and fails on any double-precision instruction (`fadd`, `fmul`, `fdiv`, `fsub`, `fmadd`, `lfd`, `stfd`, `fcmpu` on doubles...), with an explicit allow-list for compiler-generated `lfd/stfd` 8-byte copies that the check can tell apart (to be worked out in Phase 1).
- Current inventory of `double`/`float` in the code being carried over (counts of the words, not of operations): `gld_prims.c` float 76; `gld_render.c` double 9 / float 25; `gld_texture.c` double 3 / float 15; `gld_context.c` double 3; `gld_pixelformat.c` double 4; `gld_entry.c` double 1; `common/mach64_emit.c` double 2 / float 3. Each will be classified in Phase 3 (integer, LUT, fixed point, or single float) and the choice recorded.
- The hardware itself wants IEEE single floats in three places (vertex `W = 1/w`, `ONE_OVER_AREA_UC = 1/area`, and S/T). Plan: GLEngine hands us single-float vertex data; we read the bits and produce those register values with integer code (reciprocal by table plus one Newton step in fixed point, then pack to IEEE single bits). Whether a hardware single-float divide is cheaper or more accurate than that is **measured** in Phase 3 before choosing.

## 3. Architecture

```
 Quartz / WindowServer ---> ATIRageProGA.plugin ---(type 0/2 user clients)---+
 GLEngine --> ATIRageProGLDriver.bundle (new) --(type 0/1 user clients)------+--> ATIRageProLT.kext (new)
                                                                              |        |
                                              ATY_* framebuffer (Apple) <-----+        v
                                              owns display, modesets            Rage LT Pro (mach64)
```

### 3.1 Kernel classes (working names)

| Class | Role |
|---|---|
| `RageLTDevice` | `IOService` matched on PCI `1002:4c49` (and the Rage Pro family IDs the stock kext lists); maps BAR0/BAR2; owns the chip: register access, init after ROM POST, engine reset, interrupt source. |
| `RageLTVram` | Single VRAM allocator for surfaces, Z, textures, cursor reservation. Enforces the usable range `[0, 0x7FF800)` in one place. Replaces Apple's heap *and* the plugin's own texture heap. |
| `RageLTEngine` | Command validation and submission. Takes `{reg,val}` batches from user space and writes them to the chip through an **allow-list** (the 45 registers measured in `memory-map.md` §4, plus additions needing review), never raw offsets. Owns FIFO throttling and idle. DMA ring behind `RAGE_FEAT_DMA`. |
| `RageLTSurface` (user client, type 0) | Stock-compatible surface protocol (`set_id_mode`, `set_shape`, `surface_flush`, read/write lock). |
| `RageLTGLContext` (user client, type 1) | Stock-compatible selectors 0-14 plus **dedicated** selectors replacing the hacks: `get_render_target`, `upload_texture`, `alloc_vram`, `free_vram`, `get_mode_gen`. |
| `RageLT2DContext` (user client, type 2) | Stock-compatible 2D context for `ATIRageProGA.plugin` (`rProCopy`, `rProFill`, scanlines, surface lock/swap, beam position). Behind `RAGE_FEAT_2D_GA`. |
| `RageLTPower` | Sleep/wake (`RAGE_FEAT_SLEEPWAKE`). |
| `RageLTLog` | Diagnostics and panic record (`RAGE_FEAT_PANIC_LOG`), see §6. |
| `RageLTOverlay` / `RageLTScaler` | Video overlay and scaler (`RAGE_FEAT_OVERLAY`, `RAGE_FEAT_SCALER`). |
| `RageLTDVD` | Only if Phase 0 confirms stock has nothing (`RAGE_FEAT_DVD`). |

Class names and the selector/type numbers of the stock-compatible clients **must equal** what `ATIRageProGA.plugin` and GLEngine look up. Open item O1 below.

### 3.2 Build-time features

One header `config/features.h` and Makefile switches, all default ON except where noted. Each flag removes its class, selectors and Info.plist personality properties.

| Flag | Default | Notes |
|---|---|---|
| `RAGE_FEAT_PANIC_LOG` | on | First feature built (Phase 2). |
| `RAGE_FEAT_2D_GA` | on | Needed for "no loss vs stock". |
| `RAGE_FEAT_SLEEPWAKE` | on | Lombard only; unverifiable on the G4 rig. |
| `RAGE_FEAT_DMA` | on | Bus-master ring for command and texture upload; off = PIO only (smaller, simpler, slower). |
| `RAGE_FEAT_OVERLAY`, `RAGE_FEAT_SCALER` | on if stock lacks them | Decided in Phase 0 from the stock binary. |
| `RAGE_FEAT_DVD` | on if stock lacks it | Same. |
| `RAGE_FEAT_FS_SCALE` | on | Fullscreen at 32-bit within 8 MB VRAM (see bug L4). |
| `RAGE_DEBUG_TRACE` | off | Verbose, fsync'd trace. |

### 3.3 Interrupts and threads (single CPU)

- Engine-idle, DMA-complete and vblank are **interrupt events** (`IOInterruptEventSource`/`IOFilterInterruptEventSource` on the device's interrupt), not poll loops. If the chip's interrupt line is not usable on a given board, fall back to a bounded timed wait, never an unbounded spin (the Lombard "~50% CPU in R state then total lockup" in README-SONICR §2.6 looks like an unbounded spin).
- No render worker thread (`gld_engine.c` already abandoned it; `vdma_*` in `gld_render.c` goes). The plugin builds a batch, submits, and continues; completion is signalled through the stamp/fence selector.
- The only extra thread allowed is one kernel work loop for interrupt events and mode-change notifications.

### 3.4 Display coexistence with `ATY_*`

- The `ATY_*` framebuffer keeps scanout, CRTC and modesets. We register for `IOFramebuffer` notifications (the technique already proven in `ca5d788`) and treat *will-change/did-change* as an atomic quiesce: take the engine lock, fence, free VRAM-heap objects the way stock does, redirect user mappings of the aperture, re-init the engine on did-change including **`BUS_CNTL` bit 27 `BUS_EXT_REG_EN`** and bus-mastering state, bump a mode generation.
- DMA/bus-master state is **kernel state, re-established on every did-change**, never "disabled for the life of the process" (README-SONICR §2.6 hypothesis for the Snowball Run lockup).

### 3.5 Memory safety rules (from `docs/memory-map.md`, carried forward)

1. Usable VRAM is `[0, 0x7FF800)`; the register window is the top 2 KB. One checker, in the kernel, on every offset a client can supply.
2. User-supplied register numbers pass the allow-list. No arbitrary kernel read selector exists.
3. All user memory goes through `IOMemoryDescriptor` prepare/complete or `copyin`; texture data is never dereferenced by the kernel through a raw user pointer.
4. Nothing is ever mapped before the context is fully set up (the stock NULL-deref panic of mapping an un-bound context).
5. Each new guard gets a refusal case in the suite's `guards` test.

### 3.6 VRAM byte order (open item O4)

The existing notes found texels arrive byte-reversed through the plain LE aperture on the G4 stub rig, and could not establish whether Apple's framebuffer supplies a swapped view on the Lombard. The mach64 BAR0 is 16 MB; the second 8 MB is documented as a big-endian alias of the first (not probed so far). Plan: have the **kernel** own the question. It maps the aperture view that is correct for a PPC host and exposes a single "write texels in surface format" path, so the user plugin never swaps bytes. The G4 can test the BE alias with a single, bounded, read/write at a known-safe VRAM address after the existing safe-bring-up (no scan). That is a live experiment and will be proposed separately before it is run.

## 4. GL bundle

Rewrite `gldriver/` against the new selector set. Keeps GLEngine integration (`gld*` entry points, dispatch table) and the feature logic documented in `re-notes/`. Changes:

- Delete `m64_peek`, gli/kernel pointer walking, `ctx->regs` as a VRAM view, the DMA worker, `getenv`-based debug switches (replaced by a flags file read once).
- Pixel ops (`glReadPixels`, `glDrawPixels`, `glBitmap`, `glCopyPixels`) go through kernel selectors that validate spans; the plugin never holds a CPU view of VRAM.
- Fixed-point/LUT geometry setup (§2.1).
- Multiple GL windows: the surface resolver is keyed by the CGS surface id from the context, not "first window with a surface" (bug L8).
- Fullscreen at 32 bpp: `RAGE_FEAT_FS_SCALE` renders at a size that fits VRAM and upscales with the hardware scaler during present. Whether the Rage LT Pro scaler can do the upscale at the needed quality is **unverified**; fallback is the old `MACH64_FS_SHRINK` smaller-surface path, which is documented as the likely fix.

## 5. Bug ledger (every defect documented in the repo)

Source: `README-SONICR.md` (Lombard session, 2026-11-22 on), `README.md` known gaps, `docs/memory-map.md`, `docs/re-notes/`. "Fixed" means fixed in the imported code; every fixed item still gets a regression test because the new code is a rewrite.

| ID | Symptom | Cause (as documented) | State in repo | Plan in new driver |
|---|---|---|---|---|
| L1 | Panics leave no `panic.log`, no NVRAM record | Not understood; Lombard clock was 2006 | open | Phase 2: own persistent record plus UDP mirror; kernel never relies on Apple's panic log. |
| L2 | Chess `glReadPixels` crash, `KERN_INVALID_ADDRESS` reading Z at `regs+0x301000` | Pixel ops treated the 768 KB command buffer map as a VRAM view | fixed 3dad95c | Removed by design (no CPU view in plugin); regression test with Chess and a span-hostile test. |
| L3 | Windowed Chess board turns white | Present blits direct to scanout, WindowServer repaints from its white backing store | fixed 47f0ae2 (idle-repaint timer on the app's run loop) | Re-examine in Phase 3: present through the surface protocol so the backing store and scanout agree; keep the timer only if it is still required. Needs Lombard-side confirmation of any different approach. |
| L4 | 32-bit fullscreen 1024x768 white (no buffer fits 8 MB) | `target_off=0` so `render_skip`; `MACH64_FS_SHRINK` dropped in import | open | `RAGE_FEAT_FS_SCALE` / port `FS_SHRINK` (§4). Flurry renders at 16-bit already. |
| L5a | Mode change while GL open wedges the machine | Driver kept stale offsets and a live BAR0 map across Apple's heap reset | fixed ca5d788 | Designed in (§3.4); regression: mode-switch tool during Chess. |
| L5b | Screen-saver preview wedges after a mode change (once, after `sample`) | **Unknown** | open | Cannot reproduce on the G4 (no display on the Rage). Kept as an open issue; ship panic capture (L1) and the traced logging so the contributor's report is useful. |
| L6 | Snowball Run: no textures, no background, then a full lockup, no panic | Hypothesis (unconfirmed): game's own mode switch reinits `BUS_CNTL`, DMA path calls `dma_disable()` for the process lifetime; mode-generation query shared the DMA connection | open | §3.3/3.4: DMA state is kernel-owned and re-established per did-change; mode generation is separate state; no unbounded spin. Game is not on the G4 test list, so this stays open until a Lombard report. |
| L7 | Intermittent kernel panic ~10% of processes (uninitialised stack descriptor in Apple's `clientMemoryForType`) | Stock Apple bug | worked around by 1-instruction patch | Gone by replacement. |
| L8 | Several simultaneous GL windows: resolver picks the first window with a surface | Design gap | open (README) | §4. Regression with two Atlantis instances. |
| L9 | Texels byte-reversed on the G4 stub rig; Lombard behaviour unknown | Unknown | open | §3.6, open item O4. |
| L10 | Chip registers latch but setup engine never fires | `BUS_CNTL.BUS_EXT_REG_EN` (bit 27) clear after PC BIOS | fixed in `biosrun` | Kernel init sets it on attach and on every did-change. |
| L11 | Never-posted card hangs on any read | BIOS POST not run | worked around on G4 (`biosrun`) | G4 only; the Lombard POSTs from its own firmware. Not a driver concern, but `bringup` tooling for the G4 rig stays (tools/). |
| L12 | Kernel peeks of unchecked addresses (panic risk) | Patch design | mitigated by `m64_peek` range check | Eliminated: no peek exists. |
| L13 | Windowed/Atlantis/laser-arena "worked on the Lombard earlier, not re-run in the current rig" | Not re-verified | gap | Atlantis becomes a standing test on the G4 (headless via readback). Present is unverifiable there. |
| L14 | Scaler/overlay/capture/MPP/TV-out register fields undocumented ("omitted on purpose" by ATI) | Docs gap | n/a | Overlay and scaler recipes must come from tracing stock behaviour or the Win98 driver notes; flagged as risk R2. |

Not documented anywhere, therefore not in the ledger: whatever the Lombard tester has seen beyond README-SONICR. The Lombard contributor's issues will extend this ledger.

## 6. Panic and diagnostic capture (Phase 2, `RAGE_FEAT_PANIC_LOG`)

Goal: after any hang or panic, the contributor can attach something useful to an issue.

1. **In-RAM ring** inside the kext (fixed size, e.g. 64 KB) holding timestamped driver events: user-client opens, selector calls with args, will/did-change, fences, errors, last N register batches.
2. **Persistent copy:** on a normal path the ring is flushed to `/var/log/ratelt-driver.log` by a small user-space helper over a dedicated selector; on panic (`PE_register_panic_hook` / panic callback if Panther xnu-517 provides one; verify in Phase 0) the ring is written to a reserved, driver-owned region (reserved system RAM that survives warm reboot is not guaranteed on this hardware, so this is explicitly **best effort**; the NVRAM route was tried (Lombard §2.1) and left nothing, so it is not relied upon).
3. **UDP mirror** to a configurable host (works for the G4 rig, and for the Lombard if the tester has a second machine).
4. A one-shot `ragelt-report` script bundles ring dump, `ioreg` output, kext versions, and the kext's feature flags for pasting into an issue.

Whether any of the persistent paths survives the Lombard's "Please restart" screen is **not known**; the design records why each path was chosen so the next report tells us which one worked.

## 7. Test strategy

Layers, cheapest first:

1. **Host tests** (Linux): `common/` emission, span/allow-list checkers, fixed-point/LUT routines compared against a float reference over exhaustive or dense random inputs, with error bounds written down per function.
2. **Offline PPC checks:** build with the PPC toolchain, run host tests under `qemu-ppc`, run `make checkfp`.
3. **G4 headless:** the existing 178-check `mach64_pathb_suite` ported to the new selectors, plus new `guards` for each new guard; soak (100k frames), process-cycle soak, context churn, mode-switch tests, hostile-argument selector tests run against the new kext only in a mode that cannot reach a never-POSTed card.
4. **G4 apps:** Sonic R, Chess, Flurry, Atlantis, headless via framebuffer readback; per-app acceptance = starts, runs N minutes, no panic, frames read back correctly, no leak in `IOKitDiagnostics`.
5. **Lombard (contributor):** installer, revert script, a one-page checklist, and issue template asking for the `ragelt-report` bundle. Everything unverifiable on the G4 is listed in the release notes as "needs Lombard confirmation": present to a real display, sleep/wake, overlay, scaler, DVD, fullscreen at 32 bpp, 8 MB VRAM pressure, byte order.

Any live G4 run follows the standing rules: explicit safe arguments, never `mach64_post` bare, no broad kernel scans, `kextload` only through the bringup sequence, G4 not rebooted.

## 8. Open items for Phase 0 (need the stock binary or a G4 read)

| # | Question | How I'll answer it |
|---|---|---|
| O1 | Exactly which classes/IOKit properties/selector numbers does `ATIRageProGA.plugin` and GLEngine look up on the accelerator (names, `IOGLBundleName`, `IOAccelIndex`, type numbers)? Can our kext simply provide these under our own bundle id, with Apple's kext moved aside? | Static analysis of the stock kext's `Info.plist` and the GA plugin on the G4's Panther HD (read-only). |
| O2 | Does Panther's `IOAccelerator` base come from `IOGraphicsFamily` (usable by a third-party kext) or only inside Apple's kext? | Symbols and `kextstat` dependency lists. |
| O3 | What does stock already provide for sleep/wake, overlay, scaler, DVD (affects the flags' defaults)? | Symbol and selector inventory of stock `ATIRagePro` and `ATIRageProGA`. |
| O4 | Byte-order/aperture question (§3.6). | Offline first (what Apple's framebuffer does on the Lombard may be readable from its binary), then one proposed bounded G4 experiment. |
| O5 | Does Panther's xnu-517 offer a panic callback usable from a kext? | Kernel headers for 10.3.9 and symbol check on the G4. |
| O6 | Interrupt line wiring for the chip and whether stock uses it. | Stock kext symbols; OF device tree on the G4. |
| O7 | Installation: how stock `ATIRagePro.kext` is set aside (moved to a backup dir, kext caches rebuilt, revertable) so there is never a window with two accelerators matching. | Existing `install-*.sh` plus `swap.sh` patterns on the G4. |

## 9. Risks

- **R1: Lombard cannot be tested by me.** Mitigation: panic/trace capture first, compile-out flags so a contributor can bisect by rebuilding with a feature off, strict "needs Lombard confirmation" labelling.
- **R2: Overlay, scaler and DVD register semantics are undocumented** (ATI withheld them). May become "best-effort reproduction of stock behaviour" or be dropped if stock already provides the function and we cannot improve it.
- **R3: Replacing the 2D accelerator can regress the Lombard desktop** if the GA plugin ABI is not matched exactly. Mitigation: O1 first, 2D phase after GL, easy revert script, `RAGE_FEAT_2D_GA` off keeps stock 2D... but stock 2D needs stock kext, which we replace, so off means software 2D. This tension is called out for review.
- **R4: A fixed-point geometry pipeline may show visible precision artefacts.** Mitigation: per-function error bounds in host tests, then image comparison on Atlantis and Sonic R against the float version; single float stays available for any stage that fails the bound.
- **R5: VRAM is 8 MB.** The usable heap, Z and texture budgets are tight; budget table is part of the Phase 1 design.

## 10. Phases and exit criteria

| Phase | Deliverable | Exit |
|---|---|---|
| 0 | This doc, ledger, answers to O1-O7 | You approve the doc. |
| 1 | Delete Path A/B; kext skeleton: attach, map, VRAM allocator, allow-listed engine, features.h, host tests | Builds for PPC, `checkfp` clean, host tests pass; first G4 load needs your OK. |
| 2 | `RAGE_FEAT_PANIC_LOG` | Ring + mirror + report script; forced-panic test on the G4. |
| 3 | New GL bundle | Suite ports; fixed-point routines have error bounds. |
| 4 | G4 acceptance | Four apps pass; soak and process-cycle soak pass. |
| 5 | 2D GA, sleep/wake, overlay, scaler, DVD | Each behind its flag; G4-testable parts tested, the rest listed for the contributor. |
| 6 | Installer/revert/checklist/issue template | Pushed to `standalone-driver`. |

## 11. Phase 0 addendum (2026-11-24, after user review)

### 11.1 Decisions from review
- **2D acceleration is always on** (`RAGE_FEAT_2D_GA` is not a user-facing off switch; software 2D is not acceptable). Resolves R3.
- Overlay, scaler and DVD scope is decided from ATI's Win98 driver (4.13.1.2655), see `win98-*.md` in this directory.
- Path A / Path B code is deleted at the start of Phase 1; only the new driver remains.

### 11.2 Stock kext inventory (static, from the stock `ATIRagePro.kext` md5 8fc97891 and `ATIRageProGA.plugin`, pulled read-only from the G4)
- Personality: `IOClass ATIRagePro`, `IOProviderClass IOPCIDevice`, `IOMatchCategory IOAccelerator`, `IOPCIMatch` = 0x4c491002 0x4c471002 0x47491002 0x47501002 0x4c4e1002 0x47561002, `IOGLBundleName ATIRageProGLDriver`, `IOCFPlugInTypes ACCF0000-0000-0000-0000-000a2789904e -> ATIRageProGA.plugin`. Libraries: IOGraphicsFamily, IONDRVSupport, IOPCIFamily. **O1 answered in outline; O2 answered:** `IOAccelerator` is imported from IOGraphicsFamily (`__ZN13IOAccelerator10gMetaClassE`, `__ZTV13IOAccelerator`), so a third-party kext can subclass it. Our kext can reuse this personality under its own bundle id with the stock kext set aside.
- Stock user clients: surface (type 0, 16 selectors), GL context (type 1, 15), 2D context (type 2, **16 selectors**): 0 set_surface, 1 get_config, 2 get_surface_info, 3 swap_surface, 4 scale_surface, 5 lock_memory, 6 unlock_memory, 7 finish, 8 declare_image, 9 create_image, 10 create_transfer, 11 delete_image, 12 wait_image, 13 set_surface_paging_options, 14 set_surface_vsync_options, 15 set_macrovision (scalar/struct counts in the kext's `methodDescs` at 0x17c68). This is the ABI `ATIRageProGA` expects; `RAGE_FEAT_2D_GA` must reproduce it.
- **O3 answered:** stock has sleep/wake hooks (`IOATIRProAccelerator::system_will_sleep` / `system_did_wake`), AGP code (unused on this PCI chip), a polled timestamp/fence (`waitForTimeStamp`, `sleepForTimeStamp`; no hardware interrupt handler), `set_scaling` (a size record only) and `set_macrovision`. **Stock has no video overlay, no front-end scaler use, no DVD assist.** Those three are new ("stock+") features.
- Display (hardware cursor, gamma, DDC, DPMS, mode set) lives in the NDRV framebuffer (`ATY,264LTPro`), not in the accelerator, so we do not have to reproduce it.

### 11.3 Byte order (O4) resolved from documents; no live test needed
ATI's register reference (RRG-G03300, MEM_CNTL) documents `UPPER_APER_ENDIAN` (bits 27:26): the 8-16 MB half of BAR0 is a byte-swapping alias of the first 8 MB (00 none, 01 big-endian 16 bpp swapping, 10 big-endian 32 bpp swapping). The existing `RageStubFB` already sets it to 01 and WindowServer draws through it. Design: the framebuffer driver (NDRV on the Lombard) owns that setting and changes it with colour depth, so **we never touch it**. The kernel accesses VRAM through the lower (unswapped) view and uses byte-reversed loads and stores (`lwbrx`/`stwbrx`/`lhbrx`/`sthbrx`) for 32- and 16-bit data. The G4 shows the BAR0 mapping is 0x1000000 bytes, so the alias is reachable if ever needed. This removes the texel byte-order dependence on the Lombard's framebuffer.

### 11.4 Full-screen at 32 bpp (bug L4): options after the Win98 review
| Option | Basis | G4 can verify? |
|---|---|---|
| A. Textured-quad upscale at present (render small, stretch to scanout with the 3D engine, bilinear) | Existing, hardware-verified textured path; measured 35-38 Mpix/s (README audit), so a 1024x768 stretch costs about 21 ms | Yes (VRAM readback), but see 11.5 |
| B. Overlay scaler (display-side upscale, no engine time) | Win98 HAL: RGB565/xRGB8888 sources accepted; recipe in `win98-overlay-scaler.md`; needs a display to see | **No** (no display on the G4 Rage); register read-back only |
| C. `FS_SHRINK` (smaller surface, no scaling) | Pre-import code | Partly |

Overlay limits to remember: one overlay, source width max 768 (720 on later revisions) which fits 640x480 but not 1024-wide sources; key colour/blending semantics partly NOT ESTABLISHED. The overlay is also what a video/DVD path would use, so it is worth building behind `RAGE_FEAT_OVERLAY` either way.
Plan: build A first (testable here), B as the preferred path for the Lombard once confirmed by a contributor, C as the fallback.

### 11.5 G4 experiment: full-screen upscale feasibility (measured 2026-11-24)
Rig: MDD G4 (`hw.ncpu` 2), Rage LT Pro initialised at boot by `RageStubFB` (640x480 15 bpp, no monitor), `ATIRagePro` = SonicR's `e64ddc65` (rebuilt here from the stock kext with `kext-patch/build-chain.sh`; md5 identical, and already what was installed), `Mach64Accelerator` loaded. Suite built from the repo's **older** plugin (`origin/pathb-hw-render`) because the SonicR plugin draws nothing on this rig (its DMA engine path and texture heap fail: trace shows `tex: heap alloc FAILED`, heap `0x0-0x0`, even with `MACH64_SINGLE=1`; cause not found). Log: `~/g5logs/rage-standalone/upscale-oldplugin-nov24.log`.

| Measurement | Result |
|---|---|
| 160x120 source padded into a 256x128 power-of-two texture, sub-rectangle texcoords, bilinear, stretched to 256x192 (1.6x) | **Works.** Mean error 3.81/255 against a software bilinear reference; 5.4% of pixels off by >24, all at the hard 20-px checker edges (the chip blends with coarse weights). |
| Same source as a 160x120 non-power-of-two texture | **Fails** (garbage). Sources must be padded to power-of-two. |
| Throughput (six full-size quads per frame, destination pixels) | **32 Mpix/s**, i.e. **about 24.5 ms for a 1024x768 stretch**. |
| Texel byte layout | Pixels come back with their 16-bit halves swapped. This is the plugin/kext upload-path mismatch (older plugin vs the hardened upload selector), not a chip property; the kernel driver will own texel byte order (see 11.3). |

Conclusions for L4 (32-bit full screen): option A (textured-quad stretch) is **functionally viable** but costs about 24 ms per frame on top of the game's own rendering, so it only fits ~30 fps titles and leaves about 9 ms. It is a fallback. The display-side **overlay scaler (option B) costs no engine time** and is the preferred path; it still needs confirmation on a Lombard (no display on the G4 Rage), so the design keeps both, with the choice made at run time (`RAGE_FEAT_OVERLAY` first, textured stretch if the overlay is refused).
Open: the SonicR plugin failure on this rig is not understood; the new driver will not inherit it, but the Lombard testers use that plugin today.

### 11.6 Corrections (2026-11-24, after the user's review)
- **The G4's Rage has a CRT attached** (user saw shapes and a white square on it). Earlier statements here and in `README.md` that the rig has no display are wrong. Presenting to scanout, the overlay scaler and full-screen paths can be checked by eye and by dumping the scan-out buffer (`tools/mach64_scanshot`, VRAM offset 0, 640x480, 15 bpp 1:5:5:5 LE).
- **The SonicR plugin does draw on this rig.** After running the suite's `gouraud` check against it, the scan-out buffer shows the red/green/blue triangle at the expected place (bottom left of the 640x480 frame). The suite's FAILs came from reading the render target at `0x97000` in 32-bit layout; the plugin renders into the 15 bpp scan-out. The earlier conclusion in 11.5 that the plugin "draws nothing" is withdrawn; what was true is that nothing appeared at the address the suite reads. The texture-heap trace (`heap 0x0-0x0`, alloc FAILED) is still unexplained and may be why textured draws are missing from the screen; that needs a scan-out check of a textured draw.
- The throughput and error figures in 11.5 (24.5 ms per 1024x768 stretch, mean error 3.8) were taken with the older plugin where the readback matched; they stand.
- `mach64_arrange rage` (Rage as primary display) made no difference to the suite results and was restored afterwards. `mach64_repaint` restores the desktop after a test writes the scan-out (verified byte-identical).

### 11.7 Suite repaired against the SonicR plugin (2026-11-24)
Base: G4 up >1 h, no stray processes, kexts as expected (`RageStubFB`, `ATIRagePro` = e64ddc65, `Mach64Accelerator`), CRTC/BUS_CNTL/GUI_STAT sane, desktop byte-identical to the original. No reboot was needed.
Run: `suite2` built from the SonicR plugin (`-DSUITE_SHIMS -DSUITE_FENCE -DSUITE_HAVE_CBPP`), `MACH64_SINGLE=1`. Log: `~/g5logs/rage-standalone/suite-sonicr-plugin-nov24.txt`.

**Result: 180 passed, 10 failed.**
- Root cause of the earlier "draws nothing": a **suite bug**. The suite flipped y with a hard-coded 256 while the plugin flips with the real surface height (`ctx+0x1f4`, 480 here), so everything landed 224 rows below where it was read. Fixed in `tools/mach64_pathb_suite.c` (`V()`); found with a VRAM before/after page diff (`SUITE_VRAMDIFF=1`).
- 7 failures (5 DMA uploads, per-context texture binding, both upscale cases) are textured draws. In fullscreen (`win_mode=0`) the plugin **marks the texture heap invalid on purpose** (`gld_render.c` ~l.704: its allocator could return a heap overlapping the live scanout, which panicked the machine) so a fullscreen app renders **untextured**. A headless suite is always fullscreen. A windowed attach needs a real CGS window, which the suite cannot create.
- 1 failure (`SCALE_3D_CNTL bit 8 on`): the check needs the `MACH64_SC_BIT8` env knob, which only the older plugin reads. Not a hardware or driver fault.
- 1 failure (`register audit`): the stand-in `m64_reg_seen` shim records nothing. A harness artifact.

**Design consequence (new driver).** The kernel driver owns VRAM, so it can hand the user-space bundle a texture heap that is provably clear of the scanout and the cursor block, in fullscreen as well as windowed. Fullscreen texturing is therefore a requirement for us, not an optional extra, and the textured upscale (L4 option A) cannot be validated headless on this plugin. The numbers in 11.5 (older plugin) remain the only measurement of it.

### 11.8 Phase 1 status (2026-11-24)
**Done**
- Path A and Path B deleted (commit 55677a5). Kept: `rig/stubfb` (the G4's boot-time chip init, not part of the product), driver-agnostic GL test apps in `tools/`, `common/`, the old suite as `tests/suite-reference/` (not built), bug history in `docs/history/`.
- `config/features.h` (all flags of 3.2).
- `common/rage_guard.h`: usable-VRAM span check, register allow-list with per-register forbidden bits (bus-master bits of `SRC_CNTL`), base-address checks on every `*_OFF_PITCH` / `TEX_*_OFF` register, all-or-nothing batch validation. `common/rage_vram.h`: the single VRAM allocator (sorted extent table, owner-tagged, best-fit, reservations that clients cannot free, release-by-owner for closed or crashed clients). No libc, no heap, no floating point.
- `tests/guard_test.c`: 61 checks; mutation-tested (removing the SRC_CNTL mask, the span start check, the free-owner check or the alignment check each makes tests fail). Passes on Linux x86_64 and on the G4 (PPC, gcc 3.3).
- `kext/`: `RageLTDevice` skeleton (attach, BAR0 uncached map, allocator, owner bookkeeping, validated `submitBatch`), built on the G4 with `-mcpu=750 -msoft-float`. **Makes no register read or write**; the write path exists but `fEngineLive` is false, so `submitBatch` returns "not ready" after validating. `make checkfp` passes and was shown to fail on a hardware-float object (`lfd`, `fmadd`, `fdivs`). `kextload -n` reports the bundle valid; it has not been loaded.

**Provenance of the allow-list.** The measured 45-register audit of Nov 20 was not saved. The Phase 1 list is rebuilt from the registers `common/` defines for drawing (vertex block, setup, destination/source/Z/texture, scissor, DP_*, host data, `TEX_*`). It is re-audited in Phase 3 with the kext's own counters on a real run; anything the plugin legitimately writes that is missing will be refused loudly (refusal counter), not silently.

**Open item E1 (extent tracking).** Address registers are checked for their base only. Before a trigger register is written the engine must also check the real extent (pitch x height, clamped by the scissor). This is required before `fEngineLive` can be turned on; it is the first task of the engine work.

**Not started:** VRAM reservations for the scanout/cursor block (needs the chip initialised to read `CRTC_OFF_PITCH`), user clients, DMA ring, interrupts, panic log. **Exit of Phase 1 met:** builds for PPC, `checkfp` clean, host tests pass. The first G4 load is waiting for your OK.

### 11.9 Phase 2 part 1: diagnostics and the first G4 load (2026-11-24)
**O5 answered: xnu-517 offers no panic callback to a kext.** The 10.3 Kernel framework headers and `/mach_kernel` symbols contain `Debugger`, `PE_enter_debugger` and `PESavePanicInfo` -> `IODTNVRAM::savePanicInfo` (Apple's own NVRAM path, the one that left nothing on the Lombard), and nothing like `PE_register_panic_hook`. Our code cannot run at panic time. Consequence for section 6: item 2's "if available" branch does not exist; capture is **continuous**, not last-gasp. The ring is drained by `tools/ragelt daemon` every 20 ms (immediately again if a read was full), each batch is appended to a file and `fcntl(F_FULLFSYNC)`'d, and mirrored over UDP. The events before a crash are therefore outside the machine unless the crash happens inside one 20 ms window. Apple's own `panic.log` is bundled by `ragelt-report` alongside.
Kernel-originated UDP is not used: the 10.3 kernel has no socket KPI for kexts, so the mirror runs from the helper.

**Built:** `common/rage_log.h` (2048 x 32-byte events, sequence-numbered, torn/overwritten events counted not returned, libc-free text formatter), `common/rage_abi.h` (user-client contract), `kext/RageLTLogClient` (info / read 31 events / mark; no holds or timers, trivial `clientClose`), `RageLTDevice::logEvent` (short spin lock, safe from any context), `tools/ragelt/ragelt` and `ragelt-report`. `tests/log_test.c`: 16 checks, 2 of 3 mutations caught; the third (invalidate-before-write) only matters under a concurrent reader and a single-threaded host test cannot show it.

**First G4 load (MDD, next to RageStubFB + patched ATIRagePro + Mach64Accelerator):** loaded, no panic; `RageLTDevice` registered/matched/active; property `RageLTFeatures` = 255; log shows start, owner and client lifecycle, marks; daemon followed the ring live and wrote the file; BAR0 maps 16 MB; no register access was made. `kextunload -b com.local.ATIRageProLT` succeeded; `IOMalloc` allocation returned **exactly** to its pre-load value (4268955), `RageLTLogClient` count 0, both displays intact, Apple's kexts untouched.

**Not yet done in Phase 2:** the forced-panic test (what actually survives: file, UDP, `panic.log`), which needs a debug-only selector that panics on purpose; the install location and boot-time start of `ragelt daemon`.

### 11.10 Phase 2: forced-panic test (G4 MDD, 2026-11-24 15:13)
Debug build only (`make -C kext kext EXTRA=-DRAGE_DEBUG_PANIC=1`; a normal build contains no panic selector, verified with `strings`). Setup: kext loaded, `ragelt daemon FILE 192.168.5.213:9998` running, `ragelt panic 0xc0de0001 200` = MARK, wait 200 ms, then the selector logs `ERROR 0xdead0001` and calls `panic()`. Evidence (not in the repo): `~/g5logs/rage-standalone/panic-test/`.

| Capture path | Survived the panic? | Content |
|---|---|---|
| **Daemon file** (`F_FULLFSYNC` per batch) | yes | every event up to and including the MARK (seq 0x0a), 200 ms before the panic |
| **UDP mirror** | yes | the same events, received on the dev host before the machine died |
| **Last event** (`ERROR`, seq 0x0b, logged microseconds before `panic()`) | **no** | lost on both paths: it falls inside the 20 ms poll window. Inherent: nothing can drain the ring after the panic starts |
| **Apple `/Library/Logs/panic.log`** | yes, written at next boot | `panic(cpu 1): RageLT: forced panic test (token 0xc0de0001)`, a backtrace, and `com.local.ATIRageProLT(0.1)@0x320da000` named as the module in the backtrace |
| **Symbolication** | works | backtrace frame `0x320DD000` - load `0x320da000` - pad `0x1000` = `0x2000`, inside `selPanic` (`0x1f8c..0x2048`) |

Conclusions
1. The two sources complement each other: the daemon says what led up to a crash, `panic.log` says what and where (string, function via symbolication). Both go into `ragelt-report`.
2. **Expect to lose the last ~20 ms of events.** Mitigation for the engine code: log *intent* events (e.g. "about to submit batch N") before risky operations, so a crash is bracketed by events that already reached the file; never rely on the final event.
3. The machine does **not** restart after a kernel panic; the tester must power-cycle it. The issue template (Phase 6) says so.
4. The unrelated older panics visible earlier in `panic.log` (`0x300 - Data access`, `IOMemoryDescriptor::_RESERVEDIOMemoryDescriptor3 called`) predate this driver and are from earlier sessions.
5. `ragelt-report` verified on Panther's shell (15 KB with the driver absent; trims `ioreg` lines to 240 columns).

**Phase 2 exit met** (ring + file + UDP mirror + report script + forced-panic test on the G4). Still to do for the installed product (Phase 6): where `ragelt daemon` lives and how it starts at boot.


### 11.11 E1 step A: 2D scissor containment proven on hardware (G4 MDD, Nov 24)

Debug selector ENGTEST (RAGE_DEBUG_ENGTEST) drives the engine from the kernel against a 512 KB allocator block and scans every pixel afterwards.
All 17 cases pass (outside_changed = 0, every in-scissor pixel correct): fill and blit larger than the scissor, with and without fast-fill/block-write,
inverted scissor (right < left), rect wholly outside the scissor, whole-surface scissor with the rect overshooting, 0x1fff "no clip", and offset probes.

Root causes found on the way (each cost a run):
* **SETUP_CNTL (0x304) left by the boot replay made DST_X, DST_Y and the height behave as S.12.2 sub-pixel values** (a quarter of the requested
  position/size; width unaffected).  Writing SETUP_CNTL = 0 before a 2D op fixed it.  DST_CNTL writes did not (all 16 upper bits probed).
* DST_X_Y is (Y<<16)|X, DST_WIDTH_HEIGHT is (H<<16)|W (low half is the first-named field; RRG 5-13, matches Linux DRM).
* Draw-engine registers (DST_CNTL, GUI_TRAJ_CNTL, SC_*) do not read back what was written; do not use readback to verify.
* The CPU must not touch the VRAM aperture until the engine has been idle for several consecutive reads; engine reset (GEN_GUI_RESETB pulse) recovers a wedged engine.
* Table 5-2 standard context (Programmer's Guide) is now applied once by eng_init_context().

### 11.12 E1 step B: triangle scissor containment proven on hardware (G4 MDD, Nov 24)

ENGTEST with mask bit 31 runs 6 flat triangles through the 3D setup engine (SETUP_CNTL 0, SCALE_3D_CNTL 0x100c1, vertices x*4, trigger =
ONE_OVER_AREA_UC with host-computed 1/area bits) into the same 256x256 RGB565 surface and scans every pixel.  All 6 pass with 0 pixels changed outside the
scissor and no clearly-inside pixel missing (edge band of ~2 px tolerated, counted as ambiguous): big triangle with the scissor box wholly inside it
(4096/4096), edge crossing the box, triangle wholly outside the scissor (nothing drawn), whole-surface scissor (shape), inverted scissor (nothing drawn),
partly overlapping triangle.  Together with 11.11 the hardware clip is proven for fills, blits and triangles, so the kernel may own the scissor and
clamp user 3D streams to it (E1 design).

Allow-list tightened accordingly (common/rage_guard.h, tests/guard_test.c 62 checks): removed DST/Z/SRC_OFF_PITCH, TEX_n_OFF, SECONDARY_TEX_OFF, SC_*,
DP_PIX_WIDTH, DST_Y_X..DST_HEIGHT_WIDTH, DST_X_Y/WIDTH_HEIGHT, SRC_Y_X/SRC_WIDTH1, SRC_CNTL, HOST_DATA, GUI_TRAJ_CNTL.  Open item E1 is closed.

### 11.13 Phase 3 step 1: engine class (kext/RageLTEngine.cpp), G4 Nov 24

RageLTDevice now has per-owner contexts (16) holding a kernel-owned render target, Z buffer and scissor; `setRenderTarget` / `setZBuffer` accept only extents the
owner allocated (rage_vram_owns), `setScissor` clamps to the target, `submitBatch(owner, pairs, n)` validates the whole batch (guard), prepends the kernel
prologue (DST_CNTL, DP_PIX_WIDTH, DST_OFF_PITCH, scissor, Z_OFF_PITCH), neutralises Z_CNTL when the owner has no Z buffer, drains through the FIFO with a
free-slot wait per write, waits for idle (bounded; a stuck engine is reset and reported) and advances the fence counters.  `fillRect` clips to scissor/target in
the kernel as well.  `engineEnable()` is the only place that reads the chip to start the engine (needs BUS_EXT_REG_EN and CRTC on).  PIO is synchronous: the
fence is complete when submit returns; the DMA phase keeps the counters and returns earlier.
Hardware result (debug ENGTEST): API suite 7/7 (unowned/overrunning/foreign/misaligned targets refused; full fill clipped; oversized scissor clamped; client
triangle batch clipped by the kernel scissor; kernel-owned register batch refused whole; context-less submit refused; release frees the slot), plus the 17 2D and
6 triangle cases re-run unchanged.
Known gaps recorded for the next steps: per-client 3D state (SCALE_3D_CNTL, SETUP_CNTL, TEX_*, DP_*) is not saved per context, so each client batch must carry its
full state; TEX_n_OFF are not yet settable (texture binding selector comes with the GL bundle; stale texture bases can only point at memory the engine could already read);
32 bpp fills and kernel blit/present are not done yet.

### 11.14 Phase 3 step 2: GL user client, present, mode changes, VRAM transfers (G4, Nov 24)

* **GL client** (`RAGE_CLIENT_GL`, kext/RageLTGLClient.cpp, contract in common/rage_abi.h): info, alloc/free, set target/scissor/Z, fill, validated submit, fence, present, VRAM
  read/write, display info; debug-only scan-out read.  User memory is copied with an IOMemoryDescriptor of the calling task (never raw).  First client open is where the chip is first read.
* **Present** (RageLTMode.cpp): kernel blit target -> scanout, clipped on both sides, raw copy (16 bpp now; 32 bpp / scaled = FS_SCALE work).
* **Mode changes**: IOFramebuffer notifications of the Rage's framebuffer; will = odd generation, drain, invalidate every target and release the scanout reservation; did = restore
  BUS_EXT_REG_EN, re-init engine, re-read CRTC, re-reserve; even generation.  Clients get kIOReturnNotReady until they re-read DISPLAY and set the target again.  Simulated events: 9/9.
* **VRAM transfers**: byte image of the buffer == byte image in VRAM, with optional SWAP16/SWAP32 (the kernel owns texel byte order; O4 stays about which swap a given source needs).
* **Hardware results**: `tools/ragegl/ragegl_test` (user space through IOConnect) 27/27 on the G4 incl. refusals (unowned target, overrun, scanout write, unaligned, bad flags, scissor register in a batch),
  exact contained fill, triangle through the batch path (27355 px vs ~27350 expected), present read back from the scan-out (6000/6000), 5 repeated runs and a killed client: IOMalloc unchanged, owner released.
  ENGTEST: mode-change 9/9, engine API 7/7, triangles 6/6, 2D 17/17.

### 11.15 Vertex math: precision bounds and implementation choice (Nov 24)

`common/rage_vmath.h` (integer code on IEEE-single bit patterns), `tests/vmath_test.c` (exhaustive; part of `make`), `tests/vmath_bench.c` (isolated microbenchmark, run on the G4 MDD, 7450 @ 1.25 GHz, compiled
`-mcpu=750`, three runs identical to 0.1 ns; **no G3 is available, so a G3 re-measurement is open**).  Per the agreed method each implementation is measured in isolation and chosen by speed among those whose precision is acceptable; no per-frame cost is estimated.

| Function | Candidates (ns/op, bound) | Choice |
|---|---|---|
| 1/x bits (vertex W, ONE_OVER_AREA) | float `fdivs` 31.1, exact by definition; B 256-seed+2 Newton 40.8 (max 1 ulp, 99.75% exact); C 256-seed interpolated+1 Newton 35.3 (max 1 ulp, 99.74% exact); D 2048x16-bit seed+1 Newton 29.6 (max **4 ulp**, 63.5% exact) | **single float divide.**  D is 5% faster but loses precision and uses a 4 KB table that competes for the 32 KB L1; B and C are slower than the divide on this CPU.  B and C stay in the header as bounded fallbacks. |
| (int)(x*4) vertex X/Y from the float's bits | float multiply+convert 20.9 (input in memory); `rage_f2q` bit manipulation 12.0 | **`rage_f2q` (integer):** 42% faster, bit-exact vs `(int)(x*4.0f)` for all 2^32 patterns (saturating beyond 2^29).  If the value is already in a float register the float path is faster (15.2 vs 19.4), which is not the usual case for vertex arrays. |
| colour float -> byte | float `(int)(c*255+0.5f)` 20.9; `rage_f2byte` 29.4 | **single float** (integer is slower); `rage_f2byte` differs from the float formula by at most 1 in 0.8% of inputs because it evaluates exactly. |

### 11.16 DMA submission (RAGE_FEAT_DMA), G4 results (Nov 24)

`kext/RageLTDma.cpp`.  The kernel owns a physically contiguous 72 KB command buffer and a 32 KB block from which a 16 KB-aligned descriptor table is cut; the stream is built from the *validated* pairs plus the kernel prologue
(one ADRINDEX header + value per register), chained over 4092-byte descriptors (12-bit byte count), terminated by SRC_CNTL=0; kick = BUS_CNTL, BM_GUI_TABLE_CMD, SRC_CNTL, DST_HEIGHT_WIDTH=0 (the proven sequence).
Submit returns without waiting; `idleWait()` (drain DMA, then engine idle) precedes every other engine use, CPU VRAM access, free and owner release; a second submit waits for the first (serial model).
Completion = BM_GUI_TABLE at table end AND idle for 8 consecutive reads; a timeout resets the engine and abandons the work.  Bus-master mode is enabled only for the duration of a stream (BUS_CNTL restored).
`RAGE_GL_CONFIG` key 1 selects PIO (0) or DMA (1) for diagnosis; DMA is the default when the buffers allocate.

Findings that cost time (kept so they are not rediscovered):
* **PCI bus mastering must be enabled** (`setBusMasterEnable(true)`); without it the engine loads BM_GUI_TABLE and never fetches (table stays at base, GUI_STAT 0x00800001).  Our first skeleton only enabled memory decode.
* That failed attempt left the chip in a state in which later DMA attempts (even with the fix) still never fetched; engine reset (GEN_GUI_RESETB pulse) did not recover it, a **reboot did**.  After the reboot DMA worked straight away, also after our engine reset, so a reset is fine; the wedge came from kicking a stream with PCI mastering off.  Not reproduced on purpose (the cure is a reboot).
* Command/ring memory is written through the kernel's cached address and flushed with `dcbf` + `sync` before the kick (uncached second mappings are not needed).
* Debug `ENGTEST 0x1000000x` is the minimal two-register stream (bm_dma_test equivalent); the API suite reads VRAM only after `idleWait()` (CPU access to VRAM while DMA is in flight is exactly what must never happen).

Results (same G4 boot, tools/ragegl): ragegl_test 27/27 with DMA and with PIO, repeated; killed client mid-flood leaves the driver healthy (next run 27/27, no ring errors); ENGTEST 2D 17/17, triangles 6/6, API 7/7, mode change 9/9.
`ragegl_bench` (batch of K triangles, N batches, serial submit, fence at end; 1.25 GHz G4): 

| batches x triangles | PIO us/triangle | DMA us/triangle |
|---|---|---|
| 500 x 20 | 16 | 4 |
| 200 x 100 | 14 | 3 |
| 2000 x 1 | 70 | 45 |
| 20000 x 5 | 24 | 10 |

DMA is 1.5x (single triangle batches) to 4.7x (large batches) faster in submission cost.  Open: texture upload by DMA (VRAM writes still use the CPU path), interrupt completion (O6).

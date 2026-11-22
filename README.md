# RageProLT Panther Driver

A hardware **OpenGL driver for the ATI Rage LT Pro (mach64LB, PCI 1002:4c49)** on **Mac OS X 10.3 Panther / PowerPC**.
Apple shipped 2D acceleration for this chip but never a hardware GL driver; OpenGL falls back to software. This project
supplies the missing `ATIRageProGLDriver.bundle` (a GLEngine renderer plugin) and the kernel pieces it needs.

## How it works

```
OpenGL app -> Apple GLEngine -> ATIRageProGLDriver.bundle (gldriver/, this project)
                                    |  IOConnectMethod* / IOConnectMapMemory
                                    v
                         Apple ATIRagePro.kext (patched, see kext-patch/)   <- "Path B" (current)
                         Mach64Accelerator.kext (kext/)                     <- "Path A" (own DMA/ring kext, legacy)
                                    v
                              Rage LT Pro (mach64)
```

* **Path B (current):** the plugin drives Apple's own `ATIRagePro.kext`. Command batches are written as `{reg,val}` pairs into
  the kext's context buffer and drained by mapping the kext's type-1 memory (spec: `docs/path-b-driver-spec.md`).
* **Path A:** our own `Mach64Accelerator` kext with a bus-master ring. Kept for the non-Apple-kext configuration.
* Portable command emission, register definitions and the ABI live in `common/` (host-tested by `make`).

## Status (what has actually been verified, and where)

| Area | State | Where verified |
|---|---|---|
| 2D fill/clear/blit, 3D flat + Gouraud triangles, Z buffer, blend, alpha test, colour mask, fog, line stipple, logic ops | works | MDD G4, headless, `tools/mach64_pathb_suite` (178 checks pass, Nov 2026) |
| Primitives: points, lines, line strip, triangles, fan, strip, quads, quad strip, polygon | works | same |
| Textures: REPLACE / MODULATE, RGB(A), alpha, wrap, NEAREST filter, create/upload/delete churn, texel data anywhere in user space | works | same |
| Multiple GL contexts in one process (disjoint targets, per-context state and texture binding) | works | same (`multictx`) |
| Long run: 100,000 frames, 12 frame kinds, 0 bad frames, flat frame time, no stray VRAM writes | passes | same (`soak`) |
| 60 process start/stop cycles x 1,500 frames | pass | same (`tools/soak-cycles.sh 3 60 1500`) |
| Windowed drawables (CGS surface -> window bounds), back->front present, Atlantis/laser-arena demos | worked on the Lombard (earlier sessions; see `docs/re-notes/windowed.md`, `double-buffer.md`) | **not re-run in the current headless rig** |
| Mipmapped textures (all four `*_MIPMAP_*` min filters, incl. tri-linear), `GL_LINEAR` min filter | works in **hardware**: recipe reverse-engineered from ATI's Windows 98 driver (`docs/re-notes/win98-mipmap.md`); the key missing piece was `SETUP_CNTL.LOG_MAX_INC_ADJ`. Needs a complete chain (else a software level selector is used; `MACH64_SW_MIP=1` forces it). Mip+fog and mips on fan/strip/quad-strip verified. LOD bias works (negative: hardware TEX_CNTL[3:0]; positive: software levels). Hardware limit (confirmed against ATI's Win98 driver and docs): bilinear filters colour but not texel alpha | MDD G4, suite `tex_mip*`, `tex_minlin` |
| Multitexture (2 units, one pass), separate specular, 16-bit and compact texture formats (565/4444/5551, Y8 luminance, `GL_APPLE_ycbcr_422`), LRU texture eviction, 16-bpp (RGB565) colour targets (offscreen; 16-bpp config nodes only with `MACH64_16BPP`), destination alpha | works | MDD G4, headless, suite (178 checks); **not run against the real GLEngine/Lombard**, see `docs/re-notes/feature-audit.md` section 5 |
| Several simultaneous GL windows | resolver picks the first window that has a surface | known gap (`docs/re-notes/windowed.md`) |
| Presenting to a display | needs the Rage to drive a display; the MDD rig has none (the GeForce is the display), so present is unverified there | - |

Address safety (the historical "memory corruption": kernel panics from touching protected or wrong addresses) is documented in
`docs/memory-map.md`: usable VRAM is `[0, 0x7FF800)` (the register window is the top 2 KB of the aperture), every kernel peek
is range-checked, register numbers sent to Apple's unchecked drain are bounded, and the register set the plugin writes was
measured (45 registers, none of the bus-master/DMA ones).

### The kernel panic that is fixed by a patch you must apply

Apple's `ATIRagePro.kext` has a latent bug: in `clientMemoryForType` (type 1, the call that triggers a drain) a stack
descriptor is never initialised for a 3D context, and its count is read anyway. Stale stack data can make the kext drain garbage,
which panicked about 10% of processes. Fixed by `kext-patch/patch-clientmemory-desc.sh` (one instruction); details in
`docs/memory-map.md` section 9.

**Decision: the driver requires the patched kext; Apple's stock `ATIRagePro.kext` is not supported for GL.** Three patches are needed and
none can be done from user space: (1) a kernel word read (selector 14) to discover the bound surface's render target, (2) a VRAM memcpy
selector (10) for texture upload (the stock path wedges the engine), (3) the descriptor-count fix above (kernel panic otherwise).
`kext-patch/verify-kext-patches.sh <binary>` checks all three at their file offsets; `boot/bringup.sh` runs it and refuses to load a kext
that fails. `kext-patch/build-chain.sh` rebuilds the chain from the stock binary (the last step is the descriptor fix).

DMA is the default: geometry beyond one command buffer is drawn by kernel bus-master DMA (`MACH64_VDMA=0` disables) and large textures upload by bus-master DMA (`MACH64_DMA_UPLOAD=0` disables). Both need the patched `ATIRagePro` (`kext-patch/`, `patch8.py`/`patched9`) and, on the headless rig, `stubctl notify`; both fall back to the PIO path when their kernel side is missing.

## Hardware setups

* **Lombard PowerBook G3 (Rage LT Pro is the display):** install with `install.sh` (see `INSTALL.md`).
* **MDD G4, headless Rage on a PCI card (development rig):** the card is never POSTed by the Mac, so `boot/` runs the PC option
  ROM through `tools/biosemu/biosrun` at boot (and sets `BUS_CNTL.BUS_EXT_REG_EN`, without which PIO vertex writes draw
  nothing). After boot, as root: `boot/bringup.sh` (verifies the kext patches, loads patched ATIRagePro, `RageStubFB` and `Mach64Accelerator`), then
  `stubctl notify` **once per test run** (it also reserves VRAM offset 0 in Apple's heap; without that a dead surface leaves a dangling heap record that later panics the box, see `docs/re-notes/heap-offset0-dangling-record.md`), then `sudo tools/mach64_pathb_suite [test names]`. The stub framebuffer is manual by
  design so a missing card can never affect boot. Never `kextunload` ATIRagePro on a live system (crashes the machine); reboot.

## Layout

```
common/       portable: register defs, ABI, command emission, VRAM span checks (mach64_regs.h)
gldriver/     ATIRageProGLDriver.bundle sources (gld_*.c)
kext/         Mach64Accelerator + Mach64GLUserClient;  kext/stubfb: RageStubFB (headless framebuffer stub, stubctl)
kext-patch/   binary patches for Apple's ATIRagePro.kext (peek, texture upload, descriptor-count fix)
boot/         boot-time option-ROM init for the headless card
tools/        hardware test programs; mach64_pathb_suite.c is the Path B feature/soak/safety suite; biosemu/ = x86 BIOS emulator
tests/        host unit tests (vram_span_test.c)
hosttest/     host verification of common/ (what `make` runs)
docs/         memory-map.md (address safety), path-b-driver-spec.md, re-notes/ (reverse-engineering notes; feature-audit.md = what ATI documents vs what is provided), pdfs/, dri-reference/
```

## Building

* Host checks: `make` (emission core), `gcc -I. -o /tmp/vst tests/vram_span_test.c && /tmp/vst` (span predicate).
* Driver and kexts build natively on Panther (gcc 3.3): `make bundle` in `gldriver/`, `make kext` in `kext/` and `kext/stubfb/`,
  `make` in `tools/`. They use the 10.3 private GL/IOKit interfaces and will not compile against a modern SDK.

## Provenance / legal

Built from open-source DRI/DRM mach64 sources and clean-room analysis of shipping Apple/ATI binaries for interoperability
(writing the missing driver for owned hardware). The ATI PDFs under `docs/pdfs/` are historical reference. Verify licensing
before redistribution.

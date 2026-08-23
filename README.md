# RageProLT Panther Driver

A hardware **OpenGL driver for the ATI Rage LT Pro (mach64LB)** on **Mac OS X
10.3 Panther / PowerPC** — the GPU in the PowerBook G3 "Lombard". Apple shipped
2D acceleration for this chip but **never a hardware GL driver**; OpenGL falls
back to software. This project fills that one missing piece.

## Status

| Layer | State |
|---|---|
| mach64 3D register/command knowledge | ✅ complete (DRI + RRG, verified) |
| Portable command-emission core (`common/`) | ✅ builds + tested on host **and real PPC** |
| Plugin↔kext ABI (55 gld hooks, 17 selectors, 4 memory regions) | ✅ reverse-engineered (`docs/re-notes/ABI.md`) |
| Kext: compiles / links / packages on Panther (gcc 3.3) | ✅ **verified on the Lombard** |
| Kext: loads + coexists with ATIRagePro (distinct IOMatchCategory) | ✅ **verified on hardware** |
| Hardware register **read** (`CONFIG_CHIP_ID=0xdc004c49`) | ✅ **verified on hardware** |
| Hardware register **write** (scratch round-trip) | ✅ **verified on hardware** |
| **2D draw engine executes our commands** (off-screen solid fill, readback-verified) | ✅ **verified on hardware** |
| **3D setup engine rasterizes a triangle** (flat-shaded, off-screen, readback-verified) | ✅ **verified on hardware** |
| Gouraud (per-vertex color) via DMA-burst vertex path | ⬜ needs bus-master ring |
| Wire into GLDriver plugin (real OpenGL apps) | ⬜ |
| GLDriver plugin (`gldriver/`) | 🟡 skeleton — handshake transcribed, bodies TODO |

Register aperture is the little-endian framebuffer window at `BAR0 + 0x7FF800`
(NOT the optional BAR2 aux aperture, which is disabled and master-aborts).
`tools/` has `mach64_peek` (register read) and `mach64_poke` (scratch write),
both run on hardware.  `make` still builds/runs `hosttest/` for the portable
emission core.

## Layout

```
common/     portable, host-testable: register defs, ABI, command emission
gldriver/   ATIRageProGLDriver.bundle skeleton (the missing bundle)
kext/       Mach64Accelerator + Mach64GLUserClient (extends ATIRagePro.kext)
hosttest/   host verification of common/  <- what `make` runs
docs/pdfs/  the 4 ATI reference PDFs
docs/dri-reference/  DRI/DRM mach64 sources (the 3D register/command source of truth)
docs/re-notes/ABI.md the reverse-engineered Panther GL ABI
```

## Architecture (see docs/re-notes/ABI.md)

```
OpenGL.framework -> GLEngine.bundle (Apple: GL state machine, T&L)
                 -> ATIRageProGLDriver.bundle  (gld* hooks)        <- this project
                 -> ATIRagePro.kext + Mach64GLUserClient           <- this project
                 -> Rage LT Pro (register writes via BM_ADDR / DMA ring)
```

Apple's `GLEngine` owns the GL state machine; our plugin implements the `gld*`
backend hooks, translating to mach64 register-write command buffers (reusing
the DRI mach64 logic in `common/`), submitted through the kext user client.

## Building

### Host verification (works here)
```
make            # builds + runs hosttest/test_triangle
```

### The real driver (requires a Panther/PPC toolchain)
`gldriver/` and `kext/` are written against the **10.3 APIs** and the
**private OpenGL "GLD" + IOKit interfaces**. They intentionally will **not**
lint or compile against a modern macOS SDK — e.g. they use
`IOConnectMethodScalarIScalarO` (renamed `IOConnectCallScalarMethod` later) and
32-bit `vm_address_t`. Build on a 10.3/10.4 PPC host (or cross toolchain) with
the 10.3 IOKit kernel headers and the OpenGL private headers. See the `kext`
and `gldriver` targets in the `Makefile`.

## Reverse-engineering scripts

The Capstone/Mach-O analysis scripts that produced `docs/re-notes/ABI.md` live
in the session scratchpad; re-runnable against the `ATIRage128*` bundles.

## Next steps

1. Confirm the Lombard's exact PCI device id (`ioreg` / Open Firmware) and set
   `IOPCIMatch` in `kext/Info.plist`.
2. Bring up the **kext** first: PCI attach, map the register BAR, implement
   `clientMemoryForType` (types 0/1/2/4) + `get_config`.
3. **MMIO-mode first light**: skip the DMA ring — have the plugin push the
   `common/mach64_emit.c` stream to `BM_ADDR` directly (turn-9 pseudo-DMA path)
   to get a triangle on screen, then add bus-master DMA as an optimization.
4. Flesh out the `gld*` bodies against Apple's dispatch table (validate the
   exact `gld` signatures on a real 10.3 `ATIRage128GLDriver.bundle`).

## Provenance / legal

Built from open-source DRI/DRM mach64 sources and clean-room analysis of
shipping Apple/ATI binaries for **interoperability** (writing the missing
driver for owned hardware). The ATI PDFs under `docs/pdfs/` are historical
reference. Verify licensing before redistribution.

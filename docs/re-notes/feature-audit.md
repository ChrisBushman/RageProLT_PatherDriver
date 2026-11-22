# Feature audit: ATI documents + Windows 98 driver vs Apple's stock driver vs this project

Date: 2026-11-20. Purpose: every feature the Rage LT Pro / Rage Pro family documents or ATI's Windows driver exposes is listed here with who
provides it today: **Apple** (stock kext / GA plugin / framebuffer), **Ours** (this plugin + patched kext), or **nobody**. Gaps are classified
by whether OpenGL can reach them.

## Sources and how strong each claim is

| Source | Used for | Strength |
|---|---|---|
| `ATI3DCIF.H` + SDK-C02700 programmer's guide (`pdf/rp_3drageprogguidew95.txt`) | complete 3D API surface (37 render states, prims, formats) | complete, read |
| Win98 `ATI3DCIF.DLL` (disassembled) | how each 3D state maps to registers | read for the states below; not every handler traced |
| PRG-215R3 Programmer's Guide (`ATI_RagePro_Programmers_Guide...txt`) | 2D engine, aperture, video, LT Pro appendix | table of contents + feature lists (1.4, 8.2) read, chapter bodies not |
| GCS-C03500 Graphics Controller Spec | chip-level feature list (2.1, 2.2) | feature lists read |
| LT Pro Register Reference RRG-G03300 | register semantics | read. **ATI omitted the field descriptions of the 3D-setup, texture, overlay/scaler, capture, MPP and TV-out registers on purpose** (section 1.1). The address table (table 3-1) still lists the overlay, scaler, capture, MPP and TVO registers with their block/offset (block 1 offsets 0x00-0x51), which is what allowed the Win98 2D driver trace in section 7 to name them; their bit meanings are not in the document. It does document draw-engine, Z, `ALPHA_TST_CNTL`, composite-shadow, DP/clip/compare, bus-master and AGP registers |
| Win98 `ATI2DRAE.DRV` (16-bit NE GDI display driver) / `ATI2VXAE.VXD` / `ATI3DRAE.DLL` | what ATI's own driver exposes beyond 3D | export table + strings of the 2D driver and VxD; `ATI3DRAE` register descriptor table decoded; **no register-level trace of the 2D driver** |
| `ATIRagePro.kext` (patched v7r) symbol table + `ATIRageProGA.plugin` exports | what Apple's stock driver implements | symbol names; behaviour of each not traced |
| Our sources (`gldriver/`, `common/`, suite) | what we implement | read + hardware-tested where stated |

`ATIICDXX.DLL` is the CD/DVD helper. `ATI3DRAE.DLL` is the Direct3D HAL (D3DDP2OP_* handlers, DX7/DX8 era) for the whole **mach64 family** (its strings name
Mach64GT, Mach64LT, "Mach64: LTPro", Rage Mobility, RagePro, XL/XC). It carries its own register descriptor table for the same 3D registers (TEX_CNTL 0x1dd,
TEX_SIZE_PITCH 0x1dc, TEX_0..10_OFF 0x170-0x17a, SCALE_3D_CNTL 0x17f, SETUP_CNTL 0xc1, ALPHA_TST_CNTL 0x154 at DRI/4), an independent confirmation of the
`ATI3DCIF` decoding. Its D3D render-state handler (RVA 0x50070) **was** enumerated; see section 7.
In `ATI3DCIF`, the Rage Pro-only states (composite, LOD bias, destination-alpha test, specular, enhanced colour) have real setters only in the second chip
class (vtable 0x10025ae8); the first class points all of those slots at one no-op stub (0x15f0), matching the SDK's "RAGE3" tags.

What Apple's stock stack is (from symbols): `ATIRagePro.kext` = accelerator (AGP/GART, VRAM allocator, sleep/wake, display-mode change, perf stats), a **2D
context** (images, transfers to GART, `scale_surface`, `swap_surface`, `set_macrovision`, vsync/paging options, `lock_memory`), a **GL context** (new/delete/page-off texture,
set surface, swap rect, read_buffer, command-buffer submit, timestamps) and `ATIR3Surface` (swap/flip). `ATIRageProGA.plugin` = Quartz 2D acceleration
(`rProCopy`, `rProCopyRegion`, `rProFill`, `rProSolidLines`, `rProSolidScanlines`, surface alloc/lock/swap, beam position). Apple shipped **no** GL
bundle and **no** DVD driver for the Rage Pro (the 3D hardware and the kext's GL context were there, unused).

Legend: **OK** = works, hardware-tested here. **OK\*** = works with a stated limit. **Apple** = stock provides it. **GAP-GL** = hardware can do it and
OpenGL can ask for it, nobody provides it. **GAP-noGL** = hardware feature with no OpenGL analogue. **N/A** = not on this chip/board or outside a GL driver.

## 1. 3D rendering (ATI3DCIF render states)

| # | Feature (SDK name) | Ours | Apple stock | Verdict / note |
|---|---|---|---|---|
| 0-3 | FG colour, solid colour, shade mode none/solid/flat/smooth | flat + Gouraud | - | OK |
| 1,2 | Vertex types (VCF, VTCF, TL), prims: point, line, tri, quad, rect | points, lines, strips, tri, fan, strip, quad, quad strip, polygon; points drawn as quads | - | OK. `C3D_EPRIM_RECT` unused: no GL analogue (GAP-noGL) |
| 5-6 | Texture enable / select | OK | - | OK |
| 7 | Texture lighting none / modulate / alpha-decal | REPLACE, MODULATE, DECAL | - | OK. GL_BLEND / GL_ADD / GL_COMBINE are not in the single-texture pipe (hardware limit, see `texture-blend-audit.md`) |
| 8 | Perspective correction levels 0-9 (`C3D_ERS_TMAP_PERSP_COR`, default 3) | always full correction | - | OK: **decoded** (handler 0x71b0). Level 0 = off; 1-9 select a software threshold (floats 1.05-1.9, default 1.5) in ATI's primitive set-up pre-pass, which compares vertex W ratios and edge lengths per triangle (code at 0x4801-0x4a08, apparently choosing affine or full correction). No register is written; the chip always divides per pixel, so it is a speed/accuracy trade only. GL_PERSPECTIVE_CORRECTION_HINT therefore needs no hardware mapping |
| 9 | Filter: point, 2x2, mip-linear, tri-linear (7 combos) | all, hardware | - | OK (Win98-derived recipe, `win98-mipmap.md`) |
| 9 | Texel alpha under bilinear | hard alpha edge | - | OK\*: **hardware limit**, confirmed against the Win98 driver (`win98-mipmap.md`) |
| 10 | Texel ops: chroma key, alpha, alpha-mask | alpha path | - | chroma key / alpha mask: no GL analogue (GAP-noGL); alpha-test + blend cover the use |
| 11-12 | Alpha blend src/dst factors (8 each) | all 8 mapped (incl. dst-alpha factors) | - | OK |
| 13-16, 21-22 | Draw surface ptr/pitch/pixel-format, viewport, Z ptr/pitch | 32-bit colour + 16-bit Z only | surfaces from `ATIR3Surface` | OK\*: **16/15-bpp and 8-bpp colour targets are not offered** (config nodes are 32-bit); hardware supports RGB1555/565/332 draw. GAP-GL if a 16-bpp desktop must run GL |
| 17 | Fog | per-vertex factor (spec alpha), fog colour | - | OK |
| 18 | Dither | not enabled | - | Accounted for: `SCALE_3D_CNTL` bit 2; the Win98 driver itself **forces dither off for 32-bit targets** (`ATI3DCIF` 0x8350), and we are 32-bit only. Would matter only with a 16-bpp target |
| 19-20 | Z compare (8) / Z mode off, test, test+write | all | - | OK |
| 23 | Scissor | OK | - | OK |
| 24-29 | **Texture compositing** (2nd texture: blend / modulate / add-specular, factor, filter, factor-alpha) | **not implemented**; we advertise 1 texture unit | - | **GAP-GL**: Rage Pro-class feature (second-class stub confirms the first chip class lacks it). RRG documents some bits (`COMPOSITE_CI4_RGB_*` nibble select, `COMPOSITE_SHADOW`, `COMPOSITE_SHADOW_ID` reg 0xE6); the TEX_CNTL composite fields (DRI header bits 8, 11-13, 15, 22, 24-25, 31) are DRI-header only |
| 30 | LOD bias | OK (negative: hardware; positive: software) | - | OK |
| 31-34 | Destination-alpha test, dst-alpha write select, reference | dst alpha write fixed to source alpha; GL alpha test uses the same unit | - | Same hardware as the alpha test: `ALPHA_TST_CNTL` `ALPHA_DST_SEL` [10:8], `ALPHA_TST_SRC_SEL` bit 12, `REF_ALPHA` [23:16], test [6:4], documented in RRG 5-18/5-19. A "limited stencil" built on it is a speculative idea, not a verified capability (GAP-GL, speculative) |
| 35 | **Specular lighting** (additive specular colour) | specular **RGB is never written** (`spec_argb` carries only the fog factor in alpha) | - | **GAP-GL**: `GL_SEPARATE_SPECULAR_COLOR` / secondary colour. Control verified from two sources: `ALPHA_TST_CNTL` bit 31 `SPECULAR_LIGHT_EN` (RRG page 5-20, Win98 setter 0x14730 writes header 0x54 = reg 0x154 bit 31); RRG: must not be set together with `COMPOSITE_SHADOW`. Our fog uses the specular alpha, so the two uses need arbitration |
| 36 | Enhanced colour range | not set (ATI's default is FALSE too) | - | Parity. State 36 exists in `ATI3DCIF.DLL`'s table (setter 0x14840) but not in the Win95 SDK enum (which ends at 35, `C3D_ERS_NUM` = 36). **Measured:** it is `SCALE_3D_CNTL` bit 8 (Win98 setter 0x14840), which scales the interpolated colour to about 1/4 (0xe0 -> 0x38) and has no effect on edges: suite `tex_bit8`. The earlier "EXPAND_DYNAMIC" remark in this row was a different bit (bit 0) |
| - | Edge anti-aliasing (GCS 2.5.1: "first mainstream part to incorporate edge anti-aliasing") | not implemented | - | **No control known.** The ATI3DCIF API has no such state, the Win98 driver never enables it, and the register reference omits that chapter. The DRI header's `EDGE_ANTI_ALIAS` name for `SCALE_3D_CNTL` bit 8 is wrong or at least misleading: **measured** that bit is a colour-range scale and produces no partial-alpha edges (suite `tex_bit8`). Not claimable as a GAP until a control is found |
| - | Texture formats: CI4, CI8, RGB1555/565/8888/332/4444, Y8, YUV422, VQ (8:1) | **everything is expanded to ARGB8888** | - | OK\* functionally; uses 2-4x the VRAM and texture bandwidth the formats could. GAP-GL: `GL_APPLE_ycbcr_422` (YUV422 hardware texture), paletted textures; VQ has no GL analogue |
| - | Texture tiling, cache size / disable | fixed (4K cache, no tiling) | - | perf tuning only |
| - | Texture palettes / palette animation | none | - | GAP-GL only if paletted-texture extensions are exposed by GLEngine (not checked) |
| - | Texture memory in AGP (`TEX_SRC_AGP`, `LoadObject` DMA) | upload is a VRAM memcpy selector; AGP alloc/upload ops exist in our patched kext and one test uses them, **not used by the renderer** | kext has AGP/GART (`configureAGP`, `addToGART`) | **NOT AVAILABLE on this hardware.** The chip sits on the Grackle PCI bus on the Lombard (`/pci@80000000/ATY,LTProParent@11`, no `IOAGPDevice` instance) and on a PCI card in the MDD rig; ATI's AGP document (`agp.doc`) describes texture fetch over an AGP *bus*. Without an AGP host bridge the chip cannot read textures from system memory. The only remaining use of host RAM is a bus-mastered *upload* path to VRAM (performance), see gap list |
| - | Bus-mastered command/vertex DMA (`BM_GUI_TABLE`, documented in the RRG; Win98 2D driver uses it: `BMEngine`, `BMGUITableFIFO`, `BMSolidFill`, `BMStretch`) | PIO drain through Apple's `submit_buffer` | stock also drains by PIO | perf only. Checked: none of the kext's 29 fixed register-write sites touch the BM table registers (offsets 0x248 / 0x24c); they are engine reset/init writes |
| - | Hidden-surface BLT with Z compare | not used | - | no GL analogue |

Things GL needs that ATI's 3D API does not define, which we provide: logic ops (datapath ROP), 8x8 polygon stipple (hardware pattern, GL is 32x32:
approximate beyond 8x8), colour mask, alpha test on texel or vertex alpha, `glReadPixels` / `glCopyPixels` / `glDrawPixels` / `glCopyTexImage`, clear with
scissor/mask, double-buffered present (`mach64_wait_flip`), multiple contexts, windowed drawables.

## 2. 2D engine, display and aperture (PRG-215R3 ch. 2-7, GCS 2.2.2)

| Feature | Provided by | Note |
|---|---|---|
| Rect fill, line draw, bitblt, polygon fill, patterns, mono expansion, scissor, 17 ROPs, source/dest compare (transparent blit) | **Apple** (`ATIRageProGA.plugin`: copy, copy-region, fill, solid lines, solid scanlines; kext 2D context `write_regs`) | Apple exposes copy / fill / lines / scanlines; transparent blit, mono-expand text and pattern fills are hardware features not seen in Apple's exports (GAP-noGL; not a GL concern). We use the engine ourselves for clears, fills, copies, readback |
| Hardware cursor 64x64x2, DPMS, DAC power-down, gamma/palette, CRTC mode set, panning/scrolling, interrupts, linear + paged aperture, EEPROM, DDC/I2C | **Apple** (OF/NDRV framebuffer + IOGraphics; kext display-mode and sleep/wake handlers) | **verified from the Lombard's own framebuffer driver** (section 8): hardware cursor, gamma, DDC/EDID block, sync/DPMS, VBL interrupt, power states, connection probe, TV modes, panel stretch. We never touch them: the framebuffer driver owns them. Boot-time init of a headless card is ours (`boot/`) |
| Big-endian aperture, 8 MB VRAM + register block layout | Ours (documented in `memory-map.md`) | |
| Bus mastering for host-to-screen bitmaps | **not established for Apple** | the kext has GART/AGP transfer buffers (`create_transfer`, `map_transfer_to_GART`) but no static evidence that the engine bus-masters them (no BM table writes); earlier wording here was unsupported. Win98's 2D driver does use bus-mastered GUI tables |

What Win98's 2D driver (`ATI2DRAE.DRV`, from its export table and strings) implements, for completeness: the standard GDI display-driver entry points (BitBlt, StretchBlt, StrBlt,
ExtTextOut, Output = lines/polys, Pixel, ScanLR, DIBBlt, StretchDIBits, BitmapBits, CreateDIBitmap, palette and translate), hardware cursor, DDI gamma ramp, and an `IDC*`
escape set: modes/validate, DPMS, gamma, palette, EDID/DDC detect, overlay alloc/free, video capability/features, Macrovision, CRT enable/disable. Tunables show a
bus-mastered 2D path (`BMEngine`, `BMGUITableFIFO`, `BMSolidFill`, `BMStretch`, `BMTextWatermark`), a stretch engine, and legacy DAC chips (ATI68860, BT481, IBM RGB514, CH8398).
These are display/GDI services; none is a GL feature, and Apple's equivalents are the stock framebuffer and the GA plugin. Not traced at register level.

## 3. Video, DVD and capture (VT/GT features, GCS 2.1.4 / 2.2.4)

| Feature | Provided by | Note |
|---|---|---|
| Hardware overlay/scaler, colour keyer, YUV to RGB, filtered scaling | **not established for Apple.** The kext's `scale_surface` is only a size record (`IOATIRProSurface::set_scaling`), not an overlay or video scaler; `set_macrovision` exists | not part of GL. Win98 exposes overlays (`IDCAllocOverlay`/`IDCFreeOverlay`), YUV gamma, Macrovision. The chip's **front-end scaler** (PRG 8.9: scaled blit with a 2-tap filter, YUV to RGB conversion and dither, enabled by `SCALE_3D_FNC`) is unused by us; whether Apple's swap path uses it was not determined |
| DVD / MPEG-2 motion-compensation assist, planar-YUV bus-master converter | **nobody on this OS** | Apple shipped a DVD driver for Rage 128 but none for the Rage Pro; not reachable from GL (GAP-noGL) |
| TV-out (S-video / composite) | **Apple** on the Lombard: the ROM NDRV carries NTSC and PAL mode tables (512x384 to 1024x768 at 60/50 Hz, DVD modes 720x480/576), `kATITV`/`kTVConnection`, `ATY,TVActive`, and IONDRVSupport has the ATI-specific `IOATINDRV::setATITVV1`. Whether this Lombard's connector is wired and works was not tested | not a GL feature |
| AMC video port, capture, TV tuner, ImpacTV capture | N/A | no such hardware on Mac boards; the Win98 package is shared across the family (also contains the Rage Theater video capture driver names) |

## 4. LT Pro / Mobility specifics (PRG-215R3 appendix B)

LCD panel type / identity / DDC, active-display select, power-management modes, refresh rate, dithering, hardware icon, backlight: **Apple**
(framebuffer + `system_will_sleep` / `did_wake` in the kext). **Ours:** nothing, deliberately; the plugin only renders. `ATILCD.DLL` and the
Acer DPMS hook strings in the Win98 driver are OEM panel plumbing with no GL meaning.

## 5. Gap list and what became of each (Nov 20)

**GL gaps** (hardware can do it, an application can ask for it, nobody provided it), with the outcome of working through them. "Verified" = the Path B suite on the MDD rig (headless Rage, patched Apple kext), 178/178 at the last run. Nothing here has been run against the real GLEngine on the Lombard.

1. **Multitexture, 2 units in one pass: DONE, verified.** Unit-1 state (`0x6748+0x7c`, env `0x674c+0x7c`, bound object `0x15748+0x14`) and per-vertex coordinates (`+0x90/+0x94`, from the Rage128 driver layout) are used; secondary S/T/W via regs `0x328/0x334/0x2a0`, `SECONDARY_TEX_OFF` 0x778, composite `TEX_CNTL` bits 8 + 31 (+9 combine modulate, 11/12 filter). **Required:** `SCALE_3D_CNTL` TEX_BLEND_FCN = trilinear (3<<26), `TEX_CACHE_SPLIT`, `MIP_MAP_DISABLE`, `SETUP_CNTL` 0x40; without trilinear the engine wedges on the first draw. Modulate has a 4-bit factor (out = prev x ((B>>4)+1)/16). The renderer advertises `GL_MAX_TEXTURE_UNITS` = 2. Unverified against GLEngine: the unit-1 vertex offsets, and whether GLEngine exposes the extension for this renderer.
2. **16-bpp colour targets: DONE for offscreen/CPU paths, verified; display allocation unverified.** Per-context `cbpp` (set from the pixel-format node: `node[0x14]` = 0x8000), `DP_PIX_WIDTH` RGB565 (`0x60040464`), 2-byte pitch/span arithmetic, fills/clears packed to 565, ReadPixels/DrawPixels/Bitmap convert; the 3D engine dithers. 16-bpp config nodes are offered **only when `MACH64_16BPP` is set**: the windowed/fullscreen allocation paths cannot be checked on a headless rig. **8-bpp: not done** (no draw datatype work; nobody asks for it). **Open pre-existing bug, found while testing this:** `mach64_blit_surface` (the GUI-engine copy used by the back-to-front present) writes nothing to the destination on this headless rig, at 32 bpp as well as 16, and a 3D triangle drawn after it is also lost. Tried: swapping the destination register pair, adding `SRC_CNTL`/`SRC_HEIGHT1_WIDTH1`; no change. The Lombard present worked in earlier sessions (it uses `mach64_blit_2pitch` into the scanout), so this may be rig-specific (no display); not resolved. A 2D fill followed by 3D in the same frame also loses the 3D draw: that is the documented one-drain-per-frame limit (end the frame after a 2D fill).
3. **Compact texture formats: mostly DONE, verified.** Native RGB565 / ARGB4444 / ARGB1555 from the packed GL types, 8-bit luminance as Y8 (1 byte per texel; reads back 0xf6/0x7e/0x36/0x00 for 255/128/64/0, within the suite tolerance), `GL_APPLE_ycbcr_422` (`0x85B9`, 8_8 = hardware datatype 12, 8_8_REV = 11; luma and a Cr-high chroma probe both decode correctly), LRU eviction with re-upload when the 1 MB heap fills, and a 16-bit retry when an ARGB8888 texture does not fit. **Not done:** paletted CI4/CI8 (the palette location in the GLEngine gli is unknown and cannot be found without a real GLEngine capture), RGB332. Texture compression has no hardware format here (VQ has no GL analogue).
4. **Separate specular: DONE, verified.** `GL_SEPARATE_SPECULAR_COLOR` (gli `0x6380` bit 0) sets `ALPHA_TST_CNTL` bit 31 and writes the secondary colour (gli vertex `+0x40..0x48`, taken from the Radeon driver) to `SPEC_ARGB`; adds to textured pixels, folded into the colour for untextured ones; the alpha byte stays the fog factor.
5. **Front-end scaler blit: NOT DONE; recipe not decodable from the sources.** Tried the Programmer's Guide 8.9 sequence (`SCALE_3D_FNC` = scale, `ALPHA_TST_CNTL`/`TEX_CNTL` = 0, `SCALE_OFF` 0x5c0, `SCALE_WIDTH` 0x5dc, `SCALE_HEIGHT` 0x5e0, `SCALE_PITCH` 0x5ec / 0x750, `SCALE_X_INC` 0x5f0 / 0x7c0, `SCALE_Y_INC` 0x5f4 / 0x7cc, `DP_SRC` FRGD_SRC = 5, source datatype in `DP_PIX_WIDTH`[31:28], trigger `DST_WIDTH_HEIGHT`). The register addresses come from RRG table 3-1 (they alias the texture registers: the scaler pipe *is* the texture engine); the engine accepts the blit and writes the destination, but the pixels never come from the programmed source (constant, row-dependent garbage; offset shift 0/3, pitch 1..2048 px, alias addresses, a texture-cache flush edge all tried). The field encodings are in RRG chapter 6, which ATI omitted; the PRG gives only 8.12 fixed point for the increments. Needs a Win98/DirectDraw capture or another hardware reference. The speculative code was removed.
6. **Bus-mastered host-to-VRAM upload: measured and designed, not integrated.** Two prototypes on the Path A ring. The GUI-path stream (`tools/mach64_bmupload.c`) works (41 MB/s, 0 bad pixels) but occupies the draw engine. The **system transfer** (`tools/mach64_bmsys.c`, Programmer's Guide 8.10.3, `BM_SYSTEM_TABLE` at 0x1bc) is the right primitive: 45 MB/s, 0 of 28,672 dwords wrong, does not touch the register FIFO (20 of 20 rounds of a 2D fill running during a transfer left both intact), and costs the CPU 0.2-0.5 ms per 112 KB image against 3.5 ms for direct VRAM stores and about 5.6 ms for today's kext memcpy path. Static analysis of Apple's kext shows `submit_buffer` is lock-free and that GL and 2D drains are serialized by one accelerator mutex (`accel[0x544]`), which settles how a kick can coexist with WindowServer. Full measurements, register map, design and risks: `dma-upload-design.md`. Not wired in: needs the Path A kext as a second connection, cross-process locking, fencing and a run on the Lombard.
7. **Destination alpha: the measurable part DONE, verified; a stencil substitute is not provided.** `glClear` previously dropped the clear colour's alpha (destination alpha always started at 0); it is now written. `GL_DST_ALPHA` / `GL_ONE_MINUS_DST_ALPHA` blend factors read it correctly and an alpha-only colour mask writes it (suite `dstalpha`). A driver-level stencil is **not** implemented (the stencil state is not located in the gli and the hardware has no stencil buffer); apps can use destination alpha as a mask themselves.
8. **Texture tiling / cache tuning: measured, no headroom found.** Textured fill rate is 35-38 Mpix/s for every texel format; 16-bit and Y8 textures are 5-7% faster only on a large (256x256) texture, so the texture cache is not the limiter (suite `texperf`). `eTexTiling` has no documented control here. Frame overhead (one submit per frame) dominates small draws: a single quad per frame runs at 14 Mpix/s, twelve per frame at 39.

**Withdrawn or re-scoped by the audit:** edge anti-aliasing (no control exists; bit 8 is a colour-range scale); AGP texture memory (not possible, the chip is on a PCI bus on both the Lombard and the MDD rig);
perspective-correction hint (ATI's levels are a software threshold, no hardware effect); stencil, depth bias, stipple-alpha, border colour (ATI's own D3D driver ignores them too).

**Provided by Apple's stock stack, not ours to build:** hardware cursor, gamma, DDC/EDID, DPMS, VBL interrupt, power states, panel scaling, mode setting, TV-out modes (section 8).

**Hardware with no GL analogue (not planned):** texel chroma-key / alpha-mask ops, `C3D_EPRIM_RECT`, VQ textures, shadow-ID compositing, Z-compare BLT, texture morphing, 2D extras (transparent/masked blit, mono-expand, virtual sprites), video overlay/capture/MPEG assist, TV tuner.

Known hardware limits (not gaps): no GL_BLEND/ADD/COMBINE tex env in one unit, texel alpha is not bilinear-filtered, positive LOD bias is software, polygon stipple is 8x8.

## 6. What this audit did not do

* `ATI2DRAE.DRV`, `ATI2VXAE.VXD`, `ATI3DRAE.DLL` and the perspective setter were traced in section 7 (Nov 20); what remains unread there is listed at the end of that section.
* **Apple side, resolved (Nov 20):** `ATIRageProGA` (2002-line disassembly) programs only draw-engine registers, byte offsets /4 = DST_Y_X 0x43, DST_HEIGHT_WIDTH 0x46, DST_CNTL 0x4c,
  SRC_Y_X 0x63, SRC_WIDTH1 0x64, SRC_CNTL 0x6d, PAT_REG0/1 0xa0/0xa1, DP_BKGD_CLR 0xb0, DP_MIX 0xb5, DP_SRC 0xb6, CLR_CMP_CLR/MSK/CNTL 0xc0-0xc2. No scaler, overlay, bus-master or 3D
  register. Its `_GetCapabilities` returns unsupported (0xe00002c7); `_SwapSurface` is one kext call (selector 3). In the kext, `set_scale`/`set_scaling` only store a size and
  call a surface virtual (+0x5c4) then `update_contexts`. **Conclusion: Apple's stack does not use the front-end scaler, overlay, bus-master transfers, or any 3D feature beyond what
  we drive; the front-end scaler gap is real.** The +0x5c4 virtual is `ATIR3Surface::shape_surface()` (resolved through the vtable at 0x1a260; the base class slot is `__cxa_pure_virtual`): surface buffer re-layout after a size change, not a scaler.
* Apple's side is from symbol names, exports and spot disassembly (`scale_surface`, the 29 register-write sites). Statements that stock provides cursor, gamma, DPMS and panel
  control are inferences from the IOGraphics/NDRV architecture, not checks of the Lombard's framebuffer driver (which is not on this machine). Whether Apple's swap path uses
  the front-end scaler, and what `ATIRageProGA` does beyond its exported names, were not determined.
* The SCALE_3D_CNTL, TEX_* and scaler-pipe register bit semantics have no ATI primary source here (ATI omitted them from the reference); they rest on the DRI header and the Win98
  driver, and the DRI header's `EDGE_ANTI_ALIAS` label for bit 8 was shown by measurement to be wrong.

## 7. Win98 binary traces (Nov 20)

### 7.1 `ATI3DRAE.DLL`: Direct3D HAL render states (RVA 0x50070, 165-entry state map)
Processed per D3DRENDERSTATETYPE (DX7/DX8 numbering). **Applied to hardware state** (own handler): TEXTUREHANDLE(1), ANTIALIAS(2: own handler calling a helper, not decoded), TEXTUREADDRESS(3),
TEXTUREPERSPECTIVE(4), WRAPU/V(5,6), ZENABLE(7), FILLMODE(8), SHADEMODE(9), ZWRITE(14), ALPHATESTENABLE(15), LASTPIXEL(16), TEXTUREMAG/MIN(17,18), SRC/DESTBLEND(19,20),
TEXTUREMAPBLEND(21), CULLMODE(22), ZFUNC(23), ALPHAREF/FUNC(24,25), DITHER(26), ALPHABLEND(27), FOG enable/colour/table/start/end/density(28,34-38), SPECULAR(29), ZVISIBLE(30),
COLORKEYENABLE(41), TEXTUREADDRESSU/V(44,45), MIPMAPLODBIAS(46), ANISOTROPY(49: stored only, the chip has none), TEXTUREFACTOR(60), WRAP0-7(128-135), DX8 point size family(154,155,156,166: stored).
**Stored in a shadow array with no hardware effect:** LINEPATTERN(10), MONOENABLE(11), ROP2(12), PLANEMASK(13), FLUSHBATCH(50), TRANSLUCENTSORT(51), **all stencil states STENCILENABLE..STENCILWRITEMASK (52-59)**.
**Ignored entirely:** SUBPIXEL(31,32), STIPPLEDALPHA(33), **EDGEANTIALIAS(40)**, state 42, BORDERCOLOR(43), ZBIAS(47), RANGEFOGENABLE(48), and every lighting/transform/vertex-blend state 136-153, 157-165.
Consequences: (a) independent confirmation that ATI's own drivers have **no stencil and no edge anti-aliasing** on this chip, matching the measured bit-8 result; (b) ZBIAS (polygon offset) is
not implemented by ATI's driver, so a GL `glPolygonOffset` has no ATI reference behaviour; (c) wrap and colour-key are the only per-texture extras (border colour is ignored).
Confidence: *which* states are handled vs ignored is read directly from the jump table (exact). The state *names* come from the standard DX7/DX8 numbering and were not cross-checked per case,
and what each handled case writes to hardware was not decoded.
Texture stage state (function at RVA 0x50e45) accepts **stage 0 and 1 only** (`cmp edi, 2`), keeps a per-stage shadow with a dirty mask, and has special cases for texture handle, COLOROP, COLORARG1,
a mip-filter/min-filter case and the texture-transform case; it also derives a flag ([dev+0x854]) when stage 0 is MODULATE of texture/current with stage 1 matching, i.e. the 2-texture single-pass
(compositing) configuration. This confirms the **2-unit multitexture ceiling**. The per-op mapping of the stage state onto the composite registers was not traced.

### 7.2 `ATI2DRAE.DRV` (16-bit NE, 26 segments, 90,661 instructions disassembled)
Register access is memory-mapped through the driver's own data segment: block 0 at `0xC00 + 4*index`, block 1 at `0x800 + 4*index` (matches PRG: `OVERLAY_SCALE_CNTL` MM 1_09 = B000:F824).
Registers it touches, by the RRG address table: DST_OFF_PITCH, DST_Y_X, DST_HEIGHT_WIDTH, DST_X_WIDTH, DST_CNTL, SRC_*, SRC_CNTL, DP_* (MIX, SRC, WRITE_MSK, BKGD_CLR, PIX_WIDTH, SET_GUI_ENGINE),
PAT_REG0/1, SC_LEFT_RIGHT/TOP_BOTTOM, CLR_CMP_*, HOST_CNTL, GUI_TRAJ_CNTL, FIFO_STAT, GUI_STAT, GUI_CNTL, Z_CNTL, ALPHA_TST_CNTL, SETUP_CNTL, BUS_CNTL, plus the video registers below.
**Bus mastering is real in this driver:** it builds descriptor tables in system memory (walking the page tables to get physical addresses) and writes `BM_GUI_TABLE` (block 1 offset 0x6E, 4 sites)
or, when the chip identifier is 0x1C/0x5C, `BM_GUI_TABLE_CMD` (0_93, 4 sites), and `BM_SYSTEM_TABLE` (0x6F, 1 site). Tunables (read from the registry/INI with defaults): **BMEngine on by default; BMText,
BMTextWatermark, BMSolidFill, BMStretch, BMBlt off by default**, i.e. ATI itself shipped per-operation bus mastering disabled; BMGUITableFIFO, eBMMode and 3D-eBMMode select modes.
Overlay/capture: it clears bit 31 of `OVERLAY_SCALE_CNTL` and `TRIG_CNTL` on a mode switch, and masks bits [6:4] of `OVERLAY_SCALE_CNTL` on GT/VT parts (id 0x4754 / 0x5654). It does not itself program the scaler
(no `SCALER_*`, `OVERLAY_Y_X`, `OVERLAY_SCALE_INC` writes were found): the overlay/scaler programming is in the DirectDraw/video-port parts, not in this display driver.
Static scan of the **Apple kext** for the same registers (immediates 0x5b8/0x5bc, 0x580-0x58c, 0x244-0x24c, 0x1b8/0x1bc) found none, consistent with Apple not bus-mastering the 2D engine.

### 7.3 `ATI2VXAE.VXD` (mini-VDD, v4.13.2656, strings and structure)
Services: PCI/IO/memory mapping of the device, **I2C provider** (EDID/tuner access), **GPIO provider**, **IRQ virtualisation and vsync/IStream notify/flip**, **BM provider** (`DDTransfer`, with
"Bus Mastering is not supported" as an explicit fallback), **MPP (multimedia peripheral port) library** with strobe-sequence tunables and queueing (video decoder / tuner traffic), power
(`SetMonitorPowerState`, ACPI and DPMS get/set), and registry-held hardware/multimedia/TV-out tables (tuner, decoder, audio IDs: Bt829, SA7112, etc.). It does not touch 3D.

### 7.4 What is still not read
`ATI3DRAE.DLL` texture-stage state to composite-register mapping (the function at 0x50e45 was read only for structure); the DirectDraw overlay/scaler code (not in the files we have beyond
export names); the 2D driver's non-BM paths beyond register inventory.

## 8. The Lombard framebuffer stack (from RagePro-kext-patch-kit/framebuffer, pulled Nov 20)

Machine: PowerBook G3 "Lombard", 400 MHz, 10.3.9 (7W98), Rage LT Pro 8 MB, 1024x768 LG 14.1" LVDS panel, chip on the **PCI** bus under the Grackle bridge.
Two drivers, as in the kit README: the **framebuffer** is a classic-Mac-OS PEF NDRV (`ATY,264LTPro`, "LTPro 1.0f9", ROM copy byte-identical to the one inside `ATIDriver.bundle`),
hosted by `IONDRVSupport` as `IOATINDRV`/`IONDRVFramebuffer`; the **accelerator** is `ATIRagePro.kext` (its personality names `IOGLBundleName = ATIRageProGLDriver`, a bundle Apple never
shipped for this kext; the PCI match list is 4c49 LT Pro, 4c47, 4749, 4750, 4c4e, 4756).

NDRV entry points: `TheDriverDescription`, `DoDriverIO`, `SerialOut`. Imports (44 symbols): DriverServicesLib (interrupt sets, delays, cache mode), NameRegistryLib, PCILib config R/W,
InterfaceLib, and VideoServicesLib incl. **`VSLPrepareCursorForHardwareCursor`** and `VSLNewInterruptService`.
Decoded by simulating the compare tree of `DoDriverIO`'s control/status dispatchers (code 0x5de0 / 0x61e0):
* **Control codes implemented:** reset, set mode, set entries (CLUT), set gamma, gray page, set gray, set interrupt (VBL), direct set entries, set default mode, switch mode, set sync (DPMS),
  save preferred configuration, **set hardware cursor, draw hardware cursor**, set power state, set multi-connect, plus ATI-private codes 128, 132, 133, 134, 145, 146.
* **Status codes implemented:** get mode, entries, page count/pages, gray, interrupt, gamma, default mode, current mode, sync, **connection**, mode timing, preferred configuration, next resolution,
  video parameters, gamma info list, retrieve gamma table, **supports hardware cursor**, cursor draw state, power state, **DDC block**, multi-connect, plus private 128, 129, 134, 144, 146, 160, 161.
* **Tables in the data section:** 42 CRT timings (320x200 to 1600x1200 at 56-120 Hz), panel modes (p), stretched panel modes (s), **TV modes** (NTSC/PAL, 512x384-1024x768) and **DVD modes**
  (720x480/576), monitor-code names, connection kinds (LCD, Apple, TV, CRT), four named gamma tables (256 entries, 8-bit), a panel table (Sharp, LG, Samsung, IBM, Apple LVDS 12.1-14.1").
* **Registers it programs** (387 register-helper call sites, offsets resolved where the offset is an immediate, named from the RRG address table): CRTC, DAC and palette, clock/PLL, bus and memory control, `LCD_INDEX`/data (panel expansion and
  power), cursor registers, `FIFO_STAT`/`GUI_STAT` idle waits. **None** of the 3D, texture, bus-master, overlay/scaler or capture registers appear.
* **Registry facts:** panel EDID present (`AppleBacklightDisplay`, brightness 0x2f-0x73), 3 framebuffer power states, cursor info 64x64, 256-entry 8-bit gamma, LCD mode list 640x480 / 800x600 /
  1024x768, current mode 1024x768x32 (`linebytes` 0x1000), accelerator link `IOAccelTypes -> ATIRagePro`, 2D plugin `ATIRageProGA.plugin`.
IONDRVFramebuffer (shared) adds: `setGammaTable`, `getDDCBlock`, `doI2CRequest`, detailed timings, `setCursorImage`/`setCursorState`, VBL `registerForInterruptType`, `ndrvSetDisplayPowerState`;
`IOATINDRV` adds `setATITVV1`, `getStartupDisplayMode`, `findVRAM`.
Confidence: the *set* of control/status codes is exact (simulated, not guessed); code *names* use the standard Mac video driver numbering; handler bodies other than the register inventory were
not decoded. Conclusion for the audit: **all display-side features (cursor, gamma, DPMS, DDC, VBL, power, panel scaling, TV) are provided by the stock framebuffer, and none of it is ours to implement.**

## 9. Remaining sources checked (Nov 20)
* `ATIRagePro.kext` kit: v7 patched is installed; the patch chain and installers are our own work (not an Apple feature source). `ATIDriver.bundle` holds 44 NDRVs (mach64 to Radeon); the
  ones for older chips (264VT/GT/LT, 3DU) were skimmed for feature names only: the string `Overlay` appears in the 264GT and RageM3 NDRVs but not in the LT Pro one.
* `6-2_wme_dd_cp_30314.exe` (Catalyst 6-2 for Windows Me): Radeon-era package; its INFs list no Rage Pro / LT Pro device IDs, so it is not a source for this chip.
* Rage Pro SDK 4.1 header `ATI3DCIF.H`, `agp.doc`, `vq_v3.doc` (VQ spec, not needed for GL), samples (`specular`, `Rproex4`): header facts folded in below.
  `C3D_TMAP` has per-texture `bClampS/bClampT` (we map to TEX_CNTL clamp bits: OK), `bAlphaBlend` (texture alpha as blend factor), `eTexTiling` (Rage3 tiled texture layout, performance);
  `C3D_TLVERTEX` carries a second texel coordinate set (`composite.rhw/tu/tv`) for compositing; blend factors include destination alpha (we map them: OK);
  filter `MIN2BY2_MAGPNT` exists on Rage3 (point mag with 2x2 min).
* ATI3DCIF composite setters (class-B vtable +0x30..+0x48, +0x60) all write `TEX_CNTL` (reg 0x1dd = 0x774): composite enable, function field [10:9] (blend/modulate/add-specular),
  factor [7:4], composite filter bits 11 and 12, factor-alpha bit 13, texture-2 size fields at [19:16], [23:20], [27:24]; the composite-select setter also touches `ALPHA_TST_CNTL`
  (CI4 nibble bits 28/29, documented in the RRG). The second texture's base/pitch programming was not traced.
* GCS 2.2 feature list cross-check: additional hardware features with **no GL path**: shadow-ID compositing (`COMPOSITE_SHADOW`, `COMPOSITE_SHADOW_ID`; RRG documents the bits, neither
  ATI3DCIF nor the D3D HAL exposes it), Z-compare BLT, texture morphing, 24-bpp packed 2D, virtual sprites / masked blit / context chaining (2D), video mirroring.

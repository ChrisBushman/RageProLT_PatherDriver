# Mipmapping on the Rage LT Pro: what the Windows 98 driver does (reverse-engineered)

Source: `ATI3DCIF.DLL` (176 KB, i386 PE, ImageBase 0x10000000) from ATI's Windows 9x driver package `wme-j5-30-1-b02.exe` (InstallShield
SFX; the DLL is `ATIDrive/ATI3DCIF.DL_`, MS-compressed, `7z x` unpacks it). It implements the `ATI3DCIF.H` API from the Rage Pro SDK
(`AMD Docs/3DRage/`). Disassembled with Capstone; addresses below are RVAs in that DLL (add the image base for VAs).

## Why this was needed

The 2026-08 hardware attempts at mipmapping rendered black or wedged the engine, and the repo concluded "the mach64 has no usable
hardware mip chain". The SDK docs (programmer's guide, `ATI3DCIF.H`) say the Rage Pro *does* mip-map in hardware (point, mip-linear,
2x2, tri-linear), but no register-level documentation exists in the files on hand (the LT Pro register reference has no 3D chapter). The
Win98 driver is the register-level ground truth.

## Register numbering inside the DLL

The DLL's register-write helpers (`0x3380` single write, `0x2fa0` block write, `0xf250` single write with direct fallback) take
`arg = DRI_offset / 4`. In the bus-master command stream the header dword is `arg ^ 0x100`; the direct (non-stream) path stores to
`regbase + arg*4`. Examples decoded: `0x17f` = SCALE_3D_CNTL (DRI 0x5fc), `0x1dd` = TEX_CNTL (0x774), `0x1dc` = TEX_SIZE_PITCH (0x770),
`0x170` = TEX_0_OFF (0x5c0), `0xc1` = SETUP_CNTL (0x304). Our driver's DRI numbers match.

## The recipe (all verified on hardware, MDD rig, Nov 20 2026)

* `ATI3DCIF_TextureReg` (0x5d30): for a mip texture it stores `obj[8 + 4*r]` = VRAM offset (host pointer minus framebuffer base, via
  0x11860) of the level whose larger dimension is `2^r`, r = 0 (1x1) .. maxLog2. The application supplies the levels, in any layout; the
  chain must run all the way down to 1x1 (`n+1` levels for `n = max(log2 w, log2 h)`).
* Texture select (vtable method at 0x1610, `ContextSetState(C3D_ERS_TMAP_SELECT)` handler at 0x7110):
  * calls method +0x90 (0x1940) with `bMipMap ? 0 : 1`: that sets/clears **SCALE_3D_CNTL bit 24 (MIP_MAP_DISABLE)**;
  * `TEX_SIZE_PITCH` = base map only: `log2(w) | maxLog2 << 4 | log2(h) << 8`;
  * the per-level offsets are kept as 25-bit values (`& 0x1ffffff`, high bits of the register preserved);
  * emission (function around 0xf2a2..0xfce0): if bit 24 is clear, `TEX_SIZE_PITCH`, then a block write of `TEX_0_OFF .. TEX_maxLog2_OFF`
    from the offset array; if set, only `TEX_{maxLog2}_OFF`.
* Filter (`ContextSetState(C3D_ERS_TMAP_FILTER)` handler 0x7300; setters vtable +0x88 = 0xae80 sets **SCALE_3D_CNTL[27:26]**, +0x8c = 0xafd0
  sets **BILINEAR_TEX_EN (bit 25)**). Mapping from `C3D_ETEXFILTER`:

  | enum | meaning | [27:26] | bit 25 |
  |---|---|---|---|
  | 0 MINPNT_MAGPNT | point/point | 0 | 0 |
  | 1 MINPNT_MAG2BY2, 2 MIN2BY2_MAG2BY2 | | 2 | 1 |
  | 3 MIPLIN_MAGPNT | blend between maps, point mag | 1 | 0 |
  | 4 MIPLIN_MAG2BY2 | | 1 | 1 |
  | 5 MIPTRI_MAG2BY2 | tri-linear | 3 | 1 |
  | 6 MIN2BY2_MAGPNT | | 2 | 0 |

  So field 1 = mip-linear (blend two maps), 2 = 2x2 within a map, 3 = both. The header notes "2x2 MIN only if 2x2 MAG is on" on the older GT,
  so the plugin sets bit 25 whenever the field is 2 or 3.
* **`SETUP_CNTL` (0x304) bit 6, LOG_MAX_INC_ADJ, is set in the driver's context defaults** (default value 0x44; function at 0x11f80 builds
  the shadow, the shade-mode setter at 0x14ab0 only touches bits 3-5). The setup engine computes `LOG_MAX_INC` (DRI 0x748) from the S/T
  gradients, and the level selection depends on it. Our driver had `SETUP_CNTL = 0` because an old note said bit 6 "breaks gouraud"; that
  note was a misattribution (verified: gradients survive, see suite `tex_mip_gouraud`). This missing bit is the most likely reason the 2026-08
  attempts produced black output and a wedged engine; the same bit is why GL_LINEAR minification "hung" at 128x128.
* S/T scaling (select method 0x1610, 0x18c4-0x192b) is the same for mip and non-mip textures, so our normalised-S/T path needs no change.

## Plugin implementation (gld_texture.c `mach64_bind_texture`)

Hardware mips are the default for a complete chain (fog is orthogonal and works); `MACH64_SW_MIP=1` forces the older software level selector
(`mach64_tex_select_lod`), which also handles incomplete chains. `GL_LINEAR` min filter on single-level textures uses field 2 + bit 25
(`MACH64_NO_MIN_LINEAR=1` disables). All 11 `TEX_r_OFF` registers are written (those above maxLog2 repeat the base offset so none points at stale
memory). `SETUP_CNTL` bit 6 is set only for hardware-mip draws and cleared again for others.

## Hardware evidence (suite tests `tex_mip`, `tex_mip_nonsq`, `tex_mip_gouraud`, `tex_minlin`)

* 64x64 chain, solid colour per level, quads of 64/32/16/8 px: the chip picks levels 0/1/2/3 itself (TEX_SIZE_PITCH stays 0x666 = base);
  LOG_MAX_INC steps 0x10000 per level.
* Point (0), mip-linear (1), 2x2 (2) and tri-linear (3) fields all run without a hang; tri-linear at a fractional LOD (45 px quad) read
  back 0xff8f7000, about 56% level 0 + 44% level 1.
* Non-square 64x16 chain (64x16 ... 4x1, 2x1, 1x1) selects the right levels.
* GL_LINEAR min filter on 32x32 / 128x128 / 256x256 single-level textures blends minified columns, no hang.
* 15 notify+run cycles of the texture tests: no failures.

* Mip + 50% blue fog on a level-1 quad: 0x8000807f (half green, half blue).

LOD bias: Win98 `C3D_ERS_LOD_BIAS_LEVEL` (0x9420 -> vtable +0x60, method 0x138b0) writes **TEX_CNTL bits 3:0** = n (0..15) by read-modify-write; the SDK
guide: "threshold stride = 1 + n/16" (1..1 15/16). A larger stride switches to the next smaller map later, i.e. it only ever makes the result
**sharper**: it is a NEGATIVE GL bias of -log2(1 + n/16), range [0, -0.954].
GL source: `glTexEnvf(GL_TEXTURE_FILTER_CONTROL_EXT, GL_TEXTURE_LOD_BIAS_EXT, x)` is a float at **gli+0x6774** (unit 0). Found without our renderer:
`tools/mach64_lodscan.c` makes an offscreen software CGL context on the MDD, finds the gli in-process (samples candidate pointers with
`vm_read_overwrite`, accepts the one whose +0x6380 bit 8 flips with GL_FOG and +0x6340 bit 24 with GL_DEPTH_TEST), and diffs it while the bias
changes: only +0x6774 changed (0 -> 1.0 / 2.5 / -1.5). GLEngine's gli layout is shared by all renderers, so this works on any machine with a display.
Plugin: negative bias -> nearest TEX_CNTL nibble (hardware). Bias >= +0.5 (blurrier; no hardware field) -> the software level selector, which applies
whole-level shifts of both signs. Hardware evidence (suite `tex_lodbias`): a 64px-base chain switches L1->L0 between 54 and 58 px at bias 0;
at -0.5 (n = 7, stride 1.44) 46 px already shows level 0; at -0.95 (n = 15) 34 px shows level 0; the thresholds scale as predicted.
Limits: unit 0 only (no multitexture here); the software fractions are rounded to whole levels.

Not verified: mip textures with alpha blending, bilinear on alpha-blended textures (known chip limit),
compressed (VQ) textures, and presenting a
mip-mapped scene on a real display.

## Texture environment modes (checked against the same SDK)

`ATI3DCIF.H` offers only three texture-light functions: `C3D_ETL_NONE` (REPLACE), `C3D_ETL_MODULATE`, `C3D_ETL_ALPHA_DECAL` (= GL_DECAL), plus the texel
ops none / chroma-key / alpha / alpha-mask. A Rage3 "composite" second texture exists (BLEND/MOD/ADD_SPEC with a factor) but needs two texture
units. So GL_BLEND, GL_ADD and GL_COMBINE are not available in the single-texture pipe; the plugin's MODULATE approximation is the best the
hardware offers, and `texture-blend-audit.md` stands.

## Texel alpha is not bilinear-filtered: investigated, confirmed as hardware behaviour

Observation (MDD G4, suite `tex_alpha_bilinear`, `tex_alpha_edge`, `tex_alpha_decal_probe`): with GL_LINEAR magnification an RGBA texture's *colour*
edge blends (17 blended pixels, with or without GL_BLEND), but a transparent->opaque *alpha* edge stays a hard step (0 blended pixels, identical to
NEAREST) in every configuration tried:
1. GL_BLEND (SRC_ALPHA / INV_SRC_ALPHA, TEX_MAP_AEN set): hard edge.
2. Same, with `TEX_BLEND_FCN` field 2 added to bit 25 (the Win98 encoding for 2x2): hard edge (temporary A/B switch, removed afterwards: no change).
3. `ALPHA_DECAL` (`TEX_LIGHT_FCN` = 2, the texture unit's own Tclr*Talp + Cprim*(1-Talp), no framebuffer blending): hard edge.
Texels are stored as full ARGB8888, so quantisation is not the cause.

What the references say:
* RRG-G03300 (LT Pro register reference): the texel alpha feeds the blender and alpha test "if TEX_MAP_AEN in SCALE_3D_CNTL" (and is 0xFF otherwise);
  nothing about filtering it. The SCALE_3D_CNTL field table (printed page 6-7) is absent from the text extract, so the filter/alpha bit
  semantics cannot be checked there. The SDK guide (SDK-C02700) describes bilinear only in terms of "texels blended", with no alpha statement.
* Win98 `ATI3DCIF.DLL`: `TEXOP` handler (0x7400) maps NONE / CHROMAKEY / ALPHA / ALPHA_MASK onto vtable setters +0xd4 (chroma key), +0x94
  (**TEX_MAP_AEN**, bit 30, setter 0xb140), +0x9c (**TEX_AMASK_AEN**, bit 28); ALPHA = {TEX_MAP_AEN}, ALPHA_MASK = {TEX_MAP_AEN, AMASK_AEN} and
  bit 29 (`TEX_AMASK_BLEND_EDGE`) cleared. The filter setters (0xae80, 0xafd0) take no alpha-format input and have no alpha condition. So the Win98
  driver programs alpha-blended bilinear textures exactly as we do and would show the same hard edge.
* Bit 29 (`TEX_AMASK_BLEND_EDGE`) is only ever *cleared* in the whole DLL (0x4a6f, 0x4d58, 0x5158, 0x74b6, ...), never set: ATI shipped nothing that uses it.
* `ATI3DRAE.DLL` (ring-0 engine) writes SCALE_3D_CNTL only in fixed DirectDraw blit/scaler setups (0x4b5f9, constant 0x40) and its other hits are
  vertex setup arithmetic: no texture alpha logic. `ATIICDXX.DLL` is the CD/DVD helper (wnaspi32), `ATI2DRAE.DRV` is the 2D display driver; neither
  touches the 3D filter state.

Conclusion: the chip's texture unit hands the blender/decal stage the nearest texel's alpha while filtering colour; nothing in the documents or in ATI's own
driver contradicts that, and ATI's driver would behave identically. This is a hardware property, not a plugin bug. The silicon-level reason is
not documented anywhere available, so the explanation above (alpha fetched unfiltered) is the observation restated, not a proven mechanism.
Consequence for apps: magnified alpha-tested/blended sprites get pixelated alpha edges while their colour is smooth. No plugin-side fix is
attempted: faking filtered alpha would need per-pixel software work the engine cannot do.

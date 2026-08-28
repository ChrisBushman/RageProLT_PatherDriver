# Texture / blend / fragment-state audit (2026-08-27)

Coverage audit of gld_texture.c (the live state path = mach64_bind_texture, reads GL state from the
gli, emits SCALE_3D_CNTL/TEX_CNTL/ALPHA_TST_CNTL/DP_*) + gld_state.c.

## IMPLEMENTED + HW-verified (real)
- Depth test + all 8 depth funcs; Z buffer bind.  Shade model (flat/smooth).
- Alpha test: glAlphaFunc (func + ref), gli+0x62e0 (ref)/0x62e4 (enum<<16|enable).
- Fog: alpha-fog (ALPHA_FOG_EN bit12), factor from specular alpha; DP_FOG_CLR.  See fog.md.
- Blend: GL_BLEND enable (gli+0x6304 bit 0x01000000) + glBlendFunc (gli+0x62e8 = srcRGB<<16|dstRGB).
  SRC factors (8): ZERO ONE DST_COLOR 1-DST_COLOR SRC_ALPHA 1-SRC_ALPHA DST_ALPHA 1-DST_ALPHA.
  DST factors (8): ZERO ONE SRC_COLOR 1-SRC_COLOR SRC_ALPHA 1-SRC_ALPHA DST_ALPHA 1-DST_ALPHA.
- Texture: upload+bind (ARGB8888 in VRAM), formats GL_RGB/GL_RGBA, wrap REPEAT vs clamp (gli tex rec
  +0x10; any non-REPEAT -> HW clamp), env MODULATE/REPLACE/DECAL (TEX_LIGHT_FCN), TEX_MAP_AEN (RGBA
  alpha), MIPMAP LOD (per-level TEX_r_OFF, MIP_MAP_DISABLE cleared -> HW selects LOD).
- Scissor (GLEngine clips geometry, not HW -- scissor.md), logic op, polygon stipple.

## GAPS (concrete, prioritised)
1. **Texture MAG FILTER -- DONE (bilinear, 2026-08-27).**  Found the filter state in the tex state
   object (GLD_TEXFILTER_PROBE): MIN_FILTER u16 @ s+0x16, MAG_FILTER u16 @ s+0x18 (0x2600 NEAREST /
   0x2601 LINEAR / 0x2700..0x2703 mipmap variants).  mach64_bind_texture now sets SCALE_3D_CNTL
   BILINEAR_TEX_EN (bit25 = 0x02000000, HW-verified vs the XFree86 value) when MAG==GL_LINEAR.
   HW-verified (tools/mach64_texfilter): a magnified GL_LINEAR texture reads back the 4-texel blend
   (128,128,128); GL_NEAREST stays pure.  HW CAVEAT (per user): the LT Pro cannot bilinear-filter
   ALPHA-BLENDED textures (grates/fences/smoke stay pixelated) -- setting the bit is harmless there.

   1b. **MIN/mip filter (TEX_BLEND_FCN) -- NOT HARDWARE-DOABLE / HW LIMIT (2026-08-27).**  Chased at
   length (tools/mach64_texmin, mach64_texmip).  Findings, HW-verified on the LT Pro:
   - TEX_BLEND_FCN [27:26] (NEAREST=0, LINEAR=0x04000000, TRILINEAR=0x08000000) is a MIP-LEVEL BLEND
     control, NOT an intra-level minification filter (BILINEAR_TEX_EN bit25 is the mag filter; there is
     no separate intra-level *min* bilinear).  Proven by the failure mode: with LINEAR set on a
     SINGLE-level texture, at a minifying LOD the HW fetches a non-existent second level -> the textured
     output goes BLACK (small texture) or the bus HARD-HANGS (128x128 wedged the machine twice).
   - It only means anything with a real mip chain -- but the mach64 has NO usable HW mip chain.  The DRI
     reference (mach64_tex.c GL_TEXTURE_BASE_LEVEL) states it outright: "for mach64 we're only concerned
     with the base level since that's the only texture we upload."  It never uploads sub-levels and never
     enables multi-level LOD.  Our attempt to (upload all levels + clear MIP_MAP_DISABLE + per-level
     TEX_r_OFF) rendered mipmapped textures BLACK; pointing the single base register at our multi-level
     layout HARD-HUNG the machine.  So HW mipmapping is not achievable here.
   - RESOLUTION: driver now matches the reference -- ALWAYS MIP_MAP_DISABLE, base level only (tex_mip=0,
     single base offset), the identical proven path non-mipmapped textures use.  Mipmapped textures thus
     render from their base level (alias when minified) instead of black/hang.  TEX_BLEND_FCN stays
     probe-gated (GLD_TEXBLEND_PROBE) as a dead experiment; it is not shippable without a mip chain.
   - Same category as env BLEND/ADD/COMBINE (gap #3): the reference falls back because the silicon can't.
2. **Texture internal formats -- DONE (2026-08-27).**  GLEngine hands texels in the app's BASE format,
   tightly packed (fmt @ level rec+0x0c; confirmed via GLD_TEXFMT_PROBE byte dump -- NOT pre-expanded
   to RGBA).  Only 0x1907(RGB)/0x1908(RGBA) were handled; the rest fell into the RGB path and read
   garbage.  Added conversions in mach64_blit_texels: 0x1909 GL_LUMINANCE (1B -> grey opaque), 0x8049
   INTENSITY (1B -> grey, A=I), 0x1906 ALPHA (1B -> RGB 0, A), 0x190A LUMINANCE_ALPHA (2B -> grey+A);
   has_alpha now set for RGBA/ALPHA/LA/INTENSITY.  HW-verified (tools/mach64_texfmt): a 2x2
   GL_LUMINANCE texture (64/128/192/255) reads back the exact greys.  (Test gotcha: a 2x2 1-byte
   texture needs GL_UNPACK_ALIGNMENT=1, else GLEngine reads past the row.)
3. **Texture env -- DONE / HARDWARE-LIMITED (2026-08-27).**  The mach64 TEX_LIGHT_FCN field is 2 bits
   = 3 modes: REPLACE / MODULATE / ALPHA_DECAL.  Those work (and interact correctly with the new
   L/A/I formats: MODULATE/REPLACE compliant).  GL_BLEND / GL_ADD / GL_COMBINE are NOT hardware-doable
   -- the DRI reference (mach64_texstate.c) explicitly `FALLBACK`s them to Mesa's SOFTWARE rasteriser
   ("GL_BLEND not supported by RagePRO, use software"), which we can't do through Apple's GLEngine.
   Best hardware approximation = MODULATE (what the driver falls back to).  So there is no implementable
   gap here.  Minor known edge case (not worth fixing): DECAL on L/A/I/LA is GL-undefined; the reference
   disables texturing, we leave ALPHA_DECAL (which == REPLACE for the no-alpha formats -- a reasonable
   undefined result).
4. **Blend extras (LOW/niche).**  No GL_SRC_ALPHA_SATURATE, glBlendEquation (SUBTRACT/etc. -- ADD
   only), constant-colour factors (needs blend colour).  Unsupported factor -> opaque fallback.
5. **Multitexture -- N/A.**  The LT Pro has a single TMU; ARB_multitexture is not HW-supported.

## Recommendation
Do #1 (bilinear filtering) first -- it's the single biggest correctness/quality win and is the same
gli-probe + register-bit pattern already used for wrap/env/blend.  Then #2 (luminance/alpha formats).

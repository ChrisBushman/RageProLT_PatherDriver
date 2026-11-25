# GL feature audit (Nov 25)

Evidence: `tools/ragegl/gl_probe` (129 probes) on the G4 MDD against the standalone driver: **128 pass, 1 fail** (ARB fragment program); `gl_suite` 290/290.
Status words: **tested** = a probe draws and checks pixels; **accepted-only** = the call is accepted but the effect is not verified (weaker than tested);
**failing** = probe fails (driver gap); **unsupported** = GLEngine or the chip cannot do it; **untested** = no probe yet.

Resolved (Nov 25): three hardware-blend probes (func_separate, DST_COLOR, SRC_COLOR) used to fail after any run that had drawn with two texture units. Bisecting probe
groups (kext reload heals it) showed `TEX_CNTL` keeps the secondary-unit bits of the last multitexture draw, and untextured draws never rewrote it. They now write
`TEX_CNTL = 0` whenever their state is emitted (common/mach64_emit.c).

## OpenGL 1.1 core

| Area | Status | Evidence |
|---|---|---|
| Points: size, aliased wide, smooth | tested | wide point, point smooth |
| Lines: width, stipple, smooth | tested | wide line, line stipple (+factor), line smooth |
| Polygons: fill/line/point mode, cull, stipple, smooth, offset (fill/line/point) | tested | polygon mode LINE/POINT, cull, polygon stipple/smooth, three offset probes |
| Depth test, all 8 functions | tested | depth functions |
| Alpha test, all 8 functions | tested | alpha test functions; also on the CPU blend path |
| Blend: 9 source/dest factors, ADD | tested | blend probes, suite |
| Logic ops | tested (8 of 16) | logic ops, XOR |
| Scissor, user clip planes | tested | scissor, clip plane |
| Stencil (all ops, two-sided, wrap) | tested | stencil probes, suite |
| Fog LINEAR/EXP/EXP2 | tested | fog modes |
| Lighting, two-sided, colour material, spot | tested | lighting, two-sided, colour material |
| Texture 1D | tested (fixed today) | 1D texture, TexSubImage1D |
| Texture 2D: wrap, filters, env REPLACE/MODULATE/DECAL/BLEND/ADD | tested | wrap probes, env probes |
| Mipmaps, base-level clamp (`TEXTURE_MAX_LEVEL`) | tested | effective max byte at state+0x5f (GLEngine validate 0xd284c) |
| Texture border | tested (CLAMP and REPEAT) | border texels kept for CLAMP, cropped otherwise; no texel-centre inset |
| TexSubImage1D/2D, CopyTexImage2D, CopyTexSubImage2D | tested | probes |
| CopyTexImage1D | tested | |
| Texture objects, priorities, residency, delete while in flight | tested (residency result unchecked) | texture objects, tex_delete_before_read |
| texgen OBJECT_LINEAR, SPHERE_MAP, texture matrix | tested (sphere map: drawn only) | |
| texgen EYE_LINEAR | tested | |
| Display lists, vertex arrays, interleaved arrays | tested | |
| Selection, feedback | tested | |
| Pixel store, zoom, DrawPixels/ReadPixels, CopyPixels | tested (CopyPixels via suite) | |
| Pixel transfer scale/bias, colour matrix, post-matrix scale/bias | tested | DrawPixels, CopyPixels, ReadPixels; integer Q8/Q16 maths; state offsets from GLEngine glPixelTransferf 0x294b8, glMatrixMode 0x22088 |
| Pixel maps (R/G/B/A to R/G/B/A) | tested (R_TO_R) | tables at gli+0x6470.. (GLEngine 0x26bbc) |
| Draw/read buffer FRONT/BACK | tested | GLEngine needs descriptor byte +0x1d (copied to gli+0x4611): the double-buffer capability |
| Accumulation buffer | tested | |
| Evaluators, rasterpos, bitmaps | untested (GLEngine side) | |

## Extensions

Advertised today (driver mask): ARB_multitexture, texture_env_combine, texture_env_dot3, texture_env_add, texture_cube_map, texture_compression,
texture_mirrored_repeat, texture_border_clamp, texture_non_power_of_two, point_sprite, vertex_buffer_object; EXT_texture_rectangle, texture_env_add,
secondary_color, paletted_texture, texture_compression_s3tc, blend_color/minmax/subtract/func_separate/equation_separate, stencil_wrap, stencil_two_side,
separate_specular_color, texture_mirror_clamp, compiled_vertex_array, abgr, bgra; SGIS_texture_edge_clamp. Plus GLEngine's 24 fixed names.

| Extension | Status | Note |
|---|---|---|
| ARB_multitexture | tested | needs descriptor +0xae = 2; secondary textures 1x1..16x4 verified. Hardware multiplies with a 4-bit secondary factor (about +8% on mid values) |
| ARB_texture_env_combine | tested | ADD_SIGNED is exact by default (CPU compositing); `RAGE_ADD_SIGNED=fast` in the environment (or `-DGLD_APPROX_ADD_SIGNED` as build default) keeps the hardware approximation for speed. Fast mode result for the probe is 198,132,66 (exact: 123,198,198), so the probe fails in fast mode by design |
| ARB_texture_env_dot3 | tested (CPU) | |
| ARB_texture_env_crossbar | tested (CPU two-unit compositor), not advertised (no extension bit identified) | primary colour and both texels nameable |
| ARB_point_parameters | tested, advertised | per-vertex size from eye distance (clip w / projection slot 3), min/max clamp, fade threshold |
| ARB_texture_cube_map, texture_rectangle, NPOT | tested (CPU shaded) | |
| ARB_texture_compression, S3TC | tested (DXT1; DXT3/5 via suite) | |
| EXT_paletted_texture | tested | |
| EXT_blend_color/minmax/subtract, func_separate, equation_separate | tested | CPU path for the ones the chip lacks |
| EXT_secondary_color, separate_specular_color | tested | |
| EXT_stencil_wrap/two_side | tested | |
| EXT_compiled_vertex_array, ARB_vertex_buffer_object | tested | |
| EXT_abgr, EXT_bgra | tested (upload) | |
| EXT_texture_filter_anisotropic | accepted-only, not advertised | filter ignored |
| ARB_point_sprite | tested | |
| ARB_occlusion_query, ARB_vertex_program, APPLE_* (fence, flush_render, VAO), multi_draw_arrays, draw_range_elements, SGIS_generate_mipmap, ARB_window_pos | tested | GLEngine-side or driver-assisted |
| ARB_fragment_program | **failing** (accepted, not executed: draws with the primary colour) | static analysis (GLEngine 0x9d104..0x9e488): programs are parsed by the external PP library (PPParserParse) and compiled to a PPEmulatorProgram kept in the context; the driver never receives the text. GLEngine keeps the fragment PPStream at program-object+0 (object = ctx+0x10000+0x8724+4*target index) and does not build an emulator program for it (vertex programs do, and run through PPEmulatorRun, 0xd9908). Two routes: (a) the driver dlopens libGLProgrammability (present on Panther: PPEmulatorProgramCreate/ConvertFromPPStream/InitialiseHandleBanks/Run, emulator struct fields +0x2d0 program, +0x2d8/+0x2dc data blocks, 0x100-byte stride; it also spawns a secondary thread, which conflicts with the single-core rule) or (b) decode the PP stream and write an own fixed-point interpreter. Neither is small; no target app uses ARB_fragment_program (2003-era Panther titles). GLEngine advertises ARB shader names in its fixed list, so they cannot be removed |
| 3D textures | tested (CPU shaded) | descriptor +0xb4 = MAX_3D_TEXTURE_SIZE; nearest/trilinear, wrap S/T/R; level 0 only |
| ARB_depth_texture | tested (DEPTH_COMPONENT FLOAT/UINT/USHORT/UBYTE sampled as luminance, 8-bit) | GLEngine stores float depth as UNSIGNED_INT; DEPTH_TEXTURE_MODE INTENSITY/ALPHA and TEXTURE_COMPARE (shadow) not implemented |
| ARB_multisample | pixel format offered only | no multisampling |
| SGI_color_matrix | tested for pixel transfer; not advertised (no extension bit identified) | |
| Register combiners, texture shaders, NV/ATI vendor bits | unsupported | no hardware path |
| Destination alpha | unsupported | chip is RGB565/1555, ALPHA_BITS 0 |

## Known limits of the CPU rasteriser path
CPU 2D textures use one mip level per triangle (nearest level from the texel/pixel area ratio; probe cpu_mip_level); cube, 3D and rectangle maps are level 0 only. Two-unit texturing on the CPU path only for crossbar COMBINE. Alpha test (before stencil/depth, so discarded fragments touch neither) and fog (vertex fog factor interpolated, blended with the fog colour) are applied on the CPU path (probes cpu_alpha_test, cpu_fog). Colour-mask nuances remain.

## Resolved (Nov 25): texture deleted while its draw was unsubmitted
glDeleteTextures freed the texture's VRAM immediately, but the draw that sampled it could still sit in the unsubmitted batch; the kernel validates texture offsets against live allocations, so that draw sampled garbage (a stale red block in the probes). Apps that delete a texture before the frame ends (common) were exposed. `mach64_texture_deleted` now takes the context and submits the batch first when the slot was bound in the current batch. Regression probe: `tex_delete_before_read`. The bug looked like a heisenbug (any extra getenv/fprintf call moved the batch flush) and took a long bisect; lesson: when a probe result changes with unrelated code, suspect batching/lifetime races before hardware state.

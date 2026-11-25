# GL feature audit (Nov 25)

Evidence: `tools/ragegl/gl_probe` (115 probes) on the G4 MDD against the standalone driver: **105 pass, 8 fail, 2 unsupported**; `gl_suite` 290/290.
Status words: **tested** = a probe draws and checks pixels; **accepted-only** = the call is accepted but the effect is not verified (weaker than tested);
**failing** = probe fails (driver gap); **unsupported** = GLEngine or the chip cannot do it; **untested** = no probe yet.

Note: three blend probes (func_separate, DST_COLOR, SRC_COLOR) failed for a while with wrong hardware-blend output on every driver build, including builds that
had passed them; a G4 reboot cleared it. Cause not identified (stale chip/engine state after long uptime). Watch for it in the soaks.

## OpenGL 1.1 core

| Area | Status | Evidence |
|---|---|---|
| Points: size, aliased wide, smooth | tested | wide point, point smooth |
| Lines: width, stipple, smooth | tested | wide line, line stipple (+factor), line smooth |
| Polygons: fill/line/point mode, cull, stipple, smooth, offset (fill/line/point) | tested | polygon mode LINE/POINT, cull, polygon stipple/smooth, three offset probes |
| Depth test, all 8 functions | tested | depth functions |
| Alpha test, all 8 functions | tested | alpha test functions (CPU path ignores alpha test, hardware path used) |
| Blend: 9 source/dest factors, ADD | tested | blend probes, suite |
| Logic ops | tested (8 of 16) | logic ops, XOR |
| Scissor, user clip planes | tested | scissor, clip plane |
| Stencil (all ops, two-sided, wrap) | tested | stencil probes, suite |
| Fog LINEAR/EXP/EXP2 | tested | fog modes |
| Lighting, two-sided, colour material, spot | tested (spot untested) | lighting, two-sided, colour material |
| Texture 1D | tested (fixed today) | 1D texture, TexSubImage1D |
| Texture 2D: wrap, filters, env REPLACE/MODULATE/DECAL/BLEND/ADD | tested | wrap probes, env probes |
| Mipmaps, base-level clamp (`TEXTURE_MAX_LEVEL`) | mip tested; MAX_LEVEL **failing** | GLEngine passes the clamp, driver ignores it |
| Texture border | **failing** | driver treats border texels as image texels |
| TexSubImage1D/2D, CopyTexImage2D, CopyTexSubImage2D | tested | probes |
| CopyTexImage1D, CopyTexSubImage1D | untested | |
| Texture objects, priorities, residency | tested (residency result unchecked) | texture objects |
| texgen OBJECT_LINEAR, SPHERE_MAP, texture matrix | tested (sphere map: drawn only) | |
| texgen EYE_LINEAR | untested | |
| Display lists, vertex arrays, interleaved arrays | tested | |
| Selection, feedback | tested | |
| Pixel store, zoom, DrawPixels/ReadPixels, CopyPixels | tested (CopyPixels via suite) | |
| Pixel transfer scale/bias | **failing** | DrawPixels ignores scale and bias |
| Pixel maps | untested | |
| Draw/read buffer FRONT | **unsupported** (GL_INVALID_OPERATION from GLEngine) | single/back-buffer rendering only |
| Accumulation buffer | tested | |
| Evaluators, rasterpos, bitmaps | untested (GLEngine side) | |

## Extensions

Advertised today (driver mask): ARB_multitexture, texture_env_combine, texture_env_dot3, texture_env_add, texture_cube_map, texture_compression,
texture_mirrored_repeat, texture_border_clamp, texture_non_power_of_two, point_sprite, vertex_buffer_object; EXT_texture_rectangle, texture_env_add,
secondary_color, paletted_texture, texture_compression_s3tc, blend_color/minmax/subtract/func_separate/equation_separate, stencil_wrap, stencil_two_side,
separate_specular_color, texture_mirror_clamp, compiled_vertex_array, abgr, bgra; SGIS_texture_edge_clamp. Plus GLEngine's 24 fixed names.

| Extension | Status | Note |
|---|---|---|
| ARB_multitexture | tested | needs descriptor +0xae = 2; secondary-map textures narrower than 8 texels render black (open) |
| ARB_texture_env_combine | tested | ADD_SIGNED is an approximation by default (hardware has no exact form); exact behind `GLD_EXACT_ADD_SIGNED`, cost unmeasured |
| ARB_texture_env_dot3 | tested (CPU) | |
| ARB_texture_env_crossbar | **failing**, not advertised | hardware composite cannot read another unit's texel |
| ARB_point_parameters | accepted-only, not advertised | attenuation does not change point size |
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
| ARB_fragment_program | **failing** | program format not decoded; GLEngine advertises ARB shader names in its fixed list, so they cannot be removed |
| 3D textures | **unsupported** | GLEngine rejects the upload (0x501) |
| ARB_depth_texture / shadow | accepted-only (upload) | not implemented |
| ARB_multisample | pixel format offered only | no multisampling |
| SGI_color_matrix / ARB_imaging | **failing**, not advertised | |
| Register combiners, texture shaders, NV/ATI vendor bits | unsupported | no hardware path |
| Destination alpha | unsupported | chip is RGB565/1555, ALPHA_BITS 0 |

## Known limits of the CPU rasteriser path
Level-0 only for CPU textures, no multitexture on the CPU path, alpha test/fog/colour-mask nuances ignored on that path.

# GL_SCISSOR_TEST — DONE & HW-VERIFIED (via GLEngine's software-clipped geometry)

Implemented + HW-verified 2026-08-26 (Picture 90: a scissored quad shows white ONLY inside
a 305x407 box, left edge 101 px in, nearer the top -- clipped to glScissor(101,203,305,407)).

## The twist: scissor is NOT a hardware-scissor feature here
Unlike the reference Mesa DRI driver (which programs the mach64 SC_LEFT_RIGHT/SC_TOP_BOTTOM
hardware scissor for 3D), **Apple's GLEngine does all T&L *and clipping* in software** -- we
only rasterise.  When a primitive is clipped (GL_SCISSOR_TEST, or a triangle crossing the
view frustum), GLEngine:
  1. clips it in software (adds boundary vertices -- our 2-triangle quad came back as an
     n=3 triangle + an n=5 pentagon, i.e. 6 -> 8 verts),
  2. re-dispatches each clipped CONVEX POLYGON through the **elements-tris vector (dispatch
     slot 0x48)** -- NOT the flat DrawArrays slot (0x2c),
  3. as an **ARRAY OF VERTEX POINTERS**: v[0..n-1] are uint32 addresses, each pointing to a
     vertex in the usual layout (window x,y @ +0x00/+0x04, colour @ +0x30..+0x3c, NDC z @
     +0x18, s/t @ +0x80/+0x84).

So the mach64 hardware scissor is never used for GL scissor (SC_LEFT_RIGHT/SC_TOP_BOTTOM stay
wide-open).  Scissor "works" purely by our rendering GLEngine's already-clipped geometry
correctly.  This same path also fixes **frustum-clipped** triangles in general.

## How we found it (the dead-end that taught us)
1. GLD_SCISSCAN (tools/scisscan.sh): diffed the gli across a scissor-off vs scissor-on draw
   -> the box is stored verbatim at **gli+0x66f4=x, +0x66f8=y (GL bottom-left), +0x66fc=w,
   +0x6700=h, +0x6704 bit24 = GL_SCISSOR_TEST enable**.  (Unused by the render path now.)
2. Programmed SC_LEFT_RIGHT/SC_TOP_BOTTOM from that box -- spec-correct per the LT Pro RRG
   (SC_LEFT low / SC_RIGHT high, 14-bit signed; SC_TOP/BOTTOM 15-bit; inclusive) and
   byte-identical to the reference -- yet frame 2 drew NOTHING.
3. Instrumenting the draw showed the scissor-on quad arriving via render_triangles with
   n=3/n=5 and all-zero coords at +0x00/+0x04.  Raw-dumping revealed v was an array of heap
   POINTERS.  Splitting slot 0x48 off to its own handler (mach64_render_elt_tris) and
   dereferencing them gave clean box-boundary window coords -> the model above.

## Implementation
- `gld_dispatch.c`: the elements vectors now handle clipped geometry -- slot 0x48
  (SLOT_ELT_TRIS) -> `mach64_render_elt_tris`, 0x40 -> `mach64_render_elt_points`, 0x44 ->
  `mach64_render_elt_lines` (all were folded into the flat bodies / treated as dead).
- `gld_prims.c`:
  - `mach64_render_elt_tris`: treat v as n vertex pointers; deref each; feed to
    mach64_read_vertex (same y-flip/colour fold as the flat path); tessellate the convex
    polygon as a FAN (tri(0,i-1,i)), same winding as the source primitive.
  - `mach64_render_elt_points`: n pointers, one per surviving clipped point -> draw_point.
  - `mach64_render_elt_lines`: pointer PAIRS, each a clipped segment -> draw_line.
- `gld_texture.c`: the hardware-scissor sync block was REMOVED (GLEngine self-clips; the SC
  registers stay wide-open).  `mach64_set_scissor` in gld_state.c is now unused by the GL
  path (kept as a manual/2D HW-scissor entry point).

## Point/line clipping -- DONE & HW-VERIFIED (Picture 93, tools/mach64_clipptlntest)
GLEngine clips points/lines the same way it clips triangles and routes the survivors through
the elements vectors 0x40 (points) / 0x44 (lines) as vertex-POINTER arrays: a clipped point
survives unchanged (n = surviving points), a clipped line keeps its 2 endpoints moved onto
the box edge (n = 2 x segments, consecutive pointer pairs).  Verified: glScissor(300,250,
400,300) clips a full-width point row to the box x-span and trims crossing lines to the box.

### Gotcha found on the way: the drawable descriptor is unreliable
mach64_clipptlntest first came up BLANK because `gldAttachDrawable` read the drawable
descriptor's width/height (d[1]/d[2]) and GLEngine had handed it a stale/aliased pointer
(d[2] = ASCII "path", d[1] a garbage value that GREW between runs) -> ctx+0x1f4 height was
junk -> everything rendered offscreen.  FIX: for a FULLSCREEN context (type 0x36/0x5a) the
render target IS the live framebuffer, so take BOTH dims from the CRTC hardware -- width from
CRTC_OFF_PITCH pitch, height from CRTC_V_TOTAL_DISP (V_DISP[26:16]+1) -- via the kext
get_surface_size (now returns height in *hi).  The descriptor dims are used only as the
windowed fallback.  This is a general robustness fix, not point/line-specific.

## Related: FACE CULLING is also host-side (no driver code) -- HW-verified (Picture 101)
GL_CULL_FACE / glCullFace / glFrontFace are handled entirely by GLEngine's software geometry
pipeline, same as clipping: it drops the culled triangles BEFORE dispatch.  Proven with two
triangles (a CCW front + a CW back): cull-off dispatched render_triangles(n=6) = both; cull
GL_BACK dispatched render_triangles(n=3) = only the front one (the back triangle never reached
us).  So the driver needs NO cull code -- our setup engine drawing both windings is moot
because GLEngine only sends the surviving faces.  Cull state IS mirrored in the gli (enable =
gli+0x66ec bit8 / +0x66f0 bit8, via cullscan) but it's informational -- unused.  Tools:
mach64_culltest + cullscan.sh/culltest.sh.

## Caveats / follow-ups
- mach64_render_elt_tris carries no half-texel inset (emit_tri's texture path), so a
  *textured* clipped polygon samples raw texcoords -- fine for the common untextured case.

## Tooling
`tools/mach64_scissortest.c` + `scistest.sh` (default build = the HW verify).  `scisscan.sh`
(build -DGLD_SCISSCAN) re-scans the gli box offsets -- historical, the box is GLEngine's now.

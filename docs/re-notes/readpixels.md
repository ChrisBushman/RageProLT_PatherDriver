# glReadPixels — IMPLEMENTED + WORKING (2026-08-27)  (was: characterized limitation)

## RESULT: glReadPixels returns correct pixels (2026-08-27)
Our HW glReadPixels now works.  LIVE (readpixels.sh, -DGLD_DBUF):
`READPIX clear (80,80) = 64 128 191`, `READPIX quad (512,384) = 255 255 255`, glGetError 0 --
matches the expected clear (64,128,191) + white quad.  HOW: the per-renderer readback hook GLEngine
calls for glReadPixels is OUR OWN function -- `mach64_ctl08` (installed at GLEngine table[0x08] =
gc+0x78b0 in gldInitDispatch).  It was a no-op stub with the wrong (7-arg) shape; the real call is
`mach64_ctl08(rb_ctx, x, y, w, h, format, type, pixels)` with rb_ctx == our gld_context.  gld_render.c
now fills `pixels` by reading the current render target from the whole-VRAM CPU map (gc->regs) at
`target_off + (H-1-(y+row))*target_pitch*4 + (x+col)*4`, framebuffer ARGB8888 stored LE -> bytes
[B,G,R,A] -> RGBA out; drawable height H from gc+0x1f4; fence via mach64_flush_batch (+ the app's
glFinish).  Reversed via tools/mach64_rpdisp (reads [ctx+0x3b8] glReadPixels dispatch -> validation
GLEngine+0x2dc4c -> tail-calls [gc+0x78b0]).
REMAINING POLISH: only GL_RGBA/GL_UNSIGNED_BYTE handled (other format/type combos return without
filling -> add conversions, ideally via the standard path); alpha in clear region reads 0 (harmless);
larger w x h reads use the same per-pixel math (loop present) but only 1x1 verified so far.

## (historical) FOOTHOLD FOUND (2026-08-27), mid-reverse

Investigated 2026-08-26 (tools/mach64_readpixels + readpixels.sh): glReadPixels is a NO-OP for our
HW context; no gld* / kext / installed-render-slot fires (readback is internal to GLEngine).  Was
banked as a limitation.  2026-08-27 UPDATE (Path B gave us the GLEngine dispatch know-how): found
the actual dispatch site, so it is likely hookable after all -- see "## Dispatch foothold" below.

## Dispatch foothold (2026-08-27)
- Public `_glReadPixels` (libGL.dylib 0x90c7872c) tail-calls a PER-CONTEXT function pointer at
  **`ctx+0x3b8`**: `r12=[ctx+0x3b8]; r3=[ctx+0]` (the internal gc); then `func(gc, x,y,w,h, format,
  type, pixels)`.  This is a DIFFERENT table from the per-primitive render dispatch we hook (render
  slot 0x60 via gldUpdateDispatch) -- which is exactly why "no installed slot is hit" for readback.
- GLEngine installs `ctx+0x3b8` from a template table at **3 config-specific sites** (0xdeb08,
  0xe405c, 0xe95b0): each does `r11=[gc+0x7894]; r9=[gc+0x7890]; r2=[(r10+0x10000)+OFF]; stw r2,
  0x3b8(r9)` with OFF = 0xcb60 / 0x6da4 / 0x1090.  So there are 3 candidate readback impls; our HW
  renderer lands on the one that no-ops.  GLEngine imports `_glgProcessPixels` (libGLImage) = the
  format-conversion routine a real readback calls.
- ROADMAP to implement: (1) determine which of the 3 sites fires for our renderer (or read
  [ctx+0x3b8] live via a probe) -> the actual function; (2) disassemble it to see why it reads
  nothing for us (likely reads a software FB / surface field our HW path never populates -- vs the
  HW impl that reads VRAM); (3) EITHER make our surface/caps setup match the HW-readback path, OR
  install our OWN function at ctx+0x3b8 that reads our back-buffer VRAM (we have the whole-VRAM CPU
  map at gld_ctx+0xb8) at (x,y,w,h), runs the format/type conversion, writes `pixels`.  The
  signature to match: func(gc, GLint x,y, GLsizei w,h, GLenum format, GLenum type, void* pixels).

## Symptom
glReadPixels(x,y,1,1,GL_RGBA,GL_UNSIGNED_BYTE,px) leaves `px` COMPLETELY UNCHANGED (not
zeroed -- the caller's init bytes survive) after rendering a known clear+quad.  `glGetError()`
returns 0.  So GLEngine neither reads nor errors -- it silently does nothing.

## What fires (nothing driver-side)
- NO gld* driver function is invoked for the readback (only the normal render entries).
- The kext `read_buffer` selector 7 is NEVER called (wired it to a logging handler -- silent).
- No dispatch slot we install is hit.

## Reference cross-check (ATIRage128GLDriver, disassembled on the Lombard)
- The Rage 128 driver has **no gldReadPixels export** either -- readback is NOT a driver
  function; it lives inside GLEngine.
- gldCreateContext maps 4 IOConnectMapMemory regions to FIXED ctx offsets: type0(regs)->0xdc,
  **type1(surface/VRAM)->0xb8**, type2(control)->0xc0, type4(cmdbuf)->0xd0.  OUR gld_context
  matches exactly -- `regs`@0xb8 holds a valid whole-VRAM CPU mapping (allocSurface).  So
  GLEngine already has a working CPU pointer to our framebuffer.
- gldGetRendererInfo caps compared byte-for-byte: caps 0x8400/0x808000/0x1c01/0x80 all match.
  We WERE missing `buffer_modes` @0x0c (ref sets 0xd; we left it 0) -- fixed it to match, but
  glReadPixels still no-ops, so that field wasn't the gate.

## Conclusion
GLEngine reads the framebuffer for glReadPixels ITSELF (via the surface pointer / its own
representation) and, for our renderer, takes a path that does nothing.  glGetError==0 + no
hook = glReadPixels is dispatched to a per-renderer function pointer our renderer doesn't
provide, and GLEngine treats the absent hook as a successful no-op.  GLEngine (1MB) and the
OpenGL API stub (68KB) are both stripped to ~130 symbols; glReadPixels is an INTERNAL,
unsymbolized function reached through the ~300-slot GL dispatch table.  Finding + filling its
readback hook is a multi-session disassembly of Apple's closed rasterizer, with a real chance
the readback path structurally assumes a software framebuffer that HW renderers never
populate (i.e., no fillable hook exists).  Not worth it for a niche feature.

## What we KEPT from the investigation
- `gldGetRendererInfo` now sets `buffer_modes=0xd` (0x0c) to match the reference exactly --
  a correctness improvement independent of readpixels.
- tools/mach64_readpixels.c + readpixels.sh (the probe) are kept to document/re-check.
The kext `read_buffer` (selector 7) is back to the finish stub -- GLEngine never calls it.

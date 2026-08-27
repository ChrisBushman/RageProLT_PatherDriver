# glReadPixels — CHARACTERIZED STACK LIMITATION (not implementable driver-side)

Investigated 2026-08-26 (tools/mach64_readpixels + readpixels.sh).  glReadPixels is a NO-OP
for our hardware-accelerated context and cannot be fixed from the driver/kext side without
reverse-engineering Apple's closed GLEngine.  Banked as a limitation, like hw stencil/accum.

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

# glDrawPixels / glCopyPixels / glBitmap — dispatch scoping (2026-08-27)

Goal: implement the pixel WRITE/COPY ops the way we implemented glReadPixels (docs/re-notes/
readpixels.md) — by installing our own function at the renderer dispatch slot GLEngine calls.

## Dispatch map — CONFIRMED identical mechanism to glReadPixels
Disassembled the four entry points in libGL.dylib (@0x90c7xxxx).  Each fetches the current GL
context (r2) then TAIL-CALLS a fixed per-context function pointer, `func(gc=[ctx+0], args...)`:

| API | libGL @ | dispatch slot | call signature |
|-----|---------|---------------|----------------|
| glReadPixels | 0x90c7872c | **ctx+0x3b8** | (gc, x,y,w,h, format,type, pixels)  ← the one we cracked |
| glDrawPixels | 0x90c72e84 | **ctx+0x114** | (gc, width,height, format,type, pixels) |
| glCopyPixels | 0x90c72588 | **ctx+0xd4**  | (gc, x,y,w,h, type) |
| glBitmap     | 0x90c70cec | **ctx+0x1c**  | (gc, ...) |

These are all slots in GLEngine's per-context dispatch table (the ~300-slot table).  GLEngine
installs them in ONE setup routine: the DrawPixels install `stw r2,0x114(r9)` is at GLEngine 0xdd668,
right beside the ReadPixels install `stw r2,0x3b8(r9)` at 0xdeb08 (r9 = [gc+0x7890] = the dispatch
table; the fn ptr comes from a PIC template).  ReadPixels has 3 config-variant install sites
(0xdeb08/0xe405c/0xe95b0); DrawPixels has the matching pair (0xdd668/0xe2bbc).  So the pixel-write
ops dispatch EXACTLY like readback — the proven hook technique applies.

## The one open question (same as readpixels had)
[ctx+0x3b8]'s GLEngine impl is the validation front-end @0x2dc4c, which tail-calls the PER-RENDERER
readback hook gc+0x78b0 = our installed dispatch table[0x08] (mach64_ctl08).  For our HW renderer that
slot was a no-op we filled to read VRAM.  The DrawPixels/CopyPixels impls ([ctx+0x114]/[ctx+0xd4])
must be checked the same way: do they delegate to a per-renderer slot (gc+0x78xx) we can install, or
are they software-internal?  RESOLVE via the proven RUNTIME PROBE (extend tools/mach64_rpdisp.c):
read [ctx+0x114], [ctx+0xd4], [ctx+0x1c] live on an accelerated context, dladdr them -> the GLEngine
impl addresses, then disassemble each to find the gc+offset renderer hook it calls (the slot to fill).

## How each maps onto our engine (once the slot is known)
- **glCopyPixels** (framebuffer -> framebuffer): a VRAM->VRAM copy = our 2D blit (mach64_blit_surface),
  already proven.  The most natural fit; likely the easiest of the three.
- **glDrawPixels** (client pixels -> framebuffer): a CPU->VRAM upload at the raster position, honoring
  format/type (reuse the glReadPixels format/type conversion logic, reversed) + GL_UNPACK_* state +
  pixel zoom.  Writes VRAM via the whole-VRAM CPU map (gc+0xb8 / our render target), like readback did.
- **glBitmap** (1-bpp mask -> fragments): expand the bitmask; niche, lowest priority.

## Effort / risk
- Effort: comparable to the glReadPixels crack — dispatch mechanism is identical, tooling is in place,
  and the renderer primitives (blit / VRAM write) already exist.  ~1 session per op.
- Risks: the raster-position + pixel-zoom + UNPACK-state handling for glDrawPixels; confirming the
  impl actually delegates to an installable slot (vs a software path that assumes a SW framebuffer,
  the caveat that made us initially think readpixels was unhookable -- but readpixels turned out fine).
- Recommended order: glCopyPixels (blit, easy) -> glDrawPixels (upload) -> glBitmap (niche).

## Static RE confirmed (2026-08-27)
- **Dispatch template** (the fn-ptr array GLEngine copies into the per-context dispatch table) is at
  GLEngine file 0xf9a64+ : a dense array of impl pointers; readpixels entry @0xf9a64 = 0x2dc4c
  (validated).  Install idiom (per slot, in the setup routine ~0xdc000-0xe9800): `lwz r2,SRC(r10+
  0x20000); ...select...; stw r2, DST(r9)` where r9=[gc+0x7890] (dispatch base), DST=the ctx offset.
  Each pixel op has 3 install sites = 3 renderer-config variants (like readpixels):
  ReadPixels stw@ 0xdeb08/0xe405c/0xe95b0; DrawPixels 0xdd668/0xe2bbc/0xe8110;
  CopyPixels 0xdd42c/0xe2980/0xe7ed4; Bitmap 0xdcbc8/0xe21c4/0xe7718.
  (Static impl resolution beyond readpixels is unreliable -- the draw/copy installs use a different
  PIC r10 base than readpixels, so file-offset extrapolation lands past the table.  Use the probe.)
- **Renderer-hook table** (what an impl calls into our driver) = gc+0x78xx.  Map (GLEngine call sites):
  gc+0x78b0 = READBACK (readpixels; 3 sites incl. 0x2de08, right after validation 0x2dc4c) -- this is
  our installed dispatch table[0x08] = mach64_ctl08.  Other live slots: gc+0x78b4/78b8/78bc/78c8/78d0/
  78d4/78d8/78dc/78e0/78e4 (the render/draw/copy hooks).  gc+0x7898 = the common ctx (145 sites).

## glCopyPixels(GL_COLOR) — IMPLEMENTED + HW-VERIFIED (2026-08-27)
Probe (mach64_pxdisp) pinned it live: glCopyPixels -> GLEngine+0xcb5c (base 0x505000); the COLOR path
viewport-transforms the raster position into a scratch, then TAIL-CALLS gc+0x78b8 = our dispatch
table[0x10] = mach64_ctl10 with (raster_dst{x,y,z} floats, src_x, src_y, w, h, type).  ctl10 was a
ctl_dump stub -> glCopyPixels was a silent no-op.  IMPLEMENTED (gld_render.c mach64_ctl10): a Y-flipped
2D blit (mach64_blit_surface) src rect -> raster dst, read+draw both the current render target
(target_off).  KEY gotcha: drawable dims come from ctx+0x1f0/0x1f4 (like readback/clear_color), NOT
p->fb_h (only set for DB attaches -> 0 single-buffered -> negative Y-flip -> skip).  HW-VERIFIED
(tools/mach64_copypixels): render white quad (100,100)-(200,200), glRasterPos2i(400,400), glCopyPixels
-> the copy lands pixel-accurate at GL x[408..492] y[408..492], readback (450,450)=white.  The other
table[0x10] caller (0xca74) uses the identical (raster_dst, src rect, type) shape, so ctl10 serves it
too.  REMAINING: overlapping src/dst not handled (L->R T->B); GL_DEPTH copy (0x1c01 -> a different
GLEngine path, not ctl10) and read/draw-buffer selection are TODO.
(Tooling gotcha for future probes: tools that run under sudo leave root-owned binaries; a later
non-sudo rebuild silently fails and an old binary runs.  copypixels.sh now force-rm's them first.)

## glDrawPixels(RGBA/BGRA/RGB/BGR, GL_UNSIGNED_BYTE) — IMPLEMENTED + HW-VERIFIED (2026-08-27)
glDrawPixels -> GLEngine+0xe688 (dispatch ctx+0x114); after format/type validation the valid path
TAIL-CALLS gc+0x78b4 = our dispatch table[0x0c] = mach64_ctl0c with (raster_dst{x,y,z}, width, height,
format, type, pixels) -- the RAW client pixels (no GLEngine pre-convert), so ctl0c does the conversion.
ctl0c was a ctl_dump stub.  IMPLEMENTED (gld_render.c mach64_ctl0c): convert each client pixel to
native ARGB8888 (inverse of the readback/mach64_ctl08 path -- native LE bytes [B,G,R,A]) and write to
the render target via the whole-VRAM CPU map (c->regs), Y-flipped (image row 0 = bottom = raster y).
HW-VERIFIED (tools/mach64_drawpixels): a 64x64 image (left RED / right GREEN / bottom-8-rows BLUE)
uploaded at raster (300,300) reads back pixel-accurate with correct X and Y orientation.
PACKED TYPES (2026-08-27): added GL_UNSIGNED_INT_8_8_8_8 (0x8035) + _REV (0x8367) for RGBA/BGRA --
inverse of the readback pack (read the client uint32, extract FORMAT-order components c0..c3: 8_8_8_8
first-in-MSB, _REV first-in-LSB).  HW-verified: BGRA+_REV (native ARGB) -> green, RGBA+8_8_8_8 -> blue,
both pixel-accurate.  So glDrawPixels now matches readback's format/type coverage (RGBA/BGRA/RGB/BGR x
UBYTE/INT_8_8_8_8/_REV).
REMAINING limits (TODO): default UNPACK alignment=4 only (add gc UNPACK row-length/skip/swap state,
offsets TBD -- separate from the PACK offsets readback uses); pixel-zoom 1 only; GL_UNSIGNED_SHORT_5_6_5
etc. niche.
OBSERVED (separate, minor, NOT a glDrawPixels bug): a frame that does only glClear + glDrawPixels (no
geometry) shows an UNCLEARED (black) background -- our clear is lazy, fired on the first triangle
(frame_new -> mach64_clear_color in mach64_draw_triangle).  A geometry-less frame never triggers it.
Fix later: honour glClear even without a following triangle.

## Next step (needs one HW run)
Run tools/pxdisp.sh -> mach64_pxdisp reads [ctx+0x114]/[ctx+0xd4]/[ctx+0x1c] live + dladdr -> the
GLEngine impl offsets OUR renderer actually uses, and dumps the gc+0x78xx table.  Then disassemble
those impls to see which gc+0x78xx slot each calls (the one to fill).  For glCopyPixels the fill is a
framebuffer->framebuffer 2D blit (mach64_blit_surface) installed at that slot in gldInitDispatch.

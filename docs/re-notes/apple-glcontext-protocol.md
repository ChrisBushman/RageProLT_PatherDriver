# Apple IOATIRProGLContext protocol map (ATIRagePro.kext) — port spec

Reverse-engineered 2026-08-26 from `ATIRagePro.kext` (symbol-rich; scratch `ragepro.asm`/`.nm`).
This is the kernel GL user client Apple built for OUR chip but never shipped a bundle for.  Goal:
assess dropping our GL bundle onto it (Path B) instead of our own Mach64Accelerator.kext.  See
[[rageprolt-apple-kext-path]].

## Open  -- GL user client is TYPE 1 (CONFIRMED)
`IOServiceGetMatchingService("ATIRagePro")` -> `IOServiceOpen(svc, task, 1, &conn)`.
`IOATIRProAccelerator::newUserClient` accepts TYPE 0/1/2 (>2 -> err 0xe00002c2).  **TYPE 1 = the
IOATIRProGLContext** (confirmed: tools scratch mach64_gltype.c -- get_config succeeds only on
type 1, returns caps `04000000 00487000 00800000`; types 0/2 reject it 0xe00002c2).  All open
safely from ssh.  get_config is cold-safe (returns accelerator caps, skips the surface part when
no surface bound).

## Selector index table (IOATIRProGLContext methodDescs @ __TEXT.__const 0x174c0, 24B entries)
| idx | method | in | out |   | idx | method | in | out |
|-----|--------|----|----|---|-----|--------|----|----|
| 0 | set_surface        | 4 | 0 | | 8  | finish              | 0 | 0 |
| 1 | set_swap_rect      | 4 | 0 | | 9  | wait_for_stamp      | 1 | 0 |
| 2 | set_swap_interval  | 2 | 0 | | 10 | new_texture (struct)|   |   |
| 3 | get_config         | 0 | 3 | | 11 | delete_texture      | 1 | 0 |
| 4 | get_status         | 0 | 1 | | 12 | become_global_shared| 1 | 0 |
| 5 | get_surface_size   | 0 | 4 | | 13 | pageoff_texture     | 2 | 0 |
| 6 | get_surface_info   | 1 | 3 | | 14 | scale_surface       | 3 | 0 |
| 7 | read_buffer (struct)|  |   | |    |                     |   |   |
NO submit selector -- submission is via the command-buffer header + finish(8)/wait_for_stamp(9).

## Selectors (IOATIRProGLContext external methods -- the ABI surface)
The FULL standard Apple GL user-client set (same family our bundle was modeled on -> our gld*
already speaks most of it; only indices/argcounts to align).  Named methods + addrs:
- set_surface(id, eIOGLContextModeBits, m, m) @6440         -- bind a surface by ID + mode
- setCompatibleSurfaceMode(long*, mode, long) @641c
- set_swap_rect(l,l,l,l) @6700   -- **window rect: 4 shorts -> ctx+0x90/92/94/96 (THE geometry)**
- set_swap_interval(l,l) @6750    -> ctx+0x98/0x9a
- get_config(m*,m*,m*) @6798      -- renderer caps (our gldGetRendererInfo analog)
- get_status(m*) @6938
- get_surface_size(l*,l*,l*,l*) @6aa8
- get_surface_info(id, l*,l*,l*) @6b3c
- read_buffer(sIOGLContextReadBufferData*, m) @6c50   -- glReadPixels path (!)
- finish() @7070,  wait_for_stamp(stamp) @70c0         -- fences
- scale_surface(m,m,m) @710c
- new_texture(sIOGLNewTextureData*, sIOGLNewTextureReturnData*, m, m*) @71e4
- delete_texture(id) @73c8,  pageoff_texture(m,m) @758c
- become_global_shared(m) @7484,  remove_surface() @6314
Selector index table = `IOATIRProGLContext::start`'s static `methodDescs` (5-word IOExternalMethod
entries); exact indices still to be dumped, but they are the standard Apple order.

## Memory regions (clientMemoryForType, types 0-4 -> 5 regions)
5-way jump table.  Regions are registers / surface / control / command-buffer style (like our
type0 regs, type1 surface/VRAM, type2 control, type4 cmdbuf).  Type-1 handler does the
surface-bits + command-buffer work (mutex, IOATIRProSurface::convert_surface_bits, swap rect).

## Submission model -- THE key difference from our kext
NO `new_vertex_buffer`/submit selector.  Instead a shared **command buffer** with a
`VendorCommandBufferHeader`:
- allocAllContextBuffers(m) @7684, freeCommandBuffer @7914, freeAllContextBuffers @781c
- init_command_buffer_header(VendorCommandBufferHeader*, m, m) @7a64
- init_context_buffer_header(VendorContextBufferHeader*, m) @7a30
The bundle writes commands into the mapped command buffer, advances the header, and the kext
consumes them; fenced by wait_for_stamp.  (Our current path is per-batch new_vertex_buffer, a
synchronous selector.)  This is the main re-plumb.

COMMAND FORMAT -- RESOLVED (favorable): the command buffer body is an array of
**{register-offset, value} 32-bit pairs** -- i.e. exactly the mach64 register-write streams our
bundle already generates.  Proof: `ATIR32DContext::write_regs(uint *buf, uint count)` @0x143c8
loops `reg=buf[0]; val=buf[1]; reg&=0x3ff; stwbrx val -> MMIO[regbase+reg]; buf+=8` -- (reg,val)
pairs, byte-reversed for the LE hardware (our m64_cb_write_le does the same BE->LE swap).
`init_command_buffer_header(hdr, size)` @0x2bb0 zeroes a 0x20-byte header then stores the body
capacity `(size-0x20)/4` dwords at hdr+0x10; the command body starts at hdr+0x20.  So: 32-byte
VendorCommandBufferHeader (head/tail/stamp/flags) + body of (reg,val) pairs.  Submission =
advance the header; kext drains via write_regs (MMIO) / DMA; fence via wait_for_stamp.
=> Our register-generation layer maps almost directly; the port work is the buffer-header
protocol + selector/index alignment + Apple's 5 memory regions, NOT a foreign packet translation.

## Present
`ATIR3Surface::submit_swap_buffer(m, eDoSwap, IOATIRProGLContext*)` @15838 /
`submit_flip_buffer(m, IOATIRProGLContext*)` @15c00 -- the SURFACE owns swap/flip.  Surface
geometry via `IOATIRProAccelerator::find_surface_for_id(id)` @4bd0 + shape_surface / alloc_
surface_buffer.

## clientMemoryForType regions + MANDATORY setup sequence (learned the hard way)
clientMemoryForType(type 0..4) region map for the GL context:
- type 0 -> accel+0xa8  (shared accelerator region, registers-ish)
- type 1 -> ctx+0xb8    (surface/command region) -- REQUIRES a bound surface (ctx+0x10c);
  does alloc_surfaces_pageq + a mutex block-retry spin + init_command_buffer_header
- type 2 -> ctx+0xd0    (context buffer) -- init_context_buffer_header
- type 3 -> accel+0xac  (shared accelerator region)
- type 4 -> ctx+0xa4    (lazily-allocated IOBufferMemoryDescriptor)

**start() zeroes ctx+0xb8 / ctx+0xd0 / ctx+0x10c at open.** Those buffer descriptors are NULL
until the setup sequence allocates them, and the handlers immediately deref them
([ctx+0xd0]->vtable->call).  So **mapping ANY region before setup NULL-derefs -> KERNEL PANIC**
(this hard-locked the Lombard when a probe blindly mapped all regions of a freshly-opened
context).  set_surface(id): find_surface_for_id(id) resolves the surface (NULL -> err 0x2c2, so
the ID must ALREADY exist), then binds it at ctx+0x10c/0x114/0x118 and allocates the buffers.

REQUIRED sequence: a surface must already exist (ID from CGS/WindowServer -- the 0x8013 handle
in the drawable descriptor -- or IOATIRProAccelerator::new_surface) -> open GL context ->
get_config -> **set_surface(id)** -> set_swap_rect(x,y,w,h) -> map command/context buffers (now
non-NULL) -> write {reg,val} pairs + advance header -> submit -> wait_for_stamp.

**No cheap standalone hello-world exists** -- the context is inert without a bound surface, and
the surface ID must come from CGS/GLEngine or a reversed new_surface.  Validating Path B means
driving the FULL sequence with a REAL surface: best via GLEngine (an AGL app hands our bundle
the surface ID -> effectively starting the bundle port, but safe) or a fuller standalone that
also creates a surface.  Live probing cold PANICS -- reverse offline, drive in-sequence only.

## Surface creation/registration (type-2 IOATIRProSurface user client)
Phase 2 found set_surface(drawable-id) -> 0x2c2: Apple's accelerator has NO surface registered
for our drawable's CGS id (find_surface_for_id @0x4bd0 walks accel+0x5c list, next surf+0x90,
match by id -> empty).  Because no RagePro GL bundle ever shipped, nothing created a GL surface
on this accelerator.  So OUR bundle must create/register it via the type-2 surface client -- it
is Apple's STANDARD IOAccelSurface protocol (types eIOSurfaceModeBits / IOAccelDeviceRegion /
eIOAccelSurfaceShapeBits are Apple's public IOAccel types).  IOATIRProSurface methodDescs @
__TEXT.__const 0x18420 (24B entries):
| idx | method | idx | method |
|---|---|---|---|
| 0 | surface_read_lock_options | 8 | set_scale |
| 1 | surface_read_unlock_options | 9 | **set_shape(shapeBits, region)** |
| 2 | get_state | 10 | surface_flush |
| 3 | surface_write_lock_options | 11 | surface_query_lock |
| 4 | surface_write_unlock_options | 12 | surface_read_lock |
| 5 | surface_read | 13 | surface_read_unlock |
| 6 | set_shape_backing (in4+struct) | 14 | surface_write_lock |
| 7 | **set_id_mode(id, mode)** (in2) | 15 | surface_write_unlock |

Surface-creation sequence: open type-2 -> set_id_mode(id=CGS 0x800f, mode) [registers it in the
accel list] -> set_shape(shapeBits, IOAccelDeviceRegion=window rect) [geometry+VRAM] -> then the
GL context set_surface(0x800f) RESOLVES -> map buffers -> submit -> present via
ATIR3Surface::submit_swap_buffer.  CAUTION: set_shape allocs surface VRAM (alloc_surfaces) --
drive carefully, panic-prone if args wrong / WS live.

## Port spec (Path B)
- KEEP: all our GL feature logic (read_vertex, emit_triangle, texture, flat/cull/fog/scissor/
  clears) -- it emits mach64 register streams and is the bulk of our work.
- CHANGE: the bundle's kext-interface layer -> Apple's selectors + the command-buffer-header
  submission model + Apple's 5 memory regions.
- GAIN (free): windowed surface handshake (set_swap_rect + find_surface_for_id), engine
  coexistence (Apple's kext owns the device), proper present/scanout, VRAM/GART/texture alloc.
- DROP: our Mach64Accelerator.kext entirely.
- Main risk/unknown: the command-buffer FORMAT (raw regs vs vendor packets) -- next drill-down.

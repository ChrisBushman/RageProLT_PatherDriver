# Surface-backed windowed present — implementation plan (Y-win)

Handoff doc for the NEXT session. Goal: fix the intermittent windowed **white-out** by making our GL
content the window's WindowServer-composited surface, instead of blitting into the live front (which races
WS). User chose **Y-win**: implement Apple's `IOAccelSurface` protocol in OUR `Mach64Accelerator.kext` so
WindowServer binds to our surface user-client and composites our content (single accelerator, no
coexistence lock). See [[rageprolt-windowed]], [[rageprolt-app-stubs]], and the deep RE in
`apple-glcontext-protocol.md` + `windowed.md`.

## Why (root cause — confirmed on HW 2026-08-28)
Our windowed present blits the rendered board straight into the LIVE front (scanout) at the CGS-resolved
window rect. WindowServer ALSO paints that window, and because our GL region is NOT registered with WS as an
accelerator-owned surface, WS composites the window's EMPTY Quartz/GL backing (white) over the same front
pixels. Last writer before scanout wins → WS repaint = **white**, our blit = board. Static windows keep our
last blit (WS doesn't recomposite an idle window) — why simple probe apps looked "solved" — but Chess's
continuous redraw + drags + startup-activate make WS recomposite and expose the race. Instrumented proof:
white presents DID draw geometry (`frame_new=0`), `mach64_submit` waits-for-idle so the board is genuinely
in `back` before the blit; resolver returns a new position every call for the same window (drag tracking).
Full write-up in `windowed.md` §"INTERMITTENT WHITE-OUT".

**The fix:** register the window's surface via `IOAccelSurface` so WS punches it through (stops painting it)
and composites the backing we render into. Apple's `IOATIRProSurface` (in `ATIRagePro.kext`) is the exact
reference implementation — fully disassembled in scratch `ragepro.asm`/`ragepro.nm`.

## Architecture — what to add to the kext
Today `Mach64Accelerator.kext` = `Mach64Accelerator` (IOService, owns registers/DMA/present) +
`Mach64GLUserClient` (the type-1 GL context our bundle opens: `new_vertex_buffer`, `wait_for_stamp`,
`set_surface`, `clientMemoryForType`, …). We ADD a second user client:

- **`Mach64SurfaceUserClient`** — implements the standard Apple `IOAccelSurface` external-method protocol
  (the SAME one `IOATIRProSurface` exposes). WindowServer opens THIS to register+composite the window's
  GL surface. Apple opens it as **type 2** (newUserClient type 2). We must:
  1. In `Mach64Accelerator::newUserClient`, accept a surface type and return a `Mach64SurfaceUserClient`.
  2. Implement its selector table + `clientMemoryForType` (surface backing map for `surface_write_lock`).
  3. Allocate the surface backing in VRAM (from our heap) on `set_shape`, blit it on `surface_flush`.

Our GL bundle's windowed present then changes from "blit our private back → live front" to "render into the
surface backing (or blit our back → surface backing) + surface_flush", so WS composites it. The 3D path
(our DMA) is unchanged; present becomes surface-routed.

## Apple IOAccelSurface reference (from ragepro.nm / apple-glcontext-protocol.md)
`IOATIRProSurface` methodDescs @ `__TEXT.__const 0x18420` (24-byte IOExternalMethod entries). Selector idx:

| idx | method | notes |
|----|--------|-------|
| 0 | surface_read_lock_options | |
| 1 | surface_read_unlock_options | |
| 2 | get_state(eIOAccelSurfaceStateBits*) | |
| 3 | surface_write_lock_options(m, IOAccelSurfaceData*, m) | |
| 4 | surface_write_unlock_options | |
| 5 | surface_read(IOAccelSurfaceReadData*, m) | |
| 6 | set_shape_backing(shapeBits, j, m, IOAccelDeviceRegion*, m) | |
| 7 | **set_id_mode(id, eIOSurfaceModeBits)** in=2 | REGISTERS surface in accel list (the punch-through) @0xaf48 |
| 8 | set_scale(m, IOAccelSurfaceScaling*, m) | |
| 9 | **set_shape(eIOAccelSurfaceShapeBits, m, IOAccelDeviceRegion*, m)** | geometry + backing VRAM alloc @0xc5ec |
| 10 | **surface_flush** | present = clipped 2D PIO blit (build_swap @0x150f8) |
| 11 | surface_query_lock | |
| 12 | surface_read_lock | |
| 13 | surface_read_unlock | |
| 14 | **surface_write_lock(IOAccelSurfaceData*, m)** @0xc208 | maps backing to userspace |
| 15 | surface_write_unlock | |

`IOATIRProSurface` method addrs: set_id_mode @0xaf48, get_state @0xb440, surface_read @0xb558,
surface_lock_options @0xbb64, surface_read_lock(_options) @0xc140/0xc184, surface_write_lock(_options)
@0xc1c4/0xc208, set_shape @0xc5ec, set_shape_backing @0xc62c, set_scale/set_scaling @0xcb70/0xcc2c,
convert_surface_bits @0xd1d8. Surface geometry helpers: `ATIR3Surface::shape_surface` @0x14d84,
`build_swap` @0x150f8 (the reference blit stream), `submit_swap_buffer` @0x15838.

**`surface_write_lock` returns an `IOAccelSurfaceData`** (HW-confirmed, mach64_writetest.c):
`+00 address` (userspace-writable backing ptr), `+10 pitch`, `+14 w`, `+18 h`, `+1c bpp`, `+48 idx`.

**Present is engine-independent PIO 2D blit** (`build_swap` emits `{reg,val}` pairs: DST_OFF_PITCH 0x100,
DST_X_Y 0x10c, DST_WIDTH_HEIGHT 0x118, SRC_OFF_PITCH 0x180, DP_SRC/DP_MIX 0x1dc/0x1e0, per-clip-rect loop
over an IOAccelBounds list). We ALREADY have `mach64_blit_surface` doing this exact sequence — reuse it for
`surface_flush`.

Public Apple types to pull layouts for: `IOAccelSurfaceData`, `IOAccelDeviceRegion`, `IOAccelBounds`,
`eIOAccelSurfaceShapeBits`, `eIOSurfaceModeBits`, `eIOAccelSurfaceStateBits` (IOKit/graphics headers of the
era; also inferable from ragepro.asm usage).

## THE make-or-break open question (resolve FIRST next session)
**How does WindowServer discover+bind the accelerator's `IOAccelSurface` user client on Panther, and will it
bind to OURS?** Apple owns the display's IOFramebuffer/scanout; WS's IOAccel layer (CoreGraphics) finds "the
accelerator for the display" and opens its surface client. If WS only matches Apple's accelerator (via the
framebuffer↔accelerator association), it won't open ours no matter how correct our client is. This is the
gating risk for Y-win.
- STEP 0 (cheap, decisive): add `Mach64SurfaceUserClient` as newUserClient type 2 with the selector table
  wired but methods stubbed to LOG (which process opened it, which selectors, args). Install, run a windowed
  GL app, and check the kext log: **does WindowServer (or GLEngine on WS's behalf) open our surface client?**
  - If YES → implement the methods for real (set_shape backing, write_lock map, surface_flush blit).
  - If NO → RE how WS associates an accelerator with the display (IOAccelerator matching / IOFramebuffer
    `IOAccelTypes`), OR reconsider: our bundle itself opens the surface client (GLEngine→our bundle→our
    surface client) and the "WS composites it" property comes from set_id_mode registering the CGS id so WS
    punches through. i.e. the surface client may be opened by the APP side (our bundle), not WS — WS just
    honors the registered CGS surface. Verify which by checking who opens IOATIRProSurface in a working
    Apple-driver capture / by reasoning from the GL bundle→surface flow in apple-glcontext-protocol.md
    (userspace trigger chain there is bundle-side: surface_flush called by the driver, not WS).

Working hypothesis (favor this): the surface client is opened by the DRIVER (our bundle, per the
apple-glcontext-protocol trigger chain `surface_flush ← flush_surface ← the driver`), and `set_id_mode(cgs_id)`
is what makes WS treat the region as accelerator-owned. If so, Y-win is: our BUNDLE opens our kext's surface
client, registers the drawable's CGS surface id (d[2]) via set_id_mode, set_shape to the window rect, renders
into the write_lock'd backing, surface_flush. No WS-side binding needed — WS honors the CGS registration.
STEP 0 above disambiguates definitively.

## Implementation stages (each independently testable)
1. **STEP 0 / probe** — `Mach64SurfaceUserClient` (type 2), selector table wired, all methods LOG-only.
   Confirm who opens it and the call sequence for a real windowed GL app. (No behavior change; safe.)
2. **set_id_mode + set_shape** — register the CGS surface id; alloc a VRAM backing (our heap /
   `mach64_winbuf_alloc`) sized to the window rect; store geometry. Reference: IOATIRProSurface @0xaf48 /
   @0xc5ec, alloc_surfaces.
3. **surface_write_lock/unlock** — `clientMemoryForType` maps the VRAM backing to userspace; return the
   `IOAccelSurfaceData` (address/pitch/w/h/bpp). Reference @0xc208; struct from mach64_writetest.
4. **surface_flush** — 2D PIO blit backing → front at the surface rect, clipped to the region list. Reuse
   `mach64_blit_surface` (already the build_swap sequence). Reference build_swap @0x150f8.
5. **Bundle rewire** — `gld_entry.c` windowed activation opens the surface client + set_id_mode(d[2]) +
   set_shape(window rect); `gld_render.c mach64_dispatch_render_ctl` windowed branch renders into (or blits
   our back into) the surface backing, then surface_flush INSTEAD OF the live-front blit. Keep the CGS
   resolver only for the rect passed to set_shape (WS owns the on-screen position now → no more jitter/smear).
6. **Multi-window** — one surface client + backing per context/window (we already have per-context back
   buffers + winpool). Verify two GL windows compose independently.

## Files / tools
- KEXT: `kext/Mach64Accelerator.{cpp,h}` (newUserClient + surface alloc/blit), new
  `kext/Mach64SurfaceUserClient.{cpp,h}`, `kext/Mach64Accelerator.cpp` present/blit helpers, selector
  index enum in a shared `common/mach64_abi.h`. Current GL client = `kext/Mach64GLUserClient.{cpp,h}`
  (pattern to mirror: `getTargetAndMethodForIndex`, `clientMemoryForType`, IOExternalMethod table).
- BUNDLE: `gldriver/gld_entry.c` (windowed attach), `gldriver/gld_render.c` (present), `gldriver/gld_cgs.c`
  (resolver — now only feeds the set_shape rect).
- RE refs (scratch `/private/tmp/.../scratchpad`): `ragepro.asm`, `ragepro.nm`, `ragepro.const`;
  `ogldis.py` (PPC disassembler). Proven surface probes: `tools/mach64_writetest.c` (write_lock+flush→red
  square on scanout), `tools/mach64_fspresent.c`, `tools/mach64_flushtest.c`, `tools/mach64_gltype.c`.
- Apple selector/mem-region map: `docs/re-notes/apple-glcontext-protocol.md` (authoritative).

## Risks / notes
- **WS binding** (above) is the #1 unknown → STEP 0 resolves it before any real implementation.
- `set_shape` allocs VRAM and is **panic-prone with args wrong / WS live** (per apple-glcontext-protocol).
  Drive carefully; validate offsets against IOATIRProSurface before first HW run.
- Keep FULLSCREEN present unchanged (the vblank-gated back→front blit works; only windowed changes).
- Our kext already coexists with Apple's framebuffer (Apple owns 2D/scanout) — surface_flush is a 2D PIO
  blit to the front WE issue, so no CRTC touch, consistent with the fullscreen fix.

## State at handoff (2026-08-30)
DONE & HW-verified this session: Chess hang fixed (`gldGetTextureLevelInfo` proxy-width via a7 — Chess
renders the full board), `gldCreateShared`/`gldDestroyShared` implemented (was a wrongly-stubbed real fn),
full gld* stub audit ([[rageprolt-app-stubs]]). White-out root-caused + documented; diagnostic
instrumentation removed (render path back to the plain blit). NOT started: any surface-client code. Resume
at STEP 0.

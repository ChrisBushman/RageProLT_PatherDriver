# Windowed video modes — feasibility (probed 2026-08-26)

Tool: `tools/mach64_wintest.c` + `tools/wintest.sh`.  Two probes established where windowed
support stands.  Bottom line: **windowed is NOT gated on the full device takeover — engine
coexistence with a live WindowServer already works.**

## RESOLVED (2026-08-27): the surface handshake works via CGS
The "one remaining blocker" below (resolving the drawable's surface ID -> window bounds) is
SOLVED for the common single-window case and HW-verified.  `gld_cgs.c`
(`mach64_cgs_resolve_bounds`) resolves it from userspace via the private CGS API
(`CGSGetWindowList`/`CGSGetSurfaceList`/`CGSGetScreenRectForWindow`/`CGSGetSurfaceBounds`) --
Apple's WindowServer, NOT the framebuffer and NOT the kernel surface manager (so no Path B needed).
`gldAttachDrawable` calls it FIRST (env `MACH64_WIN_RECT` is only a manual fallback if it returns 0).
HW-verified (wintest, driver log): GLEngine drawable-id `sid=0x811b` -> CGS `win 64 surf 0x101` ->
screen `(120,80) 400x300`, exactly matching the window; `[win]` armed the blit there and the spinning
triangle rendered in place with the desktop live.  The env fallback code path was NOT taken (CGS
returned 1).  RESIDUAL GAP (narrow): MULTIPLE simultaneous GL windows -- the resolver picks the first
window that has a surface (GLEngine's `0x80xx` drawable-id and CGS's `0x101` surface-id are different
namespaces, so it can't ID-match to disambiguate).  Single-window apps work today.

--- original probe notes (2026-08-26), kept for history: ---
The ONE remaining blocker (at the time) was the surface handshake (resolving the drawable's surface
ID -> window bounds).

## Probe 1 — safe descriptor capture (no swap)
Bound an ACCELERATED double-buffered AGL context to a normal 400x300 Carbon window (content at
120,80), no rendering/swap.  Results:
- `GL_RENDERER = "ATI Rage LT Pro (mach64)"` — **our driver IS selected for a windowed
  accelerated context.**  No pixel-format / renderer-selection work needed.
- Attaches as `gldAttachDrawable type=0x50` (same token as atlantis game mode).
- **The drawable descriptor contains NO window geometry.**  For a known 400x300 @ (120,80)
  window, `descdump[0..15] = 0000800b 0000005c 00000102 0 0 0 ...` — none of 0x190/0x12c/0x78/
  0x50 appear.  Word [0] is a surface-ID/handle (0x800b, 0x8013 — increments per run), [2] a
  near-constant type (0x102/0x103), rest zero.  GLEngine's own `gli[0x789c]` descriptor is
  zeros too.  (This also DISPROVED the earlier fullscreen guess that d[1]/d[2]=441/258 were a
  surface size — with a known window those words are just the ID/type, not dimensions.)
- Conclusion: GLEngine expects the DRIVER to resolve the surface ID -> window bounds via the
  WindowServer/CGS surface API.  Fullscreen dodged this with CRTC dims (whole screen); windowed
  has no such shortcut and there is no geometry in the descriptor to reverse.

## Probe 2 — coexistence (render + blit into the live desktop)
Injected the window bounds via `env MACH64_WIN_RECT="x,y,w,h"` (set by the tool from
GetWindowBounds).  Driver windowed path (gld_entry.c + render_ctl, gated on the env var):
render the w x h scene into the back buffer's top-left, then `mach64_blit_surface` the window
rect into the LIVE front buffer at (x,y) each swap -- **NO CRTC page-flip**, so the desktop
stays live and the WindowServer keeps compositing.  A spinning RGB triangle rendered **cleanly
in the window for ~4s with the desktop intact, no hard lock, clean teardown.**
- **Engine coexistence with a live WindowServer WORKS** for the windowed blit path.  The scary
  unknown (would sharing the engine with the WS lock the machine?) is resolved: it does not.
- Note the atlantis intermittent HARD LOCK was in the FULLSCREEN page-flip path (CRTC flip +
  big DMA) -- a separate, intermittent race, NOT a fundamental can't-coexist problem.  The
  windowed blit path (smaller, no flip) coexists fine.

## What windowed needs now (short list)
1. ~~**Surface handshake**~~ **SOLVED (2026-08-27)** -- see below.
2. Everything else is proven: renderer selection, windowed render+blit, WS coexistence.

## Surface handshake -- SOLVED via CGS (2026-08-27)
The drawable surface-ID -> window bounds resolution is now AUTOMATIC, in the bundle, via the private
CoreGraphics CGS API (userspace, no kext).  `gldriver/gld_cgs.c` `mach64_cgs_resolve_bounds()`:
- `_CGSDefaultConnection()` -> the app's connection.
- `CGSGetWindowList(cid, cid, ..)` -> the app's windows (1 for a single-window GL app).
- per window `CGSGetScreenRectForWindow` (screen FRAME rect) + `CGSGetSurfaceList` + per surface
  `CGSGetSurfaceBounds` (WINDOW-RELATIVE bounds).
- **screen content = frame.origin + surface.origin ; size = surface size.**
  Validated: frame (120,58 400x322) + surface (0,22 400x300) = content (120,80 400x300).
NOTE the CGS surface id (0x101/0x102) is a DIFFERENT namespace from GLEngine's drawable-id (0x80xx),
so we do NOT match by id -- single GL window is unambiguous (first window with a surface).  Wired in
gld_entry.c (CGS first, MACH64_WIN_RECT env as fallback); gates windowed on a real sub-screen/offset
window so fullscreen keeps the CRTC page-flip path.  Bundle links -framework ApplicationServices.
LIVE (mach64_wintest): `[cgs] sid=0x810f -> win 108 surf 0x102 -> screen (120,80) 400x300` +
`[win] WINDOWED 400x300 at (120,80)`, triangle rendered -- env var NOT consulted (CGS resolved).
REMAINING POLISH: multi-window disambiguation (currently first-window-with-surface; fine for single-
window apps -- could match by render size later); the [cgs] enumerate probe lives in
tools/mach64_cgstest.c + mach64_wintest.c.  See [[rageprolt-windowed]] / [[rageprolt-pathb-commit]].

## INTERMITTENT WHITE-OUT — root cause CONFIRMED (2026-08-28), fix in progress

Live-front blit-present flickers to all-white under real apps (Chess: Pictures 6/8 at the same minute —
identical instance, one all-white, one perfect board). Instrumented `mach64_dispatch_render_ctl` +
`gld_cgs`; findings:
- Presents that go white DID draw geometry (`frame_new=0`), and `mach64_submit` waits-for-idle so the
  board is genuinely in `back` before the blit. So it is NOT a clear-only present nor a geometry race.
- The CGS resolver returns a DIFFERENT screen position every call for the SAME window (win 153/surf 0x10a):
  (85,74)→(115,103)→(88,96)→(116,102)→(543,157)→… — it tracks a window being dragged, but our live-front
  blit chases it a frame behind, so the board smears across the desktop during a move.
- Even STATIC (chess4: 2 presents, no interaction) can end white.

**Mechanism:** our windowed present blits the rendered board straight into the LIVE front (scanout) at the
resolved window rect. But WindowServer ALSO paints that window — and because our GL region is NOT registered
with WindowServer as an accelerator-owned surface, WS composites the window's EMPTY Quartz/GL backing
(white) over the SAME front pixels. Whoever writes last before scanout wins: WS repaint → white, our blit →
board. Static idle windows keep our last blit (WS doesn't recomposite an unchanged window) — which is why
simple probe apps looked solved — but Chess's continuous redraw + drags + the startup activate make WS
recomposite, exposing the race.

**Why Apple doesn't race (from apple-glcontext-protocol.md):** Apple's present is ALSO a PIO 2D blit to the
front, but the surface is REGISTERED with WindowServer via `IOATIRProSurface::set_id_mode(cgs_id, mode)` —
that marks the window's content region as accelerator-owned so WS STOPS painting it (punch-through) and the
driver's `surface_flush` blit is the sole writer. No registration on our side => WS overpaints => white.

**Fix = surface-backed present (user-chosen):** make our content BE the window's registered, WS-composited
surface so WS never overpaints it. The crux is registering the window's existing CGS surface region as
accelerator-owned. Two implementation paths:
- (Y-win) our Mach64Accelerator.kext implements the IOAccelSurface user-client protocol WS binds to (single
  accelerator, no coexistence; larger kernel work).
- (B-win) drive Apple's already-implemented `IOATIRProSurface` (ATIRagePro.kext) JUST for surface
  register+present — `set_id_mode`+`set_shape`+`surface_write_lock`+`surface_flush` are PROVEN on HW
  (mach64_writetest/fspresent put our pixels on the real scanout, engine-independent PIO). Reintroduces the
  two-accelerator coexistence concern, mitigated by serializing our-3D-DMA → idle → Apple-PIO-flush.
FIRST RE STEP: determine whether a LIVE window's existing CGS surface id (the drawable's d[2], e.g. 0x10a)
can be marked accelerator-owned so WS stops painting it — via Apple's set_id_mode on that id, or a pure-CGS
SPI. That decides Y-win vs B-win. (Diagnostic present/resolver instrumentation has been removed; the render
path is back to the plain blit.)

## What the full device takeover is (still) for
A formal `IOFramebuffer` driver that fully owns the LT Pro is NO LONGER a windowed
prerequisite.  It remains valuable for: fixing the intermittent FULLSCREEN page-flip hard-lock,
being a real installable driver, and robust multi-app use -- a separate, optional goal.

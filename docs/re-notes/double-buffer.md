# Double buffering — RE status & the slot-0x60 wall

## What works (committed, gated `-DGLD_DBUF`)
- **2D fill + screen-to-screen blit engine** (`mach64_fill_rect`, `mach64_blit_surface`,
  `SRC_*` regs) — verified (Picture 22).
- **Render-to-back + present pipeline** — verified end to end with the *forced* path
  (single-buffer pixel format + forced dbuf): four textured quads on clean black,
  matching single-buffered (Picture 24).
- **DB pixel format** — `aglChoosePixelFormat(AGL_DOUBLEBUFFER)` accepted,
  `AGL_DOUBLEBUFFER=1`.  Gate recovered: **`node[0x0c]` bit 3** (set when
  `gldChoosePixelFormat` sees `kCGLPFADoubleBuffer`=token 5).  `gldCreateContext`'s
  caps decode reads `f0c` bit 3 → caps `0x400` (double) vs `0x800` (single).

## The wall: GLEngine routes a DB context to dispatch slot 0x60
With a real DB pixel format, GLEngine dispatches the app's geometry **only** to
dispatch **slot 0x60** (`mach64_dispatch_render_ctl`) — the per-primitive slots
`0x18-0x3c` and control slots `0x00-0x14` never fire.  Slot 0x60 args (per frame,
`tools/mach64_dbufanim`): `r4`=state buffer (const across frames though the geometry
moves → not vertices; no heap ptr inside), `r5`=4/6/8, `r6`=0xa8, `r7`=0xc03,
`r8`=a GLEngine **function pointer** (PPC code).  Our 0x60 is a stub → **black**.

## Reference-driver disassembly (ATIRage128GLDriver, via capstone PPC)
`__text`: file offset == vaddr.  gldInitDispatch = `sub_60a60`; inner
gldUpdateDispatch = `sub_60ba8`.  Recovered vtable installs:

| slot | reference fn | note |
|------|------|------|
| 0x00 | 0x4104 | |
| 0x04 | 0x61a20 | flush |
| 0x08 | 0x61df8 | |
| 0x0c | 0xcf68 | |
| 0x10 | 0xb3d8 | |
| 0x14 | 0x985c | |
| 0x18-0x48 | many (0x2ee20, 0x12edc, …) | per-primitive render vector, **variant-selected** by ctx+0x2c |
| 0x4c | 0xe2e4 | complete/submit vertex buffer |
| 0x50 | 0xf028 = `li r3,0; blr` | stub |
| 0x54 | 0xf030 = `blr` | returns r3 |
| 0x58/0x5c | 0xd158/0xd500 | gldFinish/gldFlush |
| 0x60 | **0x60a24 = `blr`** | **NO-OP** |
| 0x64 | 0xd030 | real buffer-mgmt (reads ctx+0xb0/0xb4) |
| 0x70 | 0x60a58 = `li r3,0; blr` | stub |
| 0x74/0x78/0x7c | 0x683bc/c4/cc = `li r3,0; blr` | stubs |

**Key deduction:** the reference's slot 0x60 is a *no-op*, so the reference renders
a DB context through the **primitive slots**, same as single-buffered.  So routing
our DB context to 0x60 is a MALFUNCTION, not the intended DB path.

## Hypotheses tested and RULED OUT
1. **Missing extra vtable slots** (0x50/0x54/0x64/0x70-0x7c): installed matching
   stubs → **no change**, still routes to 0x60.
2. **Slot 0x60 return value** (reference returns r3=ctx; ours returned void):
   made ours return ctx → **no change**.
3. **Context caps** (`ctx+0x2c`): forced DB caps to `0x844` — *identical to a
   working single-buffer context* — → **still routes to 0x60**.

## Conclusion
GLEngine decides "route to 0x60" from the **pixel format being double-buffered**
directly, independent of our dispatch table and context caps.  The reference's
no-op 0x60 only works because its DB **surface** is set up so GLEngine renders it
normally.  So the real gate is the **double-buffered surface contract** — GLEngine
needs a proper back-buffer surface (likely a second kext surface handle + the DB
attach protocol), which lives INSIDE GLEngine.  Cracking it needs disassembling the
**GLEngine framework** (0x9xxxxxxx, stripped) — beyond the reference-driver RE.

## Surface contract RE (2026-08-25, ref gldAttachDrawable @0x4f38)
Disassembled the reference's gldAttachDrawable.  It handles THREE drawable types,
branching on r4: 0x50 (window), 0x5a (fullscreen), **0x36 (back-buffer attach)** --
and our DB context attaches as **type 0x36**, which we wrongly handle like 0x5a
(bind the front framebuffer).  Differences of the 0x36 path:
- Caps gate: 0x5a requires `fmt_caps & 0x20` SET; 0x36 requires it CLEAR
  (`andi. r,0x2c(ctx),0x20; bne fail`).  (caps 0x20 = set when node f08 bit1 clear.)
- Calls the surface-setup sub_a524(ctx, &ctx+0x2c, r5) with r5=0 (vs r5=drawable for
  0x5a).  sub_a524 with r5=0 sets bit1 in the object at *ctx+0x14; with a drawable it
  derives a surface-type code from drawable[0xc] (0x84f5/0x8513/-0xde0 range ->
  0x1000/0x2000/0x3000 in a surface word) and clears ctx+0x1e4.
- Increments a per-context BUFFER COUNTER at ctx+0x98 (byte).
- Calls a per-renderer surface function from a REGISTRY struct: r9 = *(PIC+0x6418c);
  r11 = registry[2] (a table indexed by ctx[0]=renderer_index); r12 = registry[5];
  `bctrl r12` with r3/r4/r5 = drawable[0/1/2].

### Registry: driver-local struct populated GLEngine-side (verified)
PIC+0x6418c resolves to absolute **0x690d8**, an `__nl_symbol_ptr` slot that is
INDIRECT_SYMBOL_LOCAL (not an import -- an earlier note misread it as
io_connect_method due to reading the section reserved1 from the wrong offset; the
correct reserved1=50).  It holds **0x69510**, the address of a `__common` (zero-fill)
struct of 0xb8 bytes.  So the "registry" is a DRIVER-LOCAL struct; registry[5]
(0x69524) is the surface-register function pointer, registry[2] (0x69518) a
per-renderer table.  CRUCIAL: the reference NEVER writes to 0x69510 (scanned all of
__text for both indirect and direct PIC stores into [0x69510,0x695c8) -- none).  So
**GLEngine populates the struct at a runtime handshake** (fills the driver's __common
registry with surface function pointers) -- this is the framework-side contract.

### Best next approach: RUNTIME tracing, not more static disasm
Static reference disasm has taken this as far as is reliable (the reserved1 bug shows
how error-prone it is).  To finish, instrument OUR driver's init entry points
(gldGetRendererInfo / gldCreateContext / gldInitDispatch and the *feature stubs*) to
DUMP their argument pointers, hunting for a GLEngine registry/vtable of function
pointers (a struct whose [5] is a code pointer in the 0x9xxxxxxx framework range).
Once found, capture it and, in gldAttachDrawable type 0x36, call registry[5](drawable)
to register the back buffer + bump ctx+0x98 + caps-0x20 gate, instead of binding the
front FB.  Then GLEngine should render the DB context via the primitive slots.

### Runtime trace started (2026-08-25)
Instrumented gldCreateContext(outp,pixfmt,shared,share_io,a7,a8,a9) to scan each arg
for framework code pointers (0x9xxxxxxx) -- the surface vtable.  Result on a DB
context: shared/share_io = NULL; a7/a8(gli)/a9 have NO framework pointers in their
first 256 words (1 KB).  So GLEngine does not hand the surface vtable directly in
gldCreateContext's args -- it is reached indirectly (follow a pointer inside the gli
to the renderer object, whose vtable holds the surface fns) or passed at a different
entry point.  NEXT runtime steps: (a) follow one level of indirection -- scan the
structs that heap pointers in a7/a8/a9 point at; (b) instrument gldInitDispatch
(table,outinfo) and gldGetRendererInfo args; (c) deep-scan the gli (80KB+) for a
region of 0x9xxxxxxx pointers.  This is a multi-session RE; the mechanism (type-0x36
back-buffer attach calling a GLEngine-populated registry[5] surface fn) is solid, but
the registry's source hasn't been located yet.

## DECISIVE (2026-08-25): full reference disasm -- it's a GLI FLAG, not the dispatch table
Disassembled the ENTIRE reference ATIRage128GLDriver swap/DB path (capstone PPC,
scratchpad disppc.py/macho.py/sel.py).  Key corrections + findings:

- **gldFlushBuffer @0x4644 is a NO-OP (`blr`).**  nm was off-by-4: 0x4644=gldFlushBuffer
  (blr), 0x4648=gldCreateContext.  The reference does **NO present work at the GLD swap
  hook**.  It neither page-flips nor blits nor calls a "swap" kext selector -- there is
  NO CRTC/display-base register write, NO blit, NO (pitch<<22)|off encoding anywhere in
  __text (scanned all immediates).  **Presentation is delegated to the kernel surface
  manager / WindowServer (IOFramebuffer side), not the GLD.**  gldFlush/gldFinish only
  SUBMIT the command DMA buffer (IOConnectMapMemory memtype 1 remap = kick+refresh);
  gldFinish additionally calls kext selector 8 (wait-idle).
- **The DB dispatch gate is a FLAG in the GLEngine gli, NOT a dispatch slot.**  slot 0x60
  stays a `blr` no-op.  On a type-0x36 attach, sub_a524(ctx,&caps,r5=0) does:
  `*(*(ctx+0x14)) |= 2`  (set bit1 of word0 of the object at ctx+0x14 = **a7/r7** from
  gldCreateContext), plus `ctx+0x1e4 = 1`, and the 0x36 path REQUIRES caps bit 0x20
  (ctx+0x2c) CLEAR.  GLEngine reads that gli flag to decide the context has a bound
  back-buffer surface -> dispatches primitives normally instead of routing to slot 0x60.
  (The GLEngine-side check is in the stripped framework, but the driver-side state to
  reproduce is now pinned: caps-0x20-clear + *(a7)|=2 + ctx+0x1e4=1.)
- **CORRECTION to the earlier note:** the registry[5] surface-fn `bctrl` is in the
  0x50/0x5a (window/fullscreen) path, NOT the 0x36 back-buffer path.  0x36 drives kext
  selectors directly.
- **Kext user-client selector map** (from r4 at every io_connect_method site):
  0=configure surface into pipe (structI), 3=buffer info (3 scalars), 4=front+back
  handles (status bit0 + 2 handles -> ctx+0x94/0x98), 5=surface geometry {id,id,base,
  pitch -> ctx+0x1f0/0x1f4}, 6=renderer info, **7=allocate/register back-buffer surface**
  (in: renderer-id byte, caps&0x803f), 8=finish/wait-idle, 9/a/b=texture, 0xf=fence.
  NO swap/flip/present selector exists.  Front/back buffers are KEXT-allocated (selector
  7), not userspace -- offset/pitch come back via selector 5.

### Implemented experiment (2026-08-25): set the gli flag on type-0x36 attach
gldCreateContext now stores a7 at ctx+0x14; gldAttachDrawable type-0x36 sets
`*(a7)|=2`, ctx+0x1e4=1, clears caps 0x20 (all logged `[DB-gate]`).  We still render to
our OWN back buffer + present via our blit/page-flip (our minimal kext has no kernel
surface manager, so unlike the reference WE must present).  TEST: tools/dbgate.sh
(build -DGLD_DBUF, mach64_dbgate draws an RGB triangle on a real AGL_DOUBLEBUFFER
context).  PASS = triangle renders (GLEngine dispatched primitives); FAIL = black
(still slot 0x60).  If it works, the register-guide CRTC_OFF_PITCH page-flip (0x414,
HW-double-buffered at vblank, encoding = DST_OFF_PITCH) replaces the present blit.

## BREAKTHROUGH (2026-08-25/26): DB dispatch routing CRACKED
Setting the gli flag (`*(a7)|=2` on the type-0x36 attach, above) WORKS: GLEngine now
dispatches a real AGL_DOUBLEBUFFER context's geometry to our primitive slots (Picture
43: a clean RGB triangle on a DB context; verified geometry correct, 81 render_triangles
dispatched).  The historic "routes to slot 0x60" blocker is SOLVED.  gldFlushBuffer/
gldFlush/gldFinish are NEVER called for a fullscreen DB context (confirmed: all 0) --
GLEngine only re-invokes gldUpdateDispatch per frame (the frame boundary), so the
present cycle is driven from gldUpdateDispatch (flush batch -> present -> clear back).

### Remaining: present-side accumulation, ISOLATED to the back/blit path
A DB swap-chain animation (tools/mach64_dbanim, -DGLD_DBUF) renders but ACCUMULATES ~4
frames with a diagonal offset.  Ruled out with evidence: geometry (tri coords are a
clean horizontal sweep), the clear (a green-fill diagnostic gave a uniform green bg ->
clear+present work), batching (1 tri/frame), fencing (mach64_submit waits for idle),
the kext cmd buffer (single synchronous buffer, no re-exec), and DST_WIDTH_HEIGHT field
order (matches the reference `(h<<16)|w`).  DECISIVE isolation (render-direct-to-FRONT
diagnostic, Picture 50/51): rendering the DB context straight to the front buffer (no
back, no blit) is CLEAN -- no accumulation, only tearing/pulsing from unsynced scanout.
So the accumulation is a HW interaction in the **back-buffer render + blit-present**
path (mach64_present -> mach64_blit_surface, looped), not the clear/geometry.  The blit
matches the DRI reference and works in isolation (blittest, Picture 22), so it's a
loop/address-specific quirk not crackable from userspace reasoning.

### FIX: page-flip via CRTC_OFF_PITCH — DONE, WORKING (2026-08-26)
Replaced the blit-present with a hardware page flip and it WORKS (no accumulation, no
flashing; tools/dbflip.sh, mach64_dbanim).  Implementation:
- **Kext**: stub selector 0 (set_surface) repurposed -> `regWrite(CRTC_OFF_PITCH, arg)`;
  the flip is made SYNCHRONOUS by then polling CRTC_VLINE_CRNT_VLINE (0x410, [26:16])
  until the scanline WRAPS -- so the just-shown buffer is fully free before the driver
  reuses it as the next render target.  (This wait is what killed the flashing.)
- **Driver**: two colour buffers (front 0x1000, back 0x301000); `render_off` = the
  hidden one.  Per frame (gld_dispatch gUD): flush geometry -> render_off, mach64_flip
  to render_off, swap render_off, set render target, clear.  gldAttachDrawable inits
  render_off=back and on detach flips scanout back to front (restores the desktop).
- **Verified** (DBflip instrumentation): geometry ALWAYS renders to the same buffer we
  flip to (all MATCH, non-empty batches) -- the alternation is correct; the earlier
  "flashing" was flip-vs-scanout TIMING, resolved by the synchronous wait-for-wrap flip.
- Encoding: CRTC_OFF_PITCH = (pitch/8<<22)|(off>>3), same as DST_OFF_PITCH.
- **TEAR-FREE via HW_DEBUG.DBL_BUFFER_EN** (2026-08-26).  The CRTC offset does NOT
  vblank-latch by default; enabling the hardware double-buffer makes the write latch at
  vblank atomically.  HW_DEBUG = reg 0x047c (RRG 0_1F -> 0x400 + 0x1F*4).  Bits confirmed
  from the RRG HW_DEBUG page image (PDF p.87/88): **bit10 = DBL_BUFFER_EN** (set),
  **bit15 = BLOCK_DBL_BUF** (clear).  setSurface does an idempotent RMW of just those two
  bits before writing CRTC_OFF_PITCH.  Result HW-verified: the moving triangle is WHOLE,
  no tearing.  (Screenshots still show a "staircase" -- that's a screen-CAPTURE artifact:
  the grabber reads the WindowServer framebuffer at 0x1000, which is our HIDDEN mid-render
  buffer half the time.  The LIVE display is clean.)
- Remaining: motion is CHOPPY (low framerate), a PERF matter not correctness -- the flip
  is synchronous (waits a full vblank for the offset to load + frees the buffer) and each
  DMA submit waits for idle.  Speedups (later): poll the flip-pending status instead of a
  full wrap-wait, and/or overlap CPU/GPU with a split command buffer.

## Fallback that works today
Force dbuf for ALL (single-buffer) contexts — no DB pixel format, no 0x60: the
render-to-back + present-on-glFinish path is proven (Picture 24).  Delivers the
visual benefit; cannot report `AGL_DOUBLEBUFFER=1` and changes single-buffer
semantics (adds a swap + permanent back-buffer VRAM cost).

## Tooling
`/tmp/csvenv` (capstone 5.x); disasm recipe in scratchpad.  Tests:
mach64_blittest, mach64_dbufprobe, mach64_dbufscene, mach64_dbufanim (build driver
`-DGLD_DBUF` for the pixel-format path).

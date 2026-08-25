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
- Calls a GLEngine registry function: r9 = *(PIC+0x6418c) (a renderer-registry
  object GLEngine populates); r12 = registry[5]; `bctrl` with r3/r4/r5 =
  drawable[0/1/2].  Plus imported-stub calls (sub_686fc/6871c = lazy-bound IOKit/libc).
So the DB "surface contract" = call GLEngine's renderer-registry surface function to
register the back buffer as a proper surface (type 0x36), gated on caps 0x20 clear,
tracking a buffer count at ctx+0x98.  The registry (PIC+0x6418c) is a GLEngine object
the driver must have captured at init (find where the reference STORES to PIC+0x6418c;
GLEngine passes it -- likely gldCreateContext/gldInitDispatch).
NOT imported symbols (nm -u ref128 = only IOKit/libc/pthread + glgConvertType/
glgProcessPixels), so the surface funcs come through that runtime registry, not the
link table.  TO IMPLEMENT: (1) capture GLEngine's renderer registry at init; (2) in
gldAttachDrawable type 0x36, call registry[5](drawable...) to register the back
surface + bump ctx+0x98, instead of binding the front FB; (3) sort the caps 0x20
gate.  Substantial multi-step RE (registry global + layout + fn signatures).

## Fallback that works today
Force dbuf for ALL (single-buffer) contexts — no DB pixel format, no 0x60: the
render-to-back + present-on-glFinish path is proven (Picture 24).  Delivers the
visual benefit; cannot report `AGL_DOUBLEBUFFER=1` and changes single-buffer
semantics (adds a swap + permanent back-buffer VRAM cost).

## Tooling
`/tmp/csvenv` (capstone 5.x); disasm recipe in scratchpad.  Tests:
mach64_blittest, mach64_dbufprobe, mach64_dbufscene, mach64_dbufanim (build driver
`-DGLD_DBUF` for the pixel-format path).

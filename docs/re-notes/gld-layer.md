# GLDriver translation layer — status

The userspace `gld*` plugin that turns GL calls into the register streams the
kext submits.  Sources: `gldriver/gld_context.c` (handshake), `gld_state.c` (GL
state → registers), `gld_render.c` (geometry + submission), reusing the portable
`common/mach64_emit.c` stream builder.

## ✅ PROVEN on hardware (mach64_gldtest)

The translation core draws a gouraud triangle through the real plugin code end
to end, bypassing only Apple's GLEngine:

```
mach64_init_state / mach64_set_render_target   (GL state -> m64_state shadow)
mach64_pack_vertex x3                           (post-T&L float -> hw vertex)
mach64_draw_triangle                            (emit state+tri+ooa trigger)
  -> m64_cb_write_le  (host BE -> LE bytes in the type-4 command buffer)
  -> new_vertex_buffer -> kext rotateCommandBuffer -> DMA ring
gldFinish -> wait_for_stamp
```

Readback was **byte-identical** to the hand-built `mach64_ringtest` stream
(near-v0=ff8a7102, v1=ff1dbf22, v2=ff1e04db, centroid=ff63405a, covered=507) —
so the emit path produces exactly the verified 50-dword/200-byte stream.

Key facts locked in by this:
- vertex = one 7-register burst {S,T,W=1.0,SPEC,Z,ARGB,X_Y}; the LE byte-swap
  lives in the plugin (`m64_cb_write_le`), NOT the kext.
- `mach64_priv` backend (shadow state + dirty + last_stamp) hangs off
  `gld_context.backend`; state re-emitted only when dirty.

## ⛔ Still stubbed: the GLEngine <-> gld dispatch ABI

The one piece we can't derive from our own hardware work.  Unknowns:
- what `gldUpdateDispatch` installs (function-pointer slots + order),
- how GLEngine hands vertices to the plugin (array layout, the
  Alloc/CompleteVertexBuffer calling convention),
- exact `gldGetRendererInfo` caps struct + `gldChoosePixelFormat` attribs.

**Reference for this:** `ATIRage128GLDriver.bundle/Contents/MacOS/
ATIRage128GLDriver` (Mach-O bundle ppc, on /Volumes/macUSB) — the sibling driver
that DOES ship a GL bundle.  Disassemble its `gldUpdateDispatch` + vertex entry
points to recover the ABI, then wire GLEngine's primitive callbacks to
`mach64_draw_triangle`.  That turns the proven core into a loadable bundle.

### Dispatch-table layout (first pass, from disassembly)

`gldUpdateDispatch(gld_ctx *r3, void *table_r4, ... r5)` fills a dispatch table
(arg2, r30) with function pointers, choosing variants from state bits at
`ctx+0x2c` (`0x20`/`0x400`/`0x800` = texture / tex-unit-0 / tex-unit-1 enables —
the same variant selection mesa does via `setup_tab[]` in mesa_mach64_vb.c).

Table slots written (byte offsets into the dispatch struct): 0x18, 0x1c, 0x20,
0x24, 0x28, 0x2c, 0x30, 0x34, 0x38, 0x3c, 0x40, 0x44, 0x48, 0x4c, 0x58, 0x5c,
0x60.  Pointers are INTERNAL (stripped) render/vertex-emit functions, not the
exported `gld*` symbols — so the slot OFFSETS are the ABI; the bodies are ours to
write.  `gldUpdateDispatch` also calls helpers at 0x4da4, 0x6899c, 0x72f0,
0x6269c, 0xd5e0 (state upload / buffer setup).  Next: map each slot to the GL op
it services (per-primitive vs per-vertex), and disassemble gldCreateVertexArray
(0x8254)/gldAllocVertexBuffer (0xdf90) for the vertex-array layout.

RE tooling notes: llvm-`otool -tv` chokes on this old PPC Mach-O ("symbol table
extends beyond the end of the file") but `nm` + capstone work; in `__text`,
**file offset == vaddr** (addr 0x13cc == offset 0x13cc); PIC base = the
`bcl/bdnzl` target loaded by `mflr` (here r31=0x60bb4), then
`addis rX,r31,HI; lwz rY,LO(rX)` loads a data-section pointer.  See the
osx-ppc reverse-engineering skill.

### Dispatch slot -> GL op MAP (recovered)

Each render slot was classified by its **minimum-vertex-count guard** at the top
of the pointed-to function (`cmpwi <count>, K; ble` = needs > K verts) plus the
grouping mask (`rlwinm rX,rX,0,0,0x1e` = round to even; `...,0x1d` = round to
mult-of-4).  Signature of a render func: `f(ctx=r3, start=r4, count=r5 [,elts=r6])`.

| slot | guard | GL primitive | confidence |
|------|-------|--------------|------------|
| 0x18 | count>0            | **GL_POINTS**         | family solid |
| 0x1c | count>1, even      | **GL_LINES**          | family solid |
| 0x20 | count>1            | GL_LINE_STRIP         | inferred |
| 0x24 | count>1            | GL_LINE_LOOP          | inferred |
| 0x28 | count>2            | **GL_TRIANGLES**      | family solid |
| 0x2c | count>2            | GL_TRIANGLE_STRIP     | inferred |
| 0x30 | count>1, **+elts arg** | TRIANGLE_FAN or an elts variant | open |
| 0x34 | count>2            | GL_TRIANGLE_FAN / GL_POLYGON | inferred |
| 0x38 | count>3, mult-of-4 | **GL_QUADS**          | family solid |
| 0x3c | count>3, even      | **GL_QUAD_STRIP**     | family solid |
| 0x40 | count>0            | POINTS (2nd vector: elts/DrawElements) | family solid |
| 0x44 | count>1, even      | LINES  (2nd vector)   | family solid |
| 0x48 | count>2            | TRIANGLES (2nd vector)| family solid |
| 0x4c | -                 | complete/submit vertex buffer (sub_e2e4, next to gldCompleteVertexBuffer 0xe2c8) | strong |
| 0x50,0x54 | (not written by gldUpdateDispatch) | — | — |
| 0x58 | -                 | **Finish** (== gldFinish 0xd158)  | CONFIRMED (symbol) |
| 0x5c | -                 | **Flush**  (== gldFlush  0xd500)  | CONFIRMED (symbol) |
| 0x60 | -                 | render control / start (sub_60a24)| address only |

Structure: slots 0x18-0x3c are the per-primitive render vector for sequential
verts; the point/line/triangle DOUBLING at 0x40/0x44/0x48 plus the extra-arg
signature on 0x30 mean there are **two vectors — DrawArrays (verts) and
DrawElements (elts, indexed)** — only points/lines/triangles need a distinct elts
path (everything decomposes to those).  All render slots are filled per
vertex-format variant (~6 blocks in gldUpdateDispatch, chosen by ctx+0x2c
texture bits).

Context-struct offsets recovered from the render funcs (what to populate/read
when we implement them): **0x6c** = current hw primitive/vertex type (they skip
state re-setup if unchanged), **0xac** = command-buffer base, **0xb0** = cmdbuf
current ptr, **0xb4** = cmdbuf end, **0x74/0x80** = a float scale (depth /
one-over-area). Vertex stride in the incoming array = **0x44 (68 bytes)**.

Wiring plan: implement the ~10 render slots as loops that pull `count` verts
from `start`, decompose per primitive, and call our `mach64_draw_triangle`;
point 0x58/0x5c at gldFinish/gldFlush; 0x4c at our submit.

### Incoming vertex layout (RECOVERED from the TRIANGLES emit, slot 0x28)

gldCreateVertexArray/Modify/Flush are STUBS (`li r3,0; blr`) — the Rage128 driver
does NOT implement vertex arrays; GLEngine uses the immediate render-dispatch
path. So the layout comes from how the render funcs copy each incoming vertex
into the command buffer (0x5d28c..0x5d428).

- **Incoming vertex stride = 0x100 (256 bytes)** (`addi r30, r30, 0x100` at the
  loop back-edge). This is the big Apple gli/driver vertex.
- **Outgoing hardware vertex = 0x44 (17 dwords)** per vertex in the command
  buffer, written with **`stwbrx`** (byte-reversed = little-endian; confirms the
  plugin does the BE→LE swap, not the kext).
- Apple's command buffer uses **vendor-opcode packets** (header top byte 0xc0…,
  plus `0x3b7` and `(count<<16)|regidx` words) — NOT the raw-ADRINDEX format our
  kext proved. We keep our simpler format; this table is only for the field
  SEMANTICS.

Copy order maps to the mach64 setup-FIFO order (S,T,W,SPEC,Z,ARGB,X_Y,…), giving
the incoming-vertex field offsets:

| hw field | incoming offset | notes |
|----------|-----------------|-------|
| S  (tex0 s)  | 0x00 | float, copied raw |
| T  (tex0 t)  | 0x04 | float, copied raw |
| W            | 0x08 | float + `ctx[0x74]` bias (W or depth-range adj — TBD) |
| SPEC_ARGB    | 0x0c | |
| Z            | 0x38 | depth |
| **ARGB**     | 0x34 | packed primary colour (hardware-ready) |
| **X_Y**      | 0x30 | packed fixed-point screen pos (hardware-ready) |
| (4th pos/sec)| 0x3c | |
| tex1 S,T,W   | 0x58,0x54,0x50 | secondary texture unit |
| tex? / fog   | 0x4c, 0x80,0x84, 0x90,0x94,0x9c | extended attrs (multitexture) |

**Key result:** X_Y (0x30), ARGB (0x34), Z (0x38) arrive **already packed to
hardware format** — an upstream build-vertex step (a dispatch callback, not the
stubbed vertex-array API) does the float→fixed x*4 and colour packing. That's
exactly what our `mach64_pack_vertex` + `m64_vertex` produce, so our design is
aligned: our render-slot bodies read in[0x30]/in[0x34]/in[0x38] straight into the
hw vertex (or, since we own the build step, use our own layout). S,T,W are floats
for texturing; for the untextured gouraud path only Z/ARGB/X_Y (+ W=1.0) matter.

Confidence: stride 0x100 and the offset→FIFO-order copy are solid (directly
disassembled); the S/T/W/SPEC vs Z/ARGB/X_Y naming follows the mach64 FIFO order
and is well-supported; the extended tex/fog fields (0x4c/0x80+) are inferred.

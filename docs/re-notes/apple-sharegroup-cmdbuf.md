# ATIRagePro.kext — Share-Group Setup & Command-Buffer Submission Contract (Path B)

Reverse-engineered offline 2026-09-05 from `/Volumes/macUSB/panther ati drivers/
ATIRagePro.kext/Contents/MacOS/ATIRagePro` (Mach-O MH_OBJECT, ppc). `__text` file
offset = vaddr + 0x33c. Binary is symbol-rich. Resolves the two Path-B Phase-1
blockers: the share-group panic and the command-buffer submit/fence contract.
Companion to `apple-glcontext-protocol.md` + `surface-backed-present-plan.md`.

**Two-layer architecture:** the IOKit user clients `IOATIRProGLContext` /
`IOATIRProSurface` / `IOATIRProShared` wrap engine-side objects `ATIR3GLContext` /
`ATIR3Surface` / `ATIRagePro`. The GL user client stores its engine accelerator at
**ctx+0xb0** and dispatches real work there.

## (a) Share-group — the #1 blocker, RESOLVED

**Headline (corrects the prior hypothesis):** `ctx+0x88` (the `IOATIRProShared*`) is
NOT NULL on a normal open — `IOATIRProGLContext::start()` allocates a fresh 0x28-byte
Shared for EVERY GL context. The earlier panic was NOT a bare-connection NULL; it came
from using `IOConnectAddClient`/`connectClient` to associate a *surface*, which is the
WRONG API. `connectClient` is GL-context↔GL-context share-group joining; surface
association is `set_surface(id)`.

- `IOATIRProGLContext::start` @0x4f70: `operator new(0x28)` → `IOATIRProShared` ctor
  @0xea08 → store into **ctx+0x88**; `vt[0x48]`=`IOATIRProShared::init`@0xea74 (NULLs
  ctx+0x88 and fails start on error, so any opened context HAS a valid Shared).
  Sets `shared+0x0c=ctx[0xb0]` (accel), `shared+0x08=ctx[0x78]` (task); links context
  into `accel+0x60` list; `allocAllContextBuffers(0x1000)`; tail `ctx+0x110=1`,
  `ctx+0x11c=0x174c0` (GL methodDescs base).
- `IOATIRProShared::init`@0xea74: zeroes shared+0x10..0x24. **shared+0x20 = in-use
  handle refcount** (checked by become_global_shared). Shared owns textures/vertex
  buffers (new_texture@0xf290, new_vertex_buffer@0x10618); GL new_texture@0x71e4
  dispatches into `ctx[0x88]->new_*`.
- `become_global_shared(flag)` @0x7484 — PUBLISH, does not create. Uses accel lock
  `accel[0x544]`, global slot `accel+0x68`/valid `accel+0x6c`. flag!=0: require
  `accel[0x68]==0` AND `ctx[0x88][0x20]==0` → publish. flag==0: relinquish. Miss ⇒
  0xE00002CC. Optional; only for a WindowServer-style single global share group.
- `connectClient(other)` @0x62a0 — GL context↔context JOIN only: require same task
  (`this[0x78]==other[0x78]` else 0xE00002BC); **release** `this[0x88]`, set
  `this[0x88]=other[0x88]`, **retain**. This release is the deref that panicked.
- **Why the prior attempt panicked (definitive):** `IOConnectAddClient(glctx,surface)`
  → `glctx::connectClient(surface)` runs `glctx[0x88]=surface[0x88]` then virtual-calls
  it. But **surface+0x88 is the surface's GL-context-LIST head** (see
  `add_gl_context_to_list`@0xac98: `ctx[0x84]=surface[0x88]; surface[0x88]=ctx`), NOT
  an OSObject → garbage vtable → panic. Offset 0x88 means different things on a GL
  context (Shared) vs a surface (list head).

### CONCRETE call order our bundle must perform (single context → one surface)
No share-group call needed — `ctx+0x88` is auto-valid.
1. Surface first: open type-2 `IOATIRProSurface`, `set_id_mode(cgs_id, mode)` (sel 7)
   → `set_shape(shapeBits, region)` (sel 9). Registers in `accel+0x5c`;
   `find_surface_for_id`@0x4bd0 now resolves.
2. Open GL context: `IOServiceOpen(svc, task, 1, &glconn)` → `start()` auto-creates
   `ctx+0x88`.
3. `get_config` (sel 3) — cold-safe.
4. **`set_surface(id, modeBits, a, b)`** (sel 0, @0x6440): `find_surface_for_id(id)`
   (NULL⇒0xE00002C2) → binds `ctx+0x10c=surface`, mode→`ctx+0x8c` →
   `surface->add_gl_context_to_list(ctx)`. THIS is the association — never
   `connectClient`.
5. `set_swap_rect(x,y,w,h)` (sel 1).
6. `clientMemoryForType` map buffers — only now that `ctx+0x10c` is bound (mapping any
   region before set_surface ⇒ NULL-deref panic).
7. Render → advance header → fence.
`newUserClient`@0x2998: type>2⇒0xE00002C2; type0→vt[0x590], type1→vt[0x598] (GL),
type2→vt[0x594] (surface); task at client+0x78.

## (b) VendorCommandBufferHeader — verified C struct (0x20 bytes, body = {reg,val} pairs at +0x20)

Verified vs `init_command_buffer_header`@0x2bb0/@0x7a64, `init_context_buffer_header`
@0x7a30, consumer `ATIR3GLContext::submit_context_buffer`@0x139c4.

```c
typedef struct VendorCommandBufferHeader {
/* 0x00 */ uint32_t head;      /* opaque; snapshot-copied by kext */
/* 0x04 */ uint32_t rsvd04;
/* 0x08 */ uint32_t rsvd08;
/* 0x0c */ uint32_t rsvd0c;
/* 0x10 */ uint32_t capacity;  /* body capacity in DWORDS = (size-0x20)/4  [init] */
/* 0x14 */ uint32_t count;     /* # body DWORDS to drain; CLIENT advances this */
/* 0x18 */ uint32_t stamp;     /* = ctx[0x7c] last submit stamp [init] */
/* 0x1c */ uint32_t flags;     /* bit0 = VALID/SUBMIT; init=1; kext clears after drain */
/* 0x20 */ uint32_t body[];    /* {reg,val} u32 pairs; count/2 pairs */
} VendorCommandBufferHeader;
```
- +0x10 capacity `(size-0x20)>>2`; +0x14 count = drain length (submit reads it, copies
  that many DWORDs from hdr+0x20); +0x18 stamp = `ctx[0x7c]`; +0x1c bit0 gates drain
  (@0x139dc), cleared after (@0x13ad4). Odd counts padded with trailing `0xffffffff`.
- `init_command_buffer_header`@0x7a64 (client-mapped `ctx[0xc4]`, size `ctx[0xc8]`,
  arg3=`surface[0x168]`) additionally seeds `body[0]=0x01000000`. OPEN: meaning of that
  seed (not a valid {reg,val}; likely a leading marker; client overwrites `count` with
  real `pairs*2` before triggering).
- clientMemoryForType (jump table @0x573c): type1→`ctx[0xb8]` command region (REQUIRES
  `ctx[0x10c]` bound surface); type2→`ctx[0xd0]` context buffer (size 0x1000).

## (c) Submit + fence protocol

**Trigger:** NO submit selector. Client writes {reg,val} pairs into body[], sets
count(+0x14) + flags bit0=1, then calls a fence selector (finish sel 8 / wait_for_stamp
sel 9). Kext `submit_context_buffer`@0x139c4 snapshots header+count DWORDs into a
ping-pong buffer (`ctx[0x108]^=1`), clears dirty, calls `ATIRagePro::submit_buffer`.

**`ATIRagePro::submit_buffer(u32* body,_,u32 count)` @0x12730 — PIO drain (authoritative):**
- count in DWORDS; pairs=count/2; FIFO batches ≤8 DWORDs. MMIO base=`accel[0x550]`.
  Each pair: `reg=body[0]; val=body[1]; stwbrx val → MMIO[base+reg]` (byte-reversed,
  BE CPU→LE regs); **reg used RAW** (full byte offset; unlike write_regs which masks
  `&0x3ff`).
- FIFO throttle: poll MMIO reg 0x310 (`lwbrx`), free vs `0x8000>>(n*2)`, `_thread_block`
  if short. Special reg 0x1fc: spin engine-idle on reg 0x338 bit0 before writing.
- Stamp: `stamp=accel[0x50]; accel[0x50]++; return stamp` (monotonic). Stored to
  `ctx[0x7c]`.
- 2D variant `ATIR32DContext::write_regs`@0x143c8: same pattern to `obj[0x94][0x550]`
  but `reg&=0x3ff` and size must be mult-of-8 else 0xE00002C2.

**Fence is COARSE = wait-for-engine-idle on reg 0x338 bit0.** `finish()`@0x7070 and
`wait_for_stamp(stamp)`@0x70c0 both ignore the stamp arg; the real wait
`ATIRagePro::waitForTimeStamp`@0x12578 also ignores stamp and polls reg 0x338 bit0 in a
bounded loop. `checkForTimeStamp`@0x126b4 = non-blocking poll. So: after advancing the
header, call finish() (or wait_for_stamp) → engine idle, batch on-chip.

## Port checklist (net)
1. Surface: type-2 set_id_mode→set_shape. 2. Open type-1 GL ctx (ctx+0x88 auto; do NOT
connectClient a surface). 3. set_surface(id) (binds ctx+0x10c). 4. set_swap_rect. 5.
clientMemoryForType map type-2 + type-1. 6. Per batch: write {reg,val} pairs at hdr+0x20,
`hdr+0x14=pairs*2`, `hdr+0x1c|=1`. 7. finish() → drain+idle. 8. Present via surface
(surface_flush, already validated). Skip become_global_shared/connectClient unless
building multi-context CGL share groups in your own task.

## OPEN items
- Meaning of the `body[0]=0x01000000` seed (init_command_buffer_header@0x7a64).
- write_regs@0x143c8 iteration-count arithmetic (2D path; not the GL/present drain).
- ATIRagePro engine-vtable slot indices for the waitForTimeStamp family (resolved
  behaviorally, not by slot).

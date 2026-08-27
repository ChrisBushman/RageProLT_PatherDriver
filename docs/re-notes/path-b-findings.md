# Path B — findings & conclusion (CORE VALIDATED 2026-08-27)

**One-line verdict:** The full userspace surface handshake onto Apple's `ATIRagePro.kext` **works
end-to-end** — `open surface → set_id_mode → set_shape → open GL → set_surface → BOUND`, proven
live. The single blocker that made this look like a dead end was a **wrong IOKit call convention on
`set_surface`** (`ScalarIScalarO` where the kext's `methodDescs` demands `ScalarIStructureI`,
`flags=4`); IOKit rejected the call with `0x2c2` before `set_surface` ever ran. With the right
convention it binds immediately. Path B is viable; the remaining work is the known command-buffer /
present plumbing.

> **History note:** an earlier draft of this doc concluded Path B was blocked by an invisible
> runtime issue and recommended pivoting to our own kext. That was WRONG — the "runtime
> contradiction" was entirely the `set_surface` call convention. The wider-find diagnostic
> (`tools/mach64_surfscan.c`, enumerating the surface list via `get_surface_info`) disproved the
> pivot rationale and led to the fix. Section 9 records the correction.

This document is the definitive record so the work can be resumed without re-deriving it. For the
raw protocol map (selectors, memory regions, command format) see
[apple-glcontext-protocol.md](apple-glcontext-protocol.md). For the windowed-on-our-kext result see
[windowed.md](windowed.md).

---

## 1. What Path B was

Apple built a full GL kernel subsystem for the Rage LT Pro (`ATIRagePro.kext`, com.apple.ATIRagePro
3.2.8, loaded, our exact chip) but **never shipped a userspace GL bundle** for it. Path B was the
plan to drop *our* GLEngine bundle onto Apple's kext instead of driving our own
`Mach64Accelerator.kext` — which would have given us, for free: engine coexistence (Apple's kext
owns the device), the windowed surface handshake, present/scanout, and VRAM/GART/texture
allocation. Its command format even turned out to be our own register-write streams (`{reg,val}`
pairs; see the protocol doc §"Submission model").

The decision to pursue it, and the user's directive not to git-commit until GL+2D apps were
validated, are recorded in memory `rageprolt-pathb-commit`.

## 2. What we PROVED works

- **Phase 1 — open Apple's GL client.** From inside our GLEngine-loaded bundle,
  `IOServiceOpen("ATIRagePro", type 1)` opens `IOATIRProGLContext`; `get_config` (selector 3)
  returns caps `04000000 00487000 00800000`. Cold-safe, repeatable. (`gld_glue.c`
  `mach64_apple_glctx_open`.)
- **User-client types (corrects the protocol doc's earlier guess):**
  **type 0 = SURFACE (`IOATIRProSurface`), type 1 = GL (`IOATIRProGLContext`), type 2 = 2D
  context.** `newUserClient` accepts 0/1/2; `>2 → 0xe00002c2`. Verified with the `get_state`
  (surface selector 2) probe — only type 0 answers it.
- **Full protocol map** — selectors, argcounts, the 5 `clientMemoryForType` regions, the mandatory
  setup sequence, and the `{reg,val}`-pair command format — all reversed. See the protocol doc.
- **Surface registration succeeds.** Opening the type-0 surface client and calling
  `set_id_mode(idx, mode=0x24)` (windowed) returns `KERN_SUCCESS`: the surface is registered.

## 3. The blocker

After registering the surface, the GL context's `set_surface(id)` (selector 0) returns
`0xe00002c2`. Decoded in the kext, that is **exactly the "`find_surface_for_id` returned NULL"**
branch of `IOATIRProGLContext::set_surface` (`0x6440`; the `find` call is a `jbsr` veneer at
`0x648c`, the null-check + error at `0x6490`/`0x649c`). So: the GL context cannot find the surface
we registered.

## 4. The full diagnostic chain — all three static hypotheses ELIMINATED

The question we chased: *why can't the GL context find a surface we successfully registered?*

### 4a. "The Rage Pro kext surface path is incomplete/stubbed" — DISPROVEN
The Rage128 shipped a working GL driver, so its surface path is the control. Pulled
`ATIRage128.kext` and compared function-for-function:

| | Rage128 (shipped, works) | Rage Pro |
|---|---|---|
| Class arch | `ATIRage128` : `IOATIR128Accelerator` + `IOATIR128Surface`/`Shared`/`GLContext` | identical (`ATIRagePro` : `IOATIRProAccelerator` + …) |
| `find_surface_for_id` | walk circular `accel+0x5c`; id @`surf+0xa4`, next @`+0x9c` | same logic; id @`surf+0x98`, next @`+0x90` |
| `set_id_mode` stores id to | `surf+0xa4` (`stw r26,0xa4(r31)`) | `surf+0x98` (`stw r26,0x98(r30)`, r30=`this`) |
| id field written == field `find` reads | **yes** | **yes** |

`set_id_mode` writes the id to the *same field* `find` reads, on **both** drivers.
`IOATIRProSurface::start` (`0xa704–0xa74c`) links `this` **unconditionally** into `provider+0x5c`
(circular insert, count at `+0x4a4`). Nothing is stubbed — the Rage Pro is a faithful, consistent
port.

### 4b. "Wrong id-encoding (windowed vs indexed path)" — DISPROVEN
`set_id_mode` has two paths (branch on `mode & 0x20` at `0xafe0`):
- **Windowed (`0x24`, bit set):** stores the id *unchanged* at `surf+0x98`; index sentinel
  `0xffff`. Accepts a large CGS handle.
- **Indexed (`0x4`, bit clear):** stores `~idx` at `surf+0x98` and does `accel_array[idx*0x1c]`
  bookkeeping; requires `idx < accel+0xc8` (max surface count).

Experiment (2026-08-27): registered with mode `0x4`. Result:
`set_id_mode(idx=0x7a03, 0x4) → 0xe00002bc` (**out of range**) — the CGS handle is far larger than
the accelerator's max array count, so the indexed path *cannot* register a CGS-sized id. This
confirms the **windowed path (`0x24`) is the architecturally correct one**, and it *does* register
the handle at `surf+0x98`. Also confirmed `bind_surface` tries all encodings (`idx`, `~idx`, raw
handle) — none resolve. Id-encoding is not the problem.

### 4c. "GL and surface clients are on different accelerator instances" — DISPROVEN
`newUserClient` (`0x2990`) creates each per-type client (factory vtable slots
`0x590`/`0x594`/`0x598`) and hands **every** client the same accelerator `this` (r29) as provider;
the task is stored at `client+0x78`. `IOATIRProGLContext::start` stores its provider at `ctx+0xb0`;
`IOATIRProSurface::start` stores its provider at `surf+0x284`. And decisively:
**`ATIRagePro`'s metaclass constructor base-constructs `IOATIRProAccelerator`** (`0x11b9c`
`jbsr __ZN20IOATIRProAccelerator…C2…`), and `ATIRagePro` defines **no** `newUserClient` of its own —
so `class ATIRagePro : public IOATIRProAccelerator`, **one single object**.
`IOServiceMatching("ATIRagePro")` returns it; both clients open it; `ctx+0xb0 == surf+0x284`; its
`accel+0x5c` is the one and only surface list. There is no instance split. (The `ioreg`
`IOATIRProAccelerator=1` separate count is a base-subobject/diagnostics artifact, not a 2nd
accelerator.)

## 5. The irreducible contradiction

Putting §4 together, by **every** static path our exact sequence should leave a findable surface:

```
open type-0 surface client
  → IOATIRProSurface::start links `this` into the (single, shared) accel+0x5c list, unconditionally
set_id_mode(handle, 0x24)  [windowed]
  → stores `handle` at this+0x98  (the field find reads)
GL set_surface(handle)
  → find_surface_for_id(ctx+0xb0 == same accel, handle)
  → walks accel+0x5c, compares node+0x98 == handle  → MUST match
```

…yet live, `set_surface` returns `0x2c2` (find null). Static reverse-engineering of the kext is
**exhausted and unanimous that it should work.** The failure is therefore a **runtime** condition
disassembly cannot show — most plausibly a surface **lifetime/teardown** issue (a user-client-
created surface not persisting in the list the way a CGS/WindowServer-created one does, or being
torn down between `make_surface` and `bind_surface`), or a task/locking subtlety at bind time.

## 6. The deeper truth (why this was never going to be a clean "port")

What Apple never implemented for the Rage Pro is **not the kext** — it is the **userspace half**:
the GL bundle *and* the WindowServer/CGS surface create→shape→commit handshake. On a Rage128, CGS
creates and registers the drawable surface through the driver as part of normal windowing, so by
the time a GL bundle calls `set_surface`, a properly-created, correctly-scoped surface already
exists with the right id. For the Rage Pro, nothing does that, so we were hand-rolling a
registration that CGS normally performs — and hand-rolling it without whatever runtime scoping CGS
provides is why `find` comes up empty despite a code-correct registration.

## 7. (SUPERSEDED) "resume needs kernel visibility"

> This section's premise — that closing the blocker needed kernel-side instrumentation — was
> **wrong**. Section 9 resolved it from userspace. Kept for history: sections 5–7 document the
> reasoning that led to the (incorrect) pivot recommendation, before the wider-find diagnostic.

## 8. (SUPERSEDED) pivot decision

> The pivot to our own kext was **not carried out** — the wider-find diagnostic reopened Path B the
> same day (§9). Our own `Mach64Accelerator.kext` remains a proven fallback (`windowed.md`;
> `wintest` spun cleanly), and the Path B probe code stays additive behind `#ifdef GLD_APPLE_PROBE`
> (only `tools/applephase1.sh` defines it; all other builds use our kext). But Path B is now the
> active track. The panicking `IOConnectAddClient` share-group call is still reverted and must not
> be re-added without first creating an `IOATIRProShared` (`ctx+0x88` NULL → hard lock).

## 9. RESOLUTION — the surface handshake works end-to-end

The blocker was never a runtime/lifetime/instance issue. It was **the IOKit call convention on
`set_surface`.**

**The diagnostic that cracked it** (`tools/mach64_surfscan.c`): enumerate the accelerator's surface
list from userspace by scanning `get_surface_info(id)` (GL selector 6 — a cold-safe find+read;
miss → `0x2c2`, no deref) across an id range. This revealed, step by step:
1. The list starts empty; `set_id_mode(idx, 0x24)` registers a surface and `get_surface_info(idx)`
   then **finds it** — so registration and cross-connection `find` work fine at runtime (disproving
   §4/§5's "invisible runtime failure").
2. `get_surface_info(idx)` FOUND while `set_surface(idx)` returned `0x2c2` → the `0x2c2` was
   `set_surface`'s **second** error path (`0x6624`), *after* `find` succeeds, not "find null".
3. That path is gated on `ctx->vtable[0x5ac]` (= `setCompatibleSurfaceMode`, `0x641c`) returning 0.
   That needs the surface to have a **backing** — so add **`set_shape`** first.

**`set_shape`** (`IOATIRProSurface` selector 9): `ScalarIStructureI`, `methodDescs {func=0xc5ec,
flags=4, count0=2 scalars, count1=0xffffffff var-struct}`. Call:
`IOConnectMethodScalarIStructureI(surf, 9, 2, sizeof(region), shapeBits, options, &region)` with a
well-formed `IOAccelDeviceRegion` (`{u32 num_rects; IOAccelBounds bounds{s16 x,y,w,h}; rect[]}`,
20 B for one rect). `set_shape → set_shape_backing` frees the old backing (`surf+0x294/0x298`; a
no-op on a fresh surface) and `IOMallocPageable`s the new one sized from the region dims. LIVE:
`set_shape(256×256, shapeBits=0)` → success; `get_surface_info` then reports dims `0x100/0x100`.

**The actual fix — `set_surface`'s convention.** After `set_shape`, `set_surface` *still* returned
`0x2c2` for **every** mode, including `mode=0` which the (correct) `setCompatibleSurfaceMode`
formula predicts compatible. That's the tell: `set_surface` wasn't running at all. Its `methodDescs`
entry is `{func=0x6440, flags=4, count0=4, count1=0}` — **`flags=4` = `ScalarIStructureI`**, the
same family as `set_shape`, **not** `flags=0`/`ScalarIScalarO`. We (and the bundle) had been calling
it via `IOConnectMethodScalarIScalarO`, so **IOKit rejected the call with `0x2c2` before
`set_surface` executed** — which is why `find` and the compat check never ran and every mode failed
identically. Correct call:

```
IOConnectMethodScalarIStructureI(gl, 0 /*set_surface*/, 4 /*scalarIn*/, 0 /*structInSize*/,
                                 id, mode, 0, 0, &dummy);
```

LIVE: `set_surface(idx=0x13, mode=0) [ScalarIStructureI]` → **`0x0` — SURFACE BOUND.** `mode=0`
binds, confirming the `setCompatibleSurfaceMode` decode was right; the *only* bug was the convention.

**The validated end-to-end sequence (all from userspace, `mach64_surfscan.c`):**
```
open type-0 surface client
  set_id_mode(idx, 0x24)                                  [ScalarIScalarO, sel 7]
  set_shape(region 256×256, shapeBits=0)                  [ScalarIStructureI, sel 9]
open type-1 GL client
  set_surface(idx, mode=0)                                [ScalarIStructureI, sel 0]  → BOUND
```

**Remaining port work** (core proven; this is the known part per the protocol doc): map the
command/context buffers (safe only *after* `set_surface`) → write `{reg,val}` pairs + advance the
`VendorCommandBufferHeader` → submit → `wait_for_stamp` / present via
`ATIR3Surface::submit_swap_buffer`.

**Bundle fixes to apply** (all behind `GLD_APPLE_PROBE`, still UNCOMMITTED per the no-commit
directive): (1) `gld_glue.c` `mach64_apple_make_surface` — add `set_shape` after `set_id_mode`;
(2) `gld_glue.c` `mach64_apple_bind_surface` — call `set_surface` via
`IOConnectMethodScalarIStructureI` (4 scalars + 0-byte struct), not `ScalarIScalarO`.

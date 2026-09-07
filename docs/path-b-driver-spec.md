# Path B: Driving Apple's `ATIRagePro.kext` from a User-Space OpenGL Bundle

**A formal specification of the Mac OS X 10.3 (Panther/PPC) ATI Rage Pro / Rage LT Pro
hardware-acceleration protocol, reverse-engineered from `ATIRagePro.kext` and verified on
hardware (Lombard PowerBook G3, Rage LT Pro).**

Status: **2D fill / clear / present pipeline is complete and hardware-verified.**
3D geometry (the setup engine) is specified but not yet wired — see §10.

---

## 0. Audience and scope

This document is for engineers writing an OpenGL renderer bundle (`GLDriver`) that drives Apple's
*shipped* `ATIRagePro.kext`, filling the empty GL socket that the kext's `IOGLBundleName`
(`"ATIRageProGLDriver"`) points at. It is the ground-truth contract for how a user-space client
submits register commands to the mach64 2D/3D engine through Apple's IOKit accelerator and presents
the result to the screen.

Everything here was recovered by static disassembly (capstone/PPC) plus live probing on hardware;
where a claim is hardware-verified it is marked **[HW]**. Register offsets are the canonical mach64
(DRI `drm_mach64_drv.h`) offsets unless stated otherwise.

**Kext-patch status (be precise about this).** The whole *rendering and present pipeline* —
register submission (§5), the 2D/3D/depth engine (§4, §10), and `surface_flush` present (§7) — runs
on the **stock, unmodified** kext. There is exactly **one** stock-kext gap: obtaining the bound
surface's render-target `OFF_PITCH` (`surf[0x12c][0x28]`, §6). No shipping selector returns it, so
the current implementation reads that single value via a **temporary peek patch** — GL selector 14
(`scale_surface`, otherwise unused) repurposed into a kernel-memory read. **This means the driver as
built today DOES require the peek-patched kext to discover its render target.** The fix for a
truly-unmodified deployment is one small kext addition: a `get_render_target` selector that returns
`surf[0x12c][0x28]` (§6.1). Nothing else in the contract depends on a patch. Where this document says
"unmodified kext," read it as "the pipeline needs no patch *given* the render-target offset" — that
one lookup is the sole outstanding dependency.

---

## 1. Architecture

```
   OpenGL app
      │  (CGL / AGL)
   Apple GLEngine  ── dlopen ──►  ATIRageProGLDriver.bundle   ← YOU write this
      │                                   │
      │                          IOKit user-client calls
      ▼                                   ▼
   Apple ATIRagePro.kext  ──────────  mach64 hardware (PIO + bus-master DMA)
      IOATIRProGLContext / IOATIRProSurface / IOATIRProAccelerator
      ATIR3GLContext / ATIRagePro / ATIR3Surface
```

The kext exposes three `IOUserClient` connection types via `IOServiceOpen`:

| Type | Class                 | Purpose                                             |
|------|-----------------------|-----------------------------------------------------|
| 0    | `IOATIRProSurface`    | a render/presentation surface (the drawable)        |
| 1    | `IOATIRProGLContext`  | the GL rendering context (command submission)       |
| 2    | `IOATIRPro2DContext`  | 2D context (WindowServer; not used by a GL bundle)  |

**[HW]** These are the *live-verified* type numbers. Do not trust static `newUserClient` vtable
analysis for this — the factory slots are inherited `__cxa_pure_virtual` placeholders resolved at
kext load.

---

## 2. External method (selector) tables

Selectors are dispatched through `getTargetAndMethodForIndex`, which returns
`this[0x11c] + index*0x18` — a table of `IOExternalMethod` descriptors
(`{object, IOMethod[2 words], flags, count0, count1}`, stride `0x18`). `flags` is the IOKit
dispatch type: `kIOUCScalarIScalarO=0`, `kIOUCScalarIStructO=2`, `kIOUCStructIStructO=3`,
**`kIOUCScalarIStructI=4`**.

### GL context (type 1) — 15 selectors

| # | Name                  | Dispatch / notes                                       |
|---|-----------------------|--------------------------------------------------------|
| 0 | `set_surface`         | ScalarIStructI in=4; binds a surface → `ctx[0x10c]`    |
| 1 | `set_swap_rect`       |                                                        |
| 2 | `set_swap_interval`   |                                                        |
| 3 | `get_config`          | ScalarIScalarO out=3                                   |
| 4 | `get_status`          | out=1                                                  |
| 5 | `get_surface_size`    | out=4 — returns **dimensions only** (w,h,w,h)          |
| 6 | `get_surface_info`    | in=1 out=3                                             |
| 7 | `read_buffer`         | StructIStructO                                         |
| 8 | `finish`              | **fence only** — see §5.3                              |
| 9 | `wait_for_stamp`      | fence                                                  |
| 10| `new_texture`         | StructIStructO. **Repurposed + multiplexed by a kext patch** on `op` (in word 0): `0`=texel upload → VRAM memcpy, `1`=alloc VRAM (best-fit, out={offset,record}), `2`=free. The alloc/free wrap `mutex_lock(accel[0x544])` around Apple's shared-heap best-fit (islands `0x7474`/`0x7464`), so they can't race the WindowServer. Nothing but our driver calls sel 10, so multiplexing here is safe. |
| 11| `delete_texture`      | **Left STOCK.** (v3 repurposed this for the allocator and paniced — sel 11 is invoked at context teardown.) |
| 12| `become_global_shared`|                                                        |
| 13| `pageoff_texture`     |                                                        |
| 14| `scale_surface`       | stock: unused. **Currently repurposed by a peek patch into a kernel-memory read** — the driver uses it to fetch the render-target `OFF_PITCH` (§6.1). One of two kext-patch dependencies (the other multiplexes sel 10 for upload + VRAM alloc/free); a shipping build replaces this with a `get_render_target` selector and sel 10's multiplex with dedicated upload/alloc selectors. |

### Surface (type 0) — 16 selectors (relevant subset)

| #  | Name                   | Dispatch / notes                                    |
|----|------------------------|-----------------------------------------------------|
| 7  | `set_id_mode`          | ScalarIScalarO in=2 (id, mode) — register surface   |
| 9  | `set_shape`            | ScalarIStructI in=2 + `IOAccelDeviceRegion`         |
| 10 | `surface_flush`        | ScalarIScalarO in=2 (doSwap, 0) — **present**        |
| 12 | `surface_read_lock`    | ScalarIStructO — copy buffer→backing, map backing   |
| 13 | `surface_read_unlock`  |                                                     |
| 14 | `surface_write_lock`   | ScalarIStructO → `IOAccelSurfaceData`               |
| 15 | `surface_write_unlock` |                                                     |

`IOAccelSurfaceData` (from `write_lock`): `{ +0x00 address (userspace backing ptr),
+0x10 pitch(bytes), +0x14 width, +0x18 height, +0x1c bpp }`. **The backing is *system* memory**, not
VRAM (§8).

---

## 3. Memory regions (`IOConnectMapMemory`)

On the **GL context (type 1)** connection, `clientMemoryForType(type)`:

| `type` | Region                                    | Size    | Use                                    |
|--------|-------------------------------------------|---------|----------------------------------------|
| 1      | the **surface buffer** (VRAM)             | (large) | mapping it **triggers the drain** (§5) |
| 2      | the **command buffer** `ctx[0xdc]`        | `0x1000`| where the client writes `{reg,val}`    |

**[HW] This is the single most important, most counter-intuitive fact:** the command buffer you
write is **map type 2**, and the *submit trigger* is **map type 1**. (Early work had these reversed
and nothing executed.) Verified by writing a magic word to the type-2 map and reading it back through
the kernel command-buffer pointer `ctx[0xdc]` — they alias.

### 3.1 `VendorCommandBufferHeader` (the type-2 region)

```
struct {                       // 0x20 bytes, then body[]
/* 0x00 */ uint32_t head;      // reserved / queue link
/* 0x04 */ uint32_t rsvd[3];
/* 0x10 */ uint32_t capacity;  // body capacity in DWORDS = (regionsize-0x20)/4  [kext-set]
/* 0x14 */ uint32_t count;     // # body DWORDS to drain   [CLIENT sets]
/* 0x18 */ uint32_t stamp;     // submit stamp             [kext-updated]
/* 0x1c */ uint32_t flags;     // bit0 = VALID/SUBMIT       [CLIENT sets; kext clears on drain]
/* 0x20 */ uint32_t body[];    // {reg, val} u32 pairs; count = pairs*2
};
```

`init_command_buffer_header` seeds `body[0] = 0x01000000` (a leading marker) and `flags |= 1`; the
client overwrites `body[]`/`count` before triggering.

---

## 4. The register aperture (critical)

`submit_buffer` (§5.2) writes each register with `stwbrx r_val, this[0x550], r_off` — a
byte-reversing store to `MMIO_base + off`, where `MMIO_base = accel[0x550]`.

**[HW] `MMIO_base == real_register_base + 0x400`.** Proven by the working command streams: `Z_CNTL`
(canonical real `0x54c`) programs correctly at `MMIO_base + 0x14c`; `SCALE_3D_CNTL` (real `0x5fc`)
at `MMIO_base + 0x1fc`; `DST_WIDTH_HEIGHT` (real `0x6ec`) at `MMIO_base + 0x2ec` triggers a fill.
Therefore:

> **The `{reg,val}` offset a client must write to program canonical register `R` is `R − 0x400`,
> for EVERY register — 2D *and* 3D.**

- `DST_OFF_PITCH` real `0x500` → write offset `0x100`
- `DP_FRGD_CLR`   real `0x6c4` → write offset `0x2c4`
- `DST_WIDTH_HEIGHT` real `0x6ec` → write offset `0x2ec`  *(the 2D fill trigger)*
- `VERTEX_1_S`    real `0x240` → write offset `0x240 − 0x400 = 0xFFFFFE40`  *(see wrap below)*
- `ONE_OVER_AREA_UC` real `0x300` → write offset `0xFFFFFF00`  *(the 3D setup trigger)*

**[HW] Block 0 is reachable — the 32-bit wrap is the mechanism, not a bug.** The BAR spans the full
real `0x000–0x7ff`; `MMIO_base` points `0x400` **into** it (the GUI/block-1 sub-aperture base), so
the real base sits `0x400` *below* `MMIO_base`, still inside the mapping. For a block-0 register
`R < 0x400`, `off = R − 0x400` is a large unsigned value (e.g. `0x240 − 0x400 = 0xFFFFFE40`); PPC
`stwbrx` computes `EA = MMIO_base + off (mod 2³²)`, which wraps to `MMIO_base − 0x1c0 = real 0x240`
— a genuine register **inside** the BAR, never kernel memory. So a **single uniform rule** (`R −
0x400`) reaches both blocks:

> **All registers are reachable via `submit_buffer` PIO. 2D fills/blits/clears AND 3D
> SETUP/VERTEX/ONE_OVER_AREA geometry submit through the exact same path — no bus-master DMA or
> vertex-buffer subsystem is required for hardware triangles.** [HW-PROVEN: a Gouraud triangle
> rasterizes through Apple's kext via this stock PIO submit path, §10 — the engine needs no patch
> (only the render-target offset lookup does, §0/§6.1).]

> **Pitfall (cost the most).** An earlier revision special-cased block 0 as `reg < 0x400 ? reg : reg
> − 0x400`, writing SETUP/VERTEX at `MMIO_base + 0x2xx/0x3xx = real 0x6xx/0x7xx` — the *wrong*
> registers. 2D still worked (its registers are all `≥ 0x400`), the triangle submit drained cleanly,
> but the setup engine received no vertex data and rasterized nothing. The fix is the uniform `R −
> 0x400`; the wrap does the rest.

---

## 5. Command submission

### 5.1 Client procedure

1. Write `{reg, val}` pairs into `body[]` of the **type-2** buffer, using offset `= real_reg − 0x400`.
   Values are written **native (big-endian)**; the kext byte-reverses (`stwbrx`) into the LE
   registers.
2. Set `header.count = (#pairs)*2`, `header.flags |= 1` (SUBMIT).
3. **Trigger the drain** by `IOConnectMapMemory(glConnect, /*type*/1, …)` then immediately
   `IOConnectUnmapMemory`. The drain is the *side effect* of the map call.

### 5.2 What the drain does (kext side)

`clientMemoryForType(type 1)` → `ATIR3GLContext::submit_context_buffer` → (thunk `0x13b5c`) →
`ATIRagePro::submit_buffer @0x12730`. `submit_buffer`:

- caps `count ≤ 0x4000`, processes it in chunks of ≤ 8 dwords (≤ 4 pairs);
- **FIFO-gates** before each chunk (waits on `FIFO_STAT`, real `0x710`, at `MMIO+0x310`);
- for each pair, if `reg == 0x1fc` it first spins on `GUI_STAT` (a sync point), else
  `stwbrx val, MMIO_base, reg`.

There is **no register masking** — the offset you supply is used verbatim (hence the `≥0x400`
constraint above).

### 5.3 What `finish` / `wait_for_stamp` are **not**

**[HW]** `finish` (sel 8) resolves (via `ctx[0xb0]->vtable`) to
`ATIRagePro::sleepForTimeStampNoLock` — a **fence/wait only**. It does **not** drain the command
buffer. Likewise `set_surface`'s internal `vt[0x5a8]` is `update_surface` (just sets the buffer's
`flags |= 1`, i.e. *arms* it). Only `clientMemoryForType(type 1)` (= `MapMemory` type 1) actually
runs `submit_context_buffer`. This mirrors Apple's shipped `ATIRage128GLDriver` `_gldFlush`, which
calls `IOConnectMapMemory` to flush.

---

## 6. The render target

The 2D engine renders into the surface's own VRAM buffer, described by
`surf[0x12c]` (an `ATIR3SurfaceBuffer` descriptor):

```
desc[0x08] = VRAM byte offset      (e.g. 0x478000 fullscreen, 0x301000 windowed)
desc[0x10] = size in bytes         (e.g. 0x300000 = 1024*768*4)
desc[0x1c] = packed w,h
desc[0x28] = OFF_PITCH             (mach64 DST/SRC_OFF_PITCH encoding)
```

`desc[0x28]` is exactly the value to program into `DST_OFF_PITCH`:
`((pitch_pixels/8) << 22) | (byte_offset >> 3)`. `build_swap` (the presenter, §7) reads the same
`desc[0x28]` as its blit **source**, so rendering there and then flushing composites correctly.

**[HW]** The buffer offset is **kernel-internal** — `get_surface_size` returns only dimensions.
A shipping driver needs the kext to expose `surf[0x12c][0x28]` through a dedicated
`get_render_target` selector. (During RE this was read with a temporary peek patch.)

### 6.1 Priming

`surf[0x12c][0x28]` is `0` until the buffer is allocated. **After `set_surface`, prime once:**
`surface_write_lock` → `surface_write_unlock` → `surface_flush`. This (a) allocates the VRAM buffer
and (b) **clears the surface dirty bit** `surf[0x164] bit 28` so the kext's backing→buffer copy
(§8) does not later overwrite the engine's render.

---

## 7. Present

`surface_flush` (surface sel 10, args `(1,0)`) → `flush_surface` → `build_swap`: a clipped 2D blit
(via the scaler) that copies the surface buffer (`SRC_OFF_PITCH = surf[0x12c][0x28]`) to the live
scanout (`DST_OFF_PITCH = ATIRagePro[…][0x130]`). No CRTC/page-flip, so it is race-free against
Apple's framebuffer driver.

**Do not** `write_lock`/CPU-fill the backing before presenting an engine render — `write_lock` sets
the dirty bit, and `flush_surface`/`clientMemoryForType` will then copy the (stale) *system* backing
over the engine's VRAM render and clobber it.

**[HW] Reset the engine to 2D state before `surface_flush`.** `build_swap` is itself a blit through
the **shared** 2D/3D drawing engine, and it does **not** fully re-initialize engine state — it
inherits whatever the last client submission left. If your frame rendered with `Z_EN` set (depth
testing), `build_swap`'s buffer→scanout copy is then **depth-tested against your Z buffer** and drops
pixels — the render buffer is correct but the screen shows holes/white where geometry was. Fix:
append `Z_CNTL = 0` (and `SCALE_3D_CNTL = 0`) to the **tail of the frame's command batch** (keeping
it one submit) so the engine is in plain 2D state at hand-off. General rule: **any Apple kext
operation that uses the engine after your render inherits your leftover state — end each frame in a
neutral 2D state before yielding to `build_swap`.**

---

## 8. The dirty bit and the backing copy

A surface has BOTH a **VRAM buffer** (`surf[0x12c]`, engine-addressable) and a **system-memory
backing** (`write_lock` address). The kext keeps them in sync:

- `copy_buffer_from_backing_store` (sys→VRAM) — runs on flush / type-1 map **iff**
  `surf[0x164] & surf[0x180] & (1<<28)` (the "backing dirty" gate).
- `copy_buffer_to_backing_store` (VRAM→sys) — on `read_lock`.

For a pure hardware render you want the dirty bit **clear** so no copy runs. The prime flush (§6.1)
clears it; thereafter, rendering with the engine (no `write_lock`) keeps it clear.

---

## 9. Worked example — hardware `glClear` (full sequence)

```
open  surface = IOServiceOpen(ATIRagePro, type 0)
open  gl      = IOServiceOpen(ATIRagePro, type 1)

surface.set_id_mode(id, 0x24)          # 0x24 = 8888 | windowed
surface.set_shape(IOAccelDeviceRegion(w,h))
gl.set_surface(id)                      # binds surf → ctx[0x10c]

# prime: allocate the VRAM buffer + clear the dirty bit
surface.write_lock(); surface.write_unlock(); surface.flush(1,0)

cmd  = IOConnectMapMemory(gl, type 2)   # the command buffer
rt   = surf[0x12c][0x28]                 # render target OFF_PITCH  (via get_render_target)

# build the 2D solid-fill command stream, offsets = real_reg - 0x400:
body = [
  0x14c, 0,               # Z_CNTL          (real 0x54c) = 0
  0x1fc, 0,               # SCALE_3D_CNTL   (real 0x5fc) = 0   (disable scaler)
  0x330, 3,               # GUI_TRAJ_CNTL   (real 0x730) = L->R,T->B
  0x2d0, 0x60060666,      # DP_PIX_WIDTH    (real 0x6d0) 32bpp
  0x2c4, colorARGB,       # DP_FRGD_CLR     (real 0x6c4)
  0x2c8, 0xffffffff,      # DP_WRITE_MASK   (real 0x6c8)
  0x2d4, 0x00070003,      # DP_MIX          (real 0x6d4) FRGD=SRC,BKGD=DST
  0x2d8, 0x00000101,      # DP_SRC          (real 0x6d8) src=FRGD_CLR
  0x2a8, 0x1fff0000,      # SC_LEFT_RIGHT   (real 0x6a8)
  0x2b4, 0x1fff0000,      # SC_TOP_BOTTOM   (real 0x6b4)
  0x308, 0,               # CLR_CMP_CNTL    (real 0x708)
  0x100, rt,              # DST_OFF_PITCH   (real 0x500) = render target
  0x2e8, 0,               # DST_X_Y         (real 0x6e8) = (0,0)
  0x2ec, (h<<16)|w,       # DST_WIDTH_HEIGHT(real 0x6ec) = TRIGGER  ← note (h<<16)|w order
]
cmd.header.count  = len(body)
cmd.header.flags |= 1

# SUBMIT + PRESENT
tmp = IOConnectMapMemory(gl, type 1); IOConnectUnmapMemory(gl, type 1, tmp)  # drain → engine fills VRAM
surface.flush(1,0)                                                            # present
```

**[HW]** verified: full-screen red on a standalone test; a windowed AGL app's blue `glClear`
rendered by the engine and presented — end to end, on the stock kext engine (the render-target
offset it fills comes from §6.1, the one peek-patch dependency).

### 9.1 The one-line bug that cost the most

`DST_WIDTH_HEIGHT` is the fill **trigger**. Its real offset is `0x6ec` → write offset **`0x2ec`**.
Writing the *canonical low* value `0x11c` (a common mach64 doc offset) lands on a dead register at
`MMIO+0x11c` (real `0x51c`); the fill never fires and *nothing draws* even though every other
register reads back correct. Always derive offsets as `real_DRI_offset − 0x400`.

---

## 10. 3D geometry (HW-PROVEN via PIO — no DMA subsystem)

The setup engine (gouraud/textured triangles) is programmed through block-0 registers
(`SETUP_CNTL 0x304`, `VERTEX_{1,2,3}_* 0x240–0x2a8`, trigger `ONE_OVER_AREA_UC 0x300`). These are
all `< 0x400`, but they are **fully reachable via `submit_buffer` PIO** through the uniform `R −
0x400` offset and its 32-bit wrap (§4). No bus-master DMA, GART vertex buffer, or `build_swap`-style
path is required: **the same `{reg,val}` command stream that does 2D fills also rasterizes
triangles.** A red/green/blue Gouraud triangle renders correctly through Apple's stock
`ATIRagePro.kext` engine on the Rage LT Pro (the render path needs no patch; only the render-target
offset lookup does — §0/§6.1).

**Per-triangle command stream** (appended to the same command buffer, drained by the same map-type-1
trigger):

1. **State (once per state change), all via `R − 0x400`:**
   `SCALE_3D_CNTL = 0x000100c1` (real `0x5fc`), `SETUP_CNTL = 0` (real `0x304`; **`0x40` breaks the
   colour gradient** — every pixel comes out white), `Z_CNTL`, `DP_PIX_WIDTH = 0x60060666`,
   `DST_OFF_PITCH` (= the surface's `rt`, §6), `SC_LEFT_RIGHT/SC_TOP_BOTTOM` (scissor), `DP_MIX =
   0x00070007`, `DP_SRC = 0x00000505` (Gouraud: colour from the interpolated vertex ARGB),
   `DP_WRITE_MASK = 0xffffffff`.
2. **Three vertices**, each the full 7-dword block written at `VERTEX_n_S` (real `0x240 + n·0x20`)
   with a run count of 7: `{S, T, W, SPEC_ARGB, Z, ARGB, X_Y}`.
   - `X_Y = (x·4 & 0xffff)<<16 | (y·4 & 0xffff)` — screen coords in 2-sub-pixel fixed point, **Y
     flipped to top-down** (`fb_h − y`) to match the framebuffer.
   - `W = 1/w_clip` as IEEE float; **it MUST be non-zero (1.0 for 2D/ortho)** or the perspective-
     correct colour interpolation divides by garbage and saturates to white.
   - `ARGB` per vertex (Gouraud); for flat shading replicate the provoking vertex's colour to all
     three before emit.
3. **Trigger:** `ONE_OVER_AREA_UC` (real `0x300`) = IEEE float `1.0 / signed_area`.
   - **Winding:** the setup engine walks spans by the *sign* of the area; a wrong-sign (opposite-
     winding) triangle rasterizes **zero coverage** (invisible). Normalize to positive area by
     swapping two vertices when the computed area is negative — both windings must draw (GL face
     culling is resolved in software upstream).

> **The two bugs that made a correct-looking triangle invisible**, in the order they bit: (a) the
> block-0 offset special-case (§4 pitfall) sent vertices to the wrong registers; (b) after fixing
> that, negative-area winding rasterized no coverage. Each masks as "geometry submits, nothing
> draws." Verify offsets first (does `SETUP_CNTL` at `real−0x400` actually change shading?), winding
> second.

---

## 11. Sequencing rationale (why this order)

1. **`set_id_mode` → `set_shape` → `set_surface`** — the surface must be *registered*, *shaped*, and
   *bound* to `ctx[0x10c]` before any buffer exists; mapping regions before the bind NULL-derefs in
   `clientMemoryForType`.
2. **Prime before first render** — allocates the VRAM buffer (so `rt ≠ 0`) and clears the dirty bit
   (so the present's backing copy won't clobber the render).
3. **Write cmd → set count/flags → map type 1** — the drain reads `count`/`flags`, so they must be
   set *before* the trigger; the trigger is a map call because `finish` only fences.
4. **`surface_flush` after the drain** — the engine render must be in VRAM before `build_swap`
   blits it to the scanout; flush before the drain completes would present stale pixels.
5. **Per-frame (`render_ctl` slot 0x60)** — render the deferred `glClear` into the buffer *then*
   flush; a clear-only frame emits no triangle, so the lazy clear must be forced here or the
   uninitialized buffer is presented.

---

## 12. Reverse-engineering method (for reproducers)

- Disassembly: capstone PPC (`Cs(CS_ARCH_PPC, CS_MODE_32|CS_MODE_BIG_ENDIAN)`); `__text` file offset
  = vaddr + `0x33c`. `otool`/`objdump` can't handle this MH_OBJECT kext.
- **Relocated thunks** (`lis r12,0; ori r12,r12,LOW; bctr`) can't be followed statically — the high
  half is filled at load. Resolve them **at runtime**: read the live `lis`/`ori` instructions and
  recombine (`(hi<<16)|lo`), then subtract the load base.
- **Load base**: anchor on a symbol whose runtime address you can read (e.g. a known method pointer
  in an `IOExternalMethod` table), `base = runtime_ptr − static_vaddr`.
- **Never** dereference an absolute kernel address you haven't validated through a real pointer chain
  — an unmapped read panics the machine (learned the hard way).
- Verify pixel writes by reading the **framebuffer aperture VA** = `accel[0xe0]`
  (`getVRAMDescriptor` caches `IOMemoryMap::getVirtualAddress` there), then reading
  `LFB + buffer_offset` before/after a render. Use **byte-order-distinct tags** (e.g. `0x11111111`
  vs `0x22222222`) — some ARGB test colors are byte-swap-ambiguous and give false negatives.

---

## Appendix A — canonical mach64 register offsets used here (DRI)

| Register            | Real off | PIO off (`−0x400`) | Block |
|---------------------|----------|--------------------|-------|
| `Z_CNTL`            | `0x54c`  | `0x14c`            | 1     |
| `SCALE_3D_CNTL`     | `0x5fc`  | `0x1fc`            | 1     |
| `DST_OFF_PITCH`     | `0x500`  | `0x100`            | 1     |
| `SC_LEFT_RIGHT`     | `0x6a8`  | `0x2a8`            | 1     |
| `SC_TOP_BOTTOM`     | `0x6b4`  | `0x2b4`            | 1     |
| `DP_FRGD_CLR`       | `0x6c4`  | `0x2c4`            | 1     |
| `DP_WRITE_MASK`     | `0x6c8`  | `0x2c8`            | 1     |
| `DP_PIX_WIDTH`      | `0x6d0`  | `0x2d0`            | 1     |
| `DP_MIX`            | `0x6d4`  | `0x2d4`            | 1     |
| `DP_SRC`            | `0x6d8`  | `0x2d8`            | 1     |
| `DST_X_Y`           | `0x6e8`  | `0x2e8`            | 1     |
| `DST_WIDTH_HEIGHT`  | `0x6ec`  | `0x2ec`            | 1     |
| `GUI_TRAJ_CNTL`     | `0x730`  | `0x330`            | 1     |
| `CLR_CMP_CNTL`      | `0x708`  | `0x308`            | 1     |
| `FIFO_STAT`         | `0x710`  | `0x310`            | 1     |
| `GUI_STAT`          | `0x738`  | `0x338`            | 1     |
| `SETUP_CNTL`        | `0x304`  | `0xFFFFFF04` (wrap) | 0     |
| `ONE_OVER_AREA_UC`  | `0x300`  | `0xFFFFFF00` (wrap) | 0     |
| `VERTEX_{1,2,3}_*`  | `0x240…` | `0xFFFFFE40…` (wrap) | 0     |

Block-0 PIO offsets are `R − 0x400` as a 32-bit unsigned value; `stwbrx`'s modular add wraps the
effective address back to `real R`, inside the BAR (§4). Reachable, not unreachable.

---

*This specification reflects the state of the `pathb-hw-render` branch. The 2D pipeline is
production-shaped; §10 (3D) and the `get_render_target` selector are the named follow-ups.*

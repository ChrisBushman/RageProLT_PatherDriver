# ATIRagePro.kext — texture-upload selector patch (Path B)

Under Path B (driving Apple's shipped `ATIRagePro.kext`), the mach64 **texture upload** cannot be
done the normal way. The texel upload is a *host-data blit*: a run of texels streamed to the
`HOST_DATA0` FIFO port. Apple's PIO `{reg,val}` command buffer can only poke registers, not feed a
FIFO — so it expands the run into consecutive register writes, overflows the 4 KB command buffer, and
**wedges the GUI engine**. (See `gldriver/gld_texture.c` `mach64_bind_texture` / `mach64_blit_texels`.)

The fix is a tiny **kext-side upload selector**: repurpose the (broken, panic-prone) `new_texture`
GL selector (index 10) into a plain VRAM `memcpy`. The driver converts texels to ARGB8888 in
userspace, then calls the selector with `{ u32 dst_off; u32 argb[] }` in ≤8 KB chunks; the kext
copies the payload into VRAM at `dst_off`.

This patch is applied **on top of** the existing peek patch (which `setup_target` needs to read the
surface render-target `OFF_PITCH`), so it starts from `ATIRagePro.patched` and produces
`ATIRagePro.patched2`.

## The handler (`handler.py`)

19 PPC instructions written over the `new_texture` body at **vaddr `0x71e4`** (file `0x7520`,
`file = vaddr + 0x33c`). ABI for `kIOUCStructIStructO` (**verified** against the original prologue):

    r3 = this (GL context)   r4 = structInPtr   r5 = structOutPtr   r6 = structInSize

    dst_off = struct[0]                         ; first word of the input struct
    if (dst_off >= 0x800000) return 0           ; bounds guard (VRAM is 8 MB) — a bad offset
                                                ;   must NOT scribble kernel memory
    lfb = this[0xb0]/*accel*/[0xe0]/*VRAM map*/
    memcpy(lfb + dst_off, struct+4, structInSize-4)
    return 0

> **Gotcha that caused a hard crash the first time:** the input **size is `r6`, not `r5`** (`r5` is
> the *output* pointer). Reading the size from `r5` gave a huge kernel pointer → a multi-gigabyte
> runaway `memcpy` → VRAM/kernel corruption and a white-out. Always confirm the dispatch ABI.

## The relocation gotcha (`patch.py`)

Three `__text` relocations fall **inside** the overwritten region and the kext loader (kld) would
apply them at load time, corrupting the replacement:

| vaddr | type | was |
|---|---|---|
| `0x7208` | JBSR `_mutex_lock` | reloc entry @ file `0x1d914` |
| `0x7218` | HA16 (jump-table hi) | reloc entry @ file `0x1d904` |
| `0x7220` | LO16 (jump-table lo) | reloc entry @ file `0x1d8f4` |

`patch.py` **neutralizes** them by repointing each entry's `r_address` to a dead word in the orphaned
jump-table region (`0x7234/0x7238/0x723c`) — **not** zeroing (a zeroed standard reloc becomes a
fixup at vaddr 0 and corrupts the start of `__text`). It verifies the entries match before touching
anything. No methodDesc edit is needed: sel 10 is already `StructIStructO` with `count0`/`count1` =
`0xffffffff` (variable size), so an 8 KB input struct is accepted as-is.

## Reproduce

Needs `capstone` (PPC) and the two kext binaries (not in the repo — pull from the target:
`/System/Library/Extensions/ATIRagePro.kext/Contents/MacOS/ATIRagePro` = the peek-patched build →
`ATIRagePro.patched`; its `.orig` sibling = stock).

    python3 handler.py     # assemble + round-trip-verify the handler -> handler.bin
    python3 patch.py       # ATIRagePro.patched -> ATIRagePro.patched2 (handler + reloc fix), verified

`kdis.py <file> <vaddr_hex> <n>` disassembles at a vaddr (`file = vaddr + 0x33c` for `__text`).

Deploy with `../install-upload-kext.sh` (backs up stock `.orig`, installs, invalidates caches;
recovery = single-user restore of `.orig`). Result md5: `05b777e9b29417910bb93317ab8591b2`.

---

# v4 — VRAM allocator, multiplexed into sel 10 (`handler_mux.py` / `patch4.py`)

For **fullscreen** rendering the driver can't guess where to place the Z buffer + texture heap without
colliding with the WindowServer's own surfaces. The fix is to borrow Apple's own best-fit VRAM
allocator (`alloc_bestfit` on `accel[0x78]`, freed with `heap_free`) via a kext selector.

**Why not a new selector.** v3 repurposed `delete_texture` (sel 11) for this — a mistake: sel 11 is a
*live* selector, invoked at context teardown, so overriding its methodDesc **panicked on teardown**.
v4 instead **multiplexes** the allocator into sel 10 (`new_texture` — already ours, called by nothing
but our driver), dispatched on a leading op word, and leaves `delete_texture` **stock**.

**Why it panicked on first use (the real bug).** External-method dispatch is **not** serialized: the
handler ran `alloc_bestfit`/`heap_free` on the shared heap while the WindowServer touched it
concurrently. The stock methods **self-lock** `mutex_lock(accel[0x544])` first; the v3 handler didn't.
v4 wraps both heap ops in that mutex, via the same branch islands stock `delete_texture` uses:
**`mutex_lock` = island `0x7474`, `mutex_unlock` = island `0x7464`** (both verified from the stock
`delete_texture` disassembly — *not* the 0x73b8/0x7368 islands, which belong to the orphaned
`new_texture` body).

## The multiplexed handler (`handler_mux.py`)

61 PPC instructions over `0x71e4` (region `0x71e4..0x72d8`; stock `delete_texture` starts at `0x73c8`).
`StructIStructO`: `r3=this  r4=inPtr  r5=outPtr  r6=inSize`, `in = { u32 op; u32 arg; <payload> }`:

    op=0 UPLOAD: dst = this[0xb0][0xe0] + arg;  memcpy(dst, in+8, inSize-8)      ; (the old sel-10 job)
    op=1 ALLOC : rec = IOMallocPageable(0x10); rec[8]=0
                 LOCK(accel[0x544]); alloc_bestfit(accel[0x78], rec, arg/*size*/, 0x40); UNLOCK
                 out = { rec[8]/*offset, 0 on OOM*/, rec }
    op=2 FREE  : LOCK(accel[0x544]); heap_free(accel[0x78], arg/*rec*/); UNLOCK; IOFreePageable(rec,0x10)

Non-volatiles hold state across the `bl`s (r27=outPtr, r28=op, r29=arg, r30=accel, r31=record).

## Relocations (`patch4.py`)

The whole `0x71e4..0x73c8` window is peppered with `__text` relocs from the orphaned `new_texture`
body + jump table + two dead islands. `patch4.py` decodes **every** reloc (scattered and plain),
and any whose write-address lands in the window is repointed to a dead scratch word (`0x73c0`),
preserving scattered-flag bits; `PPC_RELOC_PAIR` entries are skipped. It then asserts **no** surviving
reloc writes into the executed handler bytes. It also asserts sel 10 is `StructIStructO`-variable and
**sel 11 is still stock** before writing anything.

## Reproduce

Start from `ATIRagePro.patched2` (the v2 upload build) plus the parsed reloc table `_relocs.pkl`:

    python3 handler_mux.py   # assemble + verify all 14 branch/bl targets -> handler_mux.bin
    python3 patch4.py        # ATIRagePro.patched2 -> ATIRagePro.patched4, fully verified

Deploy with `../install-kext-v4.sh` (preserves `.orig` stock + `.peek` v2 fallbacks).
Result md5: `421c9c2542a7ef3d42e1a12febcda2dd`. Revert with `../revert-kext.sh` (→ v2) or
`../revert-kext.sh orig` (→ stock).

---

# v5 — enlarge the PIO command buffer 4 KB → 64 KB (`patch5.py`)

Apple's mach64 GL is **PIO-only**: geometry is register-programmed into the type-2 command buffer
and drained by `submit_buffer` (`stwbrx val → BAR+0x400+reg_off`); the `ONE_OVER_AREA` write is the
per-triangle kick. There is **no** vertex-DMA path (`new_vertex_buffer`/`IOVertexBuffer` are dead,
unreferenced code — for Radeon-class parts in the same source base). And `IOConnectMapMemory(type-1)`
— our only submit trigger — is the **full end-of-frame present** (`build_swap` + CRTC flip), valid
exactly once per frame (a 2nd hits the `accel[0x74]` latch → skips the drain + double-flips → engine
wedge). So a frame's geometry is capped at **one command buffer**, and the stock buffer is only 4 KB =
`(0x1000-0x20)/4` = 1016 body dwords ≈ **12–15 triangles/frame**.

The drain (`submit_buffer`) already accepts up to `0x4000` dwords; only the buffer size caps us, and
it's a hardcoded constant. `patch5.py` grows it to **64 KB** (→ 16376 body dwords = 8188 `{reg,val}`
pairs ≈ **~200 triangles/frame**, safely under the `0x4000` drain cap) with **two** `li r,0x1000 →
lis r,1` edits:

| vaddr | file off | old → new | role |
|---|---|---|---|
| `0x5164` | `0x54a0` | `li r4,0x1000`→`lis r4,1` | size arg to `allocAllContextBuffers` — grows the command buffer **and both ping-pong staging buffers** + alloc-time capacity (one shared size) |
| `0x5fe4` | `0x6320` | `li r5,0x1000`→`lis r5,1` | type-2 handler's per-map capacity re-init (`init_context_buffer_header`), else it clobbers capacity back to 1016 on every map |

Both allocations flow through `IOBufferMemoryDescriptor::withOptions(opts, size, align)` (RE-verified,
handles 64 KB), and no other `0x1000` constant sits on the path — so two edits are sufficient and there
is no hidden overflow. The **driver needs no change**: `mach64_draw_triangle`'s batch limit is
`(fb_size-0x20)/8`, which at a 64 KB live map = 8188 DRI dwords → expands to ≤16376 body dwords = the
new capacity exactly.

## Reproduce

    python3 patch5.py   # ATIRagePro.patched4 -> ATIRagePro.patched5 (2 sites, verified)

Deploy with `../install-kext-v5.sh` (preserves `.orig`/`.peek`). Result md5:
`3803034d703f3439aaaea3dd200a9565`. HW-verified: flurrywin at 40 quads (80 tris) renders the FULL
swirl, `cap=16376`, **0 dropped triangles** (was ~3 circles on the 4 KB buffer). Belt-and-suspenders
check: after the type-2 map, `header[0x10]` should read `0x3ff8` (16376).

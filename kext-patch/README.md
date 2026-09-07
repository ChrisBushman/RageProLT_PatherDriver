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

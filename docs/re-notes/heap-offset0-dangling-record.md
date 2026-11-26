# The MDD kernel panics: a dangling heap record at VRAM offset 0 (Nov 20, root cause found and fixed)

## Symptom
Kernel panic in `ATIRagePro` (data access, `DAR=0x6648000C`, PC kext vaddr 0x11650 = `alloc_bestfit`'s free-list walk, reached from the type-1 map -> surface-backing allocation 0x14d10). Seen once in a heavy-frame test; a later hang (a kernel thread spinning with `accel[0x544]` held, unkillable) and runs that printed "attach failed ... run `stubctl notify` first" were the same fault in milder form.

## Root cause
Apple's VRAM heap (`accel[0x78]`) is a doubly linked list of records `{next, prev, offset, flags}`; each surface embeds one at `surface+0x9c` for its backing store. The surface-release routine (kext 0xd778) frees the backing with `heap_free` only `if (record[8] != 0)`, i.e. it takes **VRAM offset 0 to mean "never allocated"**. On a real card the display framebuffer occupies offset 0, so a backing never lands there. On the headless rig nothing does, so the heap hands offset 0 to the first surface backing. When that surface dies (process exit/kill) `heap_free` is skipped, the record stays linked, and the memory is then freed/reused: the list head points at garbage. The next allocation walks it. (`mach64_heapdump` showed the node zeroed while still linked, then holding unrelated data; `stubctl notify` re-initialises the heap, which is why runs that were preceded by a notify worked.)

## Fixes (all verified on the MDD)
1. **`stubctl notify` now reserves VRAM [0,0x1000)** right after the heap is re-initialised (a small sel-10 ALLOC that must land at offset 0, never freed), so no later record can carry offset 0 (`kext/stubfb/stubctl.c`; `notify noguard` skips it). Restores Apple's own invariant; Lombard (real framebuffer at 0) is unaffected. **[Nov 26 correction: Apple's NDRV does not place the framebuffer at 0 (it starts at least 0x440 in, with the hardware cursor image below); see apple-ndrv-framebuffer.md.]**
2. **Kernel ALLOC no longer returns an unlinked record on failure** (`kext-patch/patch8.py`, "sel-10 ALLOC failure fix"). The patched ALLOC allocated a 16-byte record, set only `record[8]=0`, ignored `alloc_bestfit`'s result and returned the record even when the allocation failed; the plugin's reject path then sent that never-linked record to FREE (`heap_free` read uninitialised neighbours). It now uses `alloc_bestfit`'s return value (r3), frees the record itself on failure and returns `out[1]=0`.
   **Lesson / my own first attempt was wrong:** I first tested `record[8]==0` for failure. A *successful* allocation at offset 0 (the guard block above!) has the same value, so the kernel freed a linked record: a second panic (`DAR=0x262DE000` = that record). The same offset-0-as-sentinel mistake as Apple's. The success test must be the allocator's return value.

## Evidence
* `tools/mach64_heapdump` (read-only list walk via the guarded peek; `watch` mode prints each change with a timestamp) showed the sequence: attach (backing node at offset 0, Z, texture records) -> process exit: backing node zeroed but still linked from the head -> memory reused with garbage (`n=0x40 p=0x8fe20000`) -> next notify re-initialises.
* With the guard: after a process exits the list is head -> guard -> one free block -> end, no dangling node.
* `tools/mach64_allocfail`: oversized ALLOC returns `record=0`; heap unchanged.
* Tests after the fixes: suite 183/183 with and without `MACH64_VDMA=1`; 8 consecutive runs with no `stubctl notify` between all attach and draw (before: "attach failed"); 150 x (heavy 1500 triangles with VDMA + multictx/tex_mip) with no notify between: 150/150 and 150/150 clean; 29 `kill -9`s of heavy DMA processes at varying moments: machine and heap list intact; with the heap then exhausted by the killed processes' leaked blocks, real ALLOC failures occur (`alloc(...) -> bad off=0x0 rec=0x0 -- reject`) and are handled (no FREE sent, rendering still correct).

## Known remaining limits (not crashes)
* (fixed Nov 20, see "Releasing a dead client's blocks" below) a killed process used to leak its Z and texture heap blocks.
* FREE and AGP_UPLOAD still take a kernel pointer from the caller without validating it, and UPLOAD does not bounds-check `dst_off`; the plugin keeps them in range. A malicious/buggy caller can still corrupt the kernel; hardening them is a separate task.

## Kernel hardening of sel 10 (Nov 20, patched10; `kext-patch/tex_handler.c`, `patch9.py`, test `tools/mach64_texops`)
The hand-assembled multiplexed handler is replaced by compiled C (spread over the old handler body and the unused sel 12/13 bodies; those two selectors now return kIOReturnUnsupported). Rules:
* **FREE**: only a record that op 1 created (flags bit 30 marker), found by walking the heap list and currently used. NULL, stale, double-freed or foreign pointers (including Apple's own embedded records) are refused (they used to be passed to `heap_free`, which writes through them).
* **UPLOAD**: destination aligned, length a non-zero multiple of 4, `[off, off+len)` inside the VRAM aperture (below the heap's end record and the 0x7ff800 register window). The first version required the destination to lie inside an op-1 block; that broke the plugin paths that legitimately place Z/texture regions by fixed guesses (suite 118/183), so the kernel now only enforces what protects the kernel (stay inside the aperture).
* **ALLOC**: allocation failure handled; success decided by the allocator's return value; failure returns record 0. Requires a >= 8-byte output buffer and a sane size.
* **AGP ops 3/4/5 removed** (took a raw kernel pointer from the caller; only an opt-in probe used them). Unknown ops, short input, small output: kIOReturnBadArgument.
Verified: `mach64_texops` 31 checks (valid ops work; every hostile request refused), suite 183/183, 60 x (heavy 3000 + multictx/tex_mip/dmaupload) clean, 15 kill -9 mid-run, heap list intact.

### Two crashes I caused while building it (both found by panic analysis, both fixed)
1. I wired `IOMallocPageable` to the island at 0x7464; that island is `mutex_unlock` (IOMallocPageable is the direct function at 0x113e8). Result: `unlock(0x10)` -> NULL+0x10 fault. patch9.py now cross-checks the call targets against the code being replaced.
2. `heap_find` used `rec == 0` to select its range mode, so FREE(NULL) passed validation and reached `heap_free(NULL)`. FREE now rejects NULL first. `mach64_texops` includes the case ("FREE NULL refused").

## Releasing a dead client's blocks (Nov 20, patched10)
Problem: nothing freed a client's op-1 records when its process died (`kill -9`, crash), so Z and texture blocks leaked until the next `stubctl notify` re-initialised the heap; repeated kills exhausted the 8 MB heap (20 kills left ~20 nodes).
Fix, all in the kext (`kext-patch/tex_handler.c`, `patch9.py`):
* An op-1 record is now 32 bytes: the 4 heap words, then the OWNER (the GL client object) at rec[4].
* `IOATIRProGLContext::clientClose` (runs when the connection closes, including when the process is killed) is hooked at 0x54a8: it branches to a 44-byte trampoline that calls the handler with `in == NULL, insize == REAP_MAGIC` (user space can never produce that) and then replays the displaced instruction. The reaper frees every record owned by the closing client, under the accel mutex.
* FREE only accepts the calling client's own records (one client can no longer free another's).
* New op 4 ALLOC_KEEP: same as op 1 with owner 0 (never reaped). `stubctl notify` uses it for the offset-0 guard, which must outlive `stubctl`.
* Space: the stock `delete_texture` (sel 11, no callers once sel 10 was replaced) is gone; its body and islands (0x73c8-0x7484) hold the new code, and sel 11 now returns kIOReturnUnsupported like 4/6/12/13.
Verified: `mach64_texops` 38 checks (adds: B cannot free A's record, B's records are released when B closes and the block is reused, ALLOC_KEEP survives, an owner-0 record cannot be freed by a client); 20 `kill -9` of 3000-triangle runs mid-flight and 5 of texture-upload runs: heap list back to just the guard; suite 184/184; 60 x (heavy 3000 + multictx/tex_mip/dmaupload/tex_churn) clean.
Mistake caught on the way: my verify script had the wrong expected branch word for the hook; bringup refused to load the kext (the safety net worked), the script was corrected.

/*
 * tex_handler.c -- hardened replacement for the multiplexed new_texture handler (GL selector 10) of the patched ATIRagePro.kext.
 * Compiled to position-independent PowerPC code by build-tex.sh and spliced over the kext by patch9.py.  Same rules as dma_handler.c: no globals,
 * no libc, no switch tables; externals are the kext's own functions (patch9.py maps each `bl` to their fixed addresses).
 *
 * ABI (StructIStructO, variable sizes): r3 = this (IOATIRProGLContext), r4 = in, r5 = out, r6 = inSize, r7 = &outSize,.
 *   in = { u32 op; u32 arg; payload }, host (big-endian) words.
 *   op 0 UPLOAD     arg = dst byte offset, payload = texel words -> VRAM.        (no output)
 *   op 1 ALLOC      arg = size            -> out = { offset, record }  (record = 0 and offset = 0 when the allocation fails)
 *   op 2 FREE       arg = record          -> frees a record that THIS client's op 1 returned
 *   op 4 ALLOC_KEEP arg = size            -> like op 1 but owner 0: not freed when the client closes (stubctl's offset-0 guard)
 *   ops 3/5 (AGP) are REFUSED: only an opt-in probe used them and they took a raw kernel pointer from the caller.
 * Hardening over the original hand-assembled handler (every one of these was an unchecked kernel write or free):
 *   - UPLOAD: destination aligned, length a non-zero multiple of 4, and [off, off+len) inside the VRAM aperture (below the heap's end record and the register
 *     window) -- so whatever dst_off the caller sends, the write can never leave the mapped VRAM.  (Offset-0 blocks are legal: failure is signalled by the
 *     allocator's return value, never by offset == 0.)
 *   - FREE: the record must be found in the heap list, be used and carry the op-1 marker -- an arbitrary or stale pointer, a double free, or one of Apple's own
 *     embedded surface records is refused (heap_free used to write through whatever it was given).
 *   - ALLOC: IOMallocPageable failure handled, the record is initialised, the allocator's result decides success, and a failed allocation returns record 0
 *     (it used to hand back an unlinked, uninitialised record that the caller then FREEd).
 *   - AGP ops removed (they trusted a caller-supplied kernel pointer).
 *   - Unknown ops, short input and too-small output buffers return kIOReturnBadArgument.
 * Everything that touches the heap or the registry runs under accel[0x544].
 */
typedef unsigned int u32;

#define IO_BADARG   0xe00002c2u
#define IO_NOMEM    0xe00002bdu
#define MARK        0x40000000u            /* flags bit 30: record was created by op 1 (bit 31 = free, low bits = alignment mask: untouched by the heap code) */
#define REC_BYTES   0x20u                 /* an op-1 record: 4 heap words + owner + pad */
#define VRAM_MAX    0x007ff800u            /* the register window starts here: never a texture, on any Rage LT Pro aperture */

extern void  mutex_lock(void *l);
extern void  mutex_unlock(void *l);
extern void *IOMallocPageable(u32 size, u32 align);
extern void  IOFreePageable(void *p, u32 size);
extern u32   alloc_bestfit(void *heap, u32 *rec, u32 size, u32 align);
extern u32   heap_free(void *heap, u32 *rec);

#define NOINLINE __attribute__((noinline))

/* first heap record: *(*(heap[8])) -- the walk alloc_bestfit does */
static inline u32 *heap_first(u32 heap)
{
    u32 a = *(u32 *)(heap + 8u);
    if (!a) return 0;
    a = *(u32 *)a;
    if (!a) return 0;
    return *(u32 **)a;
}

/* One heap walk, three questions (mode):
 *   0: does [a, a+b) lie inside the VRAM the heap manages (below the heap's end record and the register window)?   (UPLOAD)   returns 1 / 0
 *   1: is `a` a used op-1 record linked in the heap and owned by client `b`?                                        (FREE)     returns the record / 0
 *   2: first used op-1 record owned by client `a`.                                                                  (reaper)   returns the record / 0
 * UPLOAD is bounded to the VRAM aperture, not to op-1 blocks: the plugin legitimately places Z/texture regions by fixed guesses on some paths, and what the
 * kernel must prevent is a write outside the aperture (lfb + off), not a write to another client's VRAM.  An op-1 record is 32 bytes: the 4 heap words, then
 * the owner (rec[4]; 0 = persistent, never reaped: stubctl's offset-0 guard).  Only marked records are ever dereferenced beyond the heap's own 16 bytes. */
static NOINLINE u32 *heap_find(u32 heap, u32 mode, u32 a, u32 b)
{
    u32 *n = heap_first(heap), end;
    if (mode == 0u && ((a & 3u) || a >= VRAM_MAX || b > VRAM_MAX - a)) return 0;
    for (; n && n[0]; n = (u32 *)n[0]) {
        if ((n[3] & (0x80000000u | MARK)) != MARK) continue;
        if (mode == 2u) { if (n[4] == a) return n; }
        else if (mode == 1u && n == (u32 *)a) return n[4] == b ? n : 0;
    }
    if (mode != 0u || !n) return 0;
    end = n[2];
    return (a <= end && b <= end - a) ? (u32 *)1 : 0;
}

static NOINLINE u32 tex_alloc(u32 heap, u32 lock, u32 size, u32 *out, u32 owner)
{
    u32 *rec, ok;
    rec = (u32 *)IOMallocPageable(REC_BYTES, 0x10u);
    if (!rec) return IO_NOMEM;
    rec[4] = owner;
    mutex_lock((void *)lock);
    ok = alloc_bestfit((void *)heap, rec, size, 0x40u);
    if (ok) rec[3] |= MARK;
    mutex_unlock((void *)lock);
    if (ok) { out[0] = rec[2]; out[1] = (u32)rec; }
    else    { out[0] = 0; out[1] = 0; IOFreePageable(rec, REC_BYTES); }
    return 0;
}

/* UPLOAD (op 0), FREE (op 2) and REAP (op 3, from the clientClose hook): one function so the validation, lock and exit paths are shared (kext space is tight) */
static NOINLINE u32 tex_op(u32 op, u32 self, u32 accel, u32 heap, u32 lock, u32 *in, u32 insize)
{
    u32 *rec, off, nb, lfb, ok;
    if (op == 3u) {                                           /* REAP: free every record this client still owns (in == NULL here) */
        for (;;) {
            mutex_lock((void *)lock);
            rec = heap_find(heap, 2, self, 0);
            if (rec) heap_free((void *)heap, rec);
            mutex_unlock((void *)lock);
            if (!rec) return 0;
            IOFreePageable(rec, REC_BYTES);
        }
    }
    rec = (u32 *)in[1]; off = in[1]; nb = insize - 8u; lfb = *(u32 *)(accel + 0xe0u);
    if (op == 0u && (!lfb || !nb || (nb & 3u))) return IO_BADARG;
    mutex_lock((void *)lock);
    ok = op == 0u ? (u32)heap_find(heap, 0, off, nb) : (u32)heap_find(heap, 1, (u32)rec, self);   /* FREE: only the calling client's own records */
    if (ok && op == 2u) heap_free((void *)heap, rec);
    mutex_unlock((void *)lock);
    if (!ok) return IO_BADARG;
    if (op == 2u) { IOFreePageable(rec, REC_BYTES); return 0; }
    for (ok = 0; ok < nb / 4u; ok++) ((volatile u32 *)(lfb + off))[ok] = in[2u + ok];
    return 0;
}

/* in == NULL with insize == REAP_MAGIC is how patch9.py's clientClose hook asks for the reaper (user space can never produce a NULL input: IOKit always passes a
 * kernel copy of the struct, and an insize of 0 for an empty one): free every record this client still owns -- a killed process never sends FREE. */
#define REAP_MAGIC 0xfffffffeu
u32 tex_handler(void *self, u32 *in, u32 *out, u32 insize, u32 *outsize)
{
    u32 accel, op, arg, heap, lock;
    accel = *(u32 *)((u32)self + 0xb0u);
    heap = *(u32 *)(accel + 0x78u);
    lock = *(u32 *)(accel + 0x544u);
    if (!heap || !lock) return IO_BADARG;
    if (!in) return insize == REAP_MAGIC ? tex_op(3u, (u32)self, accel, heap, lock, in, insize) : IO_BADARG;
    if (insize < 8u) return IO_BADARG;
    op = in[0]; arg = in[1];
    if (op == 0u || op == 2u) return tex_op(op, (u32)self, accel, heap, lock, in, insize);
    if (op == 1u || op == 4u) {                               /* 1 = ALLOC owned by this client; 4 = ALLOC persistent (owner 0: survives the client) */
        if (!out || !outsize || *outsize < 8u || !arg) return IO_BADARG;     /* an oversized request simply fails in alloc_bestfit (record 0) */
        return tex_alloc(heap, lock, arg, out, op == 1u ? (u32)self : 0u);
    }
    return IO_BADARG;
}

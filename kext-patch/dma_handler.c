/*
 * dma_handler.c -- kernel-side bus-master DMA submit selector for the patched ATIRagePro.kext (vertex-DMA plan, docs/re-notes/vertex-dma-plan.md).
 *
 * Replaces the body of IOATIRProGLContext::read_buffer (selector 7, vaddr 0x6c50; the plugin never calls it).  NOT a normal C program: it is compiled to
 * position-independent machine code by build-dma.sh and spliced over the kext by patch8.py.  Rules for this file: no globals, no string/float constants, no
 * switch tables, no libc calls; the only external calls are mutex_lock / mutex_unlock / IOMallocContiguous (patch8.py rewrites each `bl` to the kext's own
 * long-branch islands for those imports).  State lives in a small data block inside the patched region, passed in r8 by the asm wrapper.
 *
 * ABI (StructIStructO, variable sizes):  r3 = this (IOATIRProGLContext), r4 = in, r5 = out, r6 = inSize, r7 = &outSize, r8 = state block.
 *   in  = { u32 op; u32 arg; payload }  (all host-endian = big-endian; the kernel byte-swaps to the little-endian words the engine reads)
 *   op 0 RESET     discard the staged stream
 *   op 1 APPEND    payload = register-write packets {DMAREG(reg)|((n-1)<<16), n values}; appended to the staging buffer (<= 128 KB total)
 *   op 2 SUBMIT    Runs the staged stream through the GUI bus-master
 *                  (Path A's proven recipe), waits for BM_GUI_TABLE == table end && GUI idle, restores BUS_CNTL/SRC_CNTL/BM_GUI_TABLE_CMD, clears the stream.
 *   op 7 SELFTEST  store/load a word in the state block (no hardware); out = { magic, staging va, staging pa, staged bytes } (raw allocation result even if it was refused)
 *   out (if the caller gave room) = { u32 ndesc; u32 bytes; u32 ring_phys; u32 data_phys }
 * Returns an IOReturn.  Everything runs under accel[0x544] (the mutex that serialises GL and 2D drains), so it cannot interleave with WindowServer.
 */
typedef unsigned int u32;

#define IO_OK       0u
#define IO_ERR      0xe00002bcu
#define IO_NOMEM    0xe00002bdu
#define IO_BADARG   0xe00002c2u
#define IO_TIMEOUT  0xe00002d6u

#define STAGE_BYTES 0x20000u               /* stream buffer (128 KB) */
#define RING_BYTES  0x4000u                /* descriptor ring, 16 KB aligned (engine requirement) */
#define ALLOC_BYTES (RING_BYTES + STAGE_BYTES)
#define PHYS_MAXEND 0xffffffffu            /* the card masters 32-bit PCI addresses: whole allocation must end below 4 GB */
#define MAGIC       0x444d4131u            /* 'DMA1' */
#define CHUNK       4092u                  /* <= 4095 and 4-aligned: one descriptor's byte count */

/* registers (DRI numbering); engine offset from accel[0x550] (= BAR + 0x400) is reg - 0x400 (negative for block 0) */
#define OFF(r)          ((int)(r) - 0x400)
#define R_BUS_CNTL      0x04a0u
#define R_DST_HEIGHT_W  0x0518u
#define R_SRC_CNTL      0x05b4u
#define R_BM_GUI_TABLE  0x01b8u
#define R_BM_TABLE_CMD  0x064cu
#define R_GUI_STAT      0x0738u
#define R_FIFO_STAT     0x0710u
#define APERTURE_BM_ADDR 0x007ffe48u       /* APERTURE_OFFSET 0x7ff800 + BM_ADDR 0x648: where the descriptor walk streams the data */

struct st { u32 magic, va, pa, len; };

extern void  mutex_lock(void *l);
extern void  mutex_unlock(void *l);
extern void *IOMallocContiguous(unsigned long size, unsigned long align, unsigned long *phys);

static inline u32 rdle(u32 base, int off) { u32 v; __asm__ volatile("lwbrx %0,%1,%2" : "=r"(v) : "b"(base), "r"(off) : "memory"); return v; }
static inline void wrle(u32 base, int off, u32 v) { __asm__ volatile("stwbrx %0,%1,%2; eieio" :: "r"(v), "b"(base), "r"(off) : "memory"); }
static inline void stle(u32 addr, u32 v) { __asm__ volatile("stwbrx %0,0,%1" :: "r"(v), "r"(addr) : "memory"); }
static inline u32 ldle(u32 addr) { u32 v; __asm__ volatile("lwbrx %0,0,%1" : "=r"(v) : "r"(addr) : "memory"); return v; }
static inline void flush(u32 a, u32 n) { u32 e = a + n; a &= ~31u; for (; a < e; a += 32u) __asm__ volatile("dcbf 0,%0" :: "r"(a) : "memory"); __asm__ volatile("sync" ::: "memory"); }

static int gui_idle(u32 regs)
{
    u32 i;
    for (i = 0; i < 2000000u; i++)
        if (!(rdle(regs, OFF(R_FIFO_STAT)) & 0xffffu) && !(rdle(regs, OFF(R_GUI_STAT)) & 1u) && !(rdle(regs, OFF(R_GUI_STAT)) & 1u)) return 1;   /* FIFO empty and engine idle (as Path A's waitIdle) */
    return 0;
}

/* registers the stream must never write: the bus-master controls, BUS_CNTL, SRC_CNTL (we write the terminator ourselves) -- packet register indexes (ADRINDEX) */
static inline int denied(u32 idx)
{
    return idx == 0x6du || idx == 0x28u || idx == 0x92u || idx == 0x93u || idx == 0x16eu || idx == 0x16fu || (idx >= 0x160u && idx <= 0x163u);
}

#define NOINLINE __attribute__((noinline))

/* validate the staged stream: every packet well formed, exactly filling `len`, no denied register.  returns IO_OK / IO_BADARG */
static NOINLINE u32 dma_validate(u32 data, u32 len)
{
    u32 p = 0;
    while (p < len) {
        u32 h = ldle(data + p), idx = h & 0xffffu, n = (h >> 16) + 1u, j;
        if ((h & 0xfe00u) || (h >> 16) >= 0x400u || p + 4u + 4u * n > len) return IO_BADARG;
        for (j = 0; j < n; j++) if (denied(idx + j)) return IO_BADARG;
        p += 4u + 4u * n;
    }
    return IO_OK;
}

/* terminator + descriptor table for `len` staged bytes; returns the descriptor count (total = len + 8 bytes are streamed) */
static NOINLINE u32 dma_build(u32 ring, u32 data, u32 datapa, u32 len)
{
    u32 total, nd, i;
    stle(data + len, 0x6du); stle(data + len + 4u, 0);      /* terminator: SRC_CNTL = 0 (as the DRI/Path A do) */
    total = len + 8u; nd = (total + CHUNK - 1u) / CHUNK;
    for (i = 0; i < nd; i++) {
        u32 off = i * CHUNK, n = total - off; if (n > CHUNK) n = CHUNK;
        stle(ring + 16u * i, APERTURE_BM_ADDR);
        stle(ring + 16u * i + 4u, datapa + off);
        stle(ring + 16u * i + 8u, n | 0x40000000u | ((off + n >= total) ? 0x80000000u : 0u));   /* HOLD_OFFSET always; EOL on the last */
        stle(ring + 16u * i + 12u, 0);
    }
    flush(ring, nd * 16u); flush(data, total);
    return nd;
}

/* run the descriptor table at ringpa through the GUI bus-master and wait for table end + idle.  returns IO_OK / IO_TIMEOUT */
static NOINLINE u32 dma_run(u32 regs, u32 ringpa, u32 end)
{
    u32 sbus, ssrc, stbl, t, ok = 0;
    if (!gui_idle(regs)) return IO_TIMEOUT;
    sbus = rdle(regs, OFF(R_BUS_CNTL)); ssrc = rdle(regs, OFF(R_SRC_CNTL)); stbl = rdle(regs, OFF(R_BM_GUI_TABLE));
    wrle(regs, OFF(R_BUS_CNTL), (sbus & ~0x40u) | 0x08000000u);            /* bus mastering on, extended registers on */
    wrle(regs, OFF(R_BM_TABLE_CMD), ringpa);                               /* 16 KB circular size = 0 */
    wrle(regs, OFF(R_SRC_CNTL), (1u << 8) | (1u << 9) | (3u << 10));      /* BM_ENABLE | BM_SYNC | SYSTEM_TO_REG */
    __asm__ volatile("sync" ::: "memory");
    wrle(regs, OFF(R_DST_HEIGHT_W), 0);                                    /* kick the descriptor walk */
    for (t = 0; t < 8u; t++) (void)rdle(regs, OFF(R_GUI_STAT));            /* ride out the ~1 us before GUI_ACTIVE rises */
    for (t = 0; t < 4000000u; t++)
        if (rdle(regs, OFF(R_BM_GUI_TABLE)) == end) { ok = gui_idle(regs); break; }               /* descriptor walk reached the table end, then the engine drained */
    wrle(regs, OFF(R_SRC_CNTL), ssrc);
    wrle(regs, OFF(R_BM_TABLE_CMD), stbl & 0xfffffff0u);
    wrle(regs, OFF(R_BUS_CNTL), sbus);
    return ok ? IO_OK : IO_TIMEOUT;
}

u32 dma_handler(void *self, u32 *in, u32 *out, u32 insize, u32 *outsize, struct st *s)
{
    u32 accel, mtx, regs, op, arg, r = IO_OK;
    if (!in || insize < 8u) return IO_BADARG;
    op = in[0]; arg = in[1];
    accel = *(u32 *)((u32)self + 0xb0u);
    if (accel < 0x10000u) return IO_BADARG;
    mtx  = *(u32 *)(accel + 0x544u);
    regs = *(u32 *)(accel + 0x550u);
    if (!mtx || regs < 0x10000u) return IO_BADARG;
    mutex_lock((void *)mtx);
    if (op == 7u) {                                         /* SELFTEST: is the state block writable? */
        u32 keep = s->len;
        s->len = 0x5a5a1234u ^ arg;
        if (s->len != (0x5a5a1234u ^ arg)) r = IO_ERR;
        s->len = keep;
    } else if (op == 0u) {
        s->len = 0;
    } else if (op == 1u) {                                  /* APPEND */
        u32 nb = insize - 8u, k;
        if (s->magic != MAGIC) {
            unsigned long pa = 0; u32 adj; void *va = IOMallocContiguous(ALLOC_BYTES + RING_BYTES, 4096u, &pa);
            s->va = (u32)va; s->pa = (u32)pa;
            /* IOMallocContiguous only aligns the VIRTUAL address (measured: va 0x309c4000, pa 0x047bf000), so over-allocate and align the physical address ourselves */
            adj = (0u - (u32)pa) & (RING_BYTES - 1u);
            if (!va || !pa || pa > PHYS_MAXEND - ALLOC_BYTES - RING_BYTES) r = IO_NOMEM;
            else { s->va = (u32)va + adj; s->pa = (u32)pa + adj; s->len = 0; s->magic = MAGIC; }
        }
        if (r == IO_OK && (nb == 0u || (nb & 3u) || s->len + nb > STAGE_BYTES - 8u)) r = IO_BADARG;
        if (r == IO_OK) {
            u32 dst = s->va + RING_BYTES + s->len;
            for (k = 0; k < nb / 4u; k++) stle(dst + 4u * k, in[2u + k]);
            s->len += nb;
        }
    } else if (op == 2u) {                                  /* SUBMIT */
        u32 len = s->len, nd, ring, data, ringpa, datapa;
        if (s->magic != MAGIC || len == 0u) r = IO_BADARG;
        ring = s->va; data = s->va + RING_BYTES; ringpa = s->pa; datapa = s->pa + RING_BYTES;
        if (r == IO_OK) r = dma_validate(data, len);
        if (r == IO_OK) {
            nd = dma_build(ring, data, datapa, len);
            r = dma_run(regs, ringpa, ringpa + nd * 16u);
        }
        s->len = 0;
    } else r = IO_BADARG;
    mutex_unlock((void *)mtx);
    return r;
}

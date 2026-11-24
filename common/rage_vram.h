/* common/rage_vram.h - the single VRAM allocator (design 3.1 RageLTVram).  Portable, header-only, no libc, no heap, no floating point.
 *
 * One sorted table of USED extents: permanent reservations (scanout, cursor block, anything the framebuffer owns) plus client
 * allocations tagged with an owner id.  Allocation is best-fit over the gaps.  Every offset it returns lies inside the usable
 * range, is aligned, and cannot overlap a reservation or another allocation.  A client can free only what it owns, exactly once;
 * rage_vram_free_owner() releases everything a closed or crashed client held (no leaks across contexts).
 * The caller serialises access (kernel: the device lock). */
#ifndef RAGE_VRAM_H
#define RAGE_VRAM_H

#include "rage_types.h"
#include "rage_guard.h"

#define RAGE_VRAM_MAX_EXTENTS 192u
#define RAGE_OWNER_RESERVED   0u          /* never freed by a client */

struct rage_vram_extent { uint32_t off, len, owner; };
struct rage_vram {
    uint32_t n;
    struct rage_vram_extent e[RAGE_VRAM_MAX_EXTENTS];   /* sorted by off, non-overlapping */
};

static inline void rage_vram_init(struct rage_vram *v) { v->n = 0; }

static inline int rage_vram_insert(struct rage_vram *v, uint32_t at, uint32_t off, uint32_t len, uint32_t owner)
{
    uint32_t i;
    if (v->n >= RAGE_VRAM_MAX_EXTENTS) return -1;
    for (i = v->n; i > at; i--) v->e[i] = v->e[i - 1];
    v->e[at].off = off; v->e[at].len = len; v->e[at].owner = owner; v->n++;
    return 0;
}

/* Permanent reservation of an exact span.  Fails if it leaves usable VRAM or overlaps anything. */
static inline int rage_vram_reserve(struct rage_vram *v, uint32_t off, uint32_t len)
{
    uint32_t i;
    if (len == 0 || !rage_vram_span_ok(off, len)) return -1;
    for (i = 0; i < v->n; i++) {
        if (off + len <= v->e[i].off) break;                       /* fits before extent i */
        if (off < v->e[i].off + v->e[i].len) return -1;            /* overlaps */
    }
    return rage_vram_insert(v, i, off, len, RAGE_OWNER_RESERVED);
}

/* Best-fit allocation of len bytes, aligned to align (power of two, >= 8).  owner must be non-zero.  Returns 0 and *out on success. */
static inline int rage_vram_alloc(struct rage_vram *v, uint32_t len, uint32_t align, uint32_t owner, uint32_t *out)
{
    uint32_t i, best_at = 0, best_off = 0, best_gap = 0xffffffffu, prev_end = 0; int found = 0;
    if (!out || owner == RAGE_OWNER_RESERVED || len == 0) return -1;
    if (align < 8u || (align & (align - 1u))) return -1;
    len = (len + 7u) & ~7u;
    for (i = 0; i <= v->n; i++) {
        uint32_t gap_end = (i < v->n) ? v->e[i].off : RAGE_VRAM_USABLE_END;
        uint32_t start = (prev_end + align - 1u) & ~(align - 1u);
        if (start >= prev_end && start < gap_end && gap_end - start >= len && rage_vram_span_ok(start, len)) {
            uint32_t gap = gap_end - start;
            if (gap < best_gap) { best_gap = gap; best_off = start; best_at = i; found = 1; }
        }
        if (i < v->n) prev_end = v->e[i].off + v->e[i].len;
    }
    if (!found) return -1;
    if (rage_vram_insert(v, best_at, best_off, len, owner)) return -1;
    *out = best_off;
    return 0;
}

/* Free one allocation.  Must match offset and owner; reservations and foreign ranges are refused. */
static inline int rage_vram_free(struct rage_vram *v, uint32_t off, uint32_t owner)
{
    uint32_t i, j;
    if (owner == RAGE_OWNER_RESERVED) return -1;
    for (i = 0; i < v->n; i++) if (v->e[i].off == off) {
        if (v->e[i].owner != owner) return -1;
        for (j = i; j + 1 < v->n; j++) v->e[j] = v->e[j + 1];
        v->n--;
        return 0;
    }
    return -1;
}

/* Release everything a client owned (close, crash, process exit).  Returns the number of extents freed. */
static inline uint32_t rage_vram_free_owner(struct rage_vram *v, uint32_t owner)
{
    uint32_t i, w = 0, freed = 0;
    if (owner == RAGE_OWNER_RESERVED) return 0;
    for (i = 0; i < v->n; i++) { if (v->e[i].owner == owner) freed++; else v->e[w++] = v->e[i]; }
    v->n = w;
    return freed;
}

/* Does [off, off+len) belong to this owner (e.g. a texture upload target check)? */
static inline int rage_vram_owns(const struct rage_vram *v, uint32_t off, uint64_t len, uint32_t owner)
{
    uint32_t i;
    for (i = 0; i < v->n; i++)
        if (v->e[i].owner == owner && off >= v->e[i].off && off - v->e[i].off <= v->e[i].len && len <= (uint64_t)(v->e[i].len - (off - v->e[i].off)))
            return 1;
    return 0;
}

#endif

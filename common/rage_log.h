/* common/rage_log.h - the driver's event ring (design 6).  Portable, header-only, no libc, no heap, no floating point.
 *
 * Fixed-size ring of 32-byte events with a monotonically increasing 32-bit sequence number.  One writer at a time (the device
 * lock); readers copy a range and learn how many events were overwritten before they could read them.  Because Panther's kernel
 * (xnu-517) offers no panic callback to a kext, the ring is drained continuously by a user-space helper (tools/ragelt), so the
 * last events before a hang or panic are already on disk and on the network.  The text formatter is here too, so the exact line
 * format is covered by host tests. */
#ifndef RAGE_LOG_H
#define RAGE_LOG_H

#include "rage_types.h"

#define RAGE_LOG_ENTRIES 2048u                    /* 64 KB; power of two */

enum rage_ev {
    RAGE_EV_NONE = 0, RAGE_EV_START, RAGE_EV_STOP, RAGE_EV_OWNER_NEW, RAGE_EV_OWNER_FREE, RAGE_EV_VRAM_ALLOC, RAGE_EV_VRAM_FREE,
    RAGE_EV_BATCH, RAGE_EV_BATCH_REFUSED, RAGE_EV_CLIENT_OPEN, RAGE_EV_CLIENT_CLOSE, RAGE_EV_FENCE, RAGE_EV_MODE_WILL,
    RAGE_EV_MODE_DID, RAGE_EV_ERROR, RAGE_EV_MARK, RAGE_EV_COUNT
};

static const char *const rage_ev_names[RAGE_EV_COUNT] = {
    "none", "start", "stop", "owner_new", "owner_free", "vram_alloc", "vram_free", "batch", "batch_refused", "client_open",
    "client_close", "fence", "mode_will", "mode_did", "error", "mark"
};

struct rage_log_ev {                              /* 32 bytes */
    uint32_t seq;                                 /* this event's sequence number (also the validity check when reading) */
    uint32_t tick;                                /* caller-supplied time, 1 unit = 1 ms of uptime (wraps) */
    uint32_t id_owner;                            /* event id << 16 | owner (client) id */
    uint32_t a[5];
};

struct rage_log {
    uint32_t head;                                /* next sequence number to be written; events [head-N, head) are held */
    struct rage_log_ev e[RAGE_LOG_ENTRIES];
};

static inline void rage_log_init(struct rage_log *l)
{
    uint32_t i;
    l->head = 0;
    for (i = 0; i < RAGE_LOG_ENTRIES; i++) l->e[i].seq = 0xffffffffu;   /* never equals a valid expected seq until written */
}

static inline void rage_log_put(struct rage_log *l, uint32_t tick, uint32_t id, uint32_t owner,
                                uint32_t a0, uint32_t a1, uint32_t a2, uint32_t a3, uint32_t a4)
{
    struct rage_log_ev *e = &l->e[l->head & (RAGE_LOG_ENTRIES - 1u)];
    e->seq = 0xffffffffu;                         /* invalidate first: a reader that races the write sees a mismatch, not a torn event */
    e->tick = tick; e->id_owner = (id << 16) | (owner & 0xffffu);
    e->a[0] = a0; e->a[1] = a1; e->a[2] = a2; e->a[3] = a3; e->a[4] = a4;
    e->seq = l->head;
    l->head++;
}

/* Copy up to max events starting at from_seq.  *lost = events between from_seq and the oldest still held (overwritten before being
 * read).  Returns the number copied; the next read should start at from_seq + *lost + returned. */
static inline uint32_t rage_log_read(const struct rage_log *l, uint32_t from_seq, struct rage_log_ev *out, uint32_t max, uint32_t *lost)
{
    uint32_t head = l->head, oldest = (head > RAGE_LOG_ENTRIES) ? head - RAGE_LOG_ENTRIES : 0u, n = 0, s;
    *lost = 0;
    if (from_seq > head) from_seq = head;         /* a cursor from before a driver reload: nothing newer than head exists */
    if (from_seq < oldest) { *lost = oldest - from_seq; from_seq = oldest; }
    for (s = from_seq; s != head && n < max; s++) {
        const struct rage_log_ev *e = &l->e[s & (RAGE_LOG_ENTRIES - 1u)];
        if (e->seq != s) { (*lost)++; continue; } /* torn or overwritten: count it, do not return it */
        out[n++] = *e;
    }
    return n;
}

/* ---- text format, no libc: "SSSSSSSS TTTTTTTT name............. oOOOO a0 a1 a2 a3 a4\n"  (all hex).  Returns the length written,
 * or 0 if buf is too small (needs 90 bytes). */
static inline uint32_t rage_fmt_hex(char *p, uint32_t v, uint32_t digits)
{
    uint32_t i;
    for (i = 0; i < digits; i++) { uint32_t nib = (v >> (4u * (digits - 1u - i))) & 0xfu; p[i] = (char)(nib < 10u ? '0' + nib : 'a' + (nib - 10u)); }
    return digits;
}

static inline uint32_t rage_log_format(const struct rage_log_ev *e, char *buf, uint32_t size)
{
    char *p = buf; uint32_t id = e->id_owner >> 16, k = 0, i;
    const char *nm = id < RAGE_EV_COUNT ? rage_ev_names[id] : "?";
    if (size < 90u) return 0;
    p += rage_fmt_hex(p, e->seq, 8); *p++ = ' ';
    p += rage_fmt_hex(p, e->tick, 8); *p++ = ' ';
    while (nm[k] && k < 14u) *p++ = nm[k++];
    while (k < 14u) { *p++ = ' '; k++; }
    *p++ = ' '; *p++ = 'o';
    p += rage_fmt_hex(p, e->id_owner & 0xffffu, 4);
    for (i = 0; i < 5; i++) { *p++ = ' '; p += rage_fmt_hex(p, e->a[i], 8); }
    *p++ = '\n';
    return (uint32_t)(p - buf);
}

#endif

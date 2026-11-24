/* tests/log_test.c - host tests for common/rage_log.h */
#include <stdio.h>
#include <string.h>
#include "rage_log.h"

static int fails, checks;
#define CHECK(name, cond) do { checks++; if (!(cond)) { fails++; printf("FAIL  %s\n", name); } } while (0)
static struct rage_log L;

int main(void)
{
    struct rage_log_ev out[64]; uint32_t lost, n, i; char line[128]; uint32_t len;
    rage_log_init(&L);
    CHECK("empty: nothing to read", rage_log_read(&L, 0, out, 64, &lost) == 0 && lost == 0);
    rage_log_put(&L, 100, RAGE_EV_START, 0, 1, 2, 3, 4, 5);
    rage_log_put(&L, 101, RAGE_EV_VRAM_ALLOC, 7, 0x1000, 0x2000, 0, 0, 0);
    n = rage_log_read(&L, 0, out, 64, &lost);
    CHECK("two events read in order", n == 2 && lost == 0 && out[0].seq == 0 && out[1].seq == 1 && out[1].a[0] == 0x1000);
    CHECK("id and owner packed", (out[1].id_owner >> 16) == RAGE_EV_VRAM_ALLOC && (out[1].id_owner & 0xffff) == 7);
    CHECK("cursor resume reads only new events", rage_log_read(&L, 2, out, 64, &lost) == 0 && lost == 0);
    rage_log_put(&L, 102, RAGE_EV_MARK, 0, 9, 9, 9, 9, 9);
    CHECK("resume sees exactly the new one", rage_log_read(&L, 2, out, 64, &lost) == 1 && out[0].seq == 2 && out[0].a[4] == 9);
    CHECK("max respected", rage_log_read(&L, 0, out, 2, &lost) == 2);
    CHECK("cursor in the future is clamped, not read", rage_log_read(&L, 999, out, 64, &lost) == 0 && lost == 0);

    for (i = 0; i < RAGE_LOG_ENTRIES + 10; i++) rage_log_put(&L, i, RAGE_EV_BATCH, 1, i, 0, 0, 0, 0);   /* wraps */
    {   static struct rage_log_ev big[RAGE_LOG_ENTRIES + 16]; uint32_t total = L.head, got;
        got = rage_log_read(&L, 0, big, RAGE_LOG_ENTRIES + 16, &lost);
        CHECK("wrap: only the newest N are held", got == RAGE_LOG_ENTRIES);
        CHECK("wrap: lost counts what was overwritten", lost == total - RAGE_LOG_ENTRIES);
        CHECK("wrap: events returned are the newest, in order", big[0].seq == total - RAGE_LOG_ENTRIES && big[got - 1].seq == total - 1);
        { uint32_t k, ok = 1; for (k = 1; k < got; k++) if (big[k].seq != big[k - 1].seq + 1) ok = 0; CHECK("wrap: contiguous", ok); }
    }
    /* torn event: a writer was interrupted between invalidating and finishing */
    { uint32_t h = L.head; L.e[(h - 1) & (RAGE_LOG_ENTRIES - 1)].seq = 0xffffffffu;
      CHECK("torn newest event is skipped and counted", rage_log_read(&L, h - 1, out, 4, &lost) == 0 && lost == 1); }

    memset(line, '#', sizeof line);
    { struct rage_log_ev e = { 0x1234, 0xabcd, (RAGE_EV_BATCH_REFUSED << 16) | 0x2a, { 0x540, 0xdeadbeefu, 0, 1, 0xffffffffu } };
      len = rage_log_format(&e, line, sizeof line);
      CHECK("format: exact line", len == 84u && !memcmp(line, "00001234 0000abcd batch_refused  o002a 00000540 deadbeef 00000000 00000001 ffffffff\n", 84));
      CHECK("format: nothing written past the line", line[84] == '#'); }
    { struct rage_log_ev e = { 1, 2, (200u << 16), { 0, 0, 0, 0, 0 } };
      len = rage_log_format(&e, line, sizeof line);
      CHECK("format: unknown id prints '?'", len > 0 && !memcmp(line + 18, "?             ", 14)); }
    CHECK("format: too-small buffer refused", rage_log_format(&out[0], line, 89) == 0);
    printf("log_test: %d checks, %d failed\n", checks, fails);
    return fails ? 1 : 0;
}

/* tools/ragegl/sleep_sim.c - sleep/wake plan tier A2/B3 on the G4: inject the framebuffer notifications into the kext through the debug selector (RAGE_GL_CONFIG key 20, value = event | info << 8;
 * needs a kext built with RAGE_DEBUG_ENGTEST=1) and check the chip gate.  The chip stays live on the G4, so every refusal is the gate's doing (negative control = the same call succeeds awake).
 * For every chip-touching call: awake -> succeeds; after the "will" events -> kIOReturnNotReady; after the matching "did" events -> succeeds again; for several event orders. */
#include "ragegl.h"
#include <stdlib.h>
#define NOTREADY ((kern_return_t)0xe00002d8)
static ragegl_t c; static int npass, nfail, buf_off;
#define CHECK(n, cond) do { if (cond) { npass++; printf("  PASS  %s\n", n); } else { nfail++; printf("  FAIL  %s\n", n); } } while (0)
static void ev(int event, int info) { ragegl_config(c, 20, (uint32_t)event | ((uint32_t)info << 8)); }
static uint8_t data[16384], back[16384];
/* run every chip-touching call; returns a bitmask of the ones that returned kIOReturnNotReady and (via *ok) of the ones that returned success */
static void probe(uint32_t *nr, uint32_t *ok, uint32_t *total)
{
    uint32_t i = 0, w[2 * 64], done, a, b, d1, d2, d3, d4, d5; kern_return_t kr; *nr = *ok = 0;
    (void)ragegl_target(c, buf_off, 1024, 2, 64, 64);                   /* a sleep or mode cycle invalidates every client's target (by design): set it again first, as a real client does (not counted) */
#define T(call) do { kr = (call); if (kr == NOTREADY) *nr |= 1u << i; if (kr == 0) *ok |= 1u << i; i++; } while (0)
    T(ragegl_vwrite(c, buf_off, data, 16384, 3));                        /* CPU upload (vramCopyIn)   */
    T(ragegl_vread(c, buf_off, back, 16384, 0));                         /* vramCopyOut               */
    T(ragegl_scanout_read(c, 0, back, 64));                              /* scanoutCopyOut            */
    T(ragegl_fill(c, 0, 0, 8, 8, 0x1234));                               /* fillRect                  */
    T(ragegl_present(c, 0, 0, 8, 8, 0, 0));                              /* presentEx                 */
    T(ragegl_target(c, buf_off, 1024, 2, 64, 64));                       /* setRenderTarget           */
    T(ragegl_overlay_on(c, buf_off, 640, 64, 64, 0, 0, 128, 128, 2));    /* ovlOn                     */
    ragegl_overlay_off(c);
    T(ragegl_config(c, 2, 1) == 0 ? ragegl_vwrite(c, buf_off, data, 16384, 3) : -1);   /* DMA upload path (xferReserve/xferKick) */
    ragegl_config(c, 2, 0);
    T(RGL_CALL(RAGE_GL_REG_DUMP, 2, 0, (uint32_t)(uintptr_t)w, sizeof w));            /* regDump (debug) */
    (void)a; (void)b; (void)d1; (void)d2; (void)d3; (void)d4; (void)d5; (void)done;
    *total = i;
}
static int gate_ok(void) { uint32_t g, w, h, p, b, base; ragegl_display(c, &g, &w, &h, &p, &b, &base); return !(g & 1u); }
static void expect_closed(const char *name) { uint32_t nr, ok, n; probe(&nr, &ok, &n); CHECK(name, ok == 0 && nr == (1u << n) - 1u && !gate_ok()); if (nr != (1u << n) - 1u || ok) printf("        NotReady mask 0x%x ok mask 0x%x of %u calls\n", nr, ok, n); }
static void expect_open(const char *name) { uint32_t nr, ok, n; probe(&nr, &ok, &n); CHECK(name, nr == 0 && gate_ok()); if (nr) printf("        NotReady mask 0x%x ok mask 0x%x of %u calls\n", nr, ok, n); }
int main(void)
{
    uint32_t i, g, f, e, fl; ragegl_t c2;
    setvbuf(stdout, NULL, _IONBF, 0);
    if (ragegl_open(&c)) { printf("cannot open the GL client\n"); return 99; }
    if (ragegl_info(c, &g, &f, &e, &fl)) return 99;
    for (i = 0; i < sizeof data; i++) data[i] = (uint8_t)(i * 7 + 3);
    if (ragegl_alloc(c, 1024 * 2 * 64, 4096, (uint32_t *)&buf_off)) { printf("alloc failed\n"); return 1; }
    printf("-- baseline (awake): every call works (negative control)\n");
    { uint32_t nr, ok, n; probe(&nr, &ok, &n); printf("        %u chip-touching calls, ok mask 0x%x\n", n, ok); CHECK("awake: no call is refused", nr == 0); CHECK("awake: calls succeed", ok != 0); }
    printf("-- system sleep (3,1) / wake (4,1)\n");
    ev(3, 1); expect_closed("after (3,1): every call refused, generation odd");
    CHECK("a new GL client cannot open while asleep", ragegl_open(&c2) != 0);
    ev(4, 1); expect_open("after (4,1): gate open again, calls work");
    printf("-- the NDRV order (3,0)(3,1)(4,1)(4,0)\n");
    ev(3, 0); ev(3, 1); expect_closed("closed after both wills");
    ev(4, 1); expect_closed("still closed after (4,1): the display change is not finished");
    ev(4, 0); expect_open("open after (4,0)");
    printf("-- the other order (3,1)(3,0)(4,0)(4,1)\n");
    ev(3, 1); ev(3, 0); ev(4, 0); expect_closed("(4,0) while asleep does not open the gate");
    ev(4, 1); expect_open("(4,1) opens it");
    printf("-- duplicates, unmatched events, mode change inside sleep\n");
    ev(3, 1); ev(3, 1); expect_closed("duplicate sleep: closed"); ev(4, 1); expect_open("one wake opens it");
    ev(4, 1); expect_open("an extra wake changes nothing"); ev(2, 0); ev(4, 0); expect_open("an unmatched did changes nothing");
    ev(3, 1); ev(1, 0); ev(2, 0); expect_closed("a mode change inside sleep does not open the gate"); ev(4, 1); expect_open("wake opens it");
    ev(5, 0); ev(6, 0); ev(7, 0); ev(8, 0); ev(9, 0); ev(10, 0); expect_open("events 5-10 are ignored");
    printf("-- two full cycles, then real work still correct\n");
    ev(3, 1); ev(4, 1); ev(3, 1); ev(4, 1); expect_open("two cycles");
    memset(back, 0, sizeof back); ragegl_vwrite(c, buf_off, data, 16384, 0); ragegl_vread(c, buf_off, back, 16384, 0);
    CHECK("an upload after the cycles reads back byte-exact", memcmp(data, back, sizeof data) == 0);
    ev(4, 1); ev(4, 0);                                                 /* leave the kext awake whatever happened above */
    ragegl_free(c, buf_off); ragegl_close(c);
    printf("%s: %d passed, %d failed\n", nfail ? "FAILED" : "ALL PASSED", npass, nfail);
    return nfail != 0;
}

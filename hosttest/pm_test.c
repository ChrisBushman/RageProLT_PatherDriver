/* pm_test.c: host test of the framebuffer power/mode-change event state machine (common/rage_pm.h) - the same code the kext runs.
 * Feeds every sequence of the sleep/wake plan (tier A1) and checks the gate (chip may be touched), the generation parity, idempotence and the pairing counters.
 * Build: cc -O2 -Wall -I../common -o pm_test pm_test.c */
#include <stdio.h>
#include <string.h>
#include "rage_pm.h"
static int npass, nfail;
#define CHECK(n, c) do { if (c) npass++; else { nfail++; printf("FAIL %s (line %d)\n", n, __LINE__); } } while (0)

/* the model of the kext's event handler (RageLTMode.cpp fbEvent): the verification outcome of a wake is a parameter */
static struct rage_pm pm; static uint32_t gen; static int wills, dids, wake_ok = 1, repairs;
enum { WILLCHG = 1, DIDCHG = 2, WILLSLP = 3, DIDWAKE = 4 };
static void run(int act) { if (act & RAGE_PM_RUN_WILL) { gen++; wills++; } if (act & RAGE_PM_RUN_DID) { gen++; dids++; } }
static void ev(int event, int info)
{
    if (event == WILLCHG || (event == WILLSLP && !info)) run(rage_pm_will(&pm));
    else if (event == DIDCHG || (event == DIDWAKE && !info)) run(rage_pm_did(&pm));
    else if (event == WILLSLP) { int a = rage_pm_sleep_begin(&pm); run(a); if (!(a & RAGE_PM_DUP)) rage_pm_sleep_commit(&pm); }
    else if (event == DIDWAKE) {
        int w = wake_ok ? rage_pm_wake_check(0x1002, 6, 0xd0000000u) : rage_pm_wake_check(0x1002, 6, 0);
        if (w < 0) return;                                  /* stay asleep */
        if (w > 0) repairs++;
        run(rage_pm_wake_end(&pm));
    }
}
static void reset(void) { rage_pm_init(&pm); gen = 0; wills = dids = 0; wake_ok = 1; repairs = 0; }
static int ok(void) { return rage_pm_chip_ok(&pm, gen); }
static void seq(const char *name, const int (*e)[2], int n, int want_ok_end, int want_wills, int want_dids)
{
    int i; reset();
    for (i = 0; i < n; i++) {
        ev(e[i][0], e[i][1]);
        if (pm.asleep) CHECK(name, !ok());                                  /* asleep: the gate is always closed */
        if (pm.depth) CHECK(name, !ok());                                   /* any outstanding will: closed */
    }
    CHECK(name, ok() == want_ok_end); CHECK(name, wills == want_wills && dids == want_dids);
    if (want_ok_end) CHECK(name, pm.depth == 0 && !pm.asleep && !pm.sleep_depth);
}
int main(void)
{
    static const int mode[][2]    = { {1,0},{2,0} };
    static const int dispslp[][2] = { {3,0},{4,0} };
    static const int sys[][2]     = { {3,1},{4,1} };
    static const int lombard[][2] = { {3,0},{3,1},{4,1},{4,0} };                      /* the NDRV order (plan 2.2): (3,0) from IOFramebuffer, then the NDRV's pair */
    static const int swapped[][2] = { {3,1},{3,0},{4,0},{4,1} };                      /* the other interleaving */
    static const int nested[][2]  = { {1,0},{3,0},{3,1},{4,1},{4,0},{2,0} };
    static const int didonly[][2] = { {2,0},{4,0},{4,1} };                            /* did without will: ignored, nothing opens or closes */
    static const int willonly[][2]= { {1,0} };                                        /* will without did: gate stays closed (a bug to see, not to hide) */
    static const int dupsleep[][2]= { {3,1},{3,1},{4,1} };                            /* duplicate sleep is idempotent */
    static const int dupwake[][2] = { {3,1},{4,1},{4,1} };                            /* duplicate wake */
    static const int twice[][2]   = { {3,1},{4,1},{3,1},{4,1} };                      /* two full cycles */
    static const int modeinsleep[][2] = { {3,1},{1,0},{2,0},{4,1} };                  /* a mode change while asleep must not reopen the gate */
    static const int events510[][2]   = { {5,0},{6,0},{7,0},{8,0},{9,0},{10,0} };     /* other events are not ours */
    seq("mode change", mode, 2, 1, 1, 1);
    seq("display sleep (3,0)(4,0)", dispslp, 2, 1, 1, 1);
    seq("system sleep (3,1)(4,1)", sys, 2, 1, 1, 1);
    seq("NDRV order (3,0)(3,1)(4,1)(4,0)", lombard, 4, 1, 1, 1);
    seq("other interleaving (3,1)(3,0)(4,0)(4,1)", swapped, 4, 1, 1, 1);
    seq("nested", nested, 6, 1, 1, 1);
    seq("did without will", didonly, 3, 1, 0, 0);
    seq("will without did stays closed", willonly, 1, 0, 1, 0);
    seq("duplicate sleep", dupsleep, 3, 1, 1, 1);
    seq("duplicate wake", dupwake, 3, 1, 1, 1);
    seq("two cycles", twice, 4, 1, 2, 2);
    seq("mode change while asleep", modeinsleep, 4, 1, 1, 1);
    seq("other events ignored", events510, 6, 1, 0, 0);

    /* a mid-sequence check that the gate stays closed after (3,1) until (4,1) even if the did of the mode change arrives first */
    reset(); ev(3,0); ev(3,1); ev(4,0); CHECK("(4,0) while asleep does not open the gate", !ok() && pm.asleep && dids == 0);
    ev(4,1); CHECK("(4,1) opens it", ok() && dids == 1);
    /* wake verification failure: the card does not decode: stay asleep (gate closed) and a later good wake recovers */
    reset(); wake_ok = 0; ev(3,1); ev(4,1); CHECK("bad wake: stays asleep", pm.asleep && !ok() && dids == 0);
    wake_ok = 1; ev(4,1); CHECK("a later good wake recovers", !pm.asleep && ok() && dids == 1);
    /* the wake-check function */
    CHECK("check: good", rage_pm_wake_check(0x1002, 0x6, 0xd0000000u) == 0);
    CHECK("check: memory decode off is repaired", rage_pm_wake_check(0x1002, 0x4, 0xd0000000u) == 1);
    CHECK("check: vendor wrong", rage_pm_wake_check(0xffff, 0x6, 0xd0000000u) == -1);
    CHECK("check: BAR0 zero", rage_pm_wake_check(0x1002, 0x6, 0) == -1);
    CHECK("check: BAR0 all ones (card gone)", rage_pm_wake_check(0x1002, 0x6, 0xffffffffu) == -1);
    CHECK("check: BAR0 is an I/O BAR", rage_pm_wake_check(0x1002, 0x6, 0xd0000001u) == -1);
    /* randomised: any event soup must keep the invariants (counters never negative, asleep => closed, closed unless fully paired) */
    { uint32_t s = 12345; int k, i; for (k = 0; k < 2000; k++) { reset(); for (i = 0; i < 12; i++) { s = s * 1664525u + 1013904223u; ev(1 + (s >> 28) % 4, (s >> 24) & 1); CHECK("soup: asleep => closed", !pm.asleep || !ok()); CHECK("soup: depth>0 => closed", !pm.depth || !ok());
        CHECK("soup: parity", ((gen & 1u) != 0) == (pm.depth != 0)); } } }
    printf("pm_test: %d passed, %d failed\n", npass, nfail);
    return nfail != 0;
}

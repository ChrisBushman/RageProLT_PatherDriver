/* rage_pm.h - the framebuffer power / mode-change event state machine of the kernel driver (sleep/wake plan, docs/re-notes/sleep-wake-plan.md section 6).
 * Pure C, no kernel or chip access: the kext does the side effects, the host test (hosttest/pm_test.c) feeds event sequences through the same code.
 *
 * Two kinds of "will/did" pairs reach the driver through IOFramebuffer notifications:
 *   - mode change / display sleep:  (WillChange, any) .. (DidChange, any)   and   (WillSleep, info 0) .. (DidWake, info 0)  -> a nesting counter `depth`
 *   - the NDRV's chip suspend:      (WillSleep, info != 0) .. (DidWake, info != 0)                                          -> one idempotent flag, which also HOLDS one depth count
 * Holding a depth count while asleep means the "did" side effects (which read the chip) cannot run until the wake verification has passed, whatever order the events arrive in.
 * The chip may be touched only while no mode change is in progress (generation even) and the chip is not asleep. */
#ifndef RAGE_PM_H
#define RAGE_PM_H
#include <stdint.h>

struct rage_pm { uint32_t depth; uint32_t sleep_depth; uint32_t asleep; };

#define RAGE_PM_NONE     0
#define RAGE_PM_RUN_WILL 1     /* caller runs the will side effects (drain, invalidate) and makes the generation odd */
#define RAGE_PM_RUN_DID  2     /* caller runs the did side effects (re-enable engine, re-read CRTC) and makes the generation even */
#define RAGE_PM_DUP      4     /* a duplicate or unmatched event: nothing to do (log it) */

static inline void rage_pm_init(struct rage_pm *p) { p->depth = p->sleep_depth = p->asleep = 0; }

/* (WillChange, any) or (WillSleep, 0) */
static inline int rage_pm_will(struct rage_pm *p) { return p->depth++ == 0 ? RAGE_PM_RUN_WILL : RAGE_PM_NONE; }
/* (DidChange, any) or (DidWake, 0): an unmatched did is ignored */
static inline int rage_pm_did(struct rage_pm *p)
{
    if (p->depth == 0) return RAGE_PM_DUP;
    return --p->depth == 0 ? RAGE_PM_RUN_DID : RAGE_PM_NONE;
}
/* (WillSleep, info != 0).  The caller runs the will side effects if RUN_WILL is set, records its before-image, then calls rage_pm_sleep_commit. */
static inline int rage_pm_sleep_begin(struct rage_pm *p)
{
    if (p->asleep || p->sleep_depth) return RAGE_PM_DUP;
    p->sleep_depth = 1;
    return rage_pm_will(p);
}
static inline void rage_pm_sleep_commit(struct rage_pm *p) { p->asleep = 1; }
/* (DidWake, info != 0), called ONLY after the wake verification passed.  Without a matching sleep nothing changes (RAGE_PM_DUP). */
static inline int rage_pm_wake_end(struct rage_pm *p)
{
    if (!p->sleep_depth) { p->asleep = 0; return RAGE_PM_DUP; }
    p->asleep = 0; p->sleep_depth = 0;
    return rage_pm_did(p);
}
/* generation parity is kept by the caller (odd after RUN_WILL, even after RUN_DID); the gate is: */
static inline int rage_pm_chip_ok(const struct rage_pm *p, uint32_t gen) { return !(gen & 1u) && !p->asleep; }

/* wake verification of the PCI side, pure (the kext reads the values): 0 = proceed, 1 = repair memory decode then proceed, -1 = stay asleep (card gone / not decoding BARs) */
static inline int rage_pm_wake_check(uint32_t vendor, uint32_t command, uint32_t bar0)
{
    if (vendor != 0x1002u || bar0 == 0 || bar0 == 0xffffffffu || (bar0 & 1u)) return -1;      /* BAR0 is a memory BAR (bit 0 clear) with a plausible address */
    if (!(command & 2u)) return 1;                                                           /* memory space off: repair, then proceed */
    return 0;
}
#endif

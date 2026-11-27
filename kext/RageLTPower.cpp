/* kext/RageLTPower.cpp - system sleep / wake (checklist B2, docs/re-notes/sleep-wake-plan.md section 6).  Flag RAGE_FEAT_SLEEPWAKE.
 *
 * Hook: the same IOFramebuffer notifications the mode-change code already follows.  The NDRV framebuffer delivers (WillSleep, info != 0) BEFORE it suspends the chip and
 * (DidWake, info != 0) AFTER it woke it; stock treats them like a mode change plus a user-mapping redirect and saves no chip state (the NDRV owns CRTC/DAC/PLL/FIFO).  We do the same
 * and add what stock lacks: a gate so nothing reads a suspended chip (a PPC read of an un-clocked chip blocks the CPU forever), and a PCI-config-only wake check before the first
 * chip access (config reads cannot hang).  The state machine is common/rage_pm.h (host-tested, hosttest/pm_test.c).  No chip register is written here; no thread, timer or PM-tree
 * registration (our kext and the framebuffer are siblings, the PM tree gives no ordering between them).
 *
 * The notifier thread holds IOFramebuffer's gate, so these handlers are bounded: the drain in modeWillChange is the existing bounded engine wait, there is no IOSleep loop. */
#include "RageLTDevice.h"
#include <IOKit/IOLib.h>
#include "RageLTEngine.h"

#define LOG(...) IOLog("RageLT: " __VA_ARGS__)

#if RAGE_FEAT_SLEEPWAKE

/* (WillSleep, info != 0).  Runs while the chip is still awake: drain (through modeWillChange), then close the gate.  Only PCI config space is read here. */
void RageLTDevice::systemWillSleep(void)
{
    int a;
    IOLockLock(fPmLock); a = rage_pm_sleep_begin(&fPm); IOLockUnlock(fPmLock);
    if (a & RAGE_PM_DUP) { logEvent(RAGE_EV_MARK, 0, 0xe0060001u, fModeGen, 0, 0, 0); LOG("system will sleep: already asleep (duplicate event ignored)\n"); return; }
    if (a & RAGE_PM_RUN_WILL) modeWillChange();                    /* odd generation, overlay off, drain, every context's target invalid, display unreserved */
    logEvent(RAGE_EV_MARK, 0, 0xe0060000u, fModeGen, fPCI->configRead16(kIOPCIConfigCommand), fPCI->configRead32(kIOPCIConfigBaseAddress0), 0);   /* before-image: config space only */
    IOLockLock(fPmLock); rage_pm_sleep_commit(&fPm); IOLockUnlock(fPmLock);
    LOG("system will sleep: chip gate closed (generation %u)\n", fModeGen);
}

/* (DidWake, info != 0).  The chip has been woken by the framebuffer: verify the card through PCI config space FIRST; touch the chip only if it is still there. */
void RageLTDevice::systemDidWake(void)
{
    uint32_t vendor = fPCI->configRead16(kIOPCIConfigVendorID), cmd = fPCI->configRead16(kIOPCIConfigCommand), bar0 = fPCI->configRead32(kIOPCIConfigBaseAddress0);
    int w = rage_pm_wake_check(vendor, cmd, bar0), a;
    logEvent(RAGE_EV_MARK, 0, 0xe0070000u, vendor, cmd, bar0, (uint32_t)w);
    if (w < 0) {                                                    /* card gone or not decoding: stay asleep, gate closed, never touch the chip */
        logEvent(RAGE_EV_ERROR, 0, 0xe0070001u, vendor, cmd, bar0, 0);
        LOG("wake: card not decoding (vendor 0x%x command 0x%x BAR0 0x%x): staying asleep, chip not touched\n", vendor, cmd, bar0);
        return;
    }
    if (w > 0) { fPCI->setMemoryEnable(true); LOG("wake: memory decode was off (command 0x%x), re-enabled\n", cmd); logEvent(RAGE_EV_MARK, 0, 0xe0070002u, cmd, fPCI->configRead16(kIOPCIConfigCommand), 0, 0); }
    IOLockLock(fPmLock); a = rage_pm_wake_end(&fPm); IOLockUnlock(fPmLock);
    if (a & RAGE_PM_RUN_DID) modeDidChange();                     /* BUS_EXT_REG_EN, engine re-init, CRTC re-read (reads MEM_CNTL: aperture-endian guard), even generation */
    else if (a & RAGE_PM_DUP) { LOG("wake without a matching sleep (verified, no change)\n"); logEvent(RAGE_EV_MARK, 0, 0xe0070003u, fModeGen, 0, 0, 0); }
    LOG("system did wake: generation %u, gate %s\n", fModeGen, chipOK() ? "open" : "still closed (another change in progress)");
}

#else   /* sleep/wake compiled out: the info != 0 events are ignored, as in stock's non-PM path */
void RageLTDevice::systemWillSleep(void) {}
void RageLTDevice::systemDidWake(void) {}
#endif

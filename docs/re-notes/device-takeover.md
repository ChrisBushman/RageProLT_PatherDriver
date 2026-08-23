# Device takeover — moving off the shared-engine model

The quiesce/save-restore coexistence model (share the engine with ATIRagePro,
no lock) is fundamentally fragile: a collision with WindowServer's async DMA can
wedge the engine, and a tool that hangs mid-DMA leaves the engine activated ->
WindowServer freezes (Finder unresponsive, hw cursor still moves).  We hit this
running first light.  Decision: take over the engine so it is exclusively ours.

## The Lombard display stack (recon, kextstat + ioreg + Info.plists)

```
IONDRVSupport -> NDRV "ATY,264LTPro"      = FRAMEBUFFER (display / scanout)
   (the card's OpenFirmware ROM driver; provides the mode lit at boot)
com.apple.ATIRagePro  (kext)              = 2D ACCELERATOR (the engine)
   IOClass=ATIRagePro  IOProviderClass=IOPCIDevice
   IOMatchCategory=IOAccelerator  IOPCIMatch includes 0x4c491002 (our LT Pro)
ATIRageProGA.plugin  (/S/L/E, userspace)  = WindowServer's 2D-accel half; the
   thing that actually drives the engine for window compositing
```

**Key insight: the framebuffer is the NDRV, NOT ATIRagePro.**  So taking over the
ACCELERATOR (engine) cannot black out the display -- the NDRV keeps scanning out
independently.  This removes almost all risk from the takeover.

Our Mach64Accelerator matches the same IOPCIDevice (0x4c491002) with a DISTINCT
IOMatchCategory (Mach64GLAccelerator), which is why it coexists with ATIRagePro's
IOAccelerator category.

## Stage 1 (DONE): take the engine by disabling ATIRagePro's 2D accel

Move the WindowServer 2D plugin aside (reversible; it is a userspace plugin, not
a cached kext, so no kextcache dance):

```
sudo mv /System/Library/Extensions/ATIRageProGA.plugin ~/ATIRageProGA.plugin.off
sudo reboot
# revert: sudo mv ~/ATIRageProGA.plugin.off /System/Library/Extensions/ATIRageProGA.plugin
```

Verified after reboot (ioreg -l):
- display still works (NDRV framebuffer); 2D is now software-composited.
- `IOATIRPro2DContext=0`, `ATIR32DContext=0` -> WindowServer holds NO 2D contexts,
  i.e. it is NOT bus-mastering the engine.  ATIRagePro kext is still loaded
  (`IOATIRProAccelerator=1`) but DORMANT (no clients).

=> the drawing engine + bus-master DMA are now exclusively ours; no collision
possible.  Follow-up: the kext's submitCommandBuffer can drop the quiesce/
save-restore of ATIRagePro's DMA regs (nothing to preserve) and just own the
engine.

## Stage 2 (future, optional): full framebuffer replacement

To be THE driver (our kext as the IOAccelerator + our own IOFramebuffer + a GA
plugin so WindowServer 2D is accelerated by us again).  Large, and NOT required
for the GL driver.  If attempted, the IOFramebuffer must ADOPT the boot/OF
display state (no risky mode-setting), and stay kextload-only over SSH with a
tested reboot-reverts recovery path before ever going persistent in /S/L/E.

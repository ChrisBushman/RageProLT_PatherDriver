# DDC / EDID probe on the MDD Rage LT Pro (Nov 25 2026)

Goal: learn the monitor's modes from its EDID instead of asking the user (`rig/stubfb/RageStubFB.cpp`: `readEdid`, property `rage-edid`, `stubctl edid`).

## Method
* Pins (RAGE PRO GCS 4.4 / 6.13, RAGE LT PRO RRG GP_IO 0_1E = byte 0x78): DDC data = GIO0, DDC clock = GIO4, MONID0 = GIOC. GP_IO bit n = pin level, bit 16+n = direction (1 = output).
* Open-drain bit-bang: drive low = dir 1 / latch 0; release = dir 0 **and latch 1**.
* Iteration without reboots: `rig/ddctest/RageDdcTest` (throwaway kext, own match category, declines the match after running, so it unloads/reloads freely). The stub cannot be unloaded while WindowServer holds it, and on Panther the stub is **prelinked in the kernel cache**: replacing the binary inside the bundle is not enough, `touch /System/Library/Extensions` (and reboot) to rebuild it.

## Results
* GP_IO behaves as documented: a released line reads 1 (pulled up); driving it follows the latch (bit 0, 4 and 12 checked, all combinations logged).
* **Finding that cost time:** releasing a line with latch 0 while the other line also had latch 0 made *both* lines read low afterwards, which looked like a monitor holding the bus and produced false acknowledges on every byte. Keep latches at 1 when released.
* DDC1 check (both lines released, SDA sampled for 1 s): 0 transitions, SDA high 100%: the monitor is not streaming DDC1.
* I2C scan of all 128 addresses on GIO0/GIO4: **no device answers** (idle bus is clean: GP_IO 0x37ff, start condition works).
* Option ROM (`biosrun`, simulation, new hook `BIOS_EDID=1`): VBE 4F15 BL=0 returns success (BX=0x0200, DDC2 capable); BL=1 returns AX=0183 without touching GP_IO (it polls the LCD registers instead): no information about the CRT pins.
* Not established: whether this CRT/cable carries DDC at all (VGA pins 12/15), or whether the LT Pro BGA routes the CRT DDC to different pins (the BGA package has dedicated I2CCLK/I2CDATA pins).

## Mistakes
* A test kext (`ddcSample`) read GP_IO before mapping the register page (null + 0x478) and panicked the G4. Every register-touching function must call `mapRegisters()` first.

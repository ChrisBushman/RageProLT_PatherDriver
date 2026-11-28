# Rage LT Pro DVD hardware-assist bundle - design proposal (for agreement; nothing built yet)

Goal: `ATIRageProDVDDriver.bundle` implementing the `DVDDriver*` interface (see docs/re-notes/r128-dvd-driver/), loaded by DVD.framework on machines
without VSP/AltiVec (G3 Lombard) via the accelerator property `IODVDBundleName`. Host does bitstream parse + dequantisation (framework); bundle does IDCT; Rage does motion compensation (+ residual add if provable).

## Constraints (user, Nov 27)
- Target G3: compile -mcpu=750, no AltiVec; integer math and table lookups; NO double (avoid float too); minimal threads (default: zero extra threads, decode runs in the framework's caller thread; GPU is the parallelism).
- No CPU-speed simulation; correctness via bundle-loading simulation; performance = make the bundle as fast as possible.

## Pipeline per picture (DVDDriverDecode)
1. Read MbRec array + coefficient stream (spec A). Coefficients arrive dequantised (spec D).
2. IDCT (host): 8x8 integer IDCT (fixed point, 16-bit intermediates, row/column with precomputed constant table; sparse-block shortcuts: DC-only, run-limited rows) -> residual blocks.
3. Prediction + residual:
   - M1 (reference path, correctness oracle): CPU does MC + residual into the destination frame buffer (half-pel integer interpolation, rounding per MPEG-2 spec). Also used as fallback.
   - M2 (Rage MC): emit MC via our kext (LT Pro scaler regs 0xE0-0xE4, SCALE_3D_CNTL blend ONE/ONE with signed dst clamp as in Win98 init - the likely residual-add mechanism: IDCT residual uploaded as a signed plane, then blended on top of / before MC). Compare frame by frame against M1.
4. Show: ShowMPBuffer/EnableMP/SetMPRects/GetKeyColor via our kext overlay (the R128 bundle programs no overlay registers; kext owns overlay).
5. Sub-picture: port the software DCSQ/SP blend (spec C) - on the host or via our overlay path; decide after M1.

## Kext interface (to define)
Own user-client selectors modelled on the R128 set (open/close, ring map, show/enable MP, rects, SP upload, sync). Ring/command format is ours; R128's type-3 0x31 packet is NOT reusable (LT Pro mach64 registers). Must address cache coherency (CPU-written residual/reference planes: dcbf/dcbst before GPU reads; dcbf before CPU reads of GPU-written frames - the earlier B10 lesson).

## Milestones (each verified before the next; static-first, live only to verify implemented code)
- M0 stub bundle: all exports, returns success, no output; loaded by patched-framework rig (private DVD.framework copy, VSP probe string changed, DYLD_FRAMEWORK_PATH) -> proves selection/loading.
- M1 CPU-only reconstruction, then compare against the stock VA path output (reference frames) for correctness.
- M2 Rage MC + host residual; M3 SP/buttons; M4 timing/optimisation (profile per phase before optimising); M5 B-frames/field/dual-prime coverage.

## Open questions (must be resolved statically before M2)
Residual-add mechanism and rounding on LT Pro; MC register semantics for 0xE4 trigger/size codes; mismatch control location; dual-prime handling (R128 has no distinct handler, so framework likely converts); what DVD.framework does when no bundle loads on a G3.

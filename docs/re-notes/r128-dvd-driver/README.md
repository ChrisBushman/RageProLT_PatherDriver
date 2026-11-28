# ATIRage128DVDDriver.bundle (Apple, 10.3, v1.3.28) - static analysis

Why: it is Apple's shipping example of the `DVDDriver*` hardware-assist bundle interface that DVD.framework loads
when no VSP/AltiVec path applies (e.g. a G3 Lombard). A Rage LT Pro bundle (`ATIRageProDVDDriver`) implements the same entry points.
Method: static only (otool/Capstone, jump tables read from the binary), register names from the Rage 128 Pro register reference
(CCE register field = DWORD index, byte offset = index*4). Claims tagged ESTABLISHED (address) or INFERRED in the three parts.
Written by three parallel analysis passes; spot-checked against the disassembly (MbRec stride 0x1c @0x23b0, block counts @0x1f2c,
ring flush via IOConnectMapMemory @0x1f98).

- A_decode_input.md   - DVDDriverDecode params, per-macroblock record (0x1c bytes), MB loop, coefficient stream format
- B_mc_handlers.md    - MC handlers: what is emitted per motion type (MC_SRC1/SRC2/DST/START block, half-pel, bidirectional)
- C_setup_ring_overlay.md - user-client protocol, ring/command buffer, overlay/key colour, sub-picture, per-picture setup

## Framework side (DVD.framework, see also docs/re-notes/win98-dvd-assist.md)
- Bundle path /System/Library/Extensions/<name>.bundle/Contents/MacOS/<name>; <name> = accelerator IORegistry `IODVDBundleName`,
  EXCEPT when the VSP probe (VSP.framework, resource mp2decvbin1, fn VSPMaximumThreads) succeeds and the display is GL-accelerated
  (then hard-coded AppleAltiVecDVDDriver). No VSP (Lombard) => IODVDBundleName honoured.
- Optional entries may be null (framework just clears feature bits); R128 stubs FrontEnd/BackEnd/FrontEndAndBackEnd/Deinterlace/CreateRenderer with 0xfffb.

## What the R128 split looks like (hardware IDCT + MC)
Software supplies per-MB: motion vectors (int16, half-pel), motion type 0..7, field selects, and per-block run/level coefficient entries
(4 bytes: u8 run, s16 level). The driver streams run/level to the chip (hardware IDCT; IDCT_EN in MC_SRC1_CNTL) and emits one type-3 0x31
packet per MB with MC_SRC2/SRC1/DST/START words (half-pel in HACC/VACC low bits, size code in START; bidirectional = SRC2 + SRC1 with
secondary texture, hardware averages).

## Implications for the LT Pro bundle (0x4c49: HW MC, NO hw IDCT per Win98 notes)
- Host must do dequant + IDCT from run/level (quantiser matrices are NOT in params or MbRec - find where the framework supplies them).
- MC maps to scaler regs 0xE2/0xE1/0xE3/0xE4 (Win98 notes); residual-add path, rounding, and dual-prime still NOT established.
- Design rules: no AltiVec (-mcpu=750), integer math + tables, no doubles, minimal threads.

## Open (see each part's "not established")
Meaning of MB flags (+0x15) and params aux bytes (+4/+5); n=3 word and control-word mode bits in the MC packets; stream tags for coefficient groups;
kernel handling of ring block types; selectors 0/1/3/5; undocumented registers 0x1800-0x1A34; half-pel/bidir rounding.
Noted R128 driver defects (not to copy): ctx malloc never zeroed, dangling list node on selector-4 failure, unchecked 0x5fd0 allocation, SP pool short, mappings never unmapped.

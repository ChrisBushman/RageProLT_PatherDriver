# ATIRageProGA 2D command stream: exact spec for the B1b validator (static, Nov 26)

Static analysis only (nothing run on the G4). Input: stock `ATIRageProGA` plugin
(G4 copy, Mach-O ppc bundle, `__text` 0xe34..0x2cfc, one 4 KB-page-sized image). All
addresses below are plugin virtual addresses (== file offsets). Disassembly was made
with `powerpc-linux-gnu-objdump -D -EB -b binary -m powerpc:common --adjust-vma=0xe34`
on the extracted `__text`; stub names come from the indirect symbol table
(`0x2f1c IOConnectMapMemory`, `0x2cfc/0x2d1c/0x2edc io_connect_method_*`, `0x2efc memcpy`).
Register layouts were read from the rendered RRG PDF pages (RRG-G03300, chapter 5, PDF pages
155 DST_CNTL, 158 DST_HEIGHT_WIDTH, 160 DST_WIDTH, 164 DST_Y_X, 153 DST_BRES_*, 171-172 SRC_CNTL, 177 SRC_WIDTH1, 181 SRC_Y_X,
200 DP_MIX, 208 DP_SRC, 211 CLR_CMP_CNTL), not from pdftotext. Anything not proven is marked **not established**.

Conventions: "stream register" = the register number in the (reg,val) pairs = fRegs offset - 0x400
(stream 0x10c = kext offset 0x50c). All values big-endian words in the buffer; every multi-byte
field below is a 32-bit word unless noted. `W(r)` = the plugin writes register r.

## 1. Blitter entry points and dispatch

Interface callback (IOGraphicsInterface.h) `IOBlitterPtr(this r3, options r4, type r5, sourceType r6, operation r7, source r8)`.

`GetBlitter` 0x27e4: `mode = sourceType & 0x7FFFF000` (rlwinm r6,r6,0,1,19), `kind5 = type & 0xFFFF0FFF` (rlwinm r0,r5,0,20,15,
i.e. the key-colour bits 0xF000 are cleared for the test). Table at 0x30c0 (function pointers).

| condition | blitter | proof |
|---|---|---|
| type == 1 (CopyRects, Copy op, no mono/scale/key bits) and mode in {0, 0x1000} | rProCopy 0x15e0 | 0x27e4-0x2820 |
| type == 4 (CopyRegion) and mode == 0x1000 | rProCopyRegion 0x1960 | 0x2824-0x283c |
| (type & 0xFFFF0FFF) == 0 and mode == 0x6000 (Rects, solid) | rProFill 0x1d68 | 0x2840-0x2868 |
| (type & 0xFFFF0FFF) == 0x100 (MonoExpand) and mode == 0x5000 (OOLPattern) | rProFill 0x1d68 | 0x2850-0x2868 |
| type == 2 (Lines) and mode == 0x6000 | rProSolidLines 0x2294 | 0x286c-0x2884 |
| type == 3 (Scanlines) and mode == 0x6000 | rProSolidScanlines 0x2568 | 0x2888-0x289c |
| anything else | returns 0xe00002c7, `*blitter = NULL` | 0x28a0-0x28b8 |

Consequences (proven by the masks): the **operation bits 0x0FFF0000 must be 0** (kIOBlitCopyOperation only; Or/Xor/Blend/Highlight,
Scale, ColorSpaceConvert are refused), rProFill is the only blitter that accepts the key-colour bits 0xF000 of `type`, and
only for Rects/MonoExpand. No blitter looks at `operation->offsetX/offsetY` (+8/+12), `sourceKeyColor` (+16), or
`specific[]` (+24..+87). `operation->color0` (+0) and `color1` (+4) are read only by the pattern fill; `destKeyColor` (+20) only by keyed fill.
The blitters other than CopyRegion do not re-check type/mode; CopyRegion re-checks `mode == 0x1000` (0x19a8) and returns 0xe00002c7 otherwise.

IOBlit structure offsets used (IOGraphicsInterfaceTypes.h; `sizeof(IOBlitOperation)` = 6*4 + 16*4 = 88):

| structure | fields (offset from `operation`) |
|---|---|
| IOBlitRectangles | count +88; rect[i] at +92+16i: x +0, y +4, width +8, height +12 |
| IOBlitCopyRectangles | count +88; rect[i] at +92+24i: sourceX +0, sourceY +4, x +8, y +12, width +16, height +20 |
| IOBlitCopyRegion | deltaX +88, deltaY +92, `IOAccelDeviceRegion *` +96 |
| IOAccelDeviceRegion | num_rects +0 (u32), bounds +4 (4 x s16 x,y,w,h), rect[i] at +12+8i (4 x s16 x,y,w,h) |
| IOBlitVertices | count +88; vertex[j] at +92+8j (x s32, y s32) |
| IOBlitScanlines | count +88, y +92, height +96, x[2*k] pairs at +100+8k |
| source (r8), solid | the colour VALUE itself (not a pointer): `stwu r30` 0x1e... / 0x23a4 / 0x26b0 |
| source (r8), OOL pattern | `IOBlitSurface *`; its first word `memory.bytes` points at 8 bytes of pattern; plugin reads two words (0x1fac-0x1fcc) |

All other arguments (`options`, `sourceType` beyond the table, `source` for copies) are ignored.

## 2. Buffer layout and packet grammar (what the validator parses)

Shared 4 KB command buffer (map type 0). Header (kernel built, `init_command_buffer_header`, see ga-2d-client-decode.md): +0x10
capacity words (0x3F8 = 1016), +0x14 surface flags, +0x1c first packet header, +0x20 body. The plugin keeps: this+0x18 buffer base,
+0x7c "previous header" pointer (initially base+0x1c), +0x80 write pointer (initially base+0x20), +0x0c IOKit connection.

Packet header word = `op<<24 | n`, where n is patched by OR-ing `(cur - prevHeader) >> 2` (dwords from this header to the
next header, header included) when the next header (or the terminator) is written (pattern at e.g. 0x1654-0x1664).
Ops the plugin emits:

| op | meaning | words after the header word | emitted by |
|---|---|---|---|
| 0x00 | raw (reg,val) pairs (the packet at buffer+0x1c after every map; the plugin overwrites the kernel's initial `1` with 0 at 0x16a4/0x1ba0 etc.) | pairs | implicit after every buffer switch |
| 0x01 | terminator `0x01000000` (written at the write pointer just before each map) | none | all flush paths |
| 0x02 | present/swap request | none | `Flush` 0x14d0 only when `options & 0x10000` and a surface is bound (this+0x8c != 0) |
| 0x09 | copy from display `word1` (fb index = this+0x84) | word1, then pairs | rProCopyRegion |
| 0x0d | copy within the current destination | word1 (always 0), then pairs | rProCopy |

Kernel processing (ga-2d-client-decode.md, `process_command_buffer` 0x14170): op 0x09/0x0d header+word1 are replaced by the pair
(0x180 SRC_OFF_PITCH, value computed by the kernel from display idx / destination); everything after the preamble up to the next
header is plain pairs. Therefore for the validator a 0x09/0x0d packet is "a 0x180 pair the kernel computes, then pairs".

Buffer switching (flush-and-remap), identical in every blitter (example 0x1638-0x16c0, `rProCopy`): patch previous header length,
write terminator 0x01000000 at the write pointer, `IOConnectMapMemory(conn=this+0xc, memoryType 0, mach_task_self, &addr, &size, options 1)`
via 0x2f1c; on success the new buffer's +0x1c word is set to 0, `this+0x7c = buf+0x1c`, `this+0x18 = buf`, `this+0x80 = buf+0x20`.
**Stock bug (not ours to copy):** if the map fails, the blitters set `this+0x7c = this+0x18 = 0` and `r11 = 0` but fall through and store
through the null pointer (e.g. 0x16c0 -> 0x16c8 -> store at r9=0). The kernel side must never rely on user-side error handling.

Free-space bookkeeping: `cap = [buf+0x10]` (1016), `r18 = cap-3`, `free = r18 - ((wp - buf - 32) >> 2)`. Every append is gated by `free > K`
(K = words about to be written minus one, see per-op table) and flushes first when not. Hence the maximum body length of one buffer is
1013 words of data + 1 terminator word (<= 1014 of 1016). The kernel pads an odd total with 0xffffffff (kernel side).
A single IOBlit call is **not** bounded by one buffer: count is unbounded (u32 at +88) and the call continues into fresh buffers
(per-rect/line/scanline flush checks below). So the per-call stream length is unbounded; the per-buffer bound is 1014 words.
Stock submit rejects > 0x4000 words per submission.

## 3. Registers, bit layouts (RRG chapter 5; stream reg = MM dword index * 4)

| stream reg | kext off | name | RRG MM | layout (read from the diagram) |
|---|---|---|---|---|
| 0x10c | 0x50c | DST_Y_X | 0_43 | [14:0] DST_Y (signed 15), [15] SCALE_Y_LSB (hint, ignored), [30:16] DST_X (signed; RRG text: "signed 14 bit", chapter intro: X range -8192..8191, Y -16384..16383), [31] SCALE_Y_SECONDARY_LSB (hint). Plugin value = `(x<<16) \| (y & 0xffff)` |
| 0x118 | 0x518 | DST_HEIGHT_WIDTH | 0_46 | **write initiates a rectangle operation.** [15:0] DST_HEIGHT (a), [31:16] DST_WIDTH (b). DST_WIDTH: bits [13:0] used for rects, **bit 15 = DRAW_TRAP (starts a trapezoid), bits [15:14] alias DST_BRES_LNTH[15:14]**, bit 31 of the DST_WIDTH register would be FILL_DIS. DST_HEIGHT [14:0] aliases TRAIL_X. Plugin value = `(w<<16) \| (h & 0xffff)`. NB: the kext's own `eng_fill/eng_blit` trigger the alias 0x6ec DST_WIDTH_HEIGHT with `(h<<16)\|w`; the stock plugin uses 0x518 with `(w<<16)\|h` (RRG 5-8 vs 5-11, both documented). |
| 0x120 | 0x520 | DST_BRES_LNTH (LEAD_BRES_LNTH) | 0_48 | **write initiates a line (or trapezoid) operation.** [14:0] length (aliased DST_WIDTH[14:0]), [15] DRAW_TRAP (start trapezoid), [30:16] TRAIL_X (not written if bit15=1 and bit31=0), [31] d |
| 0x124 | 0x524 | DST_BRES_ERR | 0_49 | signed 18-bit error term |
| 0x128 | 0x528 | DST_BRES_INC | 0_4A | signed 18-bit increment (must be positive) |
| 0x12c | 0x52c | DST_BRES_DEC | 0_4B | signed 18-bit decrement (must be negative) |
| 0x130 | 0x530 | DST_CNTL | 0_4C | bit0 DST_X_DIR (1 = left-to-right), bit1 DST_Y_DIR (1 = top-to-bottom), bit2 DST_Y_MAJOR, bit3 DST_X_TILE, bit4 DST_Y_TILE, bit5 DST_LAST_PEL, bit6 DST_POLYGON_EN, bit7 DST_24_ROT_EN, [10:8] DST_24_ROT, bit11 DST_BRES_SIGN, bit12 DST_POLYGON_RTEDGE_DIS, bit13 TRAIL_X_DIR, bit14 TRAP_FILL_DIR, bit15 TRAIL_BRES_SIGN, bit17 BRES_SIGN_AUTO, bit19 ALPHA_OVERLAP_ENB, bit20 SUB_PIX_ON (coordinates become S.12.2: must stay 0) |
| 0x18c | 0x58c | SRC_Y_X | 0_63 | [14:0] SRC_Y, [29:16] SRC_X (bits 15, 31:30 unused). Plugin value `(sx<<16) \| (sy & 0xffff)` |
| 0x190 | 0x590 | SRC_WIDTH1 | 0_64 | [13:0] source width 1 (higher bits unused); plugin writes the raw 32-bit width |
| 0x1b4 | 0x5b4 | SRC_CNTL | 0_6D | bit0 SRC_PATT_EN, bit1 SRC_PATT_ROT_EN, bit2 SRC_LINEAR_EN, bit3 SRC_BYTE_ALIGN, bit4 SRC_LINE_X_DIR, bit5 SRC_8x8x8_BRUSH, bit6 FAST_FILL_EN, bit7 SRC_TRACK_DST, bit8 BUS_MASTER_EN, bit9 BUS_MASTER_SYNC, [11:10] BUS_MASTER_OP, bit12 SRC_8x8x8_BRUSH_LOADED, bit13 COLOR_REG_WRITE_EN, bit14 BLOCK_WRITE_EN. Bits 8-11 would start bus-master transfers: must be 0 |
| 0x280 / 0x284 | 0x680 / 0x684 | PAT_REG0 / PAT_REG1 | 0_A0 / 0_A1 | 32 bits each, the two halves of the 8x8 mono pattern |
| 0x2c0 | 0x6c0 | DP_BKGD_CLR | 0_B0 | background colour (low 8/16 bits used for 8/16 bpp; all 32 for 32 bpp; RRG Table 5-7) |
| 0x2c4 | 0x6c4 | DP_FRGD_CLR | 0_B1 | foreground colour (same width rule) |
| 0x2d4 | 0x6d4 | DP_MIX | 0_B5 | [4:0] DP_BKGD_MIX, [20:16] DP_FRGD_MIX. Mix codes: 3 = DST, 7 = SRC (RRG Table 5-10); stock only ever uses 0x00070003 (FRGD=SRC, BKGD=DST = source copy) |
| 0x2d8 | 0x6d8 | DP_SRC | 0_B6 | [2:0] DP_BKGD_SRC, [10:8] DP_FRGD_SRC, [17:16] DP_MONO_SRC. Source codes: 0 bkgd colour, 1 frgd colour, 2 host, 3 blit, 4 pattern regs, 5 scaler/3D. Mono: 0 = '1', 1 = pattern regs, 2 host, 3 blit |
| 0x300 / 0x304 / 0x308 | 0x700 / 0x704 / 0x708 | CLR_CMP_CLR / CLR_CMP_MSK / CLR_CMP_CNTL | 0_C0 / 0_C1 / 0_C2 | CNTL: [2:0] CLR_CMP_FCN (0 false, 1 true, 4 DST != CLR, 5 DST == CLR), [25:24] CLR_CMP_SRC (0 destination, 1 2D source, 2 texel/scaler). "If the comparison is true the destination is kept, otherwise the source colour is written" |

Not written by the plugin at all (so inherited from earlier hardware state, see section 9): DP_PIX_WIDTH 0x2d0, DP_WRITE_MASK 0x2c8, SC_* 0x2a8/0x2b4,
DST_OFF_PITCH 0x100, SRC_OFF_PITCH 0x180, SRC_HEIGHT1, SRC_Y_X_START, HOST_CNTL, PAT_CNTL 0x288, GUI_TRAJ_CNTL, SETUP_CNTL, Z_CNTL, SCALE_3D_CNTL.
The plugin also never writes pseudo-register 0x1fc.

## 4. Per-blitter emission (ordered (reg,val) lists)

Notation: `P(reg,val)` = two stream words. "free" = words left per section 2. After the blitter returns, `this+0x80` = write pointer; no flush is forced
(submission happens on Flush/WaitComplete/the next map).

### 4.1 rProCopy 0x15e0 (type 1; op 0x0d)
Guard: write pointer null -> return 0xe00002be (0x1620).
Setup: `free = cap-3-used`; if `free <= 7` flush (0x1638-0x16c4). Header block (8 words, 0x16c8-0x1734):
`[hdr 0x0d000000][word1 = 0][P(0x1b4, 0)][P(0x2d8, 0x300)][P(0x2d4, 0x70003)]` (patches previous header first, `this+0x7c` = this header).
Then `free -= 8`; if `free < 10*count + 12` flush once more (0x1738-0x17c4; the header block stays in the old buffer, the rect pairs go to the new buffer).
Loop i in 0..count-1 (0x17cc-0x1940), per rect (check `free > 9` else flush; each rect 10 words):

```
sx=r28=[op+92+24i] sy=r29=[+96] dx=r26=[+100] dy=r27=[+104] w=r25=[+108] h=r24=[+112]    (r20=1, r21=2 initially)
if (u32)dx > (u32)sx : sx += w-1; dx += w-1; r20 = 0          (right-to-left)     0x1890-0x18a8  (cmplw: UNSIGNED compare)
if (u32)dy > (u32)sy : sy += h-1; dy += h-1; r21 = 0          (bottom-to-top)     0x18ac-0x18c4
emit: P(0x130, r20|r21)  P(0x18c, (sx<<16)|(sy&0xffff))  P(0x190, w)  P(0x10c, (dx<<16)|(dy&0xffff))  P(0x118, (w<<16)|(h&0xffff))   <- trigger
```
DST_CNTL therefore only takes 0..3 (X_DIR=bit0, Y_DIR=bit1). Width/height are written raw (no clamp, no masking of SRC_WIDTH1, w=0 gives a decremented -1 start).
Words: 8 + 10 per rect. Per-call max for n rects in one buffer: 8+10n <= 1013.

### 4.2 rProCopyRegion 0x1960 (type 4, mode 0x1000; op 0x09)
Guards in order: write pointer null -> 0xe00002be; `mode != 0x1000` -> 0xe00002c7; then compares (0x19d4-0x1a2c) -> 0xe00002c7 (0x1d4c):
`[this+0x24] == (s16)[this+0x20]` and `[this+0x28] == (s16)[this+0x22]` (the destination snapshot taken at Reset, this+0x1c = copy of the 0x30-byte
get_buffer_info struct; these two compare the snapshot's long width/height with its own short width/height, a consistency check rather than a
comparison against a live struct) and `[this+0x40] == [this+0x70]` (snapshot pixcode byte == live struct pixcode: the only genuine
snapshot-vs-live comparison). **not established**: why the first two were written that way.
Region: `region = [op+96]`; `n = region->num_rects`; if n == 0 the single rect is `region->bounds` (r28 = 1, rects at region+4) else rects at region+12.
`ox = deltaX - bounds.x`, `oy = deltaY - bounds.y` (r19, r18, 0x19e4-0x1a0c; bounds.x/y are s16 at region+4/+6). Each rect (s16 x,y,w,h sign-extended):
source = (x,y,w,h), destination = (x+ox, y+oy).
Header block (8 words, 0x1ac4-0x1b30): `[hdr 0x09000000][word1 = this+0x84 (fb index)][P(0x1b4,0)][P(0x2d8,0x300)][P(0x2d4,0x70003)]`, preceded by a flush when `free <= 7`
and followed by the same `free < 10*n + 12` flush as 4.1. Per rect (check `free > 9`, 0x1bc4-0x1d3c): identical direction rule and emission as 4.1 with
`(sx,sy)=(x,y)`, `(dx,dy)=(x+ox,y+oy)`: `P(0x130,dir) P(0x18c, sx<<16|sy&0xffff) P(0x190, w) P(0x10c, dx<<16|dy&0xffff) P(0x118, w<<16|h&0xffff)`.
SRC_WIDTH1 is the sign-extended s16 width (negative w gives 0xffffxxxx).

### 4.3 rProFill 0x1d68 (type 0 solid / type 0x100 pattern; optional key colour)
Guard: write pointer null -> 0xe00002be. Masks: `mode = sourceType & 0x7FFFF000`; `keybits = type & 0xF000` (cr4 flag: nonzero = keyed);
`idx = (type >> 12) & 0xF` selects `CLR_CMP_CNTL` value from table `_cmpCntl.0` at 0x2f9c.
Table (read from the binary): idx 4,5,6 -> 4; idx 8,9,10 -> 5; every other idx -> 0.
(0x4000 DestKeyColorEqual -> FCN 4; 0x8000 DestKeyColorNotEqual -> FCN 5; the source-key bits 0x1000/0x2000 alone give 0 = "false", i.e. an ordinary fill; combinations containing a dest bit use the dest value.)
So `CLR_CMP_CNTL` is always one of {0, 4, 5} (SRC field 0 = destination).

**Solid (mode 0x6000)**, preamble needs `free > 7` (flush otherwise), 8 words appended to the CURRENT packet (no new header, 0x1e6c-0x1eac):
`P(0x2d8, 0x100) P(0x2d4, 0x70003) P(0x130, 3) P(0x2c4, color)` with `color = r8` raw 32-bit value of the `source` argument.
**Pattern (mode 0x5000)**, needs `free > 15`, 16 words (0x1f48-0x1fd8):
`P(0x2d8, 0x00010001) P(0x2d4, 0x70003) P(0x130, 3) P(0x2c4, color0) P(0x2c0, color1) P(0x280, pat[0]) P(0x284, pat[1]) P(0x1b4, 2)` with
`color0=[op+0]`, `color1=[op+4]`, `pat = **(u32**)source` (two words from the 8-byte pattern). DP_SRC 0x10001 = MONO_SRC pattern regs, FRGD_SRC = 0 (background colour), BKGD_SRC = 1 (foreground colour):
pattern bit 1 draws DP_BKGD_CLR (= color1), bit 0 draws DP_FRGD_CLR (= color0). SRC_CNTL = 2 (PATT_ROT_EN only, PATT_EN not set); PAT_CNTL is not written.
**not established** that hardware renders the intended pattern with exactly this state (it is not a combination listed in RRG Table 5-12).
Per rect i (0x1fe4-0x226c; rect at op+92+16i: x=[+0], y=[+4] (r28 reused), w=[+8], h=[+12]):
 1. keyed only (`keybits != 0`): need `free > 5` else flush; emit `P(0x308, table[idx])  P(0x300, [op+20] /*destKeyColor*/)  P(0x304, (this[0x70]==3) ? 0x0000ffff : 0xffffffff)` (6 words).
    The key colour is always `destKeyColor` (+20), even for source-key type bits; `sourceKeyColor` is never read.
 2. need `free > 3` else flush; emit `P(0x10c, (x<<16)|(y&0xffff))  P(0x118, (w<<16)|(h&0xffff))` (4 words; trigger).
 3. keyed only: need `free > 1` else flush; emit `P(0x308, 0)` (2 words) to turn the compare off again.
Words per rect: 4 (plain), 12 (keyed). The pixcode (this+0x70 = live get_buffer_info +0x24) is used for exactly one decision: mask 0xffff for pixcode 3 (aRGB1555), -1 otherwise.
No colour conversion exists anywhere: the colour is whatever the caller passed, in destination pixel format.

### 4.4 rProSolidLines 0x2294 (type 2, mode 0x6000 checked only in GetBlitter)
Guard: write pointer null -> 0xe00002be. Preamble, needs `free > 3` (flush otherwise), 4 words (0x2384-0x23a8): `P(0x2d8, 0x100) P(0x2c4, color)` with `color = r8`.
**No DP_MIX, SRC_CNTL, CLR_CMP_CNTL, DP_PIX_WIDTH or DST_CNTL init in the preamble**: DP_MIX (needs 0x70003) is inherited from earlier ops' state.
`count` at +88 is the number of vertices; the loop consumes 2 per line (`r24 += 2`, pointer += 16): an odd count makes the plugin read one vertex past the array (user side).
Per line (needs `free > 11` else flush; 12 words; 0x23c0-0x2548):
```
x0=[+92] y0=[+96] x1=[+100] y1=[+104] (relative to the line's first vertex)
dx = x1-x0, dy = y1-y0; adx=|dx|, ady=|dy| (xor/sub idiom, 32 bit)
if (u32)adx <= (u32)ady : minor = adx, major = ady  else  minor = ady, major = adx
cntl = (adx < ady ? 4 /*Y_MAJOR*/ : 0) | ((u32)y0 < (u32)y1 ? 0x802 /*Y_DIR | BRES_SIGN*/ : 0) | ((u32)x0 < (u32)x1 ? 1 /*X_DIR*/ : 0)
emit P(0x130, cntl) P(0x10c, (x0<<16)|(y0&0xffff)) P(0x124, 2*minor - major) P(0x128, 2*minor) P(0x12c, 2*(minor-major)) P(0x120, major+1)   <- trigger
```
DST_CNTL therefore takes only {0x000,0x001,0x004,0x005,0x802,0x803,0x806,0x807}; DST_LAST_PEL (bit 5) is never set. Unsigned compares make negative
coordinates choose directions wrongly (stock behaviour, harmless under a scissor). Whether the end pixel is drawn / what IOBlit means by inclusive/exclusive ends is **not established**.
When major+1 >= 0x8000 the value written to 0x120 has bit 15 set = DRAW_TRAP.

### 4.5 rProSolidScanlines 0x2568 (type 3, mode 0x6000; re-checked at 0x25b4)
Guard: write pointer null -> 0xe00002be, `mode != 0x6000` -> 0xe00002c7. Preamble, needs `free > 7`, 8 words (0x2668-0x26b4):
`P(0x2d8, 0x100) P(0x2d4, 0x70003) P(0x130, 3) P(0x2c4, color)`.
Per x-pair k in 0..count-1 (x pairs at op+100+8k; y=[+92] and height=[+96] are the SAME for every pair; needs `free > 3` else flush; 4 words; 0x26d8-0x27b8):
`a=[+100+8k], b=[+104+8k]; x = min(a,b) (signed compare); w = |b-a|` (no +1): `P(0x10c, (x<<16)|(y & 0xffff)) P(0x118, (w<<16)|(height & 0xffff))` where the halfwords are `lhz` of the low halves (+94, +98).

### 4.6 Flush 0x14d0 (not a blitter, but writes into the stream)
If write pointer == buf+0x20 (nothing appended) return 0. If `options & 0x10000` and this+0x8c (bound surface) != 0: patch the previous header, write header `0x02000000` as a new packet.
Then the common terminator `0x01000000` + remap (0x153c-0x15c4; same null-store-on-failure bug).

### 4.7 Totals
Registers the plugin can emit (complete list from reading every store in the five blitters): 0x10c 0x118 0x120 0x124 0x128 0x12c 0x130 0x18c 0x190 0x1b4 0x280 0x284 0x2c0 0x2c4 0x2d4 0x2d8 0x300 0x304 0x308,
plus headers 0x00 (implicit), 0x01, 0x02, 0x09, 0x0d. Nothing else (no 0x100, 0x180, 0x2a8, 0x2b4, 0x2d0, 0x2c8, 0x1fc). Basis: full read of the five blitters and Flush; no other function writes to the buffer (Lock/Unlock/Swap/SetSurface only call io_connect_method_*).
Max stream words: per buffer 1013 data words + terminator; per call unbounded (count is user-controlled).
Per-op sizes: Copy 8 + 10/rect; CopyRegion 8 + 10/rect; Fill solid 8 + 4/rect, keyed 8 + 12/rect, pattern 16 + 4 (12 keyed)/rect; Lines 4 + 12/line; Scanlines 8 + 4/pair.
A call with count = 0 still emits its preamble.

## 5. Which values come from where

| value | source |
|---|---|
| colour (solid) | `source` argument r8, raw 32-bit pixel value in the destination format (no conversion) |
| colours (pattern) | `operation->color0` (FRGD) and `color1` (BKGD); pattern words from `*(u32**)source` |
| key colour | `operation->destKeyColor` |
| key mode | `type & 0xF000` via table 0x2f9c -> CLR_CMP_CNTL in {0,4,5} |
| pixel format code | `this+0x70` (byte at get_buffer_info +0x24: 2 = 8 bpp pseudocolour, 3 = aRGB1555, 4 = RGB565, 6 = aRGB8888; RRG DP_PIX_WIDTH [3:0]). The stream never carries a pixel width. Used only for the CLR_CMP_MSK choice |
| rop / mix / alpha | none: DP_MIX is the constant 0x70003 (source copy); operation bits other than Copy are refused by GetBlitter; no alpha |
| clip | none in the plugin. No SC_* write, no region intersection beyond the caller's rect list. Clipping is the hardware scissor the kernel injects |
| destination surface / offset / pitch | not in the stream: set by `SetDestination` (selector 0 `set_surface`) in the kernel; replayed as 0x100/0x2d0/0x2a8/0x2b4 (kernel injected) |
| source surface for copies | op 0x0d: destination surface; op 0x09: display `word1`; kernel writes 0x180 |
| offsetX / offsetY of the operation | ignored by all five blitters |

## 6. What the stock path leaves to hardware state

Set by the kernel (injected, never by the plugin): 0x180 (ops 0x09/0x0d), 0x100, 0x2d0, 0x2a8, 0x2b4 (context-buffer replay `submit_context_buffer` 0x14468 when its flag is set; flag set by `invalidate()`, context switch, mode change), 0x1fc idle waits in kernel-built streams.
Inherited, **not established who set them in the stock driver** (not traced; the stock chip init/reset path `ATIRagePro::start` 0x11ec0 was not analysed here): DP_WRITE_MASK 0x2c8, SRC_HEIGHT1, SRC_Y_X_START, HOST_CNTL, PAT_CNTL, GUI_TRAJ_CNTL, SETUP_CNTL 0x304, Z_CNTL (stream 0x14c, kext 0x54c), SCALE_3D_CNTL, DP_BKGD_CLR for copies.
Inter-op hazards visible in the plugin itself:
* Lines never set DP_MIX; Scanlines/Fill/Copy set it to 0x70003. A lone Lines call relies on a previous op in the same chip session (or kernel init).
* Fill/Lines/Scanlines/Copy never set SRC_CNTL except Copy (0) and pattern fill (2): after a pattern fill, SRC_CNTL stays 2 until the next Copy.
* CLR_CMP_CNTL is reset to 0 only by the keyed-rect epilogue (4.3 step 3); Copy/Lines/Scanlines/solid-unkeyed rely on it being 0.
* The (this+0x7c) packet semantics: Fill/Lines/Scanlines append pairs to whatever packet is current (a preceding 0x0d/0x09 packet, or the raw packet after a remap).
Therefore the validator must keep a shadow of these registers ACROSS buffers within a context (a rect list can be split across maps between preamble and trigger).

## 7. Hardware semantics that matter for safety

1. **Triggers**: only 0x118 (DST_HEIGHT_WIDTH; DST_WIDTH 0x110/0x114-class alias writes would also trigger, so the allow-list excludes them) and 0x120 (DST_BRES_LNTH) start drawing (RRG 5-1: "Operations are initiated implicitly by writing to DST_WIDTH or DST_BRES_LNTH").
2. **DRAW_TRAP**: DST_WIDTH bit 15 and DST_BRES_LNTH bit 15 start a TRAPEZOID with TRAIL_X etc. from other registers. A width (as a 16-bit value) >= 0x8000 or a line length >= 0x7fff therefore changes the operation type. The stock plugin does not guard (negative `w` from `lha` gives bit 15 set). Validator: width <= 0x3fff, height <= 0x3fff, line length <= 0x3fff.
3. Fields: X [30:16] signed (RRG: 14-bit), Y [14:0] signed; the plugin writes 16-bit two's complement halves, so bit 15 of the Y half is a sign copy (SCALE_Y_LSB, ignored) and X's bit 31 hint is a sign copy.
4. **Scissor** (kernel-injected SC_LEFT_RIGHT/SC_TOP_BOTTOM = display extent) clips destination writes of rectangles, blits and lines. It does NOT limit source reads of a blit: SRC_Y_X/SRC_WIDTH1 are unclamped in stock and the engine would read any VRAM address (other clients' surfaces, the register file in the top 2 KB of the aperture, beyond the display). Whether the address wraps or faults for out-of-range sources is **not established**. The validator must therefore clamp the SOURCE rectangle to the display itself (information leak and engine-wedge risk), not rely on the scissor.
5. **Overlap/direction**: a blit within one surface needs the engine to walk in the direction opposite to the displacement (RRG DST_CNTL bits 0/1; stock sets reverse when dest > source, independently per axis). With a reverse direction the start coordinate registers hold the LAST pixel (`sx+w-1`, `dx+w-1`). The kernel's `eng_blit` is hard-wired to DST_CNTL 3 (L->R, T->B) and hardware-proven only in that direction; reverse-direction blits are **not hardware-proven here** (needs a G4 test before shipping, or the kernel can fall back to row-by-row/aux-buffer copy).
6. DP_WRITE_MASK, DP_SRC, DP_MIX, SRC_CNTL (bus-master bits 8-11), CLR_CMP (source compare), PAT_CNTL, GUI_TRAJ_CNTL, DST_CNTL bits 3-4 (tiling side effects modify DST_X/Y after draw), 6 (polygon), 7-10 (24bpp rotation), 12-20 (trapezoid, SUB_PIX_ON, alpha overlap) alter the operation class; the kernel forces or checks them.
7. A copy with CLR_CMP_SRC = 1 (2D source key) needs auto-fastfill off (RRG 5-61): not emitted by stock.

## 8. Proposed abstract op set and accept/refuse rules

### 8.1 Abstract ops (what the validator outputs; kernel executes with `eng_fill`/`eng_blit` plus two small additions)
```
OP_FILL   { x, y, w, h, color, cmp: NONE | DST_EQ | DST_NE, cmp_color }   mix = SRC copy, solid. (Fill rects, Scanlines, keyed fills all map here.)
OP_COPY   { sx, sy, dx, dy, w, h }                                          memmove semantics; kernel picks the walk direction itself.
OP_LINE   { x0, y0, dir_flags, major, minor, color }                         recomputed from the stream's DST_CNTL/BRES values (needs a new eng_line; not yet hardware-proven)
(OP_PATFILL { x, y, w, h, color0, color1, pat0, pat1 }  optional; recommended REFUSE in the first release: pattern state not proven, see 4.3)
```
The kernel always programs the full engine context itself (eng_init_context + SETUP_CNTL 0, Z_CNTL 0, SCALE_3D_CNTL 0, DP_PIX_WIDTH, DP_WRITE_MASK 0xffffffff, SC_*, DST/SRC_OFF_PITCH, CLR_CMP, PAT_CNTL 0, GUI_TRAJ_CNTL, SRC_CNTL 0, DP_SRC/DP_MIX), so no inherited state of section 6 matters. The shadow register file in the validator only decides what operation a trigger means.

### 8.2 Packet-level rules
ACCEPT: header op 0x00 (pairs), 0x0d and 0x09 (display-destination scope: `word1` ignored or must equal the kernel's display index; surface-backed destinations refused), 0x01 terminator (ends the buffer). Length field n: `1 <= n <= remaining`, `(n-1)` words are payload; ops 0x00 payload even (pairs); 0x0d/0x09 payload `1 + 2*k` words (word1 + pairs).
REFUSE the whole buffer: any other op; op 0x02 unless a surface is bound (display-only scope: refuse or ignore, stock never sends it); n beyond the buffer; payload outside the kernel's own copy of the buffer.
Handling: copy the buffer into kernel memory first (user can rewrite it concurrently; the capacity at +0x10 is user-writable, so the kernel must use its own size), validate the copy atomically (all or nothing, as `rage_batch_ok`), commit the shadow state only if the whole buffer passes, then execute ops in order. Total words <= 1016 per buffer.

### 8.3 Register rules (stream register numbers). ACCEPT writes only to:
0x10c, 0x118, 0x120, 0x124, 0x128, 0x12c, 0x130, 0x18c, 0x190, 0x1b4, 0x280, 0x284, 0x2c0, 0x2c4, 0x2d4, 0x2d8, 0x300, 0x304, 0x308 (the complete list in 4.7).
REFUSE everything else, specifically 0x100, 0x180 (only via 0x09/0x0d), 0x1fc, 0x2d0, 0x2a8, 0x2b4, 0x2c8, other DST_WIDTH/DST_X_WIDTH/DST_WIDTH_HEIGHT (0x2ec) trigger aliases, 0x138-0x188 other source/trajectory registers, 0x1a4/0x1a8 etc., 0x5xx-0x3xx host/bus-master/3D registers, odd offsets, registers >= 0x400.

Per-register value rules, applied when the register is written (reject at write time so the shadow only ever holds acceptable values):
* 0x130 DST_CNTL: rect/blit/fill context: `value & ~3 == 0`; line context: `value in {0x000,0x001,0x004,0x005,0x802,0x803,0x806,0x807}` with bit 11 == bit 1. (Both use the same register; keep the value and check at the trigger: 0x118 -> {0..3}, 0x120 -> the line set.)
* 0x2d8 DP_SRC: `{0x100 (solid), 0x300 (blit)}` (+ `0x10001` only if OP_PATFILL is enabled).
* 0x2d4 DP_MIX: `== 0x00070003` (stock never writes anything else; any other mix is a different operation class).
* 0x1b4 SRC_CNTL: `{0, 2}` (bits 8-11 bus master must stay 0; the kernel ignores 2 unless OP_PATFILL).
* 0x308 CLR_CMP_CNTL: `{0, 4, 5}` (SRC field must be 0 = destination); 0x304 CLR_CMP_MSK: `{0xffff, 0xffffffff}`; 0x300 CLR_CMP_CLR: any (masked to depth).
* 0x2c4, 0x2c0, 0x280, 0x284: any value (colours/pattern are data; mask to pixel depth).
* 0x190 SRC_WIDTH1: any 32-bit value is harmless to the engine only if bits above 13 are ignored (not established): require `<= 0x3fff`.
* 0x10c, 0x18c: decode `x = sext16(v>>16)`, `y = sext16(v & 0xffff)`; REFUSE unless `-8192 <= x <= 8191` and `-16384 <= y <= 16383` (the plugin's 16-bit sign copies decode exactly).
* 0x118: `w = (v>>16)`, `h = (v & 0xffff)`: REFUSE if `w > 0x3fff` or `h > 0x3fff` (rejects DRAW_TRAP bit 15, FILL_DIS bit 31). Zero sizes are legal (no-op or one row; skip them).
* 0x120: `v` must be `<= 0x3fff` (bit 15 DRAW_TRAP and TRAIL_X bits refused). 0x124/0x128/0x12c: signed 18-bit range; line context: `INC == 2*minor`, `DEC == 2*(minor-major)`, `ERR == 2*minor-major`, `LNTH == major+1`, `0 <= minor <= major`, `Y_MAJOR` consistent (`Y_MAJOR=1` iff the caller's |dx| < |dy|; with only registers visible, check `minor <= major` and that the 3 values are mutually consistent; recompute them from `(major, minor)` for the kernel's own line program).

### 8.4 Trigger rules (interpretation). Shadow state per context (defaults when a context starts: DP_SRC=0, DP_MIX=0x70003, DST_CNTL=3, SRC_CNTL=0, CLR_CMP_CNTL=0, CLR_CMP_MSK=0xffffffff; "DP_SRC==0" = nothing set yet => trigger refused).
`0x118` write:
* `DP_SRC == 0x100` and `DST_CNTL == 3`: OP_FILL(x=DST_Y_X.x, y=.y, w, h, color=FRGD, cmp from CLR_CMP_CNTL {0 NONE, 4 DST_EQ, 5 DST_NE}, cmp_color). `SRC_CNTL` in {0,2} tolerated and ignored.
* `DP_SRC == 0x300`, `SRC_CNTL == 0`, `CLR_CMP_CNTL == 0`, `DST_CNTL in 0..3`, and `SRC_WIDTH1 == w`: OP_COPY. Decode starts as the LAST pixel for a reversed axis: `left_s = xdir ? sx : sx-(w-1)`, `top_s = ydir ? sy : sy-(h-1)`, same for the destination with its own start; abstract rect uses the left/top. Kernel then chooses its own direction from clipped coordinates (see 8.5).
* anything else (patfill if disabled, mismatched DP_SRC/DP_MIX, DST_CNTL not in 0..3): REFUSE.
`0x120` write: `DP_SRC == 0x100` and DST_CNTL in the line set: OP_LINE (start = DST_Y_X, dir from DST_CNTL bits 0/1/2, length `LNTH`, minor from `INC/2`). Anything else REFUSE.
Writes to any register after a trigger just update the shadow. Pairs may straddle buffers: validator state persists until the context dies.
After an OP_FILL with `CLR_CMP_CNTL != 0` the plugin resets `CLR_CMP_CNTL` to 0 itself, so the shadow value 0 is the expected steady state.

### 8.5 Clamping (always done by the validator, independent of the scissor)
* OP_FILL: destination rect clipped to the display `[0,W) x [0,H)`; empty -> dropped. (The scissor would clip it too; do it anyway so the engine never gets an off-display start.)
* OP_COPY: let S = (sx,sy,w,h), D = (dx,dy,w,h). Compute `x0 = max(0, -sx, -dx)`, `x1 = min(w, W-sx, W-dx)` (same for y with H); if `x1<=x0` or `y1<=y0` drop; copy the sub-rectangle `(sx+x0, sy+y0) -> (dx+x0, dy+y0)`, size `(x1-x0) x (y1-y0)`. This keeps BOTH source and destination inside the display (source is not scissor-limited, section 7.4). Direction: reverse an axis iff the (clipped) destination coordinate is greater than the source coordinate on that axis (`signed` compares, unlike stock's unsigned); with reversal the start registers are the last pixel (`+w-1`/`+h-1`). Same-row/same-column copies with equal coordinates are no-ops.
* OP_LINE: start must be inside the display (reject otherwise); length bound `major <= max(W,H)`; the scissor clips everything else.
* Pixel format: the validator needs the display's `pixcode`/bytes-per-pixel only to mask colours and to bound sizes: use the kernel's own mode, never anything from the stream (the stream has no pixel width).

## 9. Pixel formats and colour encodings possible
`this+0x70` / get_buffer_info +0x24 can be 2 (8 bpp pseudocolour/CLUT index), 3 (aRGB1555, 15 bpp), 4 (RGB565), 6 (aRGB8888, 32 bpp) (ga-2d-client-decode.md 3.2; RRG DP_PIX_WIDTH encoding). 7 (RGB332), 8 (Y8) etc. are RRG codes the stock structure never reports. The plugin's behaviour per format:
* colour fields written raw; the hardware uses the low 8 / 16 / 16 / 32 bits (RRG Table 5-7).
* only the key-compare mask depends on pixcode: 3 -> 0xffff, others -> 0xffffffff.
* DP_PIX_WIDTH itself (0x2d0 = b | b<<8 | b<<16 with b = pixcode, ga-2d-client-decode.md) is kernel-injected; the validator never sees it.
There is no 24 bpp and no YUV path in the stock 2D plugin.

## 10. Open questions / not established
1. Whether stock hardware renders pattern fill correctly with DP_SRC 0x10001 + SRC_CNTL 2 and no PAT_CNTL (not an RRG Table 5-12 combination).
2. Whether DST_LAST_PEL = 0 is the intended IOBlit line semantics (end pixel excluded) and what IOBlit lines mean by their second vertex.
3. Engine behaviour for out-of-range source addresses (wrap, fault, harmless) and reverse-direction blits on this chip under the kext's current sequencing (hardware test needed).
4. Which stock path sets the inherited registers (DP_WRITE_MASK, SETUP_CNTL, Z_CNTL, SCALE_3D_CNTL...) before the first 2D op; the kernel here sets them itself.
5. What `process_command_buffer` does with an unknown header op (the stock plugin never emits one) and whether SRC_WIDTH1 bits above 13 are ignored.
6. CopyRegion's first two guard comparisons (0x19d4-0x1a1c) are self-comparisons of the snapshot; the intended predicate is unknown.
7. Who actually calls GetBlitter on Panther for the Rage (libXplugin for X11, plus the IOPSAllocateBlitEngine/MPHW* path per quartz-acceleration-study.md): which of Rects/Scanlines/Lines/MonoExpand/key-colour Quartz really produces is unmeasured.

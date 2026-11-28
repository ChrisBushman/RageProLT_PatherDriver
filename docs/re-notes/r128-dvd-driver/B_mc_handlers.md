# B. Motion-compensation handlers of ATIRage128DVDDriver.bundle (PPC, 10.3) - what they emit

Scope: 0x2a38-0x4ab0 of av/r128dvd_otool.txt. Static analysis only. All values below were obtained by
a symbolic re-execution of the otool listing (script: av/symx.py, per-handler word lists) plus manual
reading. Tags: E = ESTABLISHED (address given), I = INFERRED (reasoned, not proven), N = NOT established.

## 0. Big picture (E unless noted)

* The handlers do NOT write MMIO registers one by one with type-0 headers. Each handler appends ONE
  CCE **type-3 packet, opcode 0x31** (header 0xC0003100 | (count<<16)) to a user/kernel shared
  command buffer. The header word is only reserved by the handler (ctx[0x2b4] = start; first dword
  skipped); it is patched afterwards at 0x57ac..0x57d4: header = 0xC0003100 | (((end-start)/4 - 2)<<16).
  (E, 0x57a8 region; handler stores start in ctx[0x2b4] and end in ctx[0x2b8] at its tail, e.g. 0x3848/0x3908.)
* Payload layout of every handler (E): `[CW] ( [n] regwords... )+` where
  CW = ctx[0x2d4] | mode bits; ctx[0x2d4] = (arg5 of the setup fn 0x2d10/0x30b0) << 4 (E, 0x2d1c/0x2d30, 0x30bc/0x30d0).
  The small numbers n in {5,6,7,3} are literal words (li) inserted before groups of register words.
* The register words are, by field layout, writes to the **MC register block** of the Rage 128 Pro
  (register ref RRG-G04500-C.txt, p3-261): MC_SRC2_CNTL 0x19D4 (dword idx 0x675), MC_SRC1_CNTL 0x19D8 (0x676),
  MC_DST_CNTL 0x19DC (0x677), MC_START_CNTL 0x19E0 (0x678). Evidence: the word fields match those registers bit for bit
  (see section 2) and n = 5,6,7 equals the low nibble of the dword index of SRC2, SRC1, DST (I, strong:
  every group whose first word is SRC2 has n=5, SRC1 n=6, DST n=7 in all 7 handlers). The grammar of the
  microcode/FIFO that consumes the stream is not in this binary (N): in particular the meaning of n=3 (always
  precedes a lone START word for the V plane) and the meaning of CW.
* Sequencing inside a group (E): [n][regs..., last = START]; a run may contain several (SRC..START) cycles without
  a repeated n (3c7c, 4394, 3920, 4784, 3ea0). START is the trigger (I: the last word of each cycle always is START;
  MC_START_CNTL is the only MC reg with DST_OFFSET, so it is the "go").
* Nothing in these functions programs the IDCT coefficient path. IDCT_EN (SRC1 bit 28) is set in every SRC1
  word of every predicting handler (E). How coefficients arrive is N (not in scope; no IDCT_RUNS/LEVELS writes
  exist in the whole binary: grep of 0x7e0..0x7ef/0x1f80.. finds nothing).

## 1. Ring / flush mechanism (E)

ctx fields: +0x14 IOKit connection, +0x30 pointer to header word of the OPEN record, +0x34 write ptr, +0x38 limit ptr,
+0x40 pointer to shared command-buffer header (hdr+0x10 = capacity in dwords; records start at hdr+0x20, the sentinel
header word is at hdr+0x1c), +0x44 out-param of submit.
Records are `[type<<24 | dword count][payload...]`; when a new record is opened the previous header gets the count
ORed in: `*hdr |= (cur-hdr)>>2` (e.g. 0x2e6c-0x2e84). Record types seen: 0x00 sentinel/raw CCE dwords (type-0 reg writes
and type-3 packets go here), 0x01 = flush marker, 0x16/0x17/0x18/0x19 = driver-private parameter records
(consumed by the kernel, I).
Overflow check pattern in every emitter (e.g. 0x37ac-0x3838): if (ctx[0x34] + need >= ctx[0x38]) { patch open header;
write 0x01000000 flush record at cur; call 0x6e7c(ctx[0x14], 1, *global, &ctx[0x40], &ctx[0x44], 1) = IOConnectMethod
submit (I, wrapper at 0x6e7c); reset: ctx[0x34]=hdr+0x20, ctx[0x30]=hdr+0x1c, ctx[0x38]=hdr+0x20+4*hdr[0x10], hdr[0x1c]=0 }.
No explicit wait/fence is emitted by the MC handlers themselves (no WAIT_UNTIL, no register poll) (E). A WAIT_UNTIL
(idx 0x5c8, data 0x800) is emitted only in the surface-setup functions 0x2d10/0x30b0 (E).
0x5444(ctx, a4) is called first by every handler (E): if free ring dwords-1 > 0x1fff it returns immediately; otherwise
(near the end of the buffer) it calls 0x347c, pads to even dword count with a 0x80000000 NOP, flushes (0x01000000 +
0x6e7c) and re-emits DP_DATATYPE=0x30d08, DP_CNTL=0x1e7, DST_PITCH=ctx[0x2a8]>>4 and a 0x19 record (0x5444-0x5790). (I: "re-prime after
buffer nearly full")

## 2. Word formats (all stored byte-swapped, i.e. little-endian dwords; `stwbrx`)

MC_SRC1_CNTL (0x676): [12:0] SCALE_HACC, [27:16] SCALE_VACC, 28 IDCT_EN, 29 SECONDARY_TEX_EN, [31:30] SCALE_PITCH_ADJ.
MC_SRC2_CNTL (0x675): [12:0] HACC, [27:16] VACC, [31:30] PITCH_ADJ.
MC_DST_CNTL  (0x677): [13:0] DST_Y, [29:16] DST_X, [31:30] DST_PITCH_ADJ.
MC_START_CNTL(0x678): [3:0] SCALE_OFFSET_PTR, [24:4] DST_OFFSET, 25 ALPHA_EN, [28:26] SECONDARY_OFFSET_PTR, [31:29] DST_HEIGHT_WIDTH.
PITCH_ADJ (SECONDARY_SCALE_PITCH doc, line 8088): 1 = pitch x2, 2 = pitch x4.

Observed use of the fields (E from the listings; meaning I):
* HACC = mvx + 2*X  (luma), VACC = mvy + 2*Y (frame-lines) or mvy + Y (field-in-frame, see 3920/3ea0): mv is in
  half-pel units, X/Y in pixels -> HACC/VACC are positions in half-pel units (1 fractional bit). I.
* Chroma: HACC = X + trunc(mvx/2), VACC = Y' + trunc(mvy/2) (srawi 1 + addze = divide by 2 truncating toward zero).
  Chroma position is therefore again in half-(chroma)-pel units. E (arith) / I (semantics).
* PITCH_ADJ: luma plane 1 (x2) in frame access, 2 (x4) in field access; chroma 0 resp. 1. Luma pitch = 2x chroma pitch, field = 2x frame. I.
* DST word luma = adj<<30 | X<<16 | Y ; chroma word = (luma word with the adj bits clear/halved)>>1: DST_X=X/2, DST_Y=Y/2 (frame) and adj 0;
  (field handlers: word has adj 2 before the >>1 so chroma adj = 1, Y/4). E.
* START = ctx[0x2d8..0x2ec] (byte address, 16-byte aligned, = DST_OFFSET bits 24:4) | SCALE_OFFSET_PTR | ALPHA_EN | HW | SEC_PTR.
  - SCALE_OFFSET_PTR: luma = p, U = p+2, V = p+4; p = 6 for forward ref (type 1/5 style SEL==0), p = 0 for backward ref;
    in field handlers p = fieldsel + (SEL==0 ? 6 : 0) where fieldsel (0/1) is MB byte 0x10..0x13 (reference field parity). E.
    => offset-table slots 0..5 = backward-ref planes {Y top,Y bot,U top,U bot,V top,V bot}, 6..11 = forward-ref planes. I.
  - ALPHA_EN (0x02000000) = 0 only in the two intra handlers (3780, 45ec); 1 in every predicting handler. E. (blend/add of IDCT
    residual onto prediction: I; N proven.)
  - DST_HEIGHT_WIDTH: luma frame MB = 6, luma 16x8 = 5, chroma 8x8 = 1, chroma 8x4 = 0. E (values) / I (meaning: 6=16x16, 5=16x8, 1=8x8, 0=8x4;
    cf. Win98 triggers 0x100010/0x100008/0x80008/0x80004 in the same order).
  - SECONDARY_OFFSET_PTR (bidirectional handlers only): luma 1+f, U 3+f, V 5+f (f = bwd-ref field parity, 0 in frame 4394). E.
* Bidirectional handlers put the BACKWARD mv in SRC2 and the FORWARD mv in SRC1 with SECONDARY_TEX_EN=1 (E); SECONDARY_TEX_EN = "use secondary
  texture" => second reference averaged (I). No explicit "average" flag/shift/round op is emitted (E); the hardware must do the (a+b+1)>>1 (N).

CW (first payload dword) = ctx[0x2d4] | bits (E):
 3780 intra: 0x20 | (F?2:0)   45ec intra(field pic): 0x20   3c7c: (F?2:0)   4394: (F?2:0)
 3920 and 3ea0: 0x4 | (F==0 ? 1 : 0)   4784: nothing.   F = MB byte +0x15 (what it is: N; other agent documents MB layout).
 Init-time packets (0x347c) use CW = 0x100 and 0 (E). Semantics of CW bits: N.

## 3. Per-function results

Arguments to the handlers (from dispatcher 0x243c, E): r3 = ctx, r4 = passed through to 0x5444, r5 = X (luma pixel x of MB), r6 = Y (luma pixel y; frame
row in frame pictures, field row in field pictures), r7 = MB byte 0x15 (F). Dispatcher r5 (picture structure) == 3 => frame-picture table 0x24a0, else field-picture table 0x275c.

### 0x3780 (frame picture, type 0)  args: ctx,a4,X,Y,F.  Emits 10 dwords (E)
```
[hdr placeholder 0xC0003100|..]
CW  = ctx2d4 | 0x20 | (F?2:0)
7
DST   0x40000000 | X<<16 | Y                      (luma, pitch adj 1)
START 0xC0000000 | ctx[0x2d8]                     (Y top-start, HW 6, ALPHA 0, no src ptr)
7
DST   ((X<<16)|Y)>>1                              (chroma, adj 0)
START 0x20000000 | ctx[0x2e0]                     (U, HW 1)
3
START 0x20000000 | ctx[0x2e8]                     (V)
```
No SRC1/SRC2 write at all, ALPHA_EN=0, no SCALE ptr => intra: only the residual (IDCT) is stored (I). Return r3 = end ptr; ctx[0x2b4/0x2b8] set.

### 0x3c7c (frame picture, types 1 and 2: single-direction 16x16 frame prediction)  E
args: r5=X r6=Y r7=F r9=SEL (0 fwd / 1 bwd) r10=ptr to {mvx,mvy} (int pair, half-pel). 11 dwords.
```
CW = ctx2d4 | (F?2:0)
6
SRC1  0x50000000 | (mvy+2Y)<<16 | (mvx+2X)          (adj1, IDCT_EN)
DST   0x40000000 | X<<16 | Y
START 0xC2000000 | ctx[0x2d8] | p                   (p = SEL==0 ? 6 : 0)
SRC1  0x10000000 | (Y+trunc(mvy/2))<<16 | (X+trunc(mvx/2))   (chroma, adj0, IDCT_EN)
DST   ((X<<16)|Y)>>1
START 0x22000000 | ctx[0x2e0] | (p+2)
3
START 0x22000000 | ctx[0x2e8] | (p+4)               (V reuses SRC1/DST of U)
```
=> 16x16 luma + 8x8 U + 8x8 V, MV in half-pel, one reference. Forward when SEL=0 (dispatcher: type1 -> mv at MB+0/+2, SEL 0; type2 -> MB+4/+6, SEL 1). I.

### 0x4394 (frame picture, type 3: bidirectional 16x16)  E
args: r5=X r6=Y r7=F r9=MVA (fwd {x,y}), r10=MVB (bwd {x,y}) (dispatcher copies MB halfwords 0..7 into SP+0x50). 13 dwords.
```
CW = ctx2d4 | (F?2:0)
5
SRC2  0x40000000 | (mvB.y+2Y)<<16 | (mvB.x+2X)
SRC1  0x70000000 | (mvA.y+2Y)<<16 | (mvA.x+2X)      (adj1, SECONDARY_TEX_EN, IDCT_EN)
DST   0x40000000 | X<<16 | Y
START 0xC6000000 | ctx[0x2d8] | 6                    (HW6, SEC_PTR 1, ALPHA)
SRC2  (Y+trunc(mvB.y/2))<<16 | (X+trunc(mvB.x/2))
SRC1  0x30000000 | (Y+trunc(mvA.y/2))<<16 | (X+trunc(mvA.x/2))
DST   ((X<<16)|Y)>>1
START 0x2E000000 | ctx[0x2e0] | 8                    (HW1, SEC_PTR 3, ALPHA)
3
START 0x36000000 | ctx[0x2e8] | 0xA                  (SEC_PTR 5)
```
Frame bidirectional prediction: fwd ref slots 6/8/10, bwd ref via secondary slots 1/3/5. I.

### 0x3920 (frame picture, types 5 and 6: field-based prediction, two MVs, single direction, 16x8 per field)  E
args: r5=X r6=Y r7=F r9=SEL (0 fwd,1 bwd) r10=MVP (array of 2 pointers to {x,y}) stack 9th (r28) = TAB (ints = MB bytes 0x10/0x12 for fwd, 0x11/0x13 for bwd).
First action: each mv.y is halved IN PLACE (srawi 1) (dispatcher doubled it) (E, 0x3980-0x39a0) - side effect on caller arrays.
Let s0 = TAB[SEL] (+6 if SEL==0), s1 = TAB[SEL+2] (+6 if SEL==0). 23 dwords:
```
CW = ctx2d4 | 4 | (F==0)
6
SRC1 0x90000000 | (mv0.y+Y)<<16 | (mv0.x+2X)         (adj 2, IDCT_EN)   top-field dst
DST  0x80000000 | X<<16 | (Y>>1)                       (adj 2)
START 0xA2000000 | ctx[0x2d8] | s0                    (HW 5 = 16x8)
SRC1 0x90000000 | (mv1.y+Y)<<16 | (mv1.x+2X)         bottom-field dst
DST  same as above
START 0xA2000000 | ctx[0x2dc] | s1
6
SRC1 0x50000000 | (Y>>1 + trunc(mv0.y/2))<<16 | (X+trunc(mv0.x/2))   U, field 0
DST  (0x80000000|X<<16|Y>>1)>>1
START 0x02000000 | ctx[0x2e0] | s0+2                (HW 0 = 8x4)
SRC1 ... mv1 ...  DST same  START 0x02000000 | ctx[0x2e4] | s1+2
6
(same two blocks for V:  ctx[0x2e8] | s0+4 ,  ctx[0x2ec] | s1+4)
```
ctx[0x2dc/2e4/2ec] = bottom-field starts (= top + pitch, set in 0x2d10). => MPEG-2 "field prediction in a frame picture" (two 16x8 field MBs,
each with own MV and own reference-field parity). I (semantics); words E.

### 0x3ea0 (frame picture, type 7: field-based bidirectional, 4 MVs)  E
args: r5=X r6=Y r7=F r9=MVF (2 ptrs fwd, per dst field) r10=MVB (2 ptrs bwd) stack 0xe8(r1)=TAB[4] = {fwdsel0, bwdsel0, fwdsel1, bwdsel1}. All four mv.y halved in place.
29 dwords: CW = ctx2d4 | 4 | (F==0); then three groups, plane = Y,U,V:
```
5
SRC2 0x80000000|(mvB0.y+Y)<<16|(mvB0.x+2X)   [chroma: 0x40000000|(Y>>1+trunc(mvB0.y/2))<<16|(X+trunc(mvB0.x/2))]
SRC1 0xB0000000|(mvF0.y+Y)<<16|(mvF0.x+2X)   [chroma: 0x70000000|...]
DST  0x80000000|X<<16|(Y>>1)                  [chroma: >>1 of that]
START 0xA2000000|ctx[0x2d8]|(TAB[0]+6)|((TAB[1]+1)<<26)    [U: 0x02000000|ctx2e0|(TAB0+6+2)|((TAB1+3)<<26); V: ctx2e8, +4, +5]
SRC2/SRC1/DST for field 1 using mvB1/mvF1, START ctx[0x2dc] (U: 0x2e4, V: 0x2ec) with TAB[2],TAB[3]
```
=> field bidirectional prediction in a frame picture (two fields x two directions). I.

### 0x45ec (field picture, type 0, intra)  args X=r5, Y=r6, r8=picture_structure(1 top/2 bottom) E (10 dwords)
```
CW = ctx2d4 | 0x20
7  DST 0x80000000|X<<16|Y   START 0xC0000000|ctx[0x2d8+4*(ps-1)]
7  DST (that)>>1            START 0x20000000|ctx[0x2e0+4*(ps-1)]
3  START 0x20000000|ctx[0x2e8+4*(ps-1)]
```
(ps-1 selects top/bottom field base: 0x2d8 vs 0x2dc etc.). Intra, residual only. I.

### 0x4784 (field picture, types 5 and 6: 16x8 prediction, two MVs, single direction)  E
args: ctx=r3, r5=X, r6=Y, r8=SEL(0 fwd/1 bwd), r9=MVP (2 ptr to {x,y}), r10=TAB (ints). mv.y halved in place (as 3920). 20 dwords; CW = ctx2d4 (no flag bits).
Two halves h=0,1 (upper 8 rows from mv0 at Y, lower 8 rows from mv1 at Y+8); s_h = TAB[SEL+2h] (+6 if SEL==0):
```
6  SRC1 0x90000000|(mv_h.y+2Y_h)<<16|(mv_h.x+2X)  DST 0x80000000|X<<16|Y_h  START 0xA2000000|ctx[0x2d8]|s_h
   SRC1 0x50000000|(Y_h+trunc(mv.y/2))<<16|(X+trunc(mv.x/2))  DST (..)>>1  START 0x02000000|ctx[0x2e0]|(s_h+2)
3  START 0x02000000|ctx[0x2e8]|(s_h+4)
```
Note (E): both halves use the unconditional base ctx[0x2d8/2e0/2e8] (no ps-1 selection as in 45ec): the destination field must have been
programmed into ctx[0x2d8..] by 0x2d10/0x30b0 beforehand (I). 16x8 field-picture prediction. I.

### Helpers
* 0x2a38 (E): kernel call 0x6ddc(ctx[0x14], sel 4,...) fetching 5 surface offsets -> ctx[0x2c0+4i] (i=0..4). Not an emitter.
  (Callers 0x57dc/0x58b0 then build per-surface tables: Y=base, U=base+0x6c000, V=U+0x1b000 (768x576 Y, 384x288 chroma), E arithmetic, from addis/addi
  with sign-extended 0xc000/0xb000.)
* 0x2ad4 (E): kernel call 0x6e9c(conn, sel 5, {0,1}) - not an emitter (I: close/commit).
* 0x2b20 (E): ring-less reset: ClearMP x5, ClearSP x8 (DVDDriverClearMP/SP), saves/restores ctx[0x140/0x144]. Not MC.
* 0x2b94, 0x2c84, 0x2cb4 (E structure): MP/SP buffer plumbing: kernel call 0x6dbc selector 6 with a 0x1c-byte struct using ctx[0x1cc], ctx[0x188/0x1a8..];
  copy of 8 words ctx[0x188..]/[0x168..]; wrapper to 0x6328. Not MC emitters.
* 0x2d10 / 0x30b0 (E): per-picture surface setup (not per-MB). Set ctx[0x2d4]=arg5<<4; ctx[0x2d8,2dc,2e0,2e4,2e8,2ec] = Y top, Y top+tab[15], U, U+tab[16], V, V+tab[16]
  from a 15+2-int table (tab[i], tab[i+5], tab[i+10] = Y,U,V bases of surface i; tab[15] = luma pitch bytes = ctx[0x2a8], tab[16] = chroma pitch). 0xffff refs -> 0.
  Emit: 0x5444; type-0 pair WAIT_UNTIL(idx 0x5c8)=0x800; record 0x16 (2d10) / 0x17 (30b0) with {tab15, tab16, ref idxs.., ctx2d8}; type-0 writes
  idx 0x667, SECONDARY_SCALE_PITCH(0x660), DST_PITCH(0x502) = tab[15]>>4; 0x5b20; ctx[0x14c/0x150/0x154] = {surface idx, pitch Y, pitch C}; record 0x19 {ctx150,154,14c,2d8}.
  2d10 = I/P picture setup (0x16), 30b0 = B picture setup with two refs (0x17) (I).
* 0x347c (E): stream (re)initialiser. Opens record 0x18 (0x134 bytes): dwords 0x40, 0x4585b3, 70 x 0xFFFFFFFF, 0xC0003100,0x100, 0xC0003100,0x0; then
  DP_CNTL(0x5b0)=0x8107, DP_DATATYPE(0x5b1)=0x20030f03 (0x20030f02 for 8bpp via 0x6efc, 0x20030f06 for 32bpp), DST_PITCH(0x502)=(int)float from 0x6f5c.
  Purpose of the 70 0xFFFFFFFF dwords / the 0x40,0x4585b3 pair: N. Called only via 0x5444/0x5984.
* 0x5444, 0x57a8 (finaliser: patches the 0xC0003100 header), 0x5b20: outside scope, behaviour noted above.

## 4. Dispatcher mapping (0x243c; jump tables 0x24a0 = frame pic, 0x275c = field pic; type = MB byte +0x14) (E)

| type | frame picture (ps==3) | field picture (ps!=3) | MPEG-2 prediction (I) |
|---|---|---|---|
| 0 | 0x3780 | 0x45ec | intra (residual only; no source regs, ALPHA 0) |
| 1 | 0x3c7c SEL0, mv MB+0/+2 | 0x4ab0 (out of scope) | forward, 16x16 (frame MC / field-pic field MC) |
| 2 | 0x3c7c SEL1, mv MB+4/+6 | 0x4ab0 (oos) | backward |
| 3 | 0x4394 | 0x51b4 (oos) | bidirectional (avg of fwd+bwd via SRC2 + SECONDARY_TEX_EN) |
| 4 | none (jumps to 0x2a20, nothing emitted) | none | invalid/unused |
| 5 | 0x3920 SEL0 (two field MVs, 16x8 each) | 0x4784 SEL0 (16x8 upper/lower MV) | forward, field-pred (frame pic) / 16x8 (field pic) |
| 6 | 0x3920 SEL1 | 0x4784 SEL1 | backward, same |
| 7 | 0x3ea0 (4 MVs) | 0x4d48 (oos) | bidirectional field-pred / 16x8 |
Dual prime: no distinct handler or type value; N (either unsupported or pre-converted by the caller to types 3/7 with computed MVs - not shown).
Dispatcher also converts MB halfwords: x kept, y*2 (field mv y doubled, rlwinm 1) before calling the field handlers; 3920/3ea0/4784 halve it back in place.

## 5. NOT established
* Grammar of the packet consumer: meaning of n (3,5,6,7 hypothesis: first register of a cycle; 3 anomalous), of CW and its mode bits (0x20, 0x4, 0x2, 0x1), of MB byte +0x15.
* How/where the IDCT coefficients are fed (no register writes in this scope); whether ALPHA_EN performs the residual add; exact meaning of the SCALE_OFFSET slot table (which ring record loads slots 0..11).
* Half-pel interpolation rule (HACC/VACC LSB) and rounding; the average rule for bidirectional.
* Meaning of the 0x347c filler words.
* Correct handling for dual prime, and why 4784 does not index base by ps.

# ATIRage128DVDDriver `_DVDDriverDecode` - INPUT contract (static analysis)

Scope: 0x1c6c-0x2a38 (entry, 0x1d30 MB loop, 0x243c dispatcher, field arm 0x2734-0x2a20).
Handler internals (0x2a38+) NOT analysed except the first few instructions needed to see how an
argument is used. Source: av/r128dvd_otool.txt, jump tables read from av/r128dvd.bin
(file offset == text address, verified: `88e60015` at 0x24c0).
Tags: [E] ESTABLISHED (address given), [I] INFERRED (MPEG-2 cross-check / plausible, code does not prove).
PIC base: `bcl` at 0x1d54 -> r31 = 0x1d58; at 0x2444 -> r31 = 0x2448. Statics below are r31+offset.

## 1. Data structures

### 1.1 Entry arguments
`int DVDDriverDecode(Ctx *ctx /*r3*/, DecodeParams *p /*r4*/, const int16_t rect[4] /*r5*/)`
Returns 0 on success, -3 (0xfffd) if a 0x1d30 pass returned -1 (0x1cf0, 0x1d0c, 0x1d18) [E].

rect [E 0x1c84-0x1cac]: `rect[2]-rect[0]` compared with ctx->u32@0x2ac, `rect[3]-rect[1]` compared with
ctx->u32@0x2a4. If either differs: `DVDDriverSetMPRects(ctx, rect, 0, 0)` (r4=rect,r5=0,r6=0) [E 0x1cb4-0x1cc0].
So rect = {top,left,bottom,right} int16 (Mac Rect order [I]); [3]-[1] is horizontal extent (== ctx width 0x2a4),
[2]-[0] vertical extent (== ctx height 0x2ac). The decoder does not otherwise read rect.

### 1.2 DecodeParams (p)  - every byte read by 0x1c6c/0x1d30 [E unless noted]
Let f = field-call index (0 for the first/only pass, 1 for the second pass; see 2.1).
```c
struct DecodeParams {
  uint8_t  pic_type[2];   // +0,+1: read as p[f] (0x1d80: lbzx r2,r4,r5). Called "ptype". Stored at sp+0x60.
                          //   Only tested ==2 (0x1e70) with f!=0 and non-frame (see 2.2). Value 2 = P picture [I]
                          //   (MPEG-2 picture_coding_type; 1=I,2=P,3=B). Passed on to 0x57dc and (field arm) to 0x4ab0/ptype.
  uint8_t  structure[2];  // +2,+3: read as p[2+f] (0x1d58 lbz 2(r2)). 3 = frame picture [E 0x1cc4/0x1e0c/0x2474];
                          //   1 or 2 = field picture (top/bottom order [I]) [E 0x1d9c: (v-1)&0xff<=1 => field class,
                          //   0x1e84: ==1 selects r6/r8 = 0/1 else 1/0]. 0 = "nothing" for f==0 (0x1d6c-0x1d94 returns 0).
                          //   Only p[2] is tested by DVDDriverDecode (3 => single pass, else two passes, 0x1cc4).
  uint8_t  aux_flag[2];   // +4,+5: read as p[4+f] into r9 at 0x1d74, not used by 0x1d30 itself but r9 survives
                          //   untouched to the calls 0x57dc/0x58b0 where it is the 6th arg (r9->r28, used as a byte
                          //   `rlwinm r5,r28,0,24,31` at 0x5868 passed to 0x2d10). Meaning NOT established.
  uint8_t  buf_a;         // +6 : buffer index; 0xff -> rewritten to 0 in the caller's struct (0x1e24-0x1e38) [E]
  uint8_t  buf_b;         // +7 : same, 0xff -> 0 (0x1e3c-0x1e50) [E]
  uint8_t  buf_c;         // +8 : same, 0xff -> 0 (0x1e54-0x1e64) [E]
                          //   Roles (dest/fwd-ref/bwd-ref) not proven here; goes to 0x57dc(r6,r7,r8) / 0x58b0 (r4,r5,r7).
  uint8_t  pad9[3];       // +9..+0xb: not read in scope
  MbRec   *mb;            // +0x0c: MB record array base (lwz 0xc(r11) 0x1de0 -> r16) [E]
  uint8_t *coef;          // +0x10: coefficient stream base (lwz 0x10(r11) 0x1ddc -> r27) [E]
};
```
Note: for FIELD pictures the second pass (f==1) ignores p->mb / p->coef and continues from static pointers
saved by the first pass (2.1). Hence one p->mb array and one p->coef stream must hold BOTH fields back to back.

### 1.3 MbRec - per-macroblock record, stride 0x1c bytes [E 0x23b0 `addi r16,r16,0x1c`]
```c
struct MbRec {            // all multi-byte fields big-endian (PPC native)
  int16_t mv[2][2][2];    // +0x00..+0x0f: mv[r][s][t] at offset 8*r+4*s+2*t   (see below)
  uint8_t sel[2][2];      // +0x10..+0x13: sel[r][s] at offset 0x10+2*r+s
  uint8_t mc_type;        // +0x14: 0..7 selects dispatcher arm (jump table); >7 -> no handler (0x247c/0x2738)
  uint8_t flags;          // +0x15: passed as arg "flags" (r7) to intra handlers 3780/45ec and frame-arm MC handlers
                          //        3c7c/4394/3920/3ea0 (0x24c0,0x24d8,0x24f0,0x254c,0x25c4,0x2634,0x2704);
                          //        in 3780 it is only tested ==0 / !=0 (0x383c cmpwi r29,0; 0x3860/0x3868: ctx+0x2d4|0x20
                          //        vs |0x22) [E]. NOT passed in the field arm for MC types 1-7 (0x2794..0x2a18 never read +0x15).
  uint8_t blk_count[6];   // +0x16..+0x1b: number of 4-byte coefficient entries for block 0..5 (0x1f2c lbz 0x16(r2), r2=mb+blk)
};                        // sizeof == 0x1c
```
MV field layout, proven by the copy loops:
* r=0 index = first vector pair at +0/+2 (fwd) and +4/+6 (bwd); r=1 = second pair at +8/+0xa (fwd), +0xc/+0xe (bwd).
  Evidence: type 1 reads lha +0,+2 (0x24e0/0x24e8); type 2 reads +4,+6 (0x24f8/0x2500); type 3 copies +0..+7 as two
  (x,y) pairs (0x252c-0x2548, 2 iterations); type 5 loop reads +0/+2 then +8/+0xa (0x2594/0x2598, r11+=8, 2 iters);
  type 6 reads +4/+6 then +0xc/+0xe (0x2604/0x2608); type 7 reads lhax r5*4+r6 and +2 with r5 = 0,2 and inner s=0,1
  => offsets 8*r+4*s (0x26b4,0x26cc).
* t=0 -> x (lhax / lha at +0), t=1 -> y (lha +2). Units: raw int16 passed through unchanged except below. [E]
  Name "mv x/y" [I] (MPEG-2 PMV/motion_code semantic; half-sample units [I]).
* For field-prediction types 5,6,7 (both arms) vertical component is doubled before being handed to the handler:
  `rlwinm r0,r0,1,0,30` (shl 1) at 0x25a8, 0x2618, 0x26d0, 0x28a8, 0x2914, 0x29c4 [E]. Types 1,2,3 (frame arm) and
  field-arm types 1,2,3 pass y unchanged [E].
* sel bytes (+0x10..+0x13): byte value goes to the handler verbatim (stored as a 32-bit word). Type 5 reads +0x10,+0x12
  (r10=mb+0x10, step 2, 0x2590/0x25b4); type 6 reads +0x11,+0x13 (r10=mb+0x11, 0x2600); type 7 reads +0x10+2r+s (0x26dc);
  field-arm type 1/3 read +0x10 [,+0x11] (0x2794, 0x2838). Matches MPEG-2 motion_vertical_field_select[r][s] [I].
* Sel array handed to handlers lives at sp+0x90 with stride 4 for s and 8 for r: word index sel[r][s] -> sp+0x90+8r+4s
  (type 5: sp+0x90, sp+0x98; type 6: sp+0x94, sp+0x9c (0x25b8 `stw r2,0x40(r7)` / 0x2628 `0x44(r7)`); type 7: both).

### 1.4 Coefficient stream (`p->coef`, advances through the whole picture/field-pair) [E]
Per block, `blk_count[b]` consecutive 4-byte entries:
```c
struct CoefEnt { uint8_t run; uint8_t unused_byte1; int16_t level; };   // big-endian
```
* Only byte +0 (lbz 0x0/0x4/0x8/0xc(r27), 0x21d4 lbzx) and halfword +2 (lha/lhz 0x2/0x6/0xa/0xe(r27), 0x21ec) are read.
  Byte +1 is NEVER read [E]. Names "run"/"level" are [I] (MPEG-2 run/level; padding value 0x3f = 63 supports 6-bit run, see below).
* After the block's entries `p->coef` advances by 4*blk_count (0x1fdc for the dead path, 0x2200 for remainder, 0x219c per group of 4).
* No quantiser, no sign handling, no DC special-casing in this code: values are forwarded as raw words [E].
* Blocks are consumed in fixed order 0..5 (r19 loop 0x2388-0x2394 `r19<=5`). Which index is Y0..Y3/Cb/Cr is [I] (4:2:0 MPEG-2 order).
* Note for the reimplementor: for 4:2:0 only (6 blocks hardwired).

## 2. Algorithm

### 2.1 DVDDriverDecode (0x1c6c)
```
if (rect[2]-rect[0] != ctx->h(0x2ac) || rect[3]-rect[1] != ctx->w(0x2a4))  DVDDriverSetMPRects(ctx, rect, 0, 0);
if (p->structure[0] == 3) {            // frame
    rc = pass(ctx,p,0);
} else {                               // field picture(s)
    rc = pass(ctx,p,0); if (rc == -1) return -3;
    rc = pass(ctx,p,1);
}
return rc == -1 ? -3 : 0;
```
[E 0x1cc4-0x1d1c]. Dispatch is on p->structure[0] ONLY; the second field's structure p->structure[1] is read inside the pass.

### 2.2 pass(ctx, p, f)  (0x1d30), r23=ctx r11=p r13=MB counter r14=x r16=MbRec* r27=coef* r30=cmd ptr
```
ptype = p->pic_type[f]  (0x60), st = p->structure[f] (0x5c), auxf = p->aux_flag[f]   (r9)
if (st == 0 && f == 0) return 0;                                   // 0x1d6c-0x1d94
fieldclass = ((st-1)&0xff) <= 1;                                   // 0x1da0-0x1dac
if (fieldclass && f != 0) {                                        // 0x1db0-0x1dd8: 2nd field continues
    static_705c = ptype;                 // *(r31+0x5304) = p->pic_type[1]   (consumer not established)
    coef = static_7058;  mb = static_7054;                          // r27 <- r31+0x5300, r16 <- r31+0x52fc
} else { coef = p->coef; mb = p->mb; }                             // 0x1ddc/0x1de0
H = ctx->u16@0x2ae; if (H & 15) H = (H + 16) & 0xfff0;             // 0x1de4-0x1df4  (round UP to 16)
mbcols = ctx->u32@0x2a4 >> 4;  mbrows = H >> 4;                    // 0x1df8-0x1e00 (width NOT rounded)
nmb = (mbcols*mbrows) & 0xffff;  if (st != 3) nmb >>= 1;           // 0x1e04-0x1e20 (field = half the MBs)
W = ctx->u16@0x2a6 (low half of u32 width);  x = 0; y = 0;         // 0x1d70,0x1d4c,0x1d7c
fix p->buf_a/b/c: 0xff -> 0 (written back)                         // 0x1e24-0x1e64
if (st != 3 && ptype == 2 && f != 0) {      // 0x1e68-0x1e80 (cr6 = st==3 from 0x1e0c)
     // second field of a P field picture
     cmd = FUN_58b0(ctx, buf_a(r4), buf_b(r5), st==1 ? 0 : 1 (r6), buf_a (r7), st==1 ? 1 : 0 (r8), auxf(r9));
} else cmd = FUN_57dc(ctx, st, ptype, buf_a, buf_b, buf_c, auxf);
if (cmd == NULL) return -1;                                        // 0x1ed8-0x1ee0
for (n = 0; n < nmb; n++) {                                        // r13 (u16)
    FUN_243c(ctx, cmd, st, ptype, x, y, mb);                       // 0x1f04-0x1f24; return value (new cmd ptr) IGNORED
    for (b = 0; b < 6; b++) {                                      // r19
        cnt = mb->blk_count[b];                                    // 0x1f2c
        if (cr4_flag /*always 0, dead*/ && b > 3) { emit one 0 dword; coef += cnt*4; continue; }  // 0x1f30-0x1fe0 DEAD
        if (cnt == 0) { reserve(4B); emit 0x00000000; }                                          // 0x1fe4-0x2384
        else {
            for (g = 0; g < cnt/4; g++) {                          // 0x2078-0x21a8, r21 = cnt>>2
                reserve(16B);
                emit(1);                                           // stwbrx r18(=1) 0x2174
                emit(run0<<24|run1<<16|run2<<8|run3);              // r26, ents coef[0..3].byte0
                emit((uint16)lvl2<<16|(uint16)lvl3);               // r25  (levels 2,3 BEFORE 0,1)
                emit((uint16)lvl0<<16|(uint16)lvl1);               // r24
                coef += 16;
            }
            rem = cnt & 3;                                         // r22
            if (rem) {                                             // 0x21b4-0x22ec
                copy rem entries (run byte, level u16) into tmp[0..rem-1];
                last = (int16)coef_after[-1].level (lha -2(r27));
                for (i = rem; i < 4; i++) { tmprun[i] = 0x3f; tmplvl[i] = last; }   // pad by repeating last level, run=63
                reserve(16B); emit(1); emit(packed runs); emit(lvl2<<16|lvl3); emit(lvl0<<16|lvl1);
            }
            reserve(4B); emit(0x00000000);                         // 0x22f0-0x2384: block terminator
        }
    }
    FUN_57ac(ctx, cmd);          // 0x2398: closes the MB's packet: header word at ctx[0x2b4] =
                                 //   0xC0003100 | ((ndw-2)<<16)  (0x57ac-0x57d4) -> PM4 type-3 opcode 0x31 packet [E];
                                 //   header slot was reserved/recorded by the handler (3780 0x3848: ctx[0x2b4]=ptr, ptr+=4)
    x += 16;  mb++ (stride 0x1c);                                  // 0x23a4-0x23b0
    if (x >= W) { x = 0; y += 16; }                                // 0x23b4-0x23cc
}
if (fieldclass && f == 0) { static_7058 = coef; static_7054 = mb; }   // 0x23e4-0x2408  (only when st was field class AND f==0)
FUN_5984(ctx, cmd);                                                // 0x2418: end-of-picture/flush
return 0;
```
reserve(N)/ring: ptr=ctx[0x34], end=ctx[0x38]; if (ptr+N >= end) { *ctx[0x30] |= (ptr-ctx[0x30])>>2;  ctx[0x30]=ptr; *ptr=0x01000000;
rec = submit(ctx[0x14], 1, **(r31+0x52bc /*0x7014*/), &ctx[0x40], &ctx[0x44], 1) [FUN_6e7c]; ctx[0x34]=rec[0x40]+0x20;
ctx[0x30]=rec+0x1c; ctx[0x38]=ctx[0x34]+(rec[0x10]<<2); rec[0x1c]=0 } [E e.g. 0x1f3c-0x1fc4, 0x2000-0x2074]. Writes use stwbrx
(byte-reversed => GPU sees the big-endian register value as little-endian dword) [E]. The annotator's MM_INDEX/MM_DATA labels for
the literal values 0/1 are spurious (these are command-stream dwords, not register offsets).

Per-block stream emitted (for the reimplementor):  `{1, runs4, lvl23, lvl01}* , 0`  per block; the entries 1 and 0 are stream tags
(1 = "coefficient group follows", 0 = "end of block") [E for the words, I for the names].

Field vs frame differences [E]: (a) nmb halved for fields; (b) second field reuses saved mb/coef pointers; (c) y is in field lines
(row step 16 with half the rows); (d) fields use dispatcher arm 2734 (handlers 45ec/4ab0/51b4/4784/4d48), frames arm 2478 (3780/3c7c/4394/3920/3ea0);
(e) the ptype==2 && f!=0 case calls 0x58b0 instead of 0x57dc; (f) cmd packet and block emission are identical for both.

### 2.3 Dispatcher 0x243c(ctx r3, cmd r4, st r5, ptype r6, x r7, y r8, mb r9)  [E 0x243c-0x2474]
Saves: r27=ctx, r30=cmd, r29=st, r26=x, r25=y, r10=ptype, r6=mb. Selects arm by `st == 3` (cr7 from 0x243c; 0x2474 `bne+` -> field arm 0x2734).
Index = mb->mc_type (0x14); >7 -> 0x2a20; table entries (offset from table base) verified from the binary:
 frame table @0x24a0: 0->0x24c0, 1->0x24d8, 2->0x24f0, 3->0x2520, 4->0x2a20 (NO handler), 5->0x2574, 6->0x25e4, 7->0x2664
 field table @0x275c: 0->0x277c, 1->0x2794, 2->0x27c8, 3->0x2808, 4->0x2a20 (NO handler), 5->0x2874, 6->0x28e0, 7->0x2958
Return: r30 = handler's r3 (new cmd ptr) or the unchanged input cmd ptr for type 4 / >7 (0x2a1c/0x2a20-0x2a28).

FRAME arm (st==3). In every call r3=ctx, r4=cmd. "Y" = y coordinate, "X" = x coordinate:
| type | handler | args (r5..r10, [9th = sp+0x38]) |
|---|---|---|
| 0 | 0x3780 | X, Y, flags(mb+0x15), st  (r10 still = ptype but unused: 3780 overwrites r10 at 0x3840). Intra: no MV read [E]. |
| 1 | 0x3c7c | X, Y, flags, st, r9=0, r10=&{mv[0][0][0], mv[0][0][1]} (int32 x,y at sp+0xa0/0xa4) |
| 2 | 0x3c7c | same, r9=1, mv = mv[0][1][*] (+4,+6) |
| 3 | 0x4394 | X, Y, flags, st, r9=&{fx,fy}(sp+0x50), r10=&{bx,by}(sp+0x58) (mb+0..+7 copied as 4 int32) |
| 4 | none | nothing emitted by dispatcher (blocks are still processed in the loop) |
| 5 | 0x3920 | X, Y, flags, st, r9=0 (fwd), r10=&ptr[2] (sp+0xa8 -> {sp+0x60, sp+0x68}, each {x, y*2}), [9th]=sp+0x90 (sel words) |
| 6 | 0x3920 | same with r9=1 (bwd): vectors from +4/+6 and +0xc/+0xe, sel from +0x11,+0x13 |
| 7 | 0x3ea0 | X, Y, flags, st, r9=&fwdptrs[2] (sp+0xb0={sp+0x70,sp+0x80}), r10=&bwdptrs[2] (sp+0xb8={sp+0x78,sp+0x88}), [9th]=sp+0x90 |
(0x24c0-0x2730). Vector slot layout for type 7: slot(sp+0x70+16r+8s): {x, y*2} = mv[r][s].

FIELD arm (st != 3). r5=X, r6=Y in all (via r7 or r26 / r8 or r25):
| type | handler | args |
|---|---|---|
| 0 | 0x45ec | X, Y, flags(mb+0x15), st                                   (0x277c-0x2788) |
| 1 | 0x4ab0 | X, Y, r7=st, r8=ptype(p->pic_type[f]), r9=0, r10=&{mvx,mvy}(sp+0xa0), [9th]=sp+0x90 -> {mb[0x10]} (0x2794-0x2800) |
| 2 | 0x4ab0 | same, r9=1, mv=+4/+6, sel=mb[0x11] stored at sp+0x94 (0x27c8-0x27f4) |
| 3 | 0x51b4 | r5=X(r26), r6=Y(r25), r7=st, r8=&{fx,fy}(sp+0x50), r9=&{bx,by}(sp+0x58), r10=sp+0x90 -> {mb[0x10],mb[0x11]} (0x2808-0x286c) |
| 4 | none | |
| 5 | 0x4784 | X, Y, st, r8=0 (fwd), r9=&ptr[2](sp+0xa8), r10=sp+0x90 (sel words) (0x2874-0x2950) |
| 6 | 0x4784 | same r8=1 (bwd), +4/+6,+0xc/+0xe, sel +0x11,+0x13 (0x28e0-0x2948) |
| 7 | 0x4d48 | X, Y, st, r8=&fwdptrs(sp+0xb0), r9=&bwdptrs(sp+0xb8), r10=sp+0x90 (0x2958-0x2a18) |
Field arm never passes `flags`. Frame vs field arms differ in arg order (field arm shifts: st in r7, direction in r8 for 4784).

Semantic names of mc_type (matches code, [I] only): 0 intra; 1 forward(single MV); 2 backward(single MV); 3 bidirectional(single fwd+bwd MV);
4 no-MC/skip-like (nothing emitted); 5 forward with two vectors + two field selects (field/16x8 prediction); 6 backward with two vectors; 7 bidirectional with
two vectors each. This is a driver-defined type enum, not the raw MPEG-2 macroblock_type/motion_type: the host decoder must collapse
MPEG-2 (frame/field/16x8/dual-prime, fwd/bwd/bi, intra, skip) into 0..7. Dual-prime is not representable except via 5/6/7 with host-computed vectors [I].

## 3. ctx fields used in scope
| off | size | meaning | evidence |
|---|---|---|---|
| 0x14 | u32 | passed as arg1 to FUN_6e7c (buffer submit/handle) | 0x1f94 |
| 0x30 | ptr | current chunk header ptr (OR'd with dword count at flush) | 0x1f50 |
| 0x34 | ptr | ring write pointer | 0x1f3c |
| 0x38 | ptr | ring end limit | 0x1f40 |
| 0x40 | ptr | submit result: buffer record ptr (ctx+0x40/0x44 are out params of 6e7c); record+0x10 = size in dwords [I], +0x1c = chunk header slot | 0x1f9c-0x1fc0 |
| 0x2a4 | u32 | picture width in pixels (>>4 = MB columns); low u16 @0x2a6 = x-wrap limit | 0x1df8, 0x1d70, 0x1ca4 |
| 0x2ac | u32 | picture height; low u16 @0x2ae used (rounded up to 16 for MB rows) | 0x1de4, 0x1c8c |
| 0x2b4 | ptr | start of the current MB's PM4 packet header (set by handler, header finalised by 0x57ac, then +4) | 0x57ac, 3780@0x3848 |
| 0x2bc | ptr | ring pointer read in 5984 (not analysed) | 0x59a4 |
| 0x2d4 | u32 | base of per-MB intra setup word (|0x20 / |0x22 by flags) | 0x3840-0x3868 |
Statics (data segment): 0x7054 saved MbRec* for field 2; 0x7058 saved coef*; 0x705c saved p->pic_type[1] (written only; reader unknown);
0x7014 -> pointer-to-pointer handed to 6e7c as r5.

## 4. NOT established
* Meaning of p->aux_flag[f] (+4,+5), and exact roles of buf_a/b/c (+6,+7,+8) (dest / forward ref / backward ref is plausible but unproven in scope).
* What 0x57dc/0x58b0 do with them; why the P second-field case reuses buf_a for both r4/r7.
* Meaning of MbRec.flags (+0x15) beyond a boolean in 3780 (field/frame DCT? 3780 tests it ==0; 4394/3c7c/3920/3ea0 not examined). Naming "dct_type" is unproven.
* Meaning of CoefEnt byte +1 (never read; may be high bits of a 16-bit run or padding).
* Whether the first CoefEnt is DC (intra) -- no special-casing; the hardware tag meaning of stream words 1 and 0 (assumed group-follows / end-of-block) and of padding run 0x3f.
* Why 243c's return value (new cmd ptr) is ignored by 0x1d30 (each handler/blocks re-read ctx[0x34]; the loop's r30 is refreshed by every block write).
* The cr4 "chroma skip" path (0x1f30-0x1fe0) is dead: cr4 is set by `cmpwi cr4,r0,0` at 0x1efc with r0=0 and never changed, so `beq cr4` at 0x1f30 always taken.
* Behaviour when p->structure[0]==0 but a second pass runs (f=1 proceeds because only f==0 returns early at 0x1d8c; it would consume stale static pointers).
* Whether mc_type 4 MBs are ever sent by Apple's host decoder (no packet is started for them, ctx[0x2b4] is stale, yet 0x57ac still closes a packet).
* Width is not rounded up (>>4 truncates) while height is; a non-multiple-of-16 width would drop a column.
* MB row stride, mb array element order for fields (assumed row-major in field lines; field 2 follows field 1 in the same array).
* Quantisation matrices / intra DC precision / MPEG-1 vs MPEG-2 selection: not visible in this code (not part of params/MbRec).

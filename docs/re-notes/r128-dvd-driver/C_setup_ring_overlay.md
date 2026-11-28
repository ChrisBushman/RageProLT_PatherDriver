# ATIRage128DVDDriver.bundle (PPC, 10.3) - setup / user client / ring / overlay / sub-picture

Static analysis only. Source: av/r128dvd_otool.txt (otool -tV of `ATIRage128DVDDriver`), av/r128dvd.bin,
RRG-G04500-C (Rage128 Pro register guide). All addresses are file/VM addresses of the binary (text 0x7f8..0x6dbc).
Scope: 0x848-0x1c6c, 0x4ab0-0x6e00 (+ the small helpers 0x2a38/0x2ad4/0x2b20/0x2b94/0x347c/0x2c84.. that the
entry points call, read only as far as needed). Tags: **[E]** = established from the disassembly (address given),
**[I]** = inferred (reasoning given), **[N]** = not established.
Annotator used: av/a2.py (my extension of r128ann.py: sign-fixed immediates, PIC-base tracking, stub names).

Conventions: PPC big-endian host. `ctx` = the per-open "device" object (malloc(0x2f8)), returned to the caller
as an opaque `DVDDriverRef`. `G` = library-global record in `__common` at 0x7060 (reached via non-lazy pointer 0x7010).
`stub` addresses 0x6dbc.. are 32-byte picsymbolstub1 entries, in la_symbol_ptr order (derived from the indirect symbol table):

| stub | import | stub | import |
|---|---|---|---|
| 0x6dbc | io_connect_method_structureI_structureO | 0x6efc | CGDisplayBitsPerPixel |
| 0x6ddc | io_connect_method_scalarI_structureO | 0x6f1c | malloc |
| 0x6dfc | memcpy | 0x6f3c | CGDisplayIDToOpenGLDisplayMask |
| 0x6e1c | memset | 0x6f5c | CGDisplayBounds |
| 0x6e3c | IOServiceClose | 0x6f7c | exit |
| 0x6e5c | io_connect_method_scalarI_structureI | 0x6e9c | io_connect_method_scalarI_scalarO |
| 0x6e7c | IOConnectMapMemory | 0x6ebc | free |
| 0x6edc | IOServiceOpen | (nl) 0x7014 | &mach_task_self_ |

There is NO IOServiceGetMatchingServices/IOConnectUnmapMemory/IOConnectCallMethod import [E: import list].

---------------------------------------------------------------------------------------------------
## 1. Exported entry points (function by function)

Error codes returned by the bundle: 0 ok, -3 (0xfffd) no memory / buffer query failed, -4 (0xfffc, only inside helper 0x2b94,
ignored by callers), -5 (0xfffb) unsupported / device error, -7 (0xfff9) bad parameter [E, all `li r3,-N`]. CloseDevice/GetVersion
use IOReturn-style 0xe00002c2 / 0xe00002bc [E 0xc74-0xc80, 0x6d8c]. "hw" below = `ctx[0x148] != 0`; OpenDevice sets it to 1 (0xc08) and nothing
else in the file ever writes it [E: grep of all `0x148(rX)` accesses: only stw at 0xc08, all others are loads], so the "software" branches
(`!hw`) are dead code in practice [I].

### 1.1 Stubs (return -5 immediately) [E 0x848-0x86c]
`FrontEndDecode 0x848`, `BackEndDecode 0x850`, `FrontEndAndBackEndDecode 0x858`, `Deinterlace 0x860`, `CreateRenderer 0x868`: `li r3,-5; blr`.
`SetSPColorControl 0x175c`, `SetSPDefaultColorControl 0x1764`: `li r3,0; blr` (no-ops). `BeginDecode 0x2ce8`, `ContinueDecode 0x2cf0`,
`EndDecode 0x2cf8`: return 0 (no-ops). `GetFeatureParam 0x2d00` returns -7; `SetFeatureParam 0x2d08` returns -5.

### 1.2 InitializeLibrary 0x870 / TerminateLibrary 0x8d0 [E]
```c
int DVDInitializeLibrary(io_service_t *services /*r3*/, uint32_t displayMask /*r4*/,
                         void (*cb3)(a4,a5,a6) /*r5*/, int (*cb4)(a4,a5,a6,flags,display) /*r6*/);
struct G {            /* @0x7060, __common, 0x14 bytes */
  uint32_t displayMask;   /* +0x00 <- r4 */
  uint32_t slotCount;     /* +0x04 = (index of highest set bit in r4)+1   (loop 0x8a0-0x8bc) */
  io_service_t *services; /* +0x08 <- r3 : array indexed by GL-display-mask bit number */
  void (*cb3)();          /* +0x0c <- r5 */
  void *cb4;              /* +0x10 <- r6 */
};
```
`DVDTerminateLibrary` zeroes all five words (0x8e8-0x8fc). Returns void/none. The bundle does **not** do service matching; the
host (AppleVA/dvdpb) supplies the already-matched io_service_t per display slot [E: no matching imports].
`GetVersion 0x6d78(int *a, int *b)`: if G.displayMask!=0 then *a=*b=1 and return 0, else return 0xe00002bc [E 0x6d98-0x6dac].
(A non-exported function 0x6d5c returns &"ATIRage128" (0x6ff0); no callers [E].)

### 1.3 DVDDriverOpenDevice 0x91c [E]
Prototype (derived from register/stack use):
```c
int OpenDevice(DVDDriverRef *out /*r3*/, DVDSize *maxSize /*r4*/, CGDirectDisplayID disp /*r5*/,
               a4 /*r6*/, a5 /*r7*/, a6 /*r8*/, int *flagsOut /*r9*/, (r10 unused),
               short *nMPBuffers /*9th, stack*/, short *nSPBuffers /*10th*/);
```
Sequence:
1. `out==NULL` -> -7 (0x950-0x954). `*out=0`.
2. `mask = CGDisplayIDToOpenGLDisplayMask(disp)`; require `mask != 0` and `(mask & ~G.displayMask) == 0` else -5 (0x968-0x984).
3. `ctx = malloc(0x2f8)` (NOT zeroed - no memset anywhere in this function) -> NULL gives -3 (0x988-0x998).
4. slot = lowest set bit of mask with slot < G.slotCount (0x99c-0x9cc); `ctx[0x10] = G.services[slot]` (0x9d0-0x9e8).
5. Fill `ctx[0]=disp, [4]=a4, [8]=a5, [0xc]=a6`, `ctx[0x18]=0x4020` then `|= 3` if CGDisplayBitsPerPixel(disp)==16 else `|= 4` (0xa00-0xa20); 0x918 is an identity fn.
6. `IOServiceOpen(ctx[0x10], mach_task_self(), 3 /*type*/, &ctx[0x14])` (0xa48-0xa50) - failure -> free, -5.
7. user-client calls (see section 2): sel1 (->ctx[0x20],[0x24]), `IOConnectMapMemory(type 1)` -> ctx[0x40]/[0x44], `IOConnectMapMemory(type 2)` -> ctx[0x48]/[0x4c],
   host callback `G.cb4(ctx[4],ctx[8],ctx[0xc], ctx[0x18]&0x803f, ctx[0])` (must return 0, else cleanup/-5; 0xadc-0xb08), sel0 `{ctx[0xc], ctx[0x18]&0xffff4000}`, sel3 (->ctx[0x28],[0x2c]).
   Note `ctx[0x18]&0xffff4000` = 0x4000 only; the host callback gets `0x23` (16bpp) or `0x24` (other) [E arithmetic].
8. `*nMPBuffers=5; *nSPBuffers=8` (0xba0-0xbb4)  [E]; i.e. 5 picture (MP) buffers and 8 sub-picture buffers [I: matches ctx[0x2c0..] (5 entries) and ClearSP/ClearMP loop counts 8/5 in 0x2b20].
9. **Link the context into a global list before the next failure point**: node = ctx+0x2f0 = {ctx, next}; head at 0x7004 (0xb94-0xba4).
10. `0x2a38(ctx)` (selector 4, fills ctx[0x2c0..0x2d0]); failure -> IOServiceClose+free+(-3) **without unlinking the node** (0xbb8-0xbd8) [E; latent dangling list entry].
11. `0x2ad4(ctx)` (selector 5); `0x904(maxSize)` writes `{0x300, 0x240}` (width 768, height 576 [I: names]) to `*maxSize` (0x904-0x910).
12. `0x6824(ctx)` ring init (section 3.1) ; `0x6920(ctx)` type-2 init-state buffer (section 3.4).
13. `*out=ctx; ctx[0x148]=1; ctx[0x140]=ctx[0x144]=0; ctx[0x158]=ctx[0x15c]=0; *flagsOut=0xf0` (0xbfc-0xc20).
14. `0x5fd0(ctx)` allocates SP storage (return value IGNORED; failure leaves ctx[0x1c8]=0 and the A/B/data pointers uninitialised) [E 0xc24]; `0x2b20(ctx)` clears all 5 MP and 8 SP buffers.
Return 0.

### 1.4 DVDDriverCloseDevice 0xc60 [E]
NULL ctx -> 0xe00002c2. Else `EnableMP(ctx,0)`, `EnableSP(ctx,0)`, `free(ctx[0x1c8])` if non-NULL, `IOServiceClose(ctx[0x14])`,
unlink the node from the list at 0x7004 (compares `node->ctx==ctx`, 0xcc4-0xd08), `free(ctx)`; return 0. No unmap of the mapped memory [E].

### 1.5 SetMVLevel 0xd30 [E]
`sel 0x10, scalarI_scalarO, in={0x92, level}`, out count 0. Result kr: `-17 -> -5`, `-50 -> -7`, anything else -> 0 (errors other than those two are swallowed).
Meaning of 0x92 = [I] a feature/parameter id for "MV" (macrovision) level; not established. -17/-50 look like Mac OS `controlErr`/`paramErr` [I].

### 1.6 GetKeyColor 0xd9c [E]
`GetKeyColor(ctx, uint32_t *key)`: `bpp = CGDisplayBitsPerPixel(ctx[0])`; `*key = (bpp==16) ? 0x40 : (bpp==32) ? 0x1000 : 0xdeadbeef`; return 0.
No hardware access. (0x40 / 0x1000 are what the overlay colour-key must be set to by the kernel/host; the bundle only reports them. In RGB565/xRGB8888 these are
(0,2,0)... i.e. a very dark green: 0x0040 = G=0x02 in 565, 0x00001000 = G=0x10 in 8888 [I: interpretation].)

### 1.7 SetMPRects 0xdf4 [E]
```c
struct DVDRect { short s0, s1, s2, s3; };   /* read as lha at +0,+2,+4,+6; used as Mac Rect {top,left,bottom,right} [I] */
int SetMPRects(ctx, DVDRect *r);
  ctx[0x2a4] = s3 - s1;                 /* width   (right-left)  */
  ctx[0x2a8] = (ctx[0x2a4] + 63) & ~63; /* pitch in pixels (64-aligned) */
  ctx[0x2ac] = s2 - s0;                 /* height  (bottom-top) */
  sel 0x0b (scalarI_scalarO), in = { s0, s3, s2, s1 }  (4 scalars; out count 0)   -> { top, right, bottom, left }
  return 0;
```
(0xe0c-0xe70). Defaults when ctx[0x2a8]/[0x2ac] are 0 at open: 0x300 / 0x1e0 (0x6920) [E].

### 1.8 EnableMP 0xe88 [E]
`sel 0x0c`, in `{enable}`, return 0 (kernel result ignored).

### 1.9 ShowMPBuffer 0xed0 [E]
`ShowMPBuffer(ctx, int idx /*r4*/, uint16_t *mode /*r5, may be NULL*/)`:
`ctx[0x140]=idx; m = mode ? *mode : 2; 0x5f88(ctx, idx, m)` = `sel 0x0a, in {idx, m}` (out 0); then if `*(u32*)(ctx[0x40]+0x14) & 2` call `G.cb3(ctx[4],ctx[8],ctx[0xc])` (0xf08-0xf34). Return 0.
(`m`: 2 default, plausibly the field/structure code [I].) No hardware registers are touched by the bundle here: overlay/flip is done in the kernel.

### 1.10 EnableSP 0xf50 [E]
`ctx[0x158] = (int)arg > 0` (neg/rlwinm = sign bit of -arg). If `hw`: `v = shadow[0x150]` (0x677c); `arg!=0 ? v|1 : v&~1`; write SP reg 0x150 via 0x6690 (section 6).
Always (even if !hw): flush ring (3.2); if the NEW buffer header `[0x14]&2` -> `G.cb3`. Return 0.

### 1.11 ClearSP 0x1084 [E]
`ClearSP(ctx, uint8 idx)`: if hw (and the dummy `ctx != -392` test, a compiler artefact of `&ctx->A[0] != 0` [I]):
`memset(A[idx], 0x00, 0x1b000); memset(B[idx], 0xff, 0x9000)` with `A[i]=*(ctx+0x188+4i)`, `B[i]=*(ctx+0x1a8+4i)`.
If `idx == ctx[0x144]` (currently selected buffer) and hw: upload both via `0x2b94(ctx,1,idx,0x40,0x240)` and `0x2b94(ctx,0,idx,0xc0,0x240)` (sel 6), then ring blocks type 7 {ctx[0x1cc]} and 8 {ctx[0x1d0]} + terminator + submit (section 3), then `G.cb3` if flagged. Return 0.

### 1.12 ShowSPBuffer 0x1300 [E]
`ShowSPBuffer(ctx, int idx, DVDRect *r)`:
1. if `ctx[0x144] != idx`: `ctx[0x1cc] = (ctx[0x1cc]==0); ctx[0x1d0] = (ctx[0x1d0]==0)` (boolean flip; both start 0 at 0x6064 so they always stay equal [E]).
2. if hw && `ctx[0x158]` (SP enabled by host): `ctx[0x160]=*(u32*)r; ctx[0x164]=*(u32*)(r+4)` (rect copy); `sel 0x0f in {s0, s3, s2, s1 & ~0xf}` (left aligned down to 16) (0x1364-0x13b8);
   upload A (0x2b94 mode 1 with offs 0x40) and B (mode 0, 0xc0) for `idx` (0x13bc-0x13e8); if `write+0x10>=limit` flush first; emit block 0x07000002 {ctx[0x1cc]}, block 0x08000002 {ctx[0x1d0]}, terminator 0x01000000, submit (`IOConnectMapMemory` type 1) (0x13ec-0x1568).
3. `G.cb3` if `map1[0x14]&2`; `ctx[0x144]=idx`; return 0.

### 1.13 SetSPPalette 0x15b8 [E]
`SetSPPalette(ctx, uint32_t pal[16])` (r4). If `!hw` (dead path): `memcpy(ctx+0x224, pal, 0x40)` and build a software RGB table at `ctx+0x264` (16 words): per entry bytes `[1]=Y,[2]=Cr,[3]=Cb`:
`R=1.164Y+1.596Cr-222.912, G=1.164Y-0.391Cb-0.813Cr+135.488, B=1.164Y+2.018Cb-276.928` (constants literal8 0x6fa0..0x6fd8, verified by decoding doubles), clamp to 0..255, byte0=0xff.
If hw (the real path): `0x6088(ctx, pal)`: for i in 0..15: `reg[0x15f] <- i`; `reg[0x160] <- swap16(pal[i] & 0xffff) | (pal[i] & 0x00ff0000)` (i.e. entry `0x00YYCrCb` -> `0x00YYCbCr`) via 0x6690, then flush+submit (0x60f8-0x6144). Return 0.

### 1.14 SetSPBuffer 0x176c [E]
`ctx[0x144]=idx; if hw: ctx[0x160..0x166] = r[0..3]` (4 unsigned shorts, straight copy). Return 0. No kernel/ring traffic.

### 1.15 PrepareButton 0x17a4 [E]
`PrepareButton(ctx, DVDRect *r /*r4*/, uint32 x /*r5, third arg*/)`: if hw: `reg[0x15c] <- r5(original 3rd arg)`; `reg[0x15d] <- (s0<<16)|s1` (not masked); `reg[0x15e] <- (s2<<16)|(s3-1)`; flush+submit (0x17cc-0x1890).
With Rect={top,left,bottom,right}: `(top<<16)|left`, `(bottom<<16)|(right-1)` [I: names]. Return 0.

### 1.16 EnableButton 0x18ac [E]
if hw: `v=shadow[0x150]; reg[0x150] <- en ? v|2 : v&~2`; flush+submit; `G.cb3` if flagged; return 0.

### 1.17 ClearMP 0x19c0 [E]
Ensure 0x20 bytes room (else flush). Emit (all LE via stwbrx): raw `0xC0069100` (CCE type-3, opcode 0x91 = CNTL_PAINT, count field 6), `0x52f036da`, then block hdr `0x05000002`+`ctx[0x140]`; flush/submit; repeat with block hdr `0x06000002`+`ctx[0x140]`; flush/submit. If `!hw` calls `ShowMPBuffer(ctx, ctx[0x143], NULL)`. Return 0.
`0x52f036da` decodes (RRG DP_GUI_MASTER_CNTL bits): DST_PITCH_OFFSET_CNTL=1, DST_CLIPPING=1, BRUSH_DATATYPE=0xd, DST_DATATYPE=6 (32bpp aRGB8888), SRC_DATATYPE=3, ROP3=0xF0 (PATCOPY), CLR_CMP_CNTL_DIS=1, WR_MSK_DIS=1 [E bit decode]. The remaining 6 PAINT payload dwords are not in the stream: the kernel must expand block types 5/6 [I]. Type 5 vs 6 = two planes/halves of buffer `ctx[0x140]` [I, N].

### 1.18 GetSPBuffer 0x2c84 / ApplySPDCSQ 0x2cb4 [E]
`GetSPBuffer(ctx, uint32 *outA /*r4*/, uint32 *outD /*r5*/)`: for i<8: `outA[i]=ctx[0x188+4i]` (bitmap A pointers), `outD[i]=ctx[0x168+4i]` (raw SP data pointers). Return 0.
`ApplySPDCSQ(ctx, idx, off, size)`: executes ONE command located at `data[off]`, `data = ctx[0x168+4*idx]`, `end=off+size` via 0x6328 (section 6.3). Return 0 (the caller loops).

---------------------------------------------------------------------------------------------------
## 2. IOKit user-client protocol [E unless marked]

* **Service match**: not in this bundle (see 1.2). The host passes `io_service_t[]` (one per GL display-mask bit); open picks the one for the display's mask bit.
* **IOServiceOpen type = 3** (0xa4c: `li r5,3`, 0xa50 `bl IOServiceOpen`), owning task `mach_task_self()`.
* **Mapped memory** (IOConnectMapMemory(conn, type, task, &addr, &size, options=1=kIOMapAnywhere)):

| type | stored at | use |
|---|---|---|
| 1 | ctx[0x40] addr, ctx[0x44] size (open 0xa8c; every later call) | command ring buffer. **Calling MapMemory(type 1) again is the "submit current buffer and give me the next" operation** (section 3). |
| 2 | ctx[0x48] addr, ctx[0x4c] size (0xabc) | initial engine-state command buffer written once by 0x6920 (section 3.4). |

* **External methods** (`conn=ctx[0x14]`; the call stubs are the legacy `io_connect_method_*`; `selector` = r4). Complete list - every call site in the whole file:

| sel | stub / shape | caller addr | arguments | effect |
|---|---|---|---|---|
| 0x00 | scalarI_structureI, 2 in-scalars, no struct | 0xb40 | `{ctx[0xc], ctx[0x18] & 0xffff4000}` | configure client (arg a6, mode bits; =0x4000 in practice) [I name] |
| 0x01 | scalarI_scalarO, 0 in / 2 out | 0xa7c | out -> ctx[0x20],ctx[0x24] | query (values never read back by bundle [N]) |
| 0x03 | scalarI_scalarO, 0 in / 2 out | 0xb68 | out -> ctx[0x28],ctx[0x2c] | query (never read back [N]) |
| 0x04 | scalarI_structureO, in `{0}`, out struct 0x40 B | 0x2a74 (helper 0x2a38; from 0xbb8,0x5808,0x58dc) | out buffer; words `[0],[2],[4],[6],[8]` (stride 8) copied to ctx[0x2c0+4i] | get the 5 picture-buffer base addresses [I: video-memory offsets] |
| 0x05 | scalarI_scalarO, in `{0}`, out 1 | 0x2b08 (helper 0x2ad4; from 0xbe0,0x585c,0x5930,0x5b04) | result ignored | "sync/notify" after ring submit and before decode [I]; semantics [N] |
| 0x06 | structureI_structureO, in 0x1c B, out size 0 | 0x2c60 (helper 0x2b94) | struct below | upload SP bitmap A or colour-change map B into kernel/VRAM; returns 1 ok / -4 |
| 0x0a | scalarI_scalarO, in `{idx, mode}`, out 0 | 0x5fbc (0x5f88; from ShowMPBuffer 0xf04) | | show MP buffer |
| 0x0b | scalarI_scalarO, in 4, out 0 | 0xe70 | `{s0,s3,s2,s1}` | MP (video) rect |
| 0x0c | scalarI_scalarO, in 1 | 0xeb8 | `{enable}` | EnableMP |
| 0x0d | structI_structO, 4 B in/4 B out | 0x67c0 (fn 0x678c) | `{reg_dword_index<<2}` -> value | register read; **dead code** (no caller) |
| 0x0e | scalarI_scalarO, in 2 | 0x6810 (fn 0x67d8) | `{reg_dword_index<<2, value}` | register write; **dead code** |
| 0x0f | scalarI_scalarO, in 4, out count var=0 | 0x13b8 (ShowSPBuffer), 0x65ac (DCSQ SET_DAREA) | `{top,right,bottom,left&~15}` | SP display rectangle |
| 0x10 | scalarI_scalarO, in 2, out 0 | 0xd68 | `{0x92, level}` | SetMVLevel |

Selectors 2, 7, 8, 9 and >0x10 are never used [E]. Return values: only sel 6 (1/-4), sel 0x10 (-17/-50 mapped) and open-time ones (any non-zero = failure) are checked.

Selector 6 input struct (0x2b94 builds it on the stack, 28 bytes; fields [E] offsets, meaning [I]):
```c
struct SPUpload {          /* r1+0x40 */
  uint32_t zero;           /* +0x00 */
  uint32_t f4;             /* +0x04: A: 0xc0, B: 0 */
  uint32_t f8;             /* +0x08: A: arg r6 (0xc0), B: 0xc0 (arg r6 ignored) */
  uint32_t fc;             /* +0x0c: A: arg r7 (0x240), B: 0xc0 (arg r7 ignored) */
  uint32_t half;           /* +0x10: (ctx[0x1cc] != 0)  - ping-pong half to fill */
  uint32_t ptr;            /* +0x14: A: ctx[0x188+4*idx]   B: ctx[0x1a8+4*idx]  (user VA) */
  uint32_t f18;            /* +0x18: 0xc0 */
};
```
mode r4==0 -> bitmap A, r4==1 -> map B (0x2b94 0x2bb8/0x2bfc). Plane geometry [I]: A = 192 B/line x 576 lines (768 px x 576 at 2 bpp = 0x1b000 B), B = 8 entries x 8 B per line x 576 lines = 0x9000 B (section 6.3).

Mac-style error codes in method results (-17, -50) mean the kext returns OSErr values from its external methods [I].

---------------------------------------------------------------------------------------------------
## 3. Ring / command-buffer protocol

### 3.1 Buffer and cursors [E]
`buf = IOConnectMapMemory(type 1)`. Header (host-endian u32): `+0x10 = capacity in dwords`, `+0x14 = flags (bit1 = "host callback needed")`, `+0x1c = first block header word`, entries from `+0x20`.
After every (re)map (identical code repeated >30 times, e.g. 0xfe8-0x1038 or 0x6128-0x6170):
```
ctx[0x34] (write ptr) = buf+0x20 ; ctx[0x30] (current block header ptr) = buf+0x1c ;
ctx[0x38] (limit) = buf+0x20+buf[0x10]*4 ; buf[0x1c] = 0   (header of first block = type 0)
```
Initial state is set by 0x6824 from the type-1 mapping obtained at open (no OR/terminator). Space check idiom: `if (write + need*4 >= limit) FLUSH` (`cmplw`/`blt+`).

### 3.2 Block structure and FLUSH [E]
The stream is a sequence of **blocks**. A block = host-endian header word `[type:8 | count:24]` followed by payload. `count` = number of dwords from the header up to the next
header (header included); it is OR-ed into the header when the block is closed: `*ctx[0x30] |= (ctx[0x34]-ctx[0x30])>>2`. A new block is opened by `ctx[0x30]=cursor; *cursor=type<<24; cursor+=4`.
Payload dwords are of two kinds: **LE GPU dwords** (written with `stwbrx`; CCE type-0 register writes `[dword_index][value]`, type-3 packets) and **host-endian parameter dwords** (written with `stw`).
Block types seen in my scope [E]: `0x00` (implicit first block, raw CCE dwords, count only), `0x01` terminator (not counted), `0x05`,`0x06`,`0x07`,`0x08` (1 BE dword each),
`0x18` (0x347c: LE preamble, see 3.3), `0x19` (4 BE dwords {ctx[0x150],ctx[0x154],ctx[0x14c],ctx[0x2d8]}); in the decode scope also `0x16` (5 BE dwords) and `0x17` (0x3244) [E addresses 0x2e80,0x3244].
FLUSH/SUBMIT (e.g. 0xfc0-0x100c):
```
hdr=ctx[0x30]; *hdr |= (ctx[0x34]-hdr)>>2;  ctx[0x30]=ctx[0x34];  *(u32*)ctx[0x34] = 0x01000000;   /* terminator */
IOConnectMapMemory(ctx[0x14], 1, mach_task_self(), &ctx[0x40], &ctx[0x44], 1);                      /* kernel consumes buffer, returns next */
then re-init cursors as in 3.1
```
There is **no ring-pointer/doorbell register write and no IOConnectMethod call** in the bundle: the map call IS the submit [E: grep of all MapMemory sites; every one is preceded by the terminator store]. Whether the kernel hands back the same or a different buffer is not visible (the code reloads ctx[0x40] each time) [N].
After API-level flushes the bundle tests `*(ctx[0x40]+0x14) & 2` and calls `G.cb3(ctx[4],ctx[8],ctx[0xc])` (EnableSP/ClearSP/ShowSPBuffer/EnableButton/ShowMPBuffer/0x3.. ) [E]; purpose [I]: kernel asks the host to refresh overlay/display state.

### 3.3 Per-picture helpers [E]
* `0x5444(ctx, r4)` "ensure room": if free dwords `(limit-write)/4 - 1 > 0x1fff` return r4 unchanged. Else: `0x347c(ctx)` (reserve 0x134 B; block 0x18 containing: LE `[0x40][0x4585b3]`, 70 x `0xffffffff`, `0xc0003100,0x100`, `0xc0003100,0`, then `DP_CNTL(0x5b0)<-0x8107`, `DP_DATATYPE(0x5b1)<-{8bpp:0x30f02; 32bpp:0x20030f06; else 0x20030f03}` and `DST_PITCH(0x502)<-(int)CGDisplayBounds(ctx[0]).size.width` (0x347c-0x377c)); if `(ret - ctx[0x2bc]) & 4` append one `0x80000000` (CCE type-2 NOP) to keep 8-byte alignment [E 0x5490-0x553c]; then FLUSH(+submit);
  ensure 6 dwords; emit LE `DP_DATATYPE(0x5b1)<-0x30d08`, `DP_CNTL(0x5b0)<-0x1e7`, `DST_PITCH(0x502)<-ctx[0x2a8]>>4`; then block 0x19 {ctx[0x150],ctx[0x154],ctx[0x14c],ctx[0x2d8]}; `ctx[0x2b0]=ptr after the register pairs`; returns that pointer. So: **a picture needs >=32 KB of ring room; otherwise the ring is submitted and engine state re-established** [E behaviour].
* `0x5984(ctx)` "end-of-picture submit": `0x347c`, same 8-byte alignment NOP, `ctx[0x2b0]=ptr`, FLUSH(+submit), then `0x2ad4(ctx)` (sel 5), return 0. Caller 0x2418 (decode scope).
* `0x57ac(ctx, endptr)`: closes the current MC command group: writes at `ctx[0x2b4]` the LE header `0xC0000000 | (((endptr-ctx[0x2b4])>>2)-2)<<16 | 0x3100` (CCE type-3, opcode 0x31, count=dwords-2), `ctx[0x2b4]+=4`. Caller 0x23a0 (decode scope). The emitters below reserve the header slot (`ctx[0x2b4]`) and store `ctx[0x2b8]=end`.
* `0x57dc` and `0x58b0` (callers 0x1ed4 / 0x1eb4 in Decode): call sel 4 (0x2a38); build a 3x5 table on the stack: `T[i]=ctx[0x2c0+4i]` (Y base), `T[5+i]=T[i]+0x6c000` (U), `T[10+i]=T[5+i]+0x1b000` (V); `sel 5`; then call `0x2d10` resp. `0x30b0` with `stack[0x3c]=&T, stack[0x8c]=ctx[0x2a8], stack[0x90]=ctx[0x2a8]>>1`. Returns 0 if sel 4 failed. Frame layout therefore Y plane 768x576 (0x6c000) + U,V 384x288 (0x1b000 each), total 0xa2000 per buffer [E arithmetic].
* WAIT_UNTIL: in decode scope 0x2e54-0x2e68 and 0x3218 write LE `[0x5c8][0x800]` = `WAIT_UNTIL <- EVENT_OV0_FLIP` (RRG: stall command FIFO until the overlay releases the surface) [E]. No other idle/wait mechanism exists in the bundle; sel 5 [I].

### 3.4 Engine state init sequences
Same 28-register list is emitted by two functions [E] (pairs are LE `[dword_index][value]`; MMR = index*4; names from RRG):

| idx | MMR | name | value |
|---|---|---|---|
| (0x602 | 0x1808 | undocumented | 0)  *only in 0x6920* |
| 0x676 | 0x19D8 | MC_SRC1_CNTL | 0x10000000 (IDCT_EN) |
| 0x600 | 0x1800 | undocumented | 0x00c00200 |
| 0x598 | 0x1660 | AUX_SC_CNTL | 0 |
| 0x570 | 0x15C0 | CLR_CMP_CNTL | 0 |
| 0x5b3 | 0x16CC | DP_WRITE_MSK | 0xffffffff |
| 0x51f | 0x147C | DP_BRUSH_FRGD_CLR | 0x00ff0000 |
| 0x595 / 0x597 | 0x1654 / 0x165C | SRC_SC_RIGHT / SRC_SC_BOTTOM | 0x1fff / 0x1fff |
| 0x590 / 0x591 / 0x592 / 0x593 | 0x1640/44/48/4C | SC_LEFT/RIGHT/TOP/BOTTOM | 0 / 0x1fff / 0 / 0x1fff |
| 0x5b1 | 0x16C4 | DP_DATATYPE | 0x00030d08 (0x5b20); 0x20030d08 (0x6920). DST_DATATYPE=8 (Y8) |
| 0x50b | 0x142C | DST_PITCH_OFFSET | `(ctx[0x2a8]<<17) & 0xffe00000` (pitch field = ctx[0x2a8]/16 x8 px = ctx[0x2a8]/2 px, i.e. half-width chroma pitch [I]) |
| 0x5b2 | 0x16C8 | DP_MIX | 0x00cc0200 |
| 0x6f3 / 0x6f1 | 0x1BCC / 0x1BC4 | WINDOW_XY_OFFSET / undoc | 0 / 0 |
| 0x680 / 0x688 / 0x682 / 0x68d | 0x1A00/1A20/1A08/1A34 | undocumented | 0x440 / 0x00080008 / 0x02184840 / 0x0218204a |
| 0x68c | 0x1A30 | CONSTANT_COLOR | 0x7fffffff |
| 0x5b0 | 0x16C0 | DP_CNTL | 0x1e7 |
| 0x668 / 0x669 | 0x19A0 / 0x19A4 | undocumented | 0x10000 each |
| 0x661 / 0x662 | 0x1984 / 0x1988 | SECONDARY_SCALE_X_INC / Y_INC | 0x10000 each |
| 0x665 | 0x1994 | undocumented | `(ctx[0x2ac]<<16) | ctx[0x2a4]` (height<<16 | width) |
| then | | CCE type-3 `0xC0003100` | payload `0x400` |

* `0x5b20(ctx)` (callers 0x2fa4, 0x3370 in decode): ensures 0xe8 B room, emits the list (58 dwords) **into the ring** [E 0x5b20-0x5f84].
* `0x6920(ctx)` (open): writes the same list (+ the extra 0x602 pair, DP_DATATYPE bit29) **directly into the type-2 mapping** from `map2+0x20`: first `map2[0x1c] |= 1`, then entries, then `map2[0x14] = count(=0x3c dwords)`; if `count > map2[0x10]` (capacity) -> `exit(1)` (0x6d28-0x6d48). Defaults `ctx[0x2a8]=0x300`, `ctx[0x2ac]=0x1e0` if zero. [E] So map2 header: `+0x10 capacity, +0x14 count, +0x1c flags bit0 = valid`; the kernel replays it as engine initial state [I].
  (ctx[0x2a4] is NOT defaulted and ctx is un-zeroed => width word may be garbage if SetMPRects was never called [E, latent bug].)

### 3.5 MC command emitters (per-macroblock; consumers = Decode, other agents' scope) [E words / I meaning]
All write LE dwords inside a type-3 0x3100 group (header slot = `ctx[0x2b4]`, 3.3), after calling 0x5444; reserve space first. `x,y` = macroblock position, `mv` = motion-vector pair (pair[0],pair[1]).
* `0x4ab0(ctx, ?, x/r5, y/r6, nref/r7, mode/r8, sel/r9, mv*/r10, [stack]table)` reserves 0x2c B (11 dwords): `hdr_slot, ctx[0x2d4], 6,
  0x90000000|((mv1+2y)<<16)|(mv0+2x), 0x80000000|(x<<16)|y, 0xC2000000|ctx[0x2d8+4k]|flag, 0x50000000|((mv1/2+y)<<16)|(mv0/2+x),
  W4>>1, 0x22000000|ctx[0x2e0+4k]|(flag+2), 3, 0x22000000|ctx[0x2e8+4k]|(flag+4)`, `k=(nref-1)&0xff`; `flag` = 0/6/lookup from a 9th-arg table depending on mode 2/3 (0x4b84-0x4c1c).
* `0x4d48` reserves 0x60 B (24 dwords): `hdr_slot, ctx[0x2d4], 5,` then two groups of {`0x80000000|..` ref pos, `0xB0000000|..`, `0x80000000|(x<<16)|y`, `0xA2000000|ctx[0x2d8]|(d0+6)|((d1+1)<<26)`, `0x40000000|..`, `0x70000000|..`, W>>1, `0x02000000|ctx[0x2e0]|(d0+2)|((d1+3)<<26)`, 3, `0x02000000|ctx[0x2e8]|(d0+4)|((d1+5)<<26)`}, the second group preceded by an extra `5` and with `y+=8`; mv.y of the 4 vectors is halved in place (srawi) at 0x4e60-0x4ea4.
* `0x51b4` reserves 0x34 B (13 dwords): `hdr_slot, ctx[0x2d4], 5, 0x80000000|ref.., 0xB0000000|ref.., 0x80000000|(x<<16)|y, 0xC2000000|ctx[0x2d8+4k]|(d0+6)|((d1+1)<<26), 0x40000000|.., 0x70000000|.., W>>1, 0x22000000|ctx[0x2e0+4k]|(d0+8)|((d1+3)<<26), 3, 0x22000000|ctx[0x2e8+4k]|(d0+10)|((d1+5)<<26)`.
  Semantics of the opcode words 3/5/6/7 and the 0x2/0x4/0x5/0x7/0x8/0x9/0xA/0xB/0xC nibble tags are **[N]** (firmware dvdfw.bin defines them; decode agent's scope). `ctx[0x2d4..0x2ec]` are set by 0x2d10/0x30d0 (stw at 0x2d30..0x2d7c): `[0x2d4]=pic_param<<4, [0x2d8]=Y base, [0x2dc]=Y base+pitch, [0x2e0]=U base, [0x2e4]=U+pitch/2, [0x2e8]=V base, [0x2ec]=V+pitch/2` [E].

---------------------------------------------------------------------------------------------------
## 4. Overlay / key colour / rectangles
[E] The bundle programs **no overlay (OV0_*) registers**: `grep` of every stwbrx register index in 0x848-0x6e00 shows none in the OV0 range (MMR 0x400-0x4FC = idx 0x100-0x13f); the only overlay-related effects are
* `EnableMP` -> sel 0x0c `{enable}`; `ShowMPBuffer` -> sel 0x0a `{idx, mode}` (ctx[0x140]=idx); `SetMPRects` -> sel 0x0b `{top,right,bottom,left}` + ctx[0x2a4/2a8/2ac] (pixel dims used for pitch/size fields in 3.4);
* `GetKeyColor`: 0x40 (16 bpp) / 0x1000 (32 bpp) / 0xdeadbeef (other) from `CGDisplayBitsPerPixel` only;
* `SetMVLevel` -> sel 0x10 `{0x92, level}`;
* ring `WAIT_UNTIL <- 0x800 (EVENT_OV0_FLIP)` before each picture (decode scope 0x2e58) so the 2D/MC engine does not write a surface the overlay is scanning [E].
Colour-key programming, overlay scaler/position registers, flip are therefore **in the kernel kext** [I]. Rect order seen by the kext: `{top,right,bottom,left}` (MP) and `{top,right,bottom,left&~15}` (SP).

---------------------------------------------------------------------------------------------------
## 5. `ctx` fields (malloc(0x2f8), never memset) [offset: meaning (evidence)]

```
0x000 u32  CGDirectDisplayID                              (0x9fc, used 0xdb0, 0x35d4)
0x004/0x008/0x00c  opaque host args a4,a5,a6              (0x9e4-0x9f4; passed to cb3/cb4, 0xaec-0xaf8)
0x010 u32  io_service_t                                   (0x9e8, IOServiceOpen 0xa48)
0x014 u32  io_connect_t                                   (0xa50; every method call)
0x018 u32  flags = 0x4020|{3,4}                           (0x9f8-0xa34)
0x020,0x024 sel1 outputs; 0x028,0x02c sel3 outputs        (never read [N])
0x030 u32* current block header ptr  0x034 write ptr  0x038 limit   (3.1)
0x040/0x044 mapped ring addr/size (type 1)  0x048/0x04c mapped init buffer (type 2)
0x060..0x13f  register-shadow-like block initialised by 0x6824: ctx[0x64]=0x200, [0x74]=0x01030080, [0x84]=0x01038081, [0x94]=0x415e, [0x98]=0xffff0000,
              [0x9c]=-1, [0xa8]=0xc4, [0xb0]=0x101, [0xb8]=-1, [0xbc..0x13b]=-1 (32 words), [0x13c]=(((flags&0xf)==4?6:3)<<8)|0x28cc30db, rest 0   (consumer not located [N])
0x140 u32  current MP buffer index (ShowMPBuffer 0xef0; ClearMP 0x1ad8); 0x143 = low byte
0x144 u32  current SP buffer index (ShowSPBuffer 0x159c; SetSPBuffer 0x1770; ClearSP 0x10e8)
0x148 u32  "hw SP supported" flag = 1 (0xc08; tested 0xf80, 0x109c, 0x15d0, ...)
0x14c,0x150,0x154  picture params for block 0x19/0x16 (0x2fac-0x2fc8; emitted 0x5770-0x5788). NOT the SP register file.
0x158 u32  SP enabled by host (EnableSP 0xf78; tested 0x1358, 0x63e0)
0x15c u32  SP display active (DCSQ cmd 0/1/2 write 1/1/0, 0x63b0/0x63cc/0x63f8)
0x160..0x167  SP display rect (4 shorts, or 2 words at 0x1374/0x137c); 0x160 (top) is the line bias in 0x6188 (0x61f8)
0x168 +4*i (i<8)  raw SP data buffer ptrs (0x6038)          0x188 +4*i  bitmap A ptrs (0x6028)          0x1a8 +4*i  colour-change map B ptrs (0x6030)
0x1c8 u32  malloc'd SP pool (0x18ff20 B, 32-aligned base) (0x5ff8; freed 0xcac)
0x1cc,0x1d0 u32  SP ping-pong flags (blocks 7 and 8) (0x6064-0x6068 init 0; toggled 0x1344-0x1348)
0x1d4..0x223  20-word SP register SHADOW: shadow[idx]= ctx[idx*4-0x36c], idx 0x150..0x163 (0x6690 stores at 0x66d0; 0x677c reads; zeroed 0x6048-0x605c)
0x224..0x263  SP palette copy 16 words (software path only, 0x15e0)      0x264..0x2a3  software RGB table 16 words (0x168c)
0x2a4 width  0x2a8 pitch(64-aligned)  0x2ac height  (0xe24-0xe40; defaults 0x6920)
0x2b0 ptr after setup regs (0x5790, 0x5a94)   0x2b4 type-3 header slot ptr   0x2b8 end of last MC group   0x2bc aligned-ring marker (0x2e44)
0x2c0..0x2d0  5 picture buffer base addresses (0x2a9c)    0x2d4..0x2ec  per-picture params (see 3.5)
0x2f0 list node {ctx, next} (0xb94-0xba4)   (0x2f4 next)
```
Pool layout from 0x5fd0: `base=align32(malloc(0x18ff20))`; for i<8: `A[i]=base+i*0x32000`, `B[i]=A[i]+0x1b000`, `data[i]=A[i]+0x24000` (0xe000 B each). 8*0x32000 = 0x190000 > 0x18ff20: last slot's data buffer overruns the allocation by up to 0xe0+31 B if fully used [E arithmetic].

---------------------------------------------------------------------------------------------------
## 6. Sub-picture blending path at register level

### 6.1 Register write primitive [E]
`0x6690(ctx, idx, val)`: `ctx[idx*4-0x36c] = val` (shadow); ensure 8 B room (flush if not); emit LE `[idx][val]` = CCE **type-0, one register, dword index idx** => MMR address `idx*4`. `0x677c(ctx, idx)` reads the shadow. Used only for idx 0x150..0x160:

| idx | MMR | meaning (all [I] from usage; none in RRG register list - RRG 2.1.8 only says "Subpicture registers control the DVD subpicture feature") |
|---|---|---|
| 0x150 | 0x540 | control: bit0 = SP display on (EnableSP, DCSQ 0/1/2), bit1 = button highlight on (EnableButton) |
| 0x151 | 0x544 | bits 31:16 = 4 nibbles of SET_COLOR (palette indices e0..e3), bits 15:0 = 4 nibbles of SET_CONTR (alpha/contrast) |
| 0x15c | 0x570 | button param (PrepareButton 3rd arg) |
| 0x15d | 0x574 | button rect start `(top<<16)|left` |
| 0x15e | 0x578 | button rect end `(bottom<<16)|(right-1)` |
| 0x15f | 0x57c | palette index write (0..15) |
| 0x160 | 0x580 | palette data `0x00YYCbCr` |
Submission: after a group of register writes the code does a full FLUSH/submit (0xfc0-0x1010, 0x1818-0x1894, 0x60f8-0x6170, 0x65fc-0x6678).

### 6.2 Data path [E/I]
* Host writes SP bitmap into pool buffers (A: 192 B/line x 576 lines [I 2 bpp 768 px], raw data buffer from GetSPBuffer).
* `ShowSPBuffer`/`ClearSP`/DCSQ-7 call 0x2b94 -> **sel 6** (struct in 2) which hands bitmap A / map B (user VA + dims + ping-pong half) to the kernel; then ring blocks **7 {ctx[0x1cc]}** (A half select) and **8 {ctx[0x1d0]}** (B half select) tell the kernel/GPU which half is live [I]; display rect via **sel 0xf**.
* Blend itself (alpha from contrast nibbles, colour from palette regs) is performed by the SP hardware block; the bundle programs only the registers above. No software blend of SP onto video is in this bundle in the hw path [E: `!hw` software palette conversion is the only colour math and is dead].

### 6.3 DCSQ interpreter `0x6328(ctx, idx, off, end, data)` - ONE command at `data[off]` [E]
| cmd | DVD name | effect |
|---|---|---|
| 0 | FSTA_DSP | ctx[0x15c]=1; hw: reg0x150 |= 1; flush |
| 1 | STA_DSP | ctx[0x15c]=1; hw: reg0x150 = ctx[0x158] ? v|1 : v&~1; flush |
| 2 | STP_DSP | ctx[0x15c]=0; hw: reg0x150 &= ~1; flush |
| 3 | SET_COLOR | bytes b1,b2: nibbles {b2&0xf, b2>>4, b1&0xf, b1>>4} -> reg0x151 bits [19:16],[23:20],[27:24],[31:28]; flush |
| 4 | SET_CONTR | same nibble order -> reg0x151 bits [3:0],[7:4],[11:8],[15:12]; flush |
| 5 | SET_DAREA | `sx=(b1<<4 &0x3f0)|(b2>>4); ex=((b2&3)<<8)|b3; sy=(b4<<4 &0x3f0)|(b5>>4); ey=((b5&3)<<8)|b6` (10-bit); hw: sel 0xf `{sy, ex, ey, sx&~15}` |
| 6 | SET_DSPXA | ignored |
| 7 | CHG_COLCON | hw: `0x6188(ctx, off, ?, B[idx], data)` then `0x2b94(ctx,1,idx,0x40,0x240)` (upload map B) |
| other | | ignored, return 0 |

`0x6188` (CHG_COLCON -> map B) [E, behaviour]: requires `data[off]==7`; `p=off+3`; reads ONE LN_CTLI at `p` (4 bytes: `csln=((b0&3)<<8)|b1`, `ncon=b2>>4`, `ctln=((b2&3)<<8)|b3`); if `b0==0x0f` (end code) returns; for each of `ncon` PX_CTLI (6 B each at p+4+6k): `col=u16&0x3ff`, copies the 4 bytes at +2..+5 to `B[(line*8+k)*8 + 0..3]`, `B[..+4..5]=bswap16(col)`, `B[..+6..7]=0` for lines `csln-top+2 .. ctln-top` (top=`u16 ctx[0x160]`; note start/end are re-biased each k iteration, so for ncon>1 with top!=0 the range drifts [E code at 0x61f8-0x6210]); the unused entries `k=ncon..7` of those lines are filled with `0xff` (0x62b8-0x6314). Only the first LN_CTLI group is processed; always returns 1.

---------------------------------------------------------------------------------------------------
## 7. NOT established / open
1. Meaning of selectors 0, 1, 3, 5 and their outputs (ctx[0x20..0x2c] never read); selector 6 struct field semantics; sel 0x10 id 0x92.
2. Kernel's handling of block types 5-8, 0x16-0x19 (expansion of CNTL_PAINT, picture-param blocks), whether MapMemory(type 1) returns the same buffer, and the exact blocking behaviour of submit (no wait/idle call in the bundle besides `WAIT_UNTIL OV0_FLIP` and sel 5).
3. Meaning of SP registers 0x540-0x580 beyond usage (undocumented in RRG), of undocumented regs 0x1800/0x1808/0x1A00-0x1A34/0x19A0/0x19A4/0x1994 and of type-3 opcode 0x31 payloads (0x400/0x100/0).
4. The 0x60..0x13f shadow block (consumer unknown) and the semantics of the MC command words (opcode 3/5/6/7, nibble tags) - dvdfw.bin / decode agents.
5. Who sets `ctx[0x2bc]` alignment base exactly (0x2e44, decode scope) and the role of `r4`/`mode` params of the emitters.
6. Why key colours are 0x40/0x1000 (interpretation is inferred only).
7. Latent defects observed [E]: un-zeroed ctx (0x2a4 uninitialised), dangling list node on selector-4 failure (0xb94-0xbd8), 0x5fd0 failure ignored, SP pool 0xe0+ bytes short for the last slot, no unmap of mappings.

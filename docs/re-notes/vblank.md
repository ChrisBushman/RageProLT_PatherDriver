# Vertical blank: what the chip offers, what exists, what it would take (Nov 26, static; nothing run)

Written so we can refer back if a feature needs it (user request, Nov 26). Sources: RRG-G03300 pp. 4-42..4-46 (bit positions read from the rendered
diagram, pdf pages 108-109, not from text), existing kext/driver code, NDRV decode (apple-ndrv-framebuffer.md D8, sections 14/17), dma-async-queue.md.

## 1. Registers
CRTC_INT_CNTL, RRG index 0_06 = byte offset 0x18. Bit positions (letters a..B in the diagram, left of the gap at bits 10:7):

| bits | field |
|---|---|
| 0 | CRTC_VBLANK (R: 1 while in vertical blank) |
| 1 / 2 | CRTC_VBLANK_INT_EN / CRTC_VBLANK_INT (write 1 to bit 2 = ack) |
| 3 / 4 | CRTC_VLINE_INT_EN / CRTC_VLINE_INT (fires at line CRTC_VLINE) |
| 5 | CRTC_VLINE_SYNC (odd/even line) |
| 6 | CRTC_FRAME (interlace odd/even) |
| 8:7 | snapshot interrupt (named in the CRTC text; not in the table) |
| 11 | CRTC2_VBLANK (R) |
| 12 / 13 | CRTC2_VBLANK_INT_EN / INT |
| 14 / 15 | CRTC2_VLINE_INT_EN / INT |
| 16 / 17, 18 / 19 | CUPBUF0, CUPBUF1 (capture buffers) INT_EN / INT |
| 20 / 21 | OVERLAY_EOF_INT_EN / INT (overlay end of frame) |
| 22 / 23 | ONESHOT_CAP_INT_EN / INT |
| 24 / 25 | BUSMASTER_EOL_INT_EN / INT (matches RageLTDma.cpp) |
| 26 / 27 | GP_INT_EN / INT |
| 28 | CRTC2_VLINE_SYNC |
| 29 / 30 | SNAPSHOT2_INT_EN / INT |
| 31 | VBLANK_BIT2_INT |

* Writing the INT bit position acknowledges (1 clears). RRG: clear stale interrupts first, enable in a SECOND write; never in one write.
* Position in the line: CRTC_VLINE_CRNT_VLINE (0_04 = byte 0x10) holds the current line (bits 10:0) and the VLINE compare value (bits 26:16 area; see RRG 4-42). CRTC2 has its own copy.
* Consequence for the NDRV decode: the sleep path's "register 0x18" (saved, then cleared before suspend, restored on wake) IS this register, i.e. the NDRV disables and later restores its interrupt enables (the VBL service) around sleep. Section 17.6 corrected.

## 2. What exists today (all polled, no interrupt)
* Presenting waits for vblank by polling (kext waitFlip repurposed to wait for the vblank leading edge via CRTC_V_TOTAL_DISP; apple-glcontext-protocol.md ~l.149, double-buffer.md l.218: the wait is deferred so it overlaps geometry dispatch).
* The overlay flip (C1/C2 results) already showed clean swaps without tearing.
* No interrupt handler exists in the kext. Stock `ATIRagePro` has none either (design 11, O3: `waitForTimeStamp`/`sleepForTimeStamp`, no hardware interrupt).
* Interrupt wiring (O6, dma-async-queue.md): one level interrupt on the OpenPIC controller, `interrupts = <1>`. Stage A probes only polled the status bit; no IRQ was ever registered.

## 3. What an interrupt would buy, and what it would cost
* **Buys:** the CPU sleeps instead of spinning in the vblank wait. A wait is up to one frame (13-17 ms at 60-75 Hz) per presented frame; on the single-core G3/G4 that time is currently burned in a poll unless the wait is deferred behind other work (it already partly is). OVERLAY_EOF could time overlay flips (and video) exactly.
* **Does NOT buy:** accuracy beyond the poll (the poll sees the same edge), and nothing for GUI-master DMA completion (the chip never raises it, measured).
* **Unknowns (not measured):** whether CRTC_VBLANK_INT actually asserts the OpenPIC line on this board (the polled status bit is a safe first test: set EN in two writes, watch INT bit 2, ack, disable; same shape as the EOL probe); interrupt-latency cost versus a spin on this CPU; how Panther delivers a third-party level interrupt (IOInterruptEventSource on the provider) and whether the NDRV's own VBL service leaves it unmasked.
* **Contention (Lombard, can't test here, design-relevant):** the NDRV keeps its own 'vbl ' interrupt service on the same register. If we enable bits we must preserve its enables and never ack an INT bit we did not enable; a shared ack would swallow its interrupt. On the G4 our stub framebuffer owns the register (its VBL service is polled, D8), so no contention arises there.

## 4. Recommendation
Keep polling until a measured need appears. If one does, the cheapest honest first step is a G4 read-only experiment (enable VBLANK_INT_EN, poll INT, ack) to prove the status bit works, then a second experiment registering the interrupt and measuring CPU time per frame against the spin. Neither is needed for anything on the checklist today.

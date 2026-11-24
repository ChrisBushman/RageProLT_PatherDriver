# Rage LT Pro (1002:4c49) video overlay / display scaler - from Win98 ATI 4.13.1.2655

Labels: ESTABLISHED = read directly in the decompile; CROSS-CHECKED = also matches xf86-video-mach64 (pub/atiregs.h, pub/atimach64xv.c) or the RRG/PRG; NOT ESTABLISHED = needs hardware experiment.
Evidence: evidence/overlay-hal.c (ATID32AE.DLl, ImageBase 0xb00b0000; RVA = VA - base). Block-1 base = DAT_b00ddd04 (offset = index*4), block-0 = DAT_b00ddd18 (+0x400).

## Summary and open questions
- ESTABLISHED/CROSS-CHECKED: 0x4c49 runs HAL family 7 (PDEV+0x300). One overlay (dwMaxVisibleOverlays=1). Source formats: YUY2, UYVY, YV12, IYUV, YVU9, IF09, and RGB 15/16/32 overlay surfaces (allowed unless registry Ovl16/Ovl32 set). So an RGB565 or xRGB8888 640x480 back buffer CAN be scaled to 1024x768 by the overlay (VIDEO_FORMAT SCALER_IN 4 / 6). RGB gets pixel replication, YUV bilinear (PRG).
- Max source width: 768 (family<5), 720 (family>=5), CanCreateSurface at RVA 0xbaa20 (VA b00baa20). PRG says 384 on rev A. Hardware experiment: probe 1024-wide source at rev of the actual chip.
- Increments 12 fractional bits (CROSS-CHECKED).
- NOT ESTABLISHED: KEY_CNTL bits 9/27, OVERLAY_SCALE_CNTL bit 7 meaning, DDCAPS stretch limits (set in 16-bit ATI2DRAE.DRV), real filter behaviour.

## Registers (block 1 index, byte offset = idx*4 as shadow struct order)
| shadow off | reg | value (ESTABLISHED, FUN_b00bce61 RVA 0x1ce61) |
|---|---|---|
|0x00|OVERLAY_Y_X_START|x<<16 | y (LOCK bit via xorg)|
|0x04|OVERLAY_Y_X_END|((x2-1)<<16)|(y2-1)|
|0x08/0x0c|VIDEO_KEY_CLR/MSK||
|0x10/0x14|GRAPHICS_KEY_CLR/MSK|mask = depth mask 0xff/0x7fff/0xffff/0xffffff|
|0x18|OVERLAY_KEY_CNTL|see below|
|0x20|OVERLAY_SCALE_INC|(H_INC<<16)|V_INC|
|0x24|OVERLAY_SCALE_CNTL|0xc0000000 (SCALE_EN|OVERLAY_EN) | gamma<<5 | redtemp<<1|
|0x28|SCALER_HEIGHT_WIDTH|(srcW<<16)|srcH|
|0x34/0x38/0x3c|SCALER_BUF0/BUF1 OFFSET, BUF_PITCH|pitch in pixels (bytes/2 for 16bpp & packed YUV, /4 for 32bpp; byte pitch planar)|
|0x48|VIDEO_FORMAT|SCALER_IN bits 19:16: 3=15bpp 4=565 6=32bpp 9=YUV9 0xa=YUV12 0xb=VYUY(YUY2) 0xc=YVYU(UYVY); low nibble preserved|
|blk1 0x150|SCALER_COLOUR_CNTL|default 0x00101000; bright [6:0], Usat [12:8], Vsat [20:16] (0x10 neutral)|
|0x154-0x164|SCALER_H_COEFF0..4 (family>2)|0x00002000,0x0d06200d,0x0d0a1c0d,0x0c0e1a0c,0x0c14140c|

Increments: H_INC = srcW*0x1000/dstW, then x2/x4 per PLL_VCLK_CNTL ECP_DIV (read via CLOCK_CNTL index 0x14, +0x91/+0x92). V_INC = srcH*0x1000/dstH, doubled when CRTC_GEN_CNTL(0x41c) bit1 (see RVA 0xbdc763 mode-change handler: shifts INC when ECP_DIV / double-scan changes). (ESTABLISHED; xorg ScaleVideo CROSS-CHECKED, truncate not round per PRG).
Bandwidth: on family>2, OVERLAY_SCALE_CNTL &= 0xf3ffffff (clears BANDWIDTH/DIS_LIMIT bits 27:26). Bit 7 toggled with PDEV+0x248 bit0 (NOT ESTABLISHED meaning).
KEY_CNTL: 0x8000211 no key; 0x110 hidden; 0x114 source key (video != key); 0x50 dest key (gfx == key; CROSS-CHECKED with xorg 0x50); 0x154 both. Fields VIDEO_FN[2:0], GFX_FN[6:4] (0 FALSE,1 TRUE,4 NE,5 EQ), CMP_MIX bit8 (0 OR, 1 AND).

## Flip / double buffering
No hardware toggle used: Flip (RVA 0x3c40) rewrites BUF0 and BUF1 offsets to the new surface and sets shadow flags 0x100/0x200 (odd/even), then FUN_b00bdd43 -> FUN_b00bce61 reprograms. Auto-flip sets CAPTURE_CONFIG bit 28. Primary flip writes CRTC_OFF_PITCH offset field (>>3). (ESTABLISHED)

## Mode change interaction
Handler FUN_b00bc763 (cases 1-4,9): on mode set reprogram scale inc for ECP_DIV/dblscan change and re-run FUN_b00bce61; case 3 (disable) hides with KEY_CNTL 0x110, start/end=10. LCD/secondary state in PDEV+0x230..0x248 recomputes H_INC via FUN_b00bdf20 (panel expansion). (ESTABLISHED; panel math NOT fully traced.)

## Gamma
OverlayGammaSel registry (2 bits) -> OVERLAY_SCALE_CNTL [6:5]; OverlayRedTemp -> bit 1. ColorControl brightness/saturation -> SCALER_COLOUR_CNTL.

## Recipe for 640x480 RGB565 -> 1024x768
1 KEY_CNTL=0x50 (or 0x8000211 for full cover), GFX key/mask set. 2 SCALER_BUF0/1_OFFSET=fb offset, BUF_PITCH=pitch/2. 3 SCALER_HEIGHT_WIDTH=(640<<16)|480. 4 VIDEO_FORMAT SCALER_IN=4. 5 OVERLAY_SCALE_INC=(0x0a00<<16)|0x0a00 (640*4096/1024 = 480*4096/768 = 2560; adjust for ECP_DIV/dblscan). 6 Y_X_START=0, Y_X_END=(1023<<16)|767. 7 H_COEFF regs, COLOUR_CNTL. 8 OVERLAY_SCALE_CNTL=0xc0000000 (+ gamma). Hardware experiment: verify RGB565 path on LT Pro (family 7 is not special-cased out in FUN_b00bb930; registry-gated only).

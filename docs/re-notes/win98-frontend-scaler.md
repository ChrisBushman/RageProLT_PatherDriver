# Front-end scaled/stretch blit recipe (Rage LT Pro) - ATID32AE Blt callback

Evidence: evidence/frontend-scaler-blt.c (FUN_b00b6720, RVA 0x5720). Block-0 dword indices used as headers (0x7f = SCALE_3D_CNTL). Labels as in overlay-scaler.md.

## Summary and open questions
ESTABLISHED sequence (family>2). Likely causes of the user's constant-garbage result (candidates, not proven): missing DP_SET_GUI_ENGINE reset first; no explicit HACC/VACC=0; wrong increment fraction (Win98 uses 16 fractional bits: (src<<16)/dst - conflicts with PRG's 12); DP_SRC FRGD_SRC=5 (0x500) not set; DP_PIX_WIDTH scale nibble [31:28] not set; SCALE_OFF not qword aligned; missing SCALE_3D_CNTL FCN_SCALE; TEX_CNTL 0x800000 flush. NOT ESTABLISHED: SCALE_3D_CNTL bit 8, DP_SET_GUI_ENGINE 0x100000 meaning, purpose of trailing 1-pixel trigger.

## Sequence (register = block-0 index)
1. Wait FIFO (FIFO_STAT 0x310 or GUI_STAT 0x338).
2. DP_SET_GUI_ENGINE(0xbf)=0x100000; TEX_CNTL(0xdd)=0 (0x800000 if (PDEV[0x244]&3)==2); ALPHA_TST_CNTL(0x54)=0.
3. TEX_CNTL=0x800000 (TEX_CACHE_FLUSH, bit 23) when scaling.
4. SCALE_3D_CNTL(0x7f)=0x40 (FCN_SCALE) | 0x100 unless GBL flag bit 6. Alpha blits OR 0x2c0a00 / 0x402c0a00.
5. SCALE_OFF(0x70)=src offset qword aligned; leftover pixels added to SCALE_WIDTH and dest width, dest trimmed with SC_LEFT_RIGHT.
6. SCALE_PITCH(0x7b) pixels, SCALE_WIDTH(0x77)=srcW, SCALE_HEIGHT(0x78)=srcH.
7. SCALE_HACC(0xf2)=0, SCALE_VACC(0x7e)=0.
8. SCALE_X_INC(0x7c)=(srcW<<16)/dstW, SCALE_Y_INC(0x7d)=(srcH<<16)/dstH.
9. DST_CNTL(0x4c)=0; DP_WRITE_MSK(0xb2)=~0.
10. DP_PIX_WIDTH(0xb4)=dst|src<<8|host<<16|scale<<28; observed 0x30030303 (1555), 0x40040404 (565), 0x20020202 (8bpp), 0x60060606 (32bpp). YUV source ORs 0xb0000000/0xc0000000.
11. SC_LEFT_RIGHT(0xaa), SC_TOP_BOTTOM(0xad) = clip.
12. DST_OFF_PITCH(0x40)=(off>>3)|(pitch_px/8)<<22.
13. DP_SRC(0xb6)=0x500; DP_MIX(0xb5)=0x70003 (SRCCOPY). Colour key: CLR_CMP_CLR(0xc0), CLR_CMP_CNTL(0xc2).
14. GUI_TRAJ_CNTL(0xcc)=3; SRC_Y_X(0x63); DST_Y_X(0x43).
15. Trigger DST_HEIGHT_WIDTH(0x46)=dstW<<16|dstH.
16. Scaling: SCALE_3D_CNTL|=0x402c0a00, DST_HEIGHT_WIDTH=1 (purpose NOT ESTABLISHED, probably flush); then SCALE_3D_CNTL=0.
Experiment to settle: replay steps 2-16 on hardware varying one step at a time (e.g. drop step 16, HACC/VACC, 12 vs 16 bit INC).

# DVD / MPEG assist - ATIICDXX, ATIMCRAE, ATIVPEAE

Evidence: evidence/icd-chip-detect.c (ATIICDXX.DLl, base 0x10000000), evidence/mc12.c (ATIMCRAE, base 0xb0100000), evidence/mc-scaler-pipe-cmds.c (FUN_b00c6a40), evidence/vpe-caps.c.

## Summary and open questions
- 0x4c49: ICD family 4 -> ASIC\Motion Compensation=1, ASIC\IDCT absent (IDCT only for families 6,7,8 or ids 0x4c4d/0x4c52). ESTABLISHED (FUN_1000c0de and key emission ~RVA 0x17a6c).
- ICD family map: 0x4c49,4c42,4c44,4c50,4c51,4c4d,4c52 -> 4; 4c47 -> 5; 4c45,46,4b,4c,57,59,60 -> 6. MC=1 for families 3-8 (2 if subrev>1). Writer skips -1 values.
- ATIICDXX.VXd (ATICDS32) only does PCI BIOS, physmem mapping, V86 calls; programs no graphics.
- So the LT Pro has hardware motion compensation (via scaler/3D pipe) but no hardware IDCT; IDCT is host software (INI IDCTinAGPemulate, UseIDCT; FUN_b00c5340 copies/zero-pads coefficient blocks for host IDCT). Disassembly shows no MMX in ATIMCRAE: software IDCT is plain C.
- HAL family 7 -> MC family 8 (DAT_b0122e50: default3, 3->5, 4->6, 5/6->9, 7->8). MC12 needs {5,6,8,9}; MCAM/MCRM {8,9}.

## MC programming (ESTABLISHED)
Init stream (FUN_b0103f60): SCALE_OFF=0, SECONDARY_SCALE_OFF=0, SCALE_3D_CNTL=0x98a40 (FCN_SCALE, TEX_CACHE_SPLIT, blend ONE/ONE, signed dst clamp), ALPHA_TST_CNTL=0x8000, DP_PIX_WIDTH=0x80080888 (Y8), DST_CNTL=0x8e023, TEX_CNTL=0x40800180, CLR_CMP_CNTL=0, DP_FRGD_CLR=0, SCALE_WIDTH=2*pitch, SCALE_HEIGHT, INC regs (0x7c,0x7d,0xe7,0xf4)=0x10000, DP_SRC=0x505, SC_LEFT_RIGHT=w<<17, SC_TOP_BOTTOM=h<<16, DP_MIX=0x70003.
Per-block: SCALE_OFF_ACC(0xe2)/SECONDARY_SCALE_OFF_ACC(0xe1) with half-pel in bits 30:28, SCALE_PITCH_BOTH(0xe0), SCALE_DST_Y_X(0xe3), trigger reg 0xe4 with 0x100010 (16x16), 0x100008, 0x80008, 0x80004. Bus-master list entries header 0x7fffe48, length word 0x40001000.
NOT ESTABLISHED: reg 0xe4 name, exact residual-add path, hardware experiment: submit one MC block with IDCT result in a Y8 plane and compare vs software.

## ATIVPEAE
Video port extension; chip type 'L', max width 720 (0x168 for 0x4c47); talks to kernel via DeviceIoControl, key Software\ATI Technologies\Driver\VPEHAL. LT Pro video-in presence NOT ESTABLISHED (FUN_b00b8c8c queries).

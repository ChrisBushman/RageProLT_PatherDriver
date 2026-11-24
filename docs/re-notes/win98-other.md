# Other driver functions (Rage LT Pro, Win98 package)

## Summary and open questions
Items below are found by strings/decompile; register-level recipes beyond what is listed are NOT ESTABLISHED and weren't fully traced.
- Vblank: WaitForVerticalBlank (RVA 0x4020) spins on CRTC_INT_CNTL (block 0 +0x18, bit VBLANK, bit0; CROSS-CHECKED atiregs CRTC_VBLANK 0x1, INT_EN 0x2, INT ack 0x4). Interrupt use: ATI2VXAE miniVDD has IRQProvider with IRQ virtualisation (strings); LT bits CRTC2_VBLANK etc in atiregs.
- Page flip: CRTC_OFF_PITCH offset field (offset>>3) ESTABLISHED.
- I2C/DDC: ATI2VXAE miniVDD has I2C provider (error strings "No I2C Available"); ATI2DRAE registry keys DirectDDC, IDCDDCDetection. Register sequence (GP_IO / I2C via block-0) NOT ESTABLISHED - decompile ATI2VXAE.c I2C functions next.
- Hardware cursor: registry HWCursor, HWCursorHotSpotXMax; 16-bit driver CHECKCURSOR/SETCURSOR/MOVECURSOR escapes. Register recipe (CUR_* regs) NOT ESTABLISHED.
- Power: DPMS: ATI2VXAE SetDPMSState/GetDPMSState/SetMonitorPowerState; ATI2DRAE VGADPMS, IDCSetDPMS, OEM_ACER_DPMS_HOOK, ResumeReset. LCD panel registers LCD_INDEX/LCD_DATA at block-0 0x4a4/0x4a8 used in LT paths. Suspend/resume register save list NOT ESTABLISHED.
- Panel: PANEL1024 key, ATILCD.DLL, LCDOutput/LCDESCAPEFUNC (panel expansion).
- Misc: Macrovision (TV out disabled during copy protection), ATITVOUT.DLL, IDC escapes 0x7302/0x7306 between DD HAL and 2D driver, BMEngine / BMGUITableFIFO bus-mastering keys, ATIDDRAW.INI DXVA_Debug UseBM.
Hardware experiments: toggle CRTC_INT_CNTL VBLANK_INT_EN and observe PCI IRQ; read cursor regs after Windows sets cursor.

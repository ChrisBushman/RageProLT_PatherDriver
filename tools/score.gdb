# Trace cglScorePixelFormat (OpenGL @ 0x90db0000, foff 0xf8c) to see which config
# nodes get scored and what each returns.  For our node (accelerated, node[0x08]
# bit 0x100 set) we expect a high score incl. the +0x258 bonus; if it's never
# scored, it was skipped earlier (device-mask / 0x100 gate); if scored 0, a
# field check disqualified it.  r5 = node, return value = score.
set pagination off
set width 0
break *0x90db0f8c
commands
  silent
  printf "SCORE node=0x%x [04]=0x%x [08]=0x%x [0c]=0x%x [10]=0x%x [14]=0x%x [18]=0x%x [1c]=0x%x [30]=0x%x\n", $r5, *(unsigned int *)($r5+4), *(unsigned int *)($r5+8), *(unsigned int *)($r5+0xc), *(unsigned int *)($r5+0x10), *(unsigned int *)($r5+0x14), *(unsigned int *)($r5+0x18), *(unsigned int *)($r5+0x1c), *(unsigned int *)($r5+0x30)
  finish
  continue
end
run
quit

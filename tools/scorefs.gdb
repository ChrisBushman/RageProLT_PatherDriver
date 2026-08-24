# Dump the REQUEST fields vs our NODE fields at cglScorePixelFormat entry
# (r3=request_hi, r5=node) to see exactly which check disqualifies (score 0):
#   caps superset: request[0x08] bits must ALL be in node[0x08]  (andc. -> reject)
#   node[0x0c] must == request[0x0c]
#   node[0x04] matched vs request[0x04] (masks) when request[0x04]!=0
set pagination off
set width 0
break *0x90db0f8c
commands
  silent
  printf "REQ [04]=0x%x [08]=0x%x [0c]=0x%x   NODE [04]=0x%x [08]=0x%x [0c]=0x%x [10]=0x%x [1c]=0x%x   missingcaps=0x%x\n", *(unsigned int *)($r3+4), *(unsigned int *)($r3+8), *(unsigned int *)($r3+0xc), *(unsigned int *)($r5+4), *(unsigned int *)($r5+8), *(unsigned int *)($r5+0xc), *(unsigned int *)($r5+0x10), *(unsigned int *)($r5+0x1c), (*(unsigned int *)($r3+8)) & ~(*(unsigned int *)($r5+8))
  continue
end
run
quit

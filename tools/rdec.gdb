# Capture GLEngine's render decision right after gldUpdateDispatch returns
# (runtime 0x5096d0 = base 0x505000 + foff 0x46d0).  r3=return, [r28+0x4610]=
# cached flag it compares against, [r30+0x78ac]=the render-fn ptr it tail-calls.
# If [r30+0x78ac] is in our bundle (0x80xxx) it's HW; if GLEngine (0x5xxxxx) SW.
set pagination off
set width 0
break *0x5096d0
commands
  silent
  printf "AFTER gldUpdateDispatch: r3(ret)=0x%x  r28=0x%x [r28+0x4610]=0x%x\n", $r3, $r28, *(unsigned char *)($r28+0x4610)
  printf "  r30(gli)=0x%x  renderfn[78ac]=0x%x  [789c]=0x%x  updatefn[7990]=0x%x\n", $r30, *(unsigned int *)($r30+0x78ac), *(unsigned int *)($r30+0x789c), *(unsigned int *)($r30+0x7990)
  printf "  dispatch table[0x28]=0x%x\n", *(unsigned int *)(0x22c8c8+0x28)
  continue
end
run
quit

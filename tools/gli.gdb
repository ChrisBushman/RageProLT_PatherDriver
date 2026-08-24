# gliChoosePixelFormat (GLEngine 0xed120, runtime base 0x505000) iterates the
# renderer registry, calling each entry's [+0x118] chooser (bctrl @ 0xed174) and
# reading the returned node list from [r1+0x40] (0xed178).  We break just after
# each chooser returns and print the renderer's type[+0x104] and the first node's
# [0x04]/[0x08].  The renderer whose returned node has [0x08]=0x7dd (our accel
# signature) is the HARDWARE renderer forwarding to our gld -> its type code is
# what our node[0x04] must claim (instead of the software 0x200 we copied).
set pagination off
set width 0
break *0x5f2178
commands
  silent
  set $L = *(unsigned int *)($r1+0x40)
  printf "renderer type[104]=0x%x  returnedList=0x%x", *(unsigned int *)($r30+0x104), $L
  if $L != 0
    printf "  node[04]=0x%x [08]=0x%x", *(unsigned int *)($L+4), *(unsigned int *)($L+8)
  end
  printf "\n"
  continue
end
run
quit

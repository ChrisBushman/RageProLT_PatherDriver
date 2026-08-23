# Trace cglInitializeContext: which plugin does CGLCreateContext connect to, and
# what CreateContext fn does it call?  Our node won selection, but gldCreateContext
# is never entered -> the framework connected to a different (software) plugin.
#   cglInitializeContext entry 0x90db4224 (r3 = pixel-format object)
#   glcPluginConnect call     0x90db4348 (r3 = plugin index from pix entry[2])
#   CreateContext vtable call 0x90db440c (r12 = fn ptr, [r30+8] = dispatch)
set pagination off
set width 0
break *0x90db4224
commands
  silent
  printf "cglInitializeContext pix=0x%x  bytes: %02x %02x %02x %02x  %02x %02x %02x %02x\n", $r3, *(unsigned char *)($r3+0), *(unsigned char *)($r3+1), *(unsigned char *)($r3+2), *(unsigned char *)($r3+3), *(unsigned char *)($r3+4), *(unsigned char *)($r3+5), *(unsigned char *)($r3+6), *(unsigned char *)($r3+7)
  continue
end
break *0x90db4348
commands
  silent
  printf "  glcPluginConnect(pluginIndex=%d)\n", $r3
  continue
end
break *0x90db440c
commands
  silent
  printf "  CreateContext call: r12=0x%x  dispatch[r30+8]=0x%x\n", $r12, *(unsigned int *)($r30+8)
  continue
end
run
quit

# gdb batch: capture the framework's pixel-format config-node list at runtime.
# Accelerated request -> gliChoosePixelFormat iterates ALL renderers into one
# linked list; at glcPluginChoosePixelFormat+0xf0 (com.apple.opengl fixed base
# 0x90db0000) r5 = list head.  We dump every 0x34-byte node -> valid (software)
# nodes alongside ours, so we learn the format AND what ours lacks.
set pagination off
set width 0
break *0x90db386c
run
printf "\n======== pf list capture (accelerated) ========\n"
printf "list head r5 = 0x%x   gliChoose rc r3 = %d\n", $r5, $r3
set $p = $r5
set $i = 0
while ($p != 0) && ($i < 16)
  printf "\n--- node[%d] @ 0x%x  (13 words = 0x34 bytes) ---\n", $i, $p
  x/13xw $p
  set $p = *(unsigned int *)$p
  set $i = $i + 1
end
printf "\n======== end (%d nodes) ========\n", $i
quit

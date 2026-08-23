# gdb batch: capture the framework's pixel-format config-node list at runtime.
# Accelerated request -> gliChoosePixelFormat iterates the renderers into a
# linked list written to [r1+0x40]; glcPluginChoosePixelFormat (com.apple.opengl
# fixed base 0x90db0000) loads it into r5 at 0x3780 and passes it to the filter
# (bl 0xa78c) at 0x3790.  Break at 0x3790: r5 = list head, BEFORE the filter.
# (0x386c is dead code for a non-null list -- the code branches to 0x38c4.)
# We dump every 0x34-byte node -> our node (+ any others) so we learn the format.
set pagination off
set width 0
break *0x90db3790
run
printf "\n======== pf list capture (accelerated) ========\n"
printf "list head r5 = 0x%x   ctx r3 = 0x%x\n", $r5, $r3
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

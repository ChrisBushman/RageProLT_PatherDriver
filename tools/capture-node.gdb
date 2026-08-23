# gdb batch script: capture the framework's pixel-format config nodes at runtime.
# Breaks in com.apple.opengl glcPluginChoosePixelFormat right after it loads the
# pf list returned by gliChoosePixelFormat (r5 = list head), then walks the
# linked list (node[0] = next) dumping each 0x34-byte node.  Run against a
# SOFTWARE renderer (mach64_gltest sw) to learn the exact node format.
set pagination off
set width 0
# glcPluginChoosePixelFormat+0xf0 (com.apple.opengl @ fixed base 0x90db0000):
#   0x90db386c  lwz r5, 0x40(r1)   ; r5 = pf list head
#   0x90db3870  <break here>
break *0x90db3870
run sw
printf "\n======== pf node capture (r5 = list head) ========\n"
printf "list head = 0x%x\n", $r5
set $p = $r5
set $i = 0
while ($p != 0) && ($i < 8)
  printf "\n--- node[%d] @ 0x%x  (13 words = 0x34 bytes) ---\n", $i, $p
  x/13xw $p
  set $p = *(unsigned int *)$p
  set $i = $i + 1
end
printf "\n======== backtrace (confirm software path) ========\n"
bt 6
quit

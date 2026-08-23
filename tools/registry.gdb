# Dump GLEngine's renderer registry (head ptr @ runtime 0x5fec64 = base 0x505000
# + vaddr 0xf9c64) at gliCreateContext entry (0x5df998).  Each entry: linked via
# [+0], type code at [+0x104] (what gliCreateContext matches node[0x04] against),
# and [+0x118] = the renderer's gldChoosePixelFormat ptr (if it points into our
# bundle, that entry is OURS -> read its real type code to put in node[0x04]).
set pagination off
set width 0
break *0x5df998
commands
  silent
  printf "=== renderer registry @ 0x5fec64 ===\n"
  set $r = *(unsigned int *)0x5fec64
  set $i = 0
  while ($r != 0) && ($i < 16)
    printf "rend[%d] @0x%x  type[104]=0x%x  f100=0x%x  choosePF[118]=0x%x  f108=0x%x\n", $i, $r, *(unsigned int *)($r+0x104), *(unsigned int *)($r+0x100), *(unsigned int *)($r+0x118), *(unsigned int *)($r+0x108)
    set $r = *(unsigned int *)($r+0)
    set $i = $i + 1
  end
  printf "=== end (%d entries) ===\n", $i
  continue
end
run
quit

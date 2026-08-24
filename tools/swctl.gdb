# Breakpoint GLEngine's control-slot call sites (table[0x00..0x14]) in the SOFTWARE
# run (which renders).  Whichever fire = the geometry/render delivery path.  For
# our HW render only table[0x04] fires; the DELTA is the missing delivery slot.
# runtime = 0x505000 + foff.
set pagination off
set width 0
break *0x50564c
commands
 silent
 printf ">>> tbl+0x00 call @0x1624\n"
 continue
end
break *0x5097a0
commands
 silent
 printf ">>> tbl+0x04 call @0x4780 (state flush)\n"
 continue
end
break *0x5c1824
commands
 silent
 printf ">>> tbl+0x08 call @0xbc818\n"
 continue
end
break *0x5e9484
commands
 silent
 printf ">>> tbl+0x0c call @0xe45c (sub_c3c0 vertex/pixel path)\n"
 continue
end
break *0x5e9af4
commands
 silent
 printf ">>> tbl+0x0c call @0xeabc\n"
 continue
end
break *0x5cfa94
commands
 silent
 printf ">>> tbl+0x10 call @0xca74\n"
 continue
end
break *0x5cfe8c
commands
 silent
 printf ">>> tbl+0x10 call @0xce70\n"
 continue
end
break *0x507848
commands
 silent
 printf ">>> tbl+0x14 call @0x282c\n"
 continue
end
break *0x507cec
commands
 silent
 printf ">>> tbl+0x14 call @0x2ce0\n"
 continue
end
run
quit

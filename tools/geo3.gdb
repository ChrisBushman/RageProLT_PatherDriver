# Breakpoint EVERY GLEngine primitive-dispatch site (tbl+0x18..0x48, runtime =
# 0x505000+foff) to settle whether glDrawArrays uses the dispatch-table slots at
# all.  If none fire, geometry delivery for a HW renderer is a different path
# (shared DMA command buffer / culled), not the primitive slots.
set pagination off
set width 0
break *0x59507c
commands
 silent
 printf ">>> tbl+0x1c @0x9007c\n"
 continue
end
break *0x5951a8
commands
 silent
 printf ">>> tbl+0x44 @0x901a8\n"
 continue
end
break *0x59763c
commands
 silent
 printf ">>> tbl+0x4c @0x9263c\n"
 continue
end
break *0x5b2358
commands
 silent
 printf ">>> tbl+0x28 (TRI) @0xad358\n"
 continue
end
break *0x5ba3d4
commands
 silent
 printf ">>> tbl+0x24 @0xb53d4\n"
 continue
end
break *0x5c1818
commands
 silent
 printf ">>> tbl+0x08 @0xbc818\n"
 continue
end
break *0x5db484
commands
 silent
 printf ">>> tbl+0x18 @0xd6484\n"
 continue
end
break *0x5db508
commands
 silent
 printf ">>> tbl+0x40 @0xd6508\n"
 continue
end
break *0x5db5dc
commands
 silent
 printf ">>> tbl+0x1c @0xd65dc\n"
 continue
end
break *0x5db79c
commands
 silent
 printf ">>> tbl+0x20 @0xd679c\n"
 continue
end
break *0x5dd008
commands
 silent
 printf ">>> tbl+0x28 (TRI) @0xd8008\n"
 continue
end
run
quit

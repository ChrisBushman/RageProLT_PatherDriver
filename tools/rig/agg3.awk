function hx(h,  k,v){v=0; for(k=1;k<=length(h);k++) v=v*16+index("0123456789abcdef",substr(h,k,1))-1; return v}
$5=="d0060000" {b+=hx($6); p+=hx($7); i+=hx($8); sn+=hx($9)}
$5=="d0061000" {od+=hx($6); oi+=hx($7); on+=hx($8)}
$5=="d0062000" {u+=hx($6); v+=hx($7); x+=hx($8)}
$5=="d0063000" {st+=hx($6); hv+=hx($7)}
END {tb=41656251; printf "submits %d: build %.0f ms, wait-prev %.0f ms, idle-before-kick %.0f ms | other idleWait %d calls: dma %.0f ms, idle %.0f ms | userIn %.0f ms, batch_ok %.0f ms, tex-records %.0f ms\nprevious stream already finished when the next was ready: %d of %d (%.0f%%)\n", sn, b/tb*1000, p/tb*1000, i/tb*1000, on, od/tb*1000, oi/tb*1000, u/tb*1000, v/tb*1000, x/tb*1000, st, hv, 100*st/hv}

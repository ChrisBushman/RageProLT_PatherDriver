#!/bin/sh
# mirror each stage + kernel log tail to the host over UDP
U() { perl -e 'use Socket; socket(S,PF_INET,SOCK_DGRAM,getprotobyname("udp")); send(S,$ARGV[0],0,sockaddr_in(9999,inet_aton($ENV{RAGELT_LOG_IP}||"127.0.0.1")));' "$1"; }
U "stage: begin"
B=Mach64Accelerator.kext
rm -rf /tmp/$B; cp -R /Users/sam/RageProLT/kext/$B /tmp/$B; chown -R root:wheel /tmp/$B
U "stage: copied, running kextload"
( tail -f /var/log/system.log | while read l; do case "$l" in *Mach64*) U "klog: $l";; esac; done ) &
sleep 1
kextload -v /tmp/$B > /tmp/kl.out 2>&1
U "stage: kextload returned $?"
sleep 3
kextstat | grep -i mach64 > /tmp/ks.out; U "kextstat: $(cat /tmp/ks.out)"
kill %1 2>/dev/null
U "stage: end"

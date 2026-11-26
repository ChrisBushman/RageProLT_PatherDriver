#!/bin/sh
while true; do
  { date "+%H:%M:%S"; uptime; vm_stat | head -4; } >> /Users/sam/heartbeat.log
  sync
  sleep 3
done

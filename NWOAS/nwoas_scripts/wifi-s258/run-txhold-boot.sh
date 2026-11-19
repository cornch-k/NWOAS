#!/bin/bash
# restart into the default S259 launcher, then run-txhold.sh on the new boot:  bash wifi-s258/run-txhold-boot.sh <old .out> <old .link>
set -u
ROOT=/Users/angigyeom/Desktop/DEV/NWOAS/nwoas_scripts; T=$ROOT/wifi-s258/runs/boot-txh-$(date +%H%M%S).txt
(env NWOAS_AUDIO_PROT=moderate nohup bash $ROOT/wifi-s259/run-s259.sh $1 $2 > $T 2>&1 &)
for i in $(seq 1 240); do grep -a -q -E "S259 DONE|ABORT" $T && break; sleep 5; done
r=$(grep -a "S259 DONE" $T | tail -1); [ -n "$r" ] || { echo "BOOT FAIL"; tail -5 $T; exit 1; }
L=$(echo "$r" | sed -n 's/.*link=\([^ ]*\).*/\1/p'); echo "booted $r"; sleep 60
bash $ROOT/wifi-s258/run-txhold.sh $L

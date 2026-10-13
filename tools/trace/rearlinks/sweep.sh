#!/bin/sh
# Ledge frame set over a few start offsets and speeds: sweep.sh <vehicle> <LT> <UP>
cd "$(dirname "$0")/../../.."
v=$1; lt=$2; up=$3
for spd in ${SPDS:-2.2 2.5 2.8}; do
  for dx in ${DXS:-0 0.3 0.6}; do
    r=$(CZ_LT=$lt CZ_UP=$up SPD=$spd DX=$dx ./build/trace/rearlinks/trace ledge $v 2>&1 | grep "final frame set" | awk '{print $4}')
    printf "spd %s dx %s: %s   " $spd $dx "$r"
  done
  echo
done

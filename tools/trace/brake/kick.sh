#!/bin/sh
# kick.sh "extra opts": symmetric trucks (two panhards, items centred), yaw kick +-1 deg/s at brake onset
cd "$(dirname "$0")/../../../build/trace/brake" || exit 1
for v in 2 3 4; do
  for k in 0 1 -1; do
    a=$(CZ_PAN2=1 ./brake $v zero kick=$k $1 | tail -1)
    b=$(CZ_PAN2=1 ./brake $v zero noesc kick=$k $1 | tail -1)
    echo "$v kick $k: $a   noesc: $b"
  done
done

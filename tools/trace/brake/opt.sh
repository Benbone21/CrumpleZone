#!/bin/sh
# opt.sh VEH "opts" ... : per option set: kick response (symmetric truck), the real test, items mirrored, panhard mirrored
cd "$(dirname "$0")/../../../build/trace/brake" || exit 1
veh=$1; shift
for o in "$@"; do
  k=$(CZ_PAN2=1 ./brake $veh zero kick=1 $o | tail -1 | cut -c5-)
  a=$(./brake $veh $o | tail -1 | cut -c5-)
  m=$(./brake $veh mirror $o | tail -1 | cut -c13-18)
  p=$(CZ_PANMIR=1 ./brake $veh $o | tail -1 | cut -c13-18)
  echo "[$o] kick: $k | asis: $a | mirror $m panmir $p"
done

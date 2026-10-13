#!/bin/sh
# sens.sh VEH "opts" ... : heading for (as is / items zeroed / items mirrored / panhard mirrored) per option set
cd "$(dirname "$0")/../../../build/trace/brake" || exit 1
veh=$1; shift
for o in "$@"; do
  a=$(./brake $veh $o | tail -1 | awk '{print $3}')
  b=$(./brake $veh $o zero | tail -1 | awk '{print $3}')
  c=$(./brake $veh $o mirror | tail -1 | awk '{print $3}')
  d=$(CZ_PANMIR=1 ./brake $veh $o zero | tail -1 | awk '{print $3}')
  echo "[$o] asis $a zero $b mirror $c panmir+zero $d"
done

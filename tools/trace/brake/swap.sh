#!/bin/sh
# swap.sh VEH SRC field... : symmetric truck, +1 deg/s kick, one field taken from SRC at a time
cd "$(dirname "$0")/../../../build/trace/brake" || exit 1
veh=$1; src=$2; shift 2
echo "base: $(CZ_PAN2=1 ./brake $veh zero kick=1 | tail -1)"
for f in "$@"; do
  echo "$f: $(CZ_PAN2=1 ./brake $veh zero kick=1 take=$src:$f | tail -1)"
done

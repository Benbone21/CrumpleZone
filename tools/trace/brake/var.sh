#!/bin/sh
# var.sh VEH "ENV=x ..." ... : rebuild, then per env set: kick response (symmetric truck) and the real test
cd "$(dirname "$0")/../../.." || exit 1
nice make build/src/carbuild.o build/src/softbody.o 2>&1 | grep -E "error"
sh tools/trace/build.sh tools/trace/brake/brake.cpp || exit 1
cd build/trace/brake
veh=$1; shift
for e in "$@"; do
  k=$(env $e CZ_PAN2=1 ./brake $veh zero kick=1 | tail -1 | cut -c5-)
  a=$(env $e ./brake $veh | tail -1 | cut -c5-)
  echo "[$e] kick: $k | asis: $a"
done

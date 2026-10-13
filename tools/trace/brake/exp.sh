#!/bin/sh
# exp.sh VEH "ENV=1 ..." "opts" ...  : rebuild carbuild/softbody objects + tool, run variants
cd "$(dirname "$0")/../../.." || exit 1
nice make build/src/carbuild.o build/src/softbody.o 2>&1 | grep -E "error"
sh tools/trace/build.sh tools/trace/brake/brake.cpp || exit 1
cd build/trace/brake
veh=$1; shift
for envs in "$@"; do
  for o in "" zero; do
    echo "== $veh [$envs] $o: $(env $envs ./brake $veh $o | tail -1)"
  done
done

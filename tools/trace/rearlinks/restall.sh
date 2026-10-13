#!/bin/sh
# Rest spring deviation per vehicle in the catalog.
cd "$(dirname "$0")/../../.."
for v in 0 1 2 3 4 5 6 7; do
  ./build/trace/rearlinks/trace rest $v 2>/dev/null | grep " dev " | awk '{printf "%s %s %s  ", $1, $7, $8}'
  echo
done

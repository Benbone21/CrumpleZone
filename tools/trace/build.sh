#!/bin/sh
# Builds a trace tool against the game's physics: tools/trace/build.sh tools/trace/main/rltrace.cpp
# -> build/trace/main/rltrace. Run `make build/run_tests` first (it builds the objects linked here).
# Most tools take a vehicle index (vehicleCatalog(): 0 Civic, 1 Jesko, 2 Bronco, 3 4Runner,
# 4 rock buggy, 5 UTV, 6 Tacoma) and print a line per step or a summary; read the top of each.
cd "$(dirname "$0")/../.." || exit 1
src=$1
[ -f "$src" ] || { echo "usage: tools/trace/build.sh tools/trace/<folder>/<tool>.cpp"; exit 1; }
sub=$(basename "$(dirname "$src")")
out=build/trace/$sub/$(basename "$src" .cpp)
mkdir -p "build/trace/$sub"
PHYS=$(sed -n 's/^PHYS *:= //p' Makefile)
OBJS=$(for f in $PHYS; do echo build/${f%.cpp}.o; done)
nice -n 19 g++ -O2 -march=native -ffp-contract=off -std=c++17 -o "$out" "$src" $OBJS build/tests/harness.o -lpthread -lm && echo "$out"

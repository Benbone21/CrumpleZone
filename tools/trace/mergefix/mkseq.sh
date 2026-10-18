#!/bin/sh
# Builds seq_old (freeway tests before the trim fix) and seq_new (after). From the repo root.
PHYS=$(sed -n 's/^PHYS *:= //p' Makefile)
OBJS=$(for f in $PHYS; do echo build/${f%.cpp}.o; done)
S=tools/trace/mergefix; D=build/trace/mergefix; mkdir -p $D
nice -n 19 g++ -O2 -march=native -ffp-contract=off -std=c++17 -o $D/seq_old $S/seq.cpp $S/freeway_old.cpp $OBJS build/tests/harness.o -lpthread -lm || exit 1
nice -n 19 g++ -O2 -march=native -ffp-contract=off -std=c++17 -o $D/seq_new $S/seq.cpp build/tests/freeway.o $OBJS build/tests/harness.o -lpthread -lm

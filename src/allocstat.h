#pragma once
// Heap allocation counters (global operator new is replaced in allocstat.cpp). For hitch hunting and the per-frame allocation budgets:
// read count() before and after a frame. trace(true) also records the call stacks (glibc only); dump() prints the top sites.
#include <cstdint>
#include <cstdio>

namespace allocstat {
uint64_t count();        // allocations made by this thread so far
uint64_t countAll();     // ... by all threads
void trace(bool on);     // record call stacks while on (slow: for the measuring tool, not the game)
void dump(FILE* f, int top);  // the most frequent stacks recorded, symbolized when the platform can
}

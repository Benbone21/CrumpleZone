// SIMD kernels for the soft-body solver's inner loops. Every variant (AVX2, the build's
// baseline ISA, plain scalar) does the same IEEE operations in the same order and is built
// without fused multiply-add, so they agree to the last bit: clients on different CPUs
// simulate identically, and a SIMD path never changes a result.
#pragma once
#include <cstdint>
#include "softbody.h"

// Per-beam geometry, one entry per beam in beam order (broken beams included; ignore theirs):
// length, unit direction a->b and the closing speed along it (b's velocity relative to a's).
struct BeamGeo {
    double *L, *ux, *uy, *uz, *vrel;
};

void beamGeometry(const Beam* beams, int count, const Node* nodes, const BeamGeo& out);

// Which implementation runs: 0 portable (the build's baseline ISA), 1 AVX2. Starts on the best
// the CPU has; tests force a level to check they agree. Returns the level in effect.
int simdLevel();
int simdSetLevel(int level);
const char* simdName();

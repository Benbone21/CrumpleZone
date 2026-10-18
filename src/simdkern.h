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

// Per face, by face id: first corner a, unit normal, edges e0 = b - a and e1 = c - a, their dots
// d00 d01 d11 and den = d00 d11 - d01^2 (Tri's own collision geometry).
struct FaceGeo {
    const double *ax, *ay, *az, *nx, *ny, *nz, *e0x, *e0y, *e0z, *e1x, *e1y, *e1z, *d00, *d01, *d11, *den;
    static FaceGeo rows(const double* g, size_t n) {  // 16 rows of n, in that order
        return {g, g + n, g + 2 * n, g + 3 * n, g + 4 * n, g + 5 * n, g + 6 * n, g + 7 * n,
                g + 8 * n, g + 9 * n, g + 10 * n, g + 11 * n, g + 12 * n, g + 13 * n, g + 14 * n, g + 15 * n};
    }
};

// Of the faces set in cand (a bitset of words words over the rows), those a point at p (of this
// radius) can be pushed out through, whatever it has met already: d = dot(p - a, nrm) with
// d >= -0.4 and radius - d > 0, den not under 1e-12, and barycentric u, v, w none under -0.02
// (each test passes NaN, as the scalar contact test does). Written to keep, the same shape.
// Rows must hold 64 * words entries.
void faceCandidates(const FaceGeo& f, int words, const uint64_t* cand, const V3& p, double radius, uint64_t* keep);

// Which implementation runs: 0 portable (the build's baseline ISA), 1 AVX2. Starts on the best
// the CPU has; tests force a level to check they agree. Returns the level in effect.
int simdLevel();
int simdSetLevel(int level);
const char* simdName();

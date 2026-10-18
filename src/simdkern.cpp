#include "simdkern.h"

#include <atomic>
#include <cstring>

#if defined(__x86_64__) || defined(__i386__)
#include <immintrin.h>
#define CZ_X86 1
#endif

// The variants must round alike: no fused multiply-add, whatever -march the build uses.
#if defined(__clang__)
#pragma clang fp contract(off)
#elif defined(__GNUC__)
#pragma GCC optimize("fp-contract=off")
#endif

typedef double v4d __attribute__((vector_size(32)));

namespace {

// The build's baseline ISA (SSE2 on the releases, NEON on the Mac): the compiler splits each
// 4-wide op into the narrower registers.
#define KERN_NAME geometryBase
#define KERN_ATTR
#define KERN_SQRT(v) v4d{__builtin_sqrt((v)[0]), __builtin_sqrt((v)[1]), __builtin_sqrt((v)[2]), __builtin_sqrt((v)[3])}
#include "simdkern.inc"
#undef KERN_NAME
#undef KERN_ATTR
#undef KERN_SQRT

#ifdef CZ_X86
#define KERN_NAME geometryAvx2
#define KERN_ATTR __attribute__((target("avx2")))
#define KERN_SQRT(v) (v4d)_mm256_sqrt_pd((__m256d)(v))
#include "simdkern.inc"
#undef KERN_NAME
#undef KERN_ATTR
#undef KERN_SQRT
#endif

int bestLevel() {
#ifdef CZ_X86
    __builtin_cpu_init();
    if (__builtin_cpu_supports("avx2")) return 1;
#endif
    return 0;
}
// (Atomic only because a test flips it while other scenarios run: every level gives the same bits.)
std::atomic<int> level{bestLevel()};

}  // namespace

int simdLevel() { return level.load(std::memory_order_relaxed); }

int simdSetLevel(int want) {
    const int now = want >= 1 && bestLevel() >= 1 ? 1 : 0;
    level.store(now, std::memory_order_relaxed);
    return now;
}

const char* simdName() { return simdLevel() ? "AVX2" : "baseline"; }

void beamGeometry(const Beam* beams, int count, const Node* nodes, const BeamGeo& out) {
#ifdef CZ_X86
    if (simdLevel()) return geometryAvx2(beams, count, nodes, out);
#endif
    geometryBase(beams, count, nodes, out);
}

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

// The contact test's own arithmetic, in its order (mathx's dot sums (x + y) + z).
inline bool faceNear(int t, const FaceGeo& f, const V3& p, double radius) {
    const double qx = p.x - f.ax[t], qy = p.y - f.ay[t], qz = p.z - f.az[t];
    const double d = qx * f.nx[t] + qy * f.ny[t] + qz * f.nz[t];
    return !(radius - d <= 0 || d < -0.4);
}
inline bool faceOver(int t, const FaceGeo& f, const V3& p) {
    const double qx = p.x - f.ax[t], qy = p.y - f.ay[t], qz = p.z - f.az[t];
    const double d20 = qx * f.e0x[t] + qy * f.e0y[t] + qz * f.e0z[t];
    const double d21 = qx * f.e1x[t] + qy * f.e1y[t] + qz * f.e1z[t];
    const double den = f.den[t];
    const double v = (f.d11[t] * d20 - f.d01[t] * d21) / den, w = (f.d00[t] * d21 - f.d01[t] * d20) / den, u = 1 - v - w;
    return !(den < 1e-12 || u < -0.02 || v < -0.02 || w < -0.02);
}

void faceCandidatesBase(const FaceGeo& f, int words, const uint64_t* cand, const V3& p, double radius, uint64_t* keep) {
    for (int w = 0; w < words; w++) {
        uint64_t k = 0;
        for (uint64_t m = cand[w]; m; m &= m - 1) {
            const int b = __builtin_ctzll(m), t = 64 * w + b;
            if (faceNear(t, f, p, radius) && faceOver(t, f, p)) k |= 1ull << b;
        }
        keep[w] = k;
    }
}

#ifdef CZ_X86
// Four faces at a time, straight from the rows: only the blocks holding a candidate.
__attribute__((target("avx2"))) void faceCandidatesAvx2(const FaceGeo& f, int words, const uint64_t* cand, const V3& p,
                                                        double radius, uint64_t* keep) {
    const __m256d px = _mm256_set1_pd(p.x), py = _mm256_set1_pd(p.y), pz = _mm256_set1_pd(p.z);
    const __m256d r = _mm256_set1_pd(radius), zero = _mm256_setzero_pd(), back = _mm256_set1_pd(-0.4);
    const __m256d one = _mm256_set1_pd(1), tiny = _mm256_set1_pd(1e-12), slack = _mm256_set1_pd(-0.02);
    auto dot3 = [](__m256d x, __m256d y, __m256d z, const double* a, const double* b, const double* c) {
        return _mm256_add_pd(_mm256_add_pd(_mm256_mul_pd(x, _mm256_loadu_pd(a)), _mm256_mul_pd(y, _mm256_loadu_pd(b))),
                             _mm256_mul_pd(z, _mm256_loadu_pd(c)));
    };
    for (int w = 0; w < words; w++) {
        uint64_t k = 0;
        for (uint64_t m = cand[w]; m;) {
            const int b = __builtin_ctzll(m) & ~3, t = 64 * w + b;
            const int lanes = (int)(m >> b & 15);
            m &= ~(15ull << b);
            const __m256d qx = _mm256_sub_pd(px, _mm256_loadu_pd(f.ax + t)), qy = _mm256_sub_pd(py, _mm256_loadu_pd(f.ay + t)),
                          qz = _mm256_sub_pd(pz, _mm256_loadu_pd(f.az + t));
            const __m256d d = dot3(qx, qy, qz, f.nx + t, f.ny + t, f.nz + t);
            const __m256d far = _mm256_or_pd(_mm256_cmp_pd(_mm256_sub_pd(r, d), zero, _CMP_LE_OQ), _mm256_cmp_pd(d, back, _CMP_LT_OQ));
            const int near = lanes & ~_mm256_movemask_pd(far);
            const __m256d d20 = dot3(qx, qy, qz, f.e0x + t, f.e0y + t, f.e0z + t);
            const __m256d d21 = dot3(qx, qy, qz, f.e1x + t, f.e1y + t, f.e1z + t);
            const __m256d d00 = _mm256_loadu_pd(f.d00 + t), d01 = _mm256_loadu_pd(f.d01 + t), d11 = _mm256_loadu_pd(f.d11 + t),
                          den = _mm256_loadu_pd(f.den + t);
            const __m256d v = _mm256_div_pd(_mm256_sub_pd(_mm256_mul_pd(d11, d20), _mm256_mul_pd(d01, d21)), den);
            const __m256d ww = _mm256_div_pd(_mm256_sub_pd(_mm256_mul_pd(d00, d21), _mm256_mul_pd(d01, d20)), den);
            const __m256d u = _mm256_sub_pd(_mm256_sub_pd(one, v), ww);
            const __m256d off = _mm256_or_pd(_mm256_or_pd(_mm256_cmp_pd(den, tiny, _CMP_LT_OQ), _mm256_cmp_pd(u, slack, _CMP_LT_OQ)),
                                             _mm256_or_pd(_mm256_cmp_pd(v, slack, _CMP_LT_OQ), _mm256_cmp_pd(ww, slack, _CMP_LT_OQ)));
            k |= (uint64_t)(near & ~_mm256_movemask_pd(off)) << b;
        }
        keep[w] = k;
    }
}
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

void faceCandidates(const FaceGeo& f, int words, const uint64_t* cand, const V3& p, double radius, uint64_t* keep) {
#ifdef CZ_X86
    if (simdLevel()) return faceCandidatesAvx2(f, words, cand, p, radius, keep);
#endif
    faceCandidatesBase(f, words, cand, p, radius, keep);
}

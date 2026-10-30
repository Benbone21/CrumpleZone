#pragma once
// Deterministic transcendental functions: the same bits on every machine and compiler (docs/plans/xplat-world.md).
//
// The C library's sin, cos, exp, log, pow, atan2, hypot... are not correctly rounded, and glibc (Linux), the Windows CRT / mingw-w64
// and Apple's libm each round some results differently by an ulp. Where such a result decides something about the world (a rock in
// or out, which scan a rock is, a slope over a threshold) one ulp moves the thing, and a guest's world isn't the host's. World
// generation uses these instead: only + - * / and sqrt (which IEEE 754 rounds the same everywhere) and exact bit operations, built
// with -ffp-contract=off (no FMA fusing).
//
// sin, cos, atan, atan2, exp and log are fdlibm's (Sun Microsystems' freely distributable libm, the one Java's StrictMath uses for
// the same reason): under 1 ulp from the true value, so they agree with glibc on nearly every argument. hypot is double-double and
// rounds correctly but for a vanishing share of arguments. pow, tan, asin, acos, tanh and cbrt are built from those.
//
// fdlibm: Copyright (C) 1993 by Sun Microsystems, Inc. All rights reserved. Developed at SunSoft, a Sun Microsystems, Inc. business.
// Permission to use, copy, modify, and distribute this software is freely granted, provided that this notice is preserved.
#include <cmath>
#include <cstdint>
#include <cstring>
#include <utility>

namespace dm {

namespace detail {
inline uint64_t bits(double x) {
    uint64_t u;
    std::memcpy(&u, &x, sizeof u);
    return u;
}
inline double fromBits(uint64_t u) {
    double x;
    std::memcpy(&x, &u, sizeof x);
    return x;
}
inline int32_t hi(double x) { return (int32_t)(bits(x) >> 32); }
inline uint32_t lo(double x) { return (uint32_t)bits(x); }
inline double withHi(double x, int32_t h) { return fromBits(((uint64_t)(uint32_t)h << 32) | (bits(x) & 0xffffffffull)); }
inline double fromWords(int32_t h, uint32_t l) { return fromBits(((uint64_t)(uint32_t)h << 32) | l); }
// x * 2^k, exactly (by the exponent bits; k within the normal range of the result, else std::ldexp, which is exact everywhere too).
inline double scale2(double x, int k) { return std::ldexp(x, k); }

// fdlibm __kernel_sin / __kernel_cos on [-pi/4, pi/4]; y is the tail of x.
inline double kSin(double x, double y, int iy) {
    const double S1 = -1.66666666666666324348e-01, S2 = 8.33333333332248946124e-03, S3 = -1.98412698298579493134e-04,
                 S4 = 2.75573137070700676789e-06, S5 = -2.50507602534068634195e-08, S6 = 1.58969099521155010221e-10;
    const double z = x * x, v = z * x, r = S2 + z * (S3 + z * (S4 + z * (S5 + z * S6)));
    if (iy == 0) return x + v * (S1 + z * r);
    return x - ((z * (0.5 * y - v * r) - y) - v * S1);
}
inline double kCos(double x, double y) {
    const double C1 = 4.16666666666666019037e-02, C2 = -1.38888888888741095749e-03, C3 = 2.48015872894767294178e-05,
                 C4 = -2.75573143513906633035e-07, C5 = 2.08757232129817482790e-09, C6 = -1.13596475577881948265e-11;
    const double z = x * x, w = z * z;
    const double r = z * (C1 + z * (C2 + z * C3)) + w * w * (C4 + z * (C5 + z * C6));
    const double hz = 0.5 * z, ww = 1.0 - hz;
    return ww + (((1.0 - ww) - hz) + (z * r - x * y));
}
// x reduced by pi/2: x = n pi/2 + (y0 + y1), |y0 + y1| <= pi/4 (fdlibm __ieee754_rem_pio2's medium-size path). Past 2^20 pi/2 (1.6e6)
// the reduction is only approximate (a fmod by 2 pi first), but the same everywhere.
inline int remPio2(double x, double& y0, double& y1) {
    const double invpio2 = 6.36619772367581382433e-01, pio2_1 = 1.57079632673412561417e+00, pio2_1t = 6.07710050650619224932e-11,
                 pio2_2 = 6.07710050630396597660e-11, pio2_2t = 2.02226624879595063154e-21, pio2_3 = 2.02226624871116645580e-21,
                 pio2_3t = 8.47842766036889956997e-32;
    if (!(std::fabs(x) <= 1.6470993291652855e+06)) {  // (2^20 pi/2)
        if (std::isnan(x) || std::isinf(x)) {
            y0 = y1 = x - x;
            return 0;
        }
        x = std::fmod(x, 6.28318530717958647692);  // (exact)
    }
    const double fn = std::nearbyint(x * invpio2);  // (to nearest, ties even: the default rounding everywhere)
    const int n = (int)fn;
    double r = x - fn * pio2_1, w = fn * pio2_1t;
    const int j = (hi(x) >> 20) & 0x7ff;
    y0 = r - w;
    int i = j - ((hi(y0) >> 20) & 0x7ff);
    if (i > 16) {  // (cancellation: a second, and maybe a third, iteration)
        double t = r;
        w = fn * pio2_2;
        r = t - w;
        w = fn * pio2_2t - ((t - r) - w);
        y0 = r - w;
        i = j - ((hi(y0) >> 20) & 0x7ff);
        if (i > 49) {
            t = r;
            w = fn * pio2_3;
            r = t - w;
            w = fn * pio2_3t - ((t - r) - w);
            y0 = r - w;
        }
    }
    y1 = (r - y0) - w;
    return n;
}

// Exact products and sums (Dekker, Knuth): a*b = p + e, a+b = s + e.
inline void twoProd(double a, double b, double& p, double& e) {
    const double sp = 134217729.0;  // 2^27 + 1
    const double ca = sp * a, ah = ca - (ca - a), al = a - ah;
    const double cb = sp * b, bh = cb - (cb - b), bl = b - bh;
    p = a * b;
    e = ((ah * bh - p) + ah * bl + al * bh) + al * bl;
}
inline void twoSum(double a, double b, double& s, double& e) {
    s = a + b;
    const double bb = s - a;
    e = (a - (s - bb)) + (b - bb);
}
}  // namespace detail

inline double sin(double x) {
    using namespace detail;
    const int32_t ix = hi(x) & 0x7fffffff;
    if (ix <= 0x3fe921fb) {  // |x| ~<= pi/4
        if (ix < 0x3e400000) return x;  // |x| < 2^-27
        return kSin(x, 0.0, 0);
    }
    double y0, y1;
    const int n = remPio2(x, y0, y1);
    switch (n & 3) {
        case 0: return kSin(y0, y1, 1);
        case 1: return kCos(y0, y1);
        case 2: return -kSin(y0, y1, 1);
        default: return -kCos(y0, y1);
    }
}
inline double cos(double x) {
    using namespace detail;
    const int32_t ix = hi(x) & 0x7fffffff;
    if (ix <= 0x3fe921fb) {
        if (ix < 0x3e46a09e) return 1.0;  // |x| < 2^-27 * sqrt(2)
        return kCos(x, 0.0);
    }
    double y0, y1;
    const int n = remPio2(x, y0, y1);
    switch (n & 3) {
        case 0: return kCos(y0, y1);
        case 1: return -kSin(y0, y1, 1);
        case 2: return -kCos(y0, y1);
        default: return kSin(y0, y1, 1);
    }
}
inline double tan(double x) { return dm::sin(x) / dm::cos(x); }

inline double atan(double x) {
    using namespace detail;
    static const double atanhi[] = {4.63647609000806093515e-01, 7.85398163397448278999e-01, 9.82793723247329054082e-01,
                                    1.57079632679489655800e+00};
    static const double atanlo[] = {2.26987774529616870924e-17, 3.06161699786838301793e-17, 1.39033110312309984516e-17,
                                    6.12323399573676603587e-17};
    static const double aT[] = {3.33333333333329318027e-01,  -1.99999999998764832476e-01, 1.42857142725034663711e-01,
                                -1.11111104054623557880e-01, 9.09088713343650656196e-02,  -7.69187620504482999495e-02,
                                6.66107313738753120669e-02,  -5.83357013379057348645e-02, 4.97687799461593236017e-02,
                                -3.65315727442169155270e-02, 1.62858201153657823623e-02};
    const int32_t hx = hi(x), ix = hx & 0x7fffffff;
    int id;
    if (ix >= 0x44100000) {  // |x| >= 2^66
        if (std::isnan(x)) return x + x;
        return hx > 0 ? atanhi[3] + atanlo[3] : -atanhi[3] - atanlo[3];
    }
    if (ix < 0x3fdc0000) {  // |x| < 0.4375
        if (ix < 0x3e400000) return x;  // |x| < 2^-27
        id = -1;
    } else {
        x = std::fabs(x);
        if (ix < 0x3ff30000) {  // |x| < 1.1875
            if (ix < 0x3fe60000) id = 0, x = (2.0 * x - 1.0) / (2.0 + x);  // 7/16 <= |x| < 11/16
            else id = 1, x = (x - 1.0) / (x + 1.0);                        // 11/16 <= |x| < 19/16
        } else {
            if (ix < 0x40038000) id = 2, x = (x - 1.5) / (1.0 + 1.5 * x);  // |x| < 2.4375
            else id = 3, x = -1.0 / x;
        }
    }
    const double z = x * x, w = z * z;
    const double s1 = z * (aT[0] + w * (aT[2] + w * (aT[4] + w * (aT[6] + w * (aT[8] + w * aT[10])))));
    const double s2 = w * (aT[1] + w * (aT[3] + w * (aT[5] + w * (aT[7] + w * aT[9]))));
    if (id < 0) return x - x * (s1 + s2);
    const double r = atanhi[id] - ((x * (s1 + s2) - atanlo[id]) - x);
    return hx < 0 ? -r : r;
}
inline double atan2(double y, double x) {
    using namespace detail;
    const double pi_o_4 = 7.8539816339744827900E-01, pi_o_2 = 1.5707963267948965580E+00, pi = 3.1415926535897931160E+00,
                 pi_lo = 1.2246467991473531772E-16;
    if (std::isnan(x) || std::isnan(y)) return x + y;
    if (x == 1.0) return dm::atan(y);
    const int32_t hx = hi(x), hy = hi(y), ix = hx & 0x7fffffff, iy = hy & 0x7fffffff;
    const int m = ((hy >> 31) & 1) | ((hx >> 30) & 2);  // 2 * sign(x) + sign(y)
    if (y == 0) {
        switch (m) {
            case 0:
            case 1: return y;
            case 2: return pi;
            default: return -pi;
        }
    }
    if (x == 0) return hy < 0 ? -pi_o_2 : pi_o_2;
    if (std::isinf(x)) {
        if (std::isinf(y)) {
            switch (m) {
                case 0: return pi_o_4;
                case 1: return -pi_o_4;
                case 2: return 3.0 * pi_o_4;
                default: return -3.0 * pi_o_4;
            }
        }
        switch (m) {
            case 0: return 0.0;
            case 1: return -0.0;
            case 2: return pi;
            default: return -pi;
        }
    }
    if (std::isinf(y)) return hy < 0 ? -pi_o_2 : pi_o_2;
    const int k = (iy - ix) >> 20;
    double z;
    int mm = m;
    if (k > 60) z = pi_o_2 + 0.5 * pi_lo, mm &= 1;  // |y/x| > 2^60
    else if (hx < 0 && k < -60) z = 0.0;              // |y|/x < -2^60
    else z = dm::atan(std::fabs(y / x));
    switch (mm) {
        case 0: return z;
        case 1: return -z;
        case 2: return pi - (z - pi_lo);
        default: return (z - pi_lo) - pi;
    }
}

inline double exp(double x) {
    using namespace detail;
    const double halF[2] = {0.5, -0.5}, o_threshold = 7.09782712893383973096e+02, u_threshold = -7.45133219101941108420e+02,
                 ln2HI[2] = {6.93147180369123816490e-01, -6.93147180369123816490e-01},
                 ln2LO[2] = {1.90821492927058770002e-10, -1.90821492927058770002e-10}, invln2 = 1.44269504088896338700e+00,
                 P1 = 1.66666666666666019037e-01, P2 = -2.77777777770155933842e-03, P3 = 6.61375632143793436117e-05,
                 P4 = -1.65339022054652515390e-06, P5 = 4.13813679705723846039e-08;
    if (std::isnan(x)) return x + x;
    if (x > o_threshold) return HUGE_VAL;
    if (x < u_threshold) return 0.0;
    const int32_t hx = hi(x) & 0x7fffffff;
    const int xsb = (hi(x) >> 31) & 1;
    double hi_ = 0, lo_ = 0;
    int k = 0;
    if (hx > 0x3fd62e42) {    // |x| > 0.5 ln2
        if (hx < 0x3ff0a2b2) {  // |x| < 1.5 ln2
            hi_ = x - ln2HI[xsb], lo_ = ln2LO[xsb], k = 1 - xsb - xsb;
        } else {
            k = (int)(invln2 * x + halF[xsb]);
            const double t = k;
            hi_ = x - t * ln2HI[0], lo_ = t * ln2LO[0];
        }
        x = hi_ - lo_;
    } else if (hx < 0x3e300000) {  // |x| < 2^-28
        return 1.0 + x;
    }
    const double t = x * x, c = x - t * (P1 + t * (P2 + t * (P3 + t * (P4 + t * P5))));
    if (k == 0) return 1.0 - ((x * c) / (c - 2.0) - x);
    const double y = 1.0 - ((lo_ - (x * c) / (2.0 - c)) - hi_);
    return scale2(y, k);
}

// log, rounded correctly but for a vanishing share of arguments (an error under 2^-70 before the one rounding): so it gives what
// glibc's (correctly rounded in practice) gives. x = 2^k m, m in [sqrt(2)/2, sqrt(2)); log m = 2 atanh(s), s = (m - 1) / (m + 1),
// in double-double.
inline double log(double x) {
    using namespace detail;
    int32_t hx = hi(x);
    const uint32_t lx = lo(x);
    int k = 0;
    if (hx < 0x00100000) {  // x < 2^-1022
        if (((hx & 0x7fffffff) | lx) == 0) return -HUGE_VAL;
        if (hx < 0) return (x - x) / 0.0;  // (NaN)
        k -= 54, x *= 1.80143985094819840000e+16, hx = hi(x);
    }
    if (hx >= 0x7ff00000) return x + x;
    if (x == 1.0) return 0.0;
    k += (hx >> 20) - 1023;
    hx &= 0x000fffff;
    const int32_t i = (hx + 0x95f64) & 0x100000;
    const double m = withHi(x, hx | (i ^ 0x3ff00000));
    k += (i >> 20);
    const double f = m - 1.0;  // (exact)
    // s = f / (2 + f), double-double.
    double dh, dl;
    twoSum(2.0, f, dh, dl);
    const double sh = f / dh;
    double p, pe;
    twoProd(sh, dh, p, pe);
    const double sl = (((f - p) - pe) - sh * dl) / dh;
    // z = s^2
    double zh, zl;
    twoProd(sh, sh, zh, zl);
    zl += 2.0 * sh * sl;
    {
        const double t = zh + zl;
        zl = zl - (t - zh), zh = t;
    }
    // 1/3 + z (1/5 + z R), R = 1/7 + z/9 + ...: the first two terms double-double, the rest (under 1.3e-4 of the whole) double.
    const double z = zh;
    double R = 1.0 / 29;
    for (int n = 27; n >= 7; n -= 2) R = 1.0 / n + z * R;
    auto ddMul = [](double ah, double al, double bh, double bl, double& oh, double& ol) {
        double q, qe;
        twoProd(ah, bh, q, qe);
        qe += ah * bl + al * bh;
        oh = q + qe, ol = qe - (oh - q);
    };
    auto ddAdd = [](double ah, double al, double bh, double bl, double& oh, double& ol) {
        double q, qe;
        twoSum(ah, bh, q, qe);
        qe += al + bl;
        oh = q + qe, ol = qe - (oh - q);
    };
    auto ddRecip = [](double n, double& oh, double& ol) {  // 1 / n
        oh = 1.0 / n;
        double q, qe;
        twoProd(oh, n, q, qe);
        ol = ((1.0 - q) - qe) / n;
    };
    double th, tl, c5h, c5l, c3h, c3l;
    ddMul(zh, zl, R, 0.0, th, tl);  // z R
    ddRecip(5.0, c5h, c5l);
    ddAdd(c5h, c5l, th, tl, th, tl);  // 1/5 + z R
    ddMul(zh, zl, th, tl, th, tl);    // z (...)
    ddRecip(3.0, c3h, c3l);
    ddAdd(c3h, c3l, th, tl, th, tl);  // P = 1/3 + ...
    ddMul(zh, zl, th, tl, th, tl);    // z P
    ddAdd(1.0, 0.0, th, tl, th, tl);  // 1 + z P
    double Lh, Ll;
    ddMul(2.0 * sh, 2.0 * sl, th, tl, Lh, Ll);  // log m = 2 s (1 + z P)
    if (k == 0) return Lh + Ll;
    const double ln2h = 6.931471805599452862e-01, ln2l = 2.319046813846299558e-17;
    double kh, kl;
    twoProd((double)k, ln2h, kh, kl);
    kl += (double)k * ln2l;
    double outH, outL;
    ddAdd(kh, kl, Lh, Ll, outH, outL);
    return outH + outL;
}

// sqrt(x^2 + y^2), rounded correctly but for a vanishing share of arguments (the sum of squares is exact, double-double).
inline double hypot(double x, double y) {
    using namespace detail;
    x = std::fabs(x), y = std::fabs(y);
    if (std::isinf(x) || std::isinf(y)) return HUGE_VAL;
    if (std::isnan(x) || std::isnan(y)) return x + y;
    if (x < y) std::swap(x, y);
    if (y == 0) return x;
    // (Scaled into a safe range by a power of two, exactly, so the squares neither overflow nor go subnormal.)
    int e = 0;
    if (x > 0x1p500) e = 600, x = scale2(x, -600), y = scale2(y, -600);
    else if (x < 0x1p-500) e = -600, x = scale2(x, 600), y = scale2(y, 600);
    double xx, xe, yy, ye, s, se;
    twoProd(x, x, xx, xe);
    twoProd(y, y, yy, ye);
    twoSum(xx, yy, s, se);
    se += xe + ye;
    const double sh = s + se, sl = se - (sh - s);  // (renormalised: sh + sl)
    const double r = std::sqrt(sh);
    double rr, re;
    twoProd(r, r, rr, re);
    const double corr = (((sh - rr) - re) + sl) / (2.0 * r);
    const double out = r + corr;
    return e ? scale2(out, e) : out;
}

// x^y. Whole y up to 64: by squaring in double-double, rounded once (correct but for a vanishing share); y = 0.5: sqrt; else
// exp(y log x), the log's tail carried in double-double (a few ulps at worst, the same everywhere).
inline double pow(double x, double y) {
    using namespace detail;
    if (y == 0) return 1.0;
    if (x == 1.0) return 1.0;
    if (std::isnan(x) || std::isnan(y)) return x + y;
    if (y == 1.0) return x;
    if (y == 2.0) return x * x;
    if (y == 0.5 && x >= 0 && !std::isinf(x)) return std::sqrt(x);
    const bool whole = std::fabs(y) <= 64 && y == std::floor(y);
    if (whole) {
        int n = (int)std::fabs(y);
        double rh = 1, rl = 0, bh = x, bl = 0;
        while (n) {
            if (n & 1) {  // r *= b (double-double)
                double p, e;
                twoProd(rh, bh, p, e);
                e += rh * bl + rl * bh;
                rh = p + e, rl = e - (rh - p);
            }
            n >>= 1;
            if (n) {
                double p, e;
                twoProd(bh, bh, p, e);
                e += 2.0 * bh * bl;
                bh = p + e, bl = e - (bh - p);
            }
        }
        const double v = rh + rl;
        return y < 0 ? 1.0 / v : v;
    }
    if (x < 0) return (x - x) / (x - x);  // (a negative base to a fractional power: NaN)
    if (x == 0) return y > 0 ? 0.0 : HUGE_VAL;
    if (std::isinf(x)) return y > 0 ? HUGE_VAL : 0.0;
    // log x = l + (x - exp(l)) / exp(l), to first order: the tail of the log, so y log x is good to the last bits.
    const double l = dm::log(x);
    const double el = dm::exp(l);
    const double lt = (x - el) / el;
    double p, pe;
    twoProd(y, l, p, pe);
    pe += y * lt;
    const double t = p + pe, tl = pe - (t - p);
    const double v = dm::exp(t);
    return v + v * tl;
}

inline double asin(double x) { return dm::atan2(x, std::sqrt((1.0 - x) * (1.0 + x))); }
inline double acos(double x) { return dm::atan2(std::sqrt((1.0 - x) * (1.0 + x)), x); }
inline double tanh(double x) {
    if (std::fabs(x) > 22) return x > 0 ? 1.0 : -1.0;
    if (std::fabs(x) < 0x1p-28) return x;
    // expm1(2x) by Kahan's trick ((e - 1) u / log e: no cancellation near 0), then tanh = expm1 / (expm1 + 2).
    const double u = 2.0 * x, e = dm::exp(u);
    const double em1 = e == 1.0 ? u : (e - 1.0) * u / dm::log(e);
    return em1 / (em1 + 2.0);
}
inline double cbrt(double x) {
    if (x == 0 || std::isnan(x) || std::isinf(x)) return x;
    const double a = std::fabs(x);
    int e;
    const double m = std::frexp(a, &e);  // a = m 2^e, m in [0.5, 1)  (exact)
    int q = e / 3, r = e % 3;
    if (r < 0) r += 3, q -= 1;
    double t = std::ldexp(m, r);  // in [0.5, 4): cbrt(a) = cbrt(t) 2^q
    double c = 0.7 + 0.3 * t;     // (a start, then Newton: cubic convergence is not needed, six steps are plenty)
    for (int i = 0; i < 6; i++) c = c - (c * c * c - t) / (3.0 * c * c);
    const double out = std::ldexp(c, q);
    return x < 0 ? -out : out;
}

// Rodrigues rotation of v about the unit axis k (mathx.h's rotateAxis, with these sin and cos).
template <class V>
inline V rotateAxis(const V& v, const V& k, double ang) {
    const double c = dm::cos(ang), s = dm::sin(ang);
    return v * c + cross(k, v) * s + k * (dot(k, v) * (1 - c));
}

// float arguments (as std:: has them): worked out in double, rounded to float.
inline float sin(float x) { return (float)dm::sin((double)x); }
inline float cos(float x) { return (float)dm::cos((double)x); }
inline float tan(float x) { return (float)dm::tan((double)x); }
inline float atan(float x) { return (float)dm::atan((double)x); }
inline float atan2(float y, float x) { return (float)dm::atan2((double)y, (double)x); }
inline float exp(float x) { return (float)dm::exp((double)x); }
inline float log(float x) { return (float)dm::log((double)x); }
inline float hypot(float x, float y) { return (float)dm::hypot((double)x, (double)y); }
inline float pow(float x, float y) { return (float)dm::pow((double)x, (double)y); }
inline float asin(float x) { return (float)dm::asin((double)x); }
inline float acos(float x) { return (float)dm::acos((double)x); }
inline float tanh(float x) { return (float)dm::tanh((double)x); }
inline float cbrt(float x) { return (float)dm::cbrt((double)x); }
// (Mixed and integer arguments go to the double ones.)
template <class A, class B>
inline double pow(A x, B y) { return dm::pow((double)x, (double)y); }
template <class A, class B>
inline double atan2(A y, B x) { return dm::atan2((double)y, (double)x); }
template <class A, class B>
inline double hypot(A x, B y) { return dm::hypot((double)x, (double)y); }

}  // namespace dm

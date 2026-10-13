#pragma once
#include <algorithm>
#include <cmath>
#include <cstdint>

// Double-precision vector for physics (positions far from the origin stay precise).
struct V3 {
    double x = 0, y = 0, z = 0;
    V3() {}
    V3(double x_, double y_, double z_) : x(x_), y(y_), z(z_) {}
    V3 operator+(const V3& o) const { return {x + o.x, y + o.y, z + o.z}; }
    V3 operator-(const V3& o) const { return {x - o.x, y - o.y, z - o.z}; }
    V3 operator*(double s) const { return {x * s, y * s, z * s}; }
    V3 operator/(double s) const { return {x / s, y / s, z / s}; }
    V3 operator-() const { return {-x, -y, -z}; }
    V3& operator+=(const V3& o) { x += o.x; y += o.y; z += o.z; return *this; }
    V3& operator-=(const V3& o) { x -= o.x; y -= o.y; z -= o.z; return *this; }
    V3& operator*=(double s) { x *= s; y *= s; z *= s; return *this; }
};

inline double dot(const V3& a, const V3& b) { return a.x * b.x + a.y * b.y + a.z * b.z; }
inline V3 cross(const V3& a, const V3& b) {
    return {a.y * b.z - a.z * b.y, a.z * b.x - a.x * b.z, a.x * b.y - a.y * b.x};
}
inline double len(const V3& a) { return std::sqrt(dot(a, a)); }
inline V3 norm(const V3& a) {
    double l = len(a);
    return l > 1e-12 ? a / l : V3(0, 0, 0);
}
// Rodrigues rotation of v about unit axis k.
inline V3 rotateAxis(const V3& v, const V3& k, double ang) {
    double c = std::cos(ang), s = std::sin(ang);
    return v * c + cross(k, v) * s + k * (dot(k, v) * (1 - c));
}
inline double clampd(double v, double lo, double hi) { return v < lo ? lo : (v > hi ? hi : v); }
inline double smoothstep(double a, double b, double x) {
    double t = clampd((x - a) / (b - a), 0, 1);
    return t * t * (3 - 2 * t);
}
inline double wrapAngle(double a) {
    while (a > M_PI) a -= 2 * M_PI;
    while (a < -M_PI) a += 2 * M_PI;
    return a;
}

// Unit quaternion (rotations for the chassis frame fit).
struct Quat {
    double w = 1, x = 0, y = 0, z = 0;
    V3 rotate(const V3& v) const {
        V3 q(x, y, z);
        V3 t = cross(q, v) * 2.0;
        return v + t * w + cross(q, t);
    }
    V3 unrotate(const V3& v) const { return Quat{w, -x, -y, -z}.rotate(v); }
    Quat operator*(const Quat& o) const {
        return {w * o.w - x * o.x - y * o.y - z * o.z, w * o.x + x * o.w + y * o.z - z * o.y,
                w * o.y - x * o.z + y * o.w + z * o.x, w * o.z + x * o.y - y * o.x + z * o.w};
    }
    static Quat axisAngle(const V3& axis, double ang) {
        double s = std::sin(ang * 0.5);
        return {std::cos(ang * 0.5), axis.x * s, axis.y * s, axis.z * s};
    }
    void normalize() {
        double l = std::sqrt(w * w + x * x + y * y + z * z);
        if (l < 1e-12) {
            w = 1;
            x = y = z = 0;
            return;
        }
        w /= l;
        x /= l;
        y /= l;
        z /= l;
    }
};

// 3x3 matrix stored as three columns.
struct M3 {
    V3 c[3];
    V3 operator*(const V3& v) const { return c[0] * v.x + c[1] * v.y + c[2] * v.z; }
};
// Inverse of a symmetric positive-definite 3x3 (inertia tensors); identity-ish fallback if singular.
inline M3 inverseSym(const M3& m) {
    double a = m.c[0].x, b = m.c[1].x, c = m.c[2].x, d = m.c[1].y, e = m.c[2].y, f = m.c[2].z;
    double A = d * f - e * e, B = c * e - b * f, C = b * e - c * d;
    double det = a * A + b * B + c * C;
    M3 r;
    if (std::fabs(det) < 1e-12) {
        r.c[0] = V3(1, 0, 0);
        r.c[1] = V3(0, 1, 0);
        r.c[2] = V3(0, 0, 1);
        return r;
    }
    double D = a * f - c * c, E = b * c - a * e, F = a * d - b * b;
    r.c[0] = V3(A, B, C) / det;
    r.c[1] = V3(B, D, E) / det;
    r.c[2] = V3(C, E, F) / det;
    return r;
}

// Column-major 4x4 float matrix for rendering.
struct M4 {
    float m[16];
};

inline M4 m4identity() {
    M4 r{};
    r.m[0] = r.m[5] = r.m[10] = r.m[15] = 1;
    return r;
}
inline M4 operator*(const M4& a, const M4& b) {
    M4 r{};
    for (int c = 0; c < 4; c++)
        for (int rr = 0; rr < 4; rr++) {
            float s = 0;
            for (int k = 0; k < 4; k++) s += a.m[k * 4 + rr] * b.m[c * 4 + k];
            r.m[c * 4 + rr] = s;
        }
    return r;
}
inline M4 perspective(float fovy, float aspect, float n, float f) {
    float t = 1.0f / std::tan(fovy * 0.5f);
    M4 r{};
    r.m[0] = t / aspect;
    r.m[5] = t;
    r.m[10] = (f + n) / (n - f);
    r.m[11] = -1;
    r.m[14] = 2 * f * n / (n - f);
    return r;
}
inline M4 lookAt(const V3& eye, const V3& center, const V3& up) {
    V3 f = norm(center - eye);
    V3 s = norm(cross(f, up));
    V3 u = cross(s, f);
    M4 r = m4identity();
    r.m[0] = (float)s.x; r.m[4] = (float)s.y; r.m[8] = (float)s.z;
    r.m[1] = (float)u.x; r.m[5] = (float)u.y; r.m[9] = (float)u.z;
    r.m[2] = (float)-f.x; r.m[6] = (float)-f.y; r.m[10] = (float)-f.z;
    r.m[12] = (float)-dot(s, eye);
    r.m[13] = (float)-dot(u, eye);
    r.m[14] = (float)dot(f, eye);
    return r;
}
inline M4 ortho(float l, float rgt, float b, float t, float n, float f) {
    M4 r = m4identity();
    r.m[0] = 2 / (rgt - l);
    r.m[5] = 2 / (t - b);
    r.m[10] = -2 / (f - n);
    r.m[12] = -(rgt + l) / (rgt - l);
    r.m[13] = -(t + b) / (t - b);
    r.m[14] = -(f + n) / (f - n);
    return r;
}
inline V3 transformPoint(const M4& m, const V3& p) {
    return {m.m[0] * p.x + m.m[4] * p.y + m.m[8] * p.z + m.m[12],
            m.m[1] * p.x + m.m[5] * p.y + m.m[9] * p.z + m.m[13],
            m.m[2] * p.x + m.m[6] * p.y + m.m[10] * p.z + m.m[14]};
}
inline bool invert(const M4& in, M4& out) {
    const float* m = in.m;
    float inv[16];
    inv[0] = m[5] * m[10] * m[15] - m[5] * m[11] * m[14] - m[9] * m[6] * m[15] + m[9] * m[7] * m[14] + m[13] * m[6] * m[11] - m[13] * m[7] * m[10];
    inv[4] = -m[4] * m[10] * m[15] + m[4] * m[11] * m[14] + m[8] * m[6] * m[15] - m[8] * m[7] * m[14] - m[12] * m[6] * m[11] + m[12] * m[7] * m[10];
    inv[8] = m[4] * m[9] * m[15] - m[4] * m[11] * m[13] - m[8] * m[5] * m[15] + m[8] * m[7] * m[13] + m[12] * m[5] * m[11] - m[12] * m[7] * m[9];
    inv[12] = -m[4] * m[9] * m[14] + m[4] * m[10] * m[13] + m[8] * m[5] * m[14] - m[8] * m[6] * m[13] - m[12] * m[5] * m[10] + m[12] * m[6] * m[9];
    inv[1] = -m[1] * m[10] * m[15] + m[1] * m[11] * m[14] + m[9] * m[2] * m[15] - m[9] * m[3] * m[14] - m[13] * m[2] * m[11] + m[13] * m[3] * m[10];
    inv[5] = m[0] * m[10] * m[15] - m[0] * m[11] * m[14] - m[8] * m[2] * m[15] + m[8] * m[3] * m[14] + m[12] * m[2] * m[11] - m[12] * m[3] * m[10];
    inv[9] = -m[0] * m[9] * m[15] + m[0] * m[11] * m[13] + m[8] * m[1] * m[15] - m[8] * m[3] * m[13] - m[12] * m[1] * m[11] + m[12] * m[3] * m[9];
    inv[13] = m[0] * m[9] * m[14] - m[0] * m[10] * m[13] - m[8] * m[1] * m[14] + m[8] * m[2] * m[13] + m[12] * m[1] * m[10] - m[12] * m[2] * m[9];
    inv[2] = m[1] * m[6] * m[15] - m[1] * m[7] * m[14] - m[5] * m[2] * m[15] + m[5] * m[3] * m[14] + m[13] * m[2] * m[7] - m[13] * m[3] * m[6];
    inv[6] = -m[0] * m[6] * m[15] + m[0] * m[7] * m[14] + m[4] * m[2] * m[15] - m[4] * m[3] * m[14] - m[12] * m[2] * m[7] + m[12] * m[3] * m[6];
    inv[10] = m[0] * m[5] * m[15] - m[0] * m[7] * m[13] - m[4] * m[1] * m[15] + m[4] * m[3] * m[13] + m[12] * m[1] * m[7] - m[12] * m[3] * m[5];
    inv[14] = -m[0] * m[5] * m[14] + m[0] * m[6] * m[13] + m[4] * m[1] * m[14] - m[4] * m[2] * m[13] - m[12] * m[1] * m[6] + m[12] * m[2] * m[5];
    inv[3] = -m[1] * m[6] * m[11] + m[1] * m[7] * m[10] + m[5] * m[2] * m[11] - m[5] * m[3] * m[10] - m[9] * m[2] * m[7] + m[9] * m[3] * m[6];
    inv[7] = m[0] * m[6] * m[11] - m[0] * m[7] * m[10] - m[4] * m[2] * m[11] + m[4] * m[3] * m[10] + m[8] * m[2] * m[7] - m[8] * m[3] * m[6];
    inv[11] = -m[0] * m[5] * m[11] + m[0] * m[7] * m[9] + m[4] * m[1] * m[11] - m[4] * m[3] * m[9] - m[8] * m[1] * m[7] + m[8] * m[3] * m[5];
    inv[15] = m[0] * m[5] * m[10] - m[0] * m[6] * m[9] - m[4] * m[1] * m[10] + m[4] * m[2] * m[9] + m[8] * m[1] * m[6] - m[8] * m[2] * m[5];
    float det = m[0] * inv[0] + m[1] * inv[4] + m[2] * inv[8] + m[3] * inv[12];
    if (std::fabs(det) < 1e-20f) return false;
    det = 1.0f / det;
    for (int i = 0; i < 16; i++) out.m[i] = inv[i] * det;
    return true;
}

// ---- Noise for terrain and scenery --------------------------------------------------
inline double latticeHash(int i, int j) {
    uint32_t h = (uint32_t)i * 374761393u + (uint32_t)j * 668265263u;
    h = (h ^ (h >> 13)) * 1274126177u;
    return ((h ^ (h >> 16)) & 0xffffff) / double(0xffffff);
}
// Smooth value noise in [0, 1].
inline double valueNoise(double x, double z) {
    int i = (int)std::floor(x), j = (int)std::floor(z);
    double fx = x - i, fz = z - j;
    fx = fx * fx * (3 - 2 * fx);
    fz = fz * fz * (3 - 2 * fz);
    double a = latticeHash(i, j), b = latticeHash(i + 1, j), c = latticeHash(i, j + 1), d = latticeHash(i + 1, j + 1);
    return (a + (b - a) * fx) * (1 - fz) + (c + (d - c) * fx) * fz;
}
// Ridged fractal noise in [0, 1]: sharp crests, like mountain ridges.
inline double ridgedNoise(double x, double z, int oct) {
    double s = 0, a = 0.5, total = 0;
    for (int o = 0; o < oct; o++) {
        double n = 1 - std::fabs(2 * valueNoise(x, z) - 1);
        s += a * n * n;
        total += a;
        double nx = 1.6 * x + 1.2 * z, nz = -1.2 * x + 1.6 * z;
        x = nx + 17.3, z = nz - 9.1;
        a *= 0.5;
    }
    return s / total;
}
// Ordinary fractal noise in [0, 1].
inline double fbmNoise(double x, double z, int oct) {
    double s = 0, a = 0.5, total = 0;
    for (int o = 0; o < oct; o++) {
        s += a * valueNoise(x, z);
        total += a;
        double nx = 1.6 * x + 1.2 * z, nz = -1.2 * x + 1.6 * z;
        x = nx + 5.1, z = nz + 3.7;
        a *= 0.5;
    }
    return s / total;
}

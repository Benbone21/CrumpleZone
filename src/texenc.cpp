#include "texenc.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstring>

namespace texenc {
namespace {

inline int sq(int x) { return x * x; }

inline uint16_t pack565(int r, int g, int b) { return (uint16_t)(((r * 31 + 127) / 255) << 11 | ((g * 63 + 127) / 255) << 5 | ((b * 31 + 127) / 255)); }
inline void unpack565(uint16_t c, int o[3]) {
    const int r = c >> 11, g = (c >> 5) & 63, b = c & 31;
    o[0] = (r << 3) | (r >> 2), o[1] = (g << 2) | (g >> 4), o[2] = (b << 3) | (b >> 2);
}

// The four colours a BC1 block's endpoints make (the 4-colour mode, as BC3 always uses).
inline void palette1(uint16_t c0, uint16_t c1, int p[4][3]) {
    unpack565(c0, p[0]), unpack565(c1, p[1]);
    for (int k = 0; k < 3; k++) p[2][k] = (2 * p[0][k] + p[1][k]) / 3, p[3][k] = (p[0][k] + 2 * p[1][k]) / 3;
}

// Picks each texel's nearest palette entry; returns the squared error.
int assign1(const int px[16][3], const int p[4][3], uint32_t& idx) {
    int err = 0;
    idx = 0;
    for (int i = 0; i < 16; i++) {
        int best = 0, be = 1 << 30;
        for (int j = 0; j < 4; j++) {
            const int e = sq(px[i][0] - p[j][0]) + sq(px[i][1] - p[j][1]) + sq(px[i][2] - p[j][2]);
            if (e < be) be = e, best = j;
        }
        err += be;
        idx |= (uint32_t)best << (2 * i);
    }
    return err;
}

inline int clamp255(float v) { return v < 0 ? 0 : v > 255 ? 255 : (int)(v + 0.5f); }

void putBC1(uint16_t c0, uint16_t c1, uint32_t idx, uint8_t* o) {
    o[0] = c0 & 255, o[1] = c0 >> 8, o[2] = c1 & 255, o[3] = c1 >> 8;
    std::memcpy(o + 4, &idx, 4);  // (little endian, as the GPUs and this machine are)
}

// One 4x4 block of RGB (16 texels) to 8 bytes: the principal axis picks the endpoints, then a few
// least-squares passes refine them against the chosen indices.
void blockBC1(const int px[16][3], uint8_t* o) {
    float mean[3] = {0, 0, 0};
    for (int i = 0; i < 16; i++)
        for (int k = 0; k < 3; k++) mean[k] += px[i][k];
    for (float& m : mean) m /= 16;
    float cov[6] = {0, 0, 0, 0, 0, 0};  // xx xy xz yy yz zz
    for (int i = 0; i < 16; i++) {
        const float d[3] = {px[i][0] - mean[0], px[i][1] - mean[1], px[i][2] - mean[2]};
        cov[0] += d[0] * d[0], cov[1] += d[0] * d[1], cov[2] += d[0] * d[2], cov[3] += d[1] * d[1], cov[4] += d[1] * d[2], cov[5] += d[2] * d[2];
    }
    float ax[3] = {0.57735f, 0.57735f, 0.57735f};
    if (cov[0] + cov[3] + cov[5] < 1e-3f) {  // flat
        const int c = pack565(px[0][0], px[0][1], px[0][2]);
        putBC1((uint16_t)c, (uint16_t)c, 0, o);
        // (a flat block may sit between two 565 steps: try the neighbouring endpoints, whose
        // thirds land closer)
        int be = 1 << 30;
        for (int a = -1; a <= 1; a++) {
            const int d0 = std::min(65535, std::max(0, c + a)), d1 = std::min(65535, std::max(0, c - a * 8));
            if (d0 == d1) continue;
            const uint16_t hi = (uint16_t)std::max(d0, d1), lo = (uint16_t)std::min(d0, d1);
            int p[4][3];
            uint32_t idx;
            palette1(hi, lo, p);
            const int e = assign1(px, p, idx);
            if (e < be) be = e, putBC1(hi, lo, idx, o);
        }
        return;
    }
    for (int it = 0; it < 6; it++) {
        const float x = cov[0] * ax[0] + cov[1] * ax[1] + cov[2] * ax[2], y = cov[1] * ax[0] + cov[3] * ax[1] + cov[4] * ax[2], z = cov[2] * ax[0] + cov[4] * ax[1] + cov[5] * ax[2];
        const float l = std::sqrt(x * x + y * y + z * z);
        if (l < 1e-6f) break;
        ax[0] = x / l, ax[1] = y / l, ax[2] = z / l;
    }
    float tmin = 1e9f, tmax = -1e9f;
    for (int i = 0; i < 16; i++) {
        const float t = (px[i][0] - mean[0]) * ax[0] + (px[i][1] - mean[1]) * ax[1] + (px[i][2] - mean[2]) * ax[2];
        tmin = std::min(tmin, t), tmax = std::max(tmax, t);
    }
    int e0[3], e1[3];
    for (int k = 0; k < 3; k++) e0[k] = clamp255(mean[k] + ax[k] * tmax), e1[k] = clamp255(mean[k] + ax[k] * tmin);
    uint16_t c0 = pack565(e0[0], e0[1], e0[2]), c1 = pack565(e1[0], e1[1], e1[2]);
    uint16_t bc0 = 0, bc1 = 0;
    uint32_t bidx = 0;
    int berr = 1 << 30;
    for (int pass = 0; pass < 3; pass++) {
        if (c0 < c1) std::swap(c0, c1);
        int p[4][3];
        uint32_t idx = 0;
        int err = 0;
        palette1(c0, c1, p);
        err = assign1(px, p, idx);
        if (err < berr) berr = err, bc0 = c0, bc1 = c1, bidx = idx;
        if (err == 0) break;
        // Least squares for the endpoints given the indices.
        static const float w0[4] = {1, 0, 2.0f / 3, 1.0f / 3};
        float a2 = 0, b2 = 0, ab = 0, ax_[3] = {0, 0, 0}, bx[3] = {0, 0, 0};
        for (int i = 0; i < 16; i++) {
            const float a = w0[(idx >> (2 * i)) & 3], b = 1 - a;
            a2 += a * a, b2 += b * b, ab += a * b;
            for (int k = 0; k < 3; k++) ax_[k] += a * px[i][k], bx[k] += b * px[i][k];
        }
        const float det = a2 * b2 - ab * ab;
        if (std::fabs(det) < 1e-4f) break;
        int n0[3], n1[3];
        for (int k = 0; k < 3; k++) {
            n0[k] = clamp255((ax_[k] * b2 - bx[k] * ab) / det);
            n1[k] = clamp255((bx[k] * a2 - ax_[k] * ab) / det);
        }
        const uint16_t d0 = pack565(n0[0], n0[1], n0[2]), d1 = pack565(n1[0], n1[1], n1[2]);
        if ((d0 == c0 && d1 == c1) || (d0 == c1 && d1 == c0)) break;
        c0 = d0, c1 = d1;
    }
    if (bc0 == bc1) bidx = 0;
    putBC1(bc0, bc1, bidx, o);
}

// One channel of 16 texels to a BC4 block (the 8-value mode).
void blockBC4(const int v[16], uint8_t* o) {
    int lo = 255, hi = 0;
    for (int i = 0; i < 16; i++) lo = std::min(lo, v[i]), hi = std::max(hi, v[i]);
    int p[8];
    p[0] = hi, p[1] = lo;
    for (int i = 0; i < 6; i++) p[2 + i] = ((6 - i) * hi + (1 + i) * lo) / 7;
    uint64_t bits = 0;
    if (hi != lo)
        for (int i = 0; i < 16; i++) {
            int best = 0, be = 1 << 30;
            for (int j = 0; j < 8; j++) {
                const int e = std::abs(v[i] - p[j]);
                if (e < be) be = e, best = j;
            }
            bits |= (uint64_t)best << (3 * i);
        }
    o[0] = (uint8_t)hi, o[1] = (uint8_t)lo;
    for (int i = 0; i < 6; i++) o[2 + i] = (uint8_t)(bits >> (8 * i));
}

inline const uint8_t* at(const uint8_t* rgba, int w, int h, int x, int y) { return rgba + ((size_t)std::min(y, h - 1) * w + std::min(x, w - 1)) * 4; }

template <class F>
void forBlocks(int w, int h, F f) {
    for (int by = 0; by < (h + 3) / 4; by++)
        for (int bx = 0; bx < (w + 3) / 4; bx++) f(bx * 4, by * 4);
}

void gather(const uint8_t* rgba, int w, int h, int x0, int y0, int ch, int v[16]) {
    for (int i = 0; i < 16; i++) v[i] = at(rgba, w, h, x0 + (i & 3), y0 + (i >> 2))[ch];
}

void decodeBC4Block(const uint8_t* b, int out[16]) {
    int p[8];
    p[0] = b[0], p[1] = b[1];
    if (p[0] > p[1])
        for (int i = 0; i < 6; i++) p[2 + i] = ((6 - i) * p[0] + (1 + i) * p[1]) / 7;
    else {
        for (int i = 0; i < 4; i++) p[2 + i] = ((4 - i) * p[0] + (1 + i) * p[1]) / 5;
        p[6] = 0, p[7] = 255;
    }
    uint64_t bits = 0;
    for (int i = 0; i < 6; i++) bits |= (uint64_t)b[2 + i] << (8 * i);
    for (int i = 0; i < 16; i++) out[i] = p[(bits >> (3 * i)) & 7];
}

void decodeBC1Block(const uint8_t* b, int out[16][3]) {
    const uint16_t c0 = (uint16_t)(b[0] | b[1] << 8), c1 = (uint16_t)(b[2] | b[3] << 8);
    int p[4][3];
    palette1(c0, c1, p);
    uint32_t idx;
    std::memcpy(&idx, b + 4, 4);
    for (int i = 0; i < 16; i++)
        for (int k = 0; k < 3; k++) out[i][k] = p[(idx >> (2 * i)) & 3][k];
}

struct SrgbLut {
    float toLin[256];
    uint8_t fromLin[4096];
    SrgbLut() {
        for (int i = 0; i < 256; i++) {
            const float c = i / 255.0f;
            toLin[i] = c <= 0.04045f ? c / 12.92f : std::pow((c + 0.055f) / 1.055f, 2.4f);
        }
        for (int i = 0; i < 4096; i++) {
            const float l = (i + 0.5f) / 4096.0f;
            const float c = l <= 0.0031308f ? l * 12.92f : 1.055f * std::pow(l, 1 / 2.4f) - 0.055f;
            fromLin[i] = (uint8_t)std::min(255.0f, c * 255.0f + 0.5f);
        }
    }
};

}  // namespace

void encodeBC1(const uint8_t* rgba, int w, int h, std::vector<uint8_t>& out) {
    out.assign(blockBytes(w, h, 8), 0);
    uint8_t* o = out.data();
    forBlocks(w, h, [&](int x0, int y0) {
        int px[16][3];
        for (int i = 0; i < 16; i++) {
            const uint8_t* p = at(rgba, w, h, x0 + (i & 3), y0 + (i >> 2));
            px[i][0] = p[0], px[i][1] = p[1], px[i][2] = p[2];
        }
        blockBC1(px, o);
        o += 8;
    });
}

void encodeBC4(const uint8_t* rgba, int w, int h, int channel, std::vector<uint8_t>& out) {
    out.assign(blockBytes(w, h, 8), 0);
    uint8_t* o = out.data();
    forBlocks(w, h, [&](int x0, int y0) {
        int v[16];
        gather(rgba, w, h, x0, y0, channel, v);
        blockBC4(v, o);
        o += 8;
    });
}

void encodeBC3(const uint8_t* rgba, int w, int h, std::vector<uint8_t>& out) {
    out.assign(blockBytes(w, h, 16), 0);
    uint8_t* o = out.data();
    forBlocks(w, h, [&](int x0, int y0) {
        int v[16], px[16][3];
        for (int i = 0; i < 16; i++) {
            const uint8_t* p = at(rgba, w, h, x0 + (i & 3), y0 + (i >> 2));
            v[i] = p[3], px[i][0] = p[0], px[i][1] = p[1], px[i][2] = p[2];
        }
        blockBC4(v, o);
        blockBC1(px, o + 8);
        o += 16;
    });
}

void encodeBC5(const uint8_t* rgba, int w, int h, int chA, int chB, std::vector<uint8_t>& out) {
    out.assign(blockBytes(w, h, 16), 0);
    uint8_t* o = out.data();
    forBlocks(w, h, [&](int x0, int y0) {
        int v[16];
        gather(rgba, w, h, x0, y0, chA, v);
        blockBC4(v, o);
        gather(rgba, w, h, x0, y0, chB, v);
        blockBC4(v, o + 8);
        o += 16;
    });
}

namespace {
template <class F>
void decodeBlocks(int w, int h, int per, const uint8_t* blocks, std::vector<uint8_t>& rgba, F f) {
    rgba.assign((size_t)w * h * 4, 255);
    const uint8_t* b = blocks;
    forBlocks(w, h, [&](int x0, int y0) {
        for (int i = 0; i < 16; i++) {
            const int x = x0 + (i & 3), y = y0 + (i >> 2);
            if (x < w && y < h) f(b, i, &rgba[((size_t)y * w + x) * 4]);
        }
        b += per;
    });
}
}  // namespace

void decodeBC1(const uint8_t* blocks, int w, int h, std::vector<uint8_t>& rgba) {
    int px[16][3];
    const uint8_t* last = nullptr;
    decodeBlocks(w, h, 8, blocks, rgba, [&](const uint8_t* b, int i, uint8_t* o) {
        if (b != last) decodeBC1Block(b, px), last = b;
        o[0] = (uint8_t)px[i][0], o[1] = (uint8_t)px[i][1], o[2] = (uint8_t)px[i][2];
    });
}
void decodeBC3(const uint8_t* blocks, int w, int h, std::vector<uint8_t>& rgba) {
    int px[16][3], a[16];
    const uint8_t* last = nullptr;
    decodeBlocks(w, h, 16, blocks, rgba, [&](const uint8_t* b, int i, uint8_t* o) {
        if (b != last) decodeBC4Block(b, a), decodeBC1Block(b + 8, px), last = b;
        o[0] = (uint8_t)px[i][0], o[1] = (uint8_t)px[i][1], o[2] = (uint8_t)px[i][2], o[3] = (uint8_t)a[i];
    });
}
void decodeBC4(const uint8_t* blocks, int w, int h, std::vector<uint8_t>& rgba) {
    int a[16];
    const uint8_t* last = nullptr;
    decodeBlocks(w, h, 8, blocks, rgba, [&](const uint8_t* b, int i, uint8_t* o) {
        if (b != last) decodeBC4Block(b, a), last = b;
        o[0] = (uint8_t)a[i], o[1] = o[2] = 0;
    });
}
void decodeBC5(const uint8_t* blocks, int w, int h, std::vector<uint8_t>& rgba) {
    int a[16], c[16];
    const uint8_t* last = nullptr;
    decodeBlocks(w, h, 16, blocks, rgba, [&](const uint8_t* b, int i, uint8_t* o) {
        if (b != last) decodeBC4Block(b, a), decodeBC4Block(b + 8, c), last = b;
        o[0] = (uint8_t)a[i], o[1] = (uint8_t)c[i], o[2] = 0;
    });
}

int mipCount(int w, int h) {
    int n = 1;
    while (w > 1 || h > 1) w = std::max(1, w / 2), h = std::max(1, h / 2), n++;
    return n;
}

std::vector<std::vector<uint8_t>> mipChain(const uint8_t* rgba, int w, int h, bool srgb) {
    static const SrgbLut lut;
    std::vector<std::vector<uint8_t>> chain;
    chain.emplace_back(rgba, rgba + (size_t)w * h * 4);
    while (w > 1 || h > 1) {
        const int w2 = std::max(1, w / 2), h2 = std::max(1, h / 2);
        const std::vector<uint8_t>& s = chain.back();
        std::vector<uint8_t> o((size_t)w2 * h2 * 4);
        for (int y = 0; y < h2; y++)
            for (int x = 0; x < w2; x++) {
                const int x0 = std::min(2 * x, w - 1), x1 = std::min(2 * x + 1, w - 1), y0 = std::min(2 * y, h - 1), y1 = std::min(2 * y + 1, h - 1);
                const uint8_t* q[4] = {&s[((size_t)y0 * w + x0) * 4], &s[((size_t)y0 * w + x1) * 4], &s[((size_t)y1 * w + x0) * 4], &s[((size_t)y1 * w + x1) * 4]};
                uint8_t* d = &o[((size_t)y * w2 + x) * 4];
                for (int k = 0; k < 4; k++) {
                    if (srgb && k < 3) {
                        const float l = (lut.toLin[q[0][k]] + lut.toLin[q[1][k]] + lut.toLin[q[2][k]] + lut.toLin[q[3][k]]) * 0.25f;
                        d[k] = lut.fromLin[std::min(4095, (int)(l * 4096.0f))];
                    } else {
                        d[k] = (uint8_t)((q[0][k] + q[1][k] + q[2][k] + q[3][k] + 2) / 4);
                    }
                }
            }
        chain.push_back(std::move(o));
        w = w2, h = h2;
    }
    return chain;
}

Encoded encode(Kind kind, bool srgb, const std::vector<std::vector<uint8_t>>& levels, int w, int h, bool allowS3tc) {
    Encoded e;
    e.w = w, e.h = h, e.kind = kind, e.srgb = srgb;
    const bool rgtc = kind == Kind::Normal2;  // (BC4/5 are core GL 3; S3TC is the extension)
    if (kind == Kind::Raw || (!allowS3tc && !rgtc)) {
        e.levels = levels;
        return e;
    }
    e.compressed = true;
    int lw = w, lh = h;
    for (const auto& l : levels) {
        std::vector<uint8_t> out;
        switch (kind) {
            case Kind::Color:
            case Kind::Data: encodeBC1(l.data(), lw, lh, out); break;
            case Kind::ColorAlpha: encodeBC3(l.data(), lw, lh, out); break;
            case Kind::Normal2: encodeBC5(l.data(), lw, lh, 0, 1, out); break;
            case Kind::Packed4: {
                // (x,y,r,a) to BC3's R,G,B,A = (r,y,a,x): x gets the 8-bit alpha block, y the 6-bit green.
                std::vector<uint8_t> sw(l.size());
                for (size_t i = 0; i < l.size(); i += 4) sw[i] = l[i + 2], sw[i + 1] = l[i + 1], sw[i + 2] = l[i + 3], sw[i + 3] = l[i];
                encodeBC3(sw.data(), lw, lh, out);
                e.swizzle = true;
                break;
            }
            case Kind::Raw: break;
        }
        e.levels.push_back(std::move(out));
        lw = std::max(1, lw / 2), lh = std::max(1, lh / 2);
    }
    return e;
}

namespace {
std::map<std::string, TexInfo>& reg() {
    static std::map<std::string, TexInfo> r;
    return r;
}
}  // namespace
void noteTexture(const TexInfo& t) { reg()[t.name] = t; }
void forgetTexture(const std::string& name) { reg().erase(name); }
const std::map<std::string, TexInfo>& textureList() { return reg(); }
size_t totalTextureBytes() {
    size_t n = 0;
    for (const auto& t : reg()) n += t.second.bytes;
    return n;
}

std::vector<std::string> textureReport(std::string* table) {
    std::vector<std::string> bad;
    char buf[256];
    for (const auto& kv : reg()) {
        const TexInfo& t = kv.second;
        if (t.mips != 0 && t.mips < mipCount(t.w, t.h)) bad.push_back(t.name);
        if (table) {
            std::snprintf(buf, sizeof buf, "%-28s %-12s %4dx%-4d x%-2d mips %-2d %9.2f MB %s\n", t.name.c_str(), t.format.c_str(), t.w, t.h, t.layers, t.mips, t.bytes / 1048576.0, t.runtime ? "runtime" : "loaded");
            *table += buf;
        }
    }
    if (table) {
        std::snprintf(buf, sizeof buf, "total %.2f MB\n", totalTextureBytes() / 1048576.0);
        *table += buf;
    }
    return bad;
}

}  // namespace texenc

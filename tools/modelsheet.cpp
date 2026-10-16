// Contact sheets of the code-made models (modelkit.h), drawn on the CPU: one PNG per model,
// a column per design, seen from the front-left and the back-right, with a ground grid,
// a 1.8 m figure beside the big things, and each design's size and triangle count.
//
//   build/modelsheet [--out DIR] [--size PX] [id ...]      (ids: substrings; none = all)
//
// It's for looking over the shapes quickly; the model lab (crumplezone --lab) shows them as
// the game draws them.
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <string>
#include <vector>

#include "../src/font.h"
#include "../src/modelkit.h"
#include "../src/platform.h"

namespace {
using mk::mix;

struct Img {
    int w = 0, h = 0;
    std::vector<float> rgb;  // linear
    Img() {}
    Img(int w_, int h_, V3 bg) : w(w_), h(h_), rgb(w_ * h_ * 3) {
        for (int i = 0; i < w * h; i++) rgb[i * 3] = (float)bg.x, rgb[i * 3 + 1] = (float)bg.y, rgb[i * 3 + 2] = (float)bg.z;
    }
    void set(int x, int y, const V3& c) {
        if (x < 0 || y < 0 || x >= w || y >= h) return;
        float* p = &rgb[(y * w + x) * 3];
        p[0] = (float)c.x, p[1] = (float)c.y, p[2] = (float)c.z;
    }
};

// ---- PNG (stored deflate: no zlib needed) ----
uint32_t crcTable[256];
void initCrc() {
    for (uint32_t n = 0; n < 256; n++) {
        uint32_t c = n;
        for (int k = 0; k < 8; k++) c = c & 1 ? 0xEDB88320u ^ (c >> 1) : c >> 1;
        crcTable[n] = c;
    }
}
uint32_t crc(const unsigned char* b, size_t n, uint32_t c = 0xFFFFFFFFu) {
    for (size_t i = 0; i < n; i++) c = crcTable[(c ^ b[i]) & 0xFF] ^ (c >> 8);
    return c;
}
void be32(std::vector<unsigned char>& o, uint32_t v) {
    o.push_back(v >> 24), o.push_back(v >> 16), o.push_back(v >> 8), o.push_back(v);
}
void chunk(FILE* f, const char* type, const std::vector<unsigned char>& data) {
    std::vector<unsigned char> b;
    be32(b, (uint32_t)data.size());
    b.insert(b.end(), type, type + 4);
    b.insert(b.end(), data.begin(), data.end());
    be32(b, crc(b.data() + 4, b.size() - 4) ^ 0xFFFFFFFFu);
    std::fwrite(b.data(), 1, b.size(), f);
}
bool writePng(const std::string& path, const Img& im) {
    FILE* f = std::fopen(path.c_str(), "wb");
    if (!f) return false;
    const unsigned char sig[8] = {137, 80, 78, 71, 13, 10, 26, 10};
    std::fwrite(sig, 1, 8, f);
    std::vector<unsigned char> hdr;
    be32(hdr, im.w), be32(hdr, im.h);
    hdr.insert(hdr.end(), {8, 2, 0, 0, 0});
    chunk(f, "IHDR", hdr);
    std::vector<unsigned char> raw;
    for (int y = 0; y < im.h; y++) {
        raw.push_back(0);
        for (int x = 0; x < im.w * 3; x++) {
            const double v = std::pow(std::clamp((double)im.rgb[y * im.w * 3 + x], 0.0, 1.0), 1 / 2.2);
            raw.push_back((unsigned char)std::lround(v * 255));
        }
    }
    std::vector<unsigned char> z = {0x78, 0x01};
    uint32_t a = 1, b = 0;
    for (unsigned char c : raw) a = (a + c) % 65521, b = (b + a) % 65521;
    for (size_t at = 0; at < raw.size() || at == 0;) {
        const size_t n = std::min<size_t>(65535, raw.size() - at);
        const bool last = at + n >= raw.size();
        z.push_back(last ? 1 : 0);
        z.push_back(n & 0xFF), z.push_back(n >> 8), z.push_back(~n & 0xFF), z.push_back((~n >> 8) & 0xFF);
        z.insert(z.end(), raw.begin() + at, raw.begin() + at + n);
        at += n;
        if (last) break;
    }
    be32(z, (b << 16) | a);
    chunk(f, "IDAT", z);
    chunk(f, "IEND", {});
    return std::fclose(f) == 0;
}

// ---- Text ----
void text(Img& im, int x, int y, int s, const std::string& str, const V3& c) {
    for (char ch : str) {
        const char up = (char)std::toupper((unsigned char)ch);
        for (const Glyph& g : kGlyphs)
            if (g.c == up) {
                for (int r = 0; r < 7; r++)
                    for (int k = 0; k < 5; k++)
                        if (g.rows[r][k] == '#')
                            for (int dy = 0; dy < s; dy++)
                                for (int dx = 0; dx < s; dx++) im.set(x + k * s + dx, y + r * s + dy, c);
                break;
            }
        x += 6 * s;
    }
}

// ---- Rasteriser ----
struct Cam {
    V3 eye, fwd, right, up;
    double f;  // focal (px)
    int w, h;
    bool ortho = false;
    double scale = 1;  // ortho: px per m
    bool project(const V3& p, double& sx, double& sy, double& z) const {
        const V3 d = p - eye;
        z = dot(d, fwd);
        if (ortho) {
            sx = w / 2.0 + dot(d, right) * scale, sy = h / 2.0 - dot(d, up) * scale;
            return true;
        }
        if (z < 0.01) return false;
        sx = w / 2.0 + dot(d, right) / z * f, sy = h / 2.0 - dot(d, up) / z * f;
        return true;
    }
};

Cam lookCam(const V3& eye, const V3& at, double fovDeg, int w, int h) {
    Cam c;
    c.eye = eye, c.fwd = norm(at - eye);
    c.right = norm(cross(c.fwd, V3(0, 1, 0)));
    c.up = cross(c.right, c.fwd);
    c.f = h / 2.0 / std::tan(fovDeg * M_PI / 360);
    c.w = w, c.h = h;
    return c;
}

struct Tri {
    V3 p[3], n[3];
    V3 c;
    int mat;
};

template <class Fn>
void raster(const Cam& cam, const Tri& t, Fn pixel) {
    double sx[3], sy[3], sz[3];
    for (int k = 0; k < 3; k++)
        if (!cam.project(t.p[k], sx[k], sy[k], sz[k])) return;
    const double area = (sx[1] - sx[0]) * (sy[2] - sy[0]) - (sx[2] - sx[0]) * (sy[1] - sy[0]);
    if (std::fabs(area) < 1e-9) return;
    const int x0 = std::max(0, (int)std::floor(std::min({sx[0], sx[1], sx[2]}))), x1 = std::min(cam.w - 1, (int)std::ceil(std::max({sx[0], sx[1], sx[2]})));
    const int y0 = std::max(0, (int)std::floor(std::min({sy[0], sy[1], sy[2]}))), y1 = std::min(cam.h - 1, (int)std::ceil(std::max({sy[0], sy[1], sy[2]})));
    for (int y = y0; y <= y1; y++)
        for (int x = x0; x <= x1; x++) {
            const double px = x + 0.5, py = y + 0.5;
            double w0 = ((sx[1] - px) * (sy[2] - py) - (sx[2] - px) * (sy[1] - py)) / area;
            double w1 = ((sx[2] - px) * (sy[0] - py) - (sx[0] - px) * (sy[2] - py)) / area;
            double w2 = 1 - w0 - w1;
            if (w0 < 0 || w1 < 0 || w2 < 0) continue;
            if (!cam.ortho) {  // perspective-correct
                const double a = w0 / sz[0], b = w1 / sz[1], c = w2 / sz[2], s = a + b + c;
                w0 = a / s, w1 = b / s, w2 = c / s;
            }
            pixel(x, y, w0, w1, w2, w0 * sz[0] + w1 * sz[1] + w2 * sz[2]);
        }
}

const V3 kSun = norm(V3(0.45, 0.8, 0.38));

struct Scene {
    std::vector<Tri> tris;
    void add(const mk::Mesh& m, const V3& off = V3()) {
        for (size_t i = 0; i + 2 < m.v.size(); i += 3) {
            Tri t;
            for (int k = 0; k < 3; k++) {
                const mk::MVert& v = m.v[i + k];
                t.p[k] = V3(v.p[0], v.p[1], v.p[2]) + off;
                t.n[k] = V3(v.n[0], v.n[1], v.n[2]);
            }
            const mk::MVert& v = m.v[i];
            t.c = V3(v.c[0], v.c[1], v.c[2]);
            t.mat = v.mat;
            tris.push_back(t);
        }
    }
};

V3 lin(const V3& c) { return V3(std::pow(std::max(0.0, c.x), 2.2), std::pow(std::max(0.0, c.y), 2.2), std::pow(std::max(0.0, c.z), 2.2)); }
V3 mul(const V3& a, const V3& b) { return V3(a.x * b.x, a.y * b.y, a.z * b.z); }

// Draws the scene into a cell: ground grid (spacing `grid`), sun shadows.
Img render(const Scene& sc, const Cam& cam, double groundR, double grid, double extentForShadow) {
    Img im(cam.w, cam.h, V3());
    std::vector<double> zb(cam.w * cam.h, 1e30);
    // Shadow map from the sun (orthographic).
    const int SM = 1024;
    Cam sun;
    sun.ortho = true, sun.w = sun.h = SM;
    sun.fwd = -kSun, sun.right = norm(cross(sun.fwd, V3(0, 1, 0))), sun.up = cross(sun.right, sun.fwd);
    sun.scale = SM / (2.2 * extentForShadow);
    sun.eye = V3(0, extentForShadow * 0.3, 0) + kSun * 100;
    std::vector<double> sm(SM * SM, 1e30);
    for (const Tri& t : sc.tris)
        raster(sun, t, [&](int x, int y, double, double, double, double z) {
            double& d = sm[y * SM + x];
            if (z < d) d = z;
        });
    auto shadow = [&](const V3& p, const V3& n) {
        double sx = 0, sy = 0, sz = 0;
        sun.project(p + n * 0.01, sx, sy, sz);
        int lit = 0, tot = 0;
        for (int dy = -1; dy <= 1; dy++)
            for (int dx = -1; dx <= 1; dx++) {
                const int x = (int)sx + dx, y = (int)sy + dy;
                tot++;
                if (x < 0 || y < 0 || x >= SM || y >= SM || sz <= sm[y * SM + x] + 0.02 + 2.0 / sun.scale) lit++;
            }
        return lit / (double)tot;
    };
    // Sky behind.
    for (int y = 0; y < cam.h; y++)
        for (int x = 0; x < cam.w; x++) {
            const double t = y / (double)cam.h;
            im.set(x, y, lin(mix(V3(0.55, 0.68, 0.82), V3(0.85, 0.88, 0.9), t)));
        }
    // Ground: a disc of grid, shaded with the shadows.
    {
        const int N = 48;
        for (int i = 0; i < N; i++)
            for (int j = 0; j < N; j++) {
                const double x0 = -groundR + 2 * groundR * i / N, x1 = -groundR + 2 * groundR * (i + 1) / N;
                const double z0 = -groundR + 2 * groundR * j / N, z1 = -groundR + 2 * groundR * (j + 1) / N;
                Tri a, b;
                a.p[0] = V3(x0, 0, z0), a.p[1] = V3(x1, 0, z0), a.p[2] = V3(x1, 0, z1);
                b.p[0] = V3(x0, 0, z0), b.p[1] = V3(x1, 0, z1), b.p[2] = V3(x0, 0, z1);
                for (Tri* t : {&a, &b}) {
                    raster(cam, *t, [&](int x, int y, double w0, double w1, double w2, double z) {
                        double& d = zb[y * cam.w + x];
                        if (z >= d) return;
                        d = z;
                        const V3 p = t->p[0] * w0 + t->p[1] * w1 + t->p[2] * w2;
                        const double r = std::hypot(p.x, p.z);
                        if (r > groundR) return;
                        const double gx = std::fabs(p.x / grid - std::round(p.x / grid)), gz = std::fabs(p.z / grid - std::round(p.z / grid));
                        const double line = (gx < 0.02 || gz < 0.02) ? 0.82 : 1.0;
                        const double sh = shadow(p, V3(0, 1, 0));
                        const V3 base = lin(V3(0.62, 0.6, 0.55)) * line;
                        const double fade = std::clamp((groundR - r) / (groundR * 0.25), 0.0, 1.0);
                        const V3 col = base * (0.42 + 0.75 * kSun.y * sh);
                        const double t = y / (double)cam.h;
                        im.set(x, y, mix(lin(mix(V3(0.55, 0.68, 0.82), V3(0.85, 0.88, 0.9), t)), col, fade));
                    });
                }
            }
    }
    for (const Tri& t : sc.tris)
        raster(cam, t, [&](int x, int y, double w0, double w1, double w2, double z) {
            double& d = zb[y * cam.w + x];
            if (z >= d) return;
            d = z;
            const V3 p = t.p[0] * w0 + t.p[1] * w1 + t.p[2] * w2;
            V3 n = norm(t.n[0] * w0 + t.n[1] * w1 + t.n[2] * w2);
            const V3 V = norm(cam.eye - p);
            if (dot(n, V) < 0) n = -n;  // (two-sided, as the game draws them)
            const double sh = shadow(p, n);
            const V3 base = lin(t.c);
            const double diff = std::max(0.0, dot(n, kSun));
            const double sky = 0.5 + 0.5 * n.y;
            const V3 amb = V3(0.30, 0.34, 0.40) * sky + V3(0.16, 0.14, 0.11) * (1 - sky);
            const V3 sunC(1.35, 1.25, 1.1);
            const V3 H = norm(V + kSun);
            const double spec = std::pow(std::max(0.0, dot(n, H)), 40);
            const double fres = 0.04 + 0.96 * std::pow(1 - std::max(0.0, dot(n, V)), 5);
            V3 col;
            switch (t.mat) {
            case mk::EMISSIVE: col = base * 2.2; break;
            case mk::GLASS: col = V3(0.02, 0.025, 0.03) + V3(0.4, 0.5, 0.6) * (0.1 + 0.9 * fres) + V3(1, 1, 1) * spec * sh * 2; break;
            case mk::CHROME: col = mix(V3(0.25, 0.27, 0.3), V3(0.75, 0.8, 0.85), 0.5 + 0.5 * n.y) * 0.8 + V3(1, 1, 1) * std::pow(std::max(0.0, dot(n, H)), 120) * sh * 3; break;
            case mk::METAL: col = mul(base, amb + sunC * diff * sh) * 0.6 + mul(base, V3(0.45, 0.5, 0.55)) * (0.5 + 0.5 * n.y) * 0.6 + V3(1, 1, 1) * spec * sh * 0.6; break;
            case mk::PAINT: col = mul(base, amb + sunC * diff * sh) + V3(0.5, 0.6, 0.7) * fres * 0.5 + V3(1, 1, 1) * std::pow(std::max(0.0, dot(n, H)), 120) * sh * 1.2; break;
            case mk::FOLIAGE: col = mul(base, amb * 0.9 + sunC * std::max(0.0, dot(n, kSun) * 0.6 + 0.4) * sh); break;
            default: col = mul(base, amb + sunC * diff * sh) + V3(1, 1, 1) * spec * sh * 0.05;
            }
            im.set(x, y, col);
        });
    return im;
}

Img downsample(const Img& a, int k) {
    Img b(a.w / k, a.h / k, V3());
    for (int y = 0; y < b.h; y++)
        for (int x = 0; x < b.w; x++) {
            V3 s;
            for (int dy = 0; dy < k; dy++)
                for (int dx = 0; dx < k; dx++) {
                    const float* p = &a.rgb[((y * k + dy) * a.w + x * k + dx) * 3];
                    s += V3(p[0], p[1], p[2]);
                }
            b.set(x, y, s / (k * k));
        }
    return b;
}

void blit(Img& dst, const Img& src, int ox, int oy) {
    for (int y = 0; y < src.h; y++)
        for (int x = 0; x < src.w; x++) {
            const float* p = &src.rgb[(y * src.w + x) * 3];
            dst.set(ox + x, oy + y, V3(p[0], p[1], p[2]));
        }
}

// A plain 1.8 m person for scale (grey), standing at `at`.
mk::Mesh scaleFigure() {
    mk::Mesh m;
    mk::B b(m);
    b.style(V3(0.55, 0.55, 0.58), mk::MATTE);
    b.box(V3(0, 0.45, 0.1), V3(0.16, 0.9, 0.14), 0.04);
    b.box(V3(0, 0.45, -0.1), V3(0.16, 0.9, 0.14), 0.04);
    b.box(V3(0, 1.22, 0), V3(0.24, 0.66, 0.42), 0.06);
    b.box(V3(0, 1.2, 0.27), V3(0.12, 0.62, 0.1), 0.04);
    b.box(V3(0, 1.2, -0.27), V3(0.12, 0.62, 0.1), 0.04);
    b.sphere(V3(0, 1.68, 0), V3(0.11, 0.13, 0.11), 2);
    m.finish();
    return m;
}

}  // namespace

int main(int argc, char** argv) {
    initCrc();
    std::string out = "build/review/models";
    int cell = 420;
    std::vector<std::string> only;
    for (int i = 1; i < argc; i++) {
        const std::string a = argv[i];
        if (a == "--out" && i + 1 < argc) out = argv[++i];
        else if (a == "--size" && i + 1 < argc) cell = std::max(120, std::atoi(argv[++i]));
        else only.push_back(a);
    }
    makeDir("build"), makeDir("build/review"), makeDir(out);
    const mk::Mesh figure = scaleFigure();
    int made = 0;
    for (const mk::ModelDef& d : mk::catalog()) {
        bool want = only.empty();
        for (const std::string& o : only) want |= d.id.find(o) != std::string::npos;
        if (!want) continue;
        const int nv = (int)d.variants.size();
        const int head = 34, label = 40;
        Img sheet(cell * std::max(1, nv), head + label + cell * 2, lin(V3(0.13, 0.14, 0.16)));
        text(sheet, 8, 8, 3, d.title + "  (" + d.id + ")  " + d.category, lin(V3(0.95, 0.85, 0.4)));
        for (int v = 0; v < nv; v++) {
            mk::Mesh m;
            mk::build(d, v, mk::P(), m);
            const V3 size = m.hi - m.lo;
            const V3 c = (m.lo + m.hi) * 0.5;
            const double radius = std::max(0.05, len(size) * 0.5);
            const bool big = std::max({size.x, size.y, size.z}) > 1.2;
            Scene sc;
            sc.add(m);
            double frameR = radius;
            V3 center = c;
            if (big) {  // a person beside it, off its left; frame the two together
                const V3 at(c.x, 0, m.hi.z + 0.6);
                sc.add(figure, at);
                const V3 lo(std::min(m.lo.x, at.x - 0.3), std::min(m.lo.y, 0.0), m.lo.z), hi(std::max(m.hi.x, at.x + 0.3), std::max(m.hi.y, 1.8), at.z + 0.3);
                center = (lo + hi) * 0.5;
                frameR = len(hi - lo) * 0.5;
            }
            const double grid = std::max({size.x, size.y, size.z}) < 1.5 ? 0.1 : 1.0;
            for (int view = 0; view < 2; view++) {
                const V3 dir = view == 0 ? norm(V3(0.78, 0.5, 0.62)) : norm(V3(-0.7, 0.42, -0.66));
                const double fov = 32, dist = frameR / std::sin(fov * M_PI / 360) * 1.05;
                const int S = 2;
                const Cam cam = lookCam(center + dir * dist, center, fov, cell * S, cell * S);
                const Img hi = render(sc, cam, std::max(2.0, frameR * 2.5), grid, frameR * 1.6 + 1);
                blit(sheet, downsample(hi, S), v * cell, head + label + view * cell);
            }
            char info[160];
            std::snprintf(info, sizeof info, "%.2f X %.2f X %.2f M  %zu TRIS", size.x, size.z, size.y, m.triangles());
            const std::string tag = std::string(1, (char)('A' + v)) + ": " + d.variants[v].name;
            text(sheet, v * cell + 8, head + 4, 2, tag, lin(V3(1, 1, 1)));
            text(sheet, v * cell + 8, head + 22, 2, info, lin(V3(0.7, 0.75, 0.8)));
            for (int y = head; y < sheet.h; y++) sheet.set(v * cell, y, lin(V3(0.13, 0.14, 0.16)));
        }
        const std::string path = out + "/" + d.id + ".png";
        if (writePng(path, sheet)) {
            std::printf("%s\n", path.c_str());
            made++;
        }
    }
    if (!made) std::fprintf(stderr, "modelsheet: no models matched\n");
    return made ? 0 : 1;
}

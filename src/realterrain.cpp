// Real-world terrain: the DEM loaders and writers, the sidecar, the prepared land and the active real map (realterrain.h).
#include "realterrain.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <map>
#include <sstream>

#include "mathx.h"
#include "platform.h"
#include "world.h"

// stb_image as static functions (groundtex.cpp and propdraw.cpp have their own copies): its PNG reader and its inflate.
#define STB_IMAGE_STATIC
#define STB_IMAGE_IMPLEMENTATION
#define STBI_ONLY_PNG
#define STBI_NO_STDIO
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wunused-function"
#include "third_party/stb_image.h"
#pragma GCC diagnostic pop

namespace realterrain {

namespace {
bool fail(std::string* err, const std::string& why) {
    if (err) *err = why;
    return false;
}
bool readAll(const std::string& path, std::vector<uint8_t>& out) {
    std::ifstream f(path, std::ios::binary);
    if (!f) return false;
    f.seekg(0, std::ios::end);
    const std::streamoff n = f.tellg();
    if (n < 0) return false;
    f.seekg(0);
    out.resize((size_t)n);
    if (n > 0) f.read((char*)out.data(), n);
    return (bool)f;
}
bool writeAll(const std::string& path, const std::vector<uint8_t>& b) {
    std::ofstream f(path, std::ios::binary);
    if (!f) return false;
    f.write((const char*)b.data(), (std::streamsize)b.size());
    return (bool)f;
}
std::string lower(std::string s) {
    for (char& c : s) c = (char)std::tolower((unsigned char)c);
    return s;
}
std::string upper(std::string s) {
    for (char& c : s) c = (char)std::toupper((unsigned char)c);
    return s;
}
std::string extOf(const std::string& path) {
    const size_t d = path.find_last_of('.'), s = path.find_last_of("/\\");
    if (d == std::string::npos || (s != std::string::npos && d < s)) return "";
    return lower(path.substr(d + 1));
}
std::string baseName(const std::string& path) {
    const size_t s = path.find_last_of("/\\");
    return s == std::string::npos ? path : path.substr(s + 1);
}

// Fills the pixels with no data from their neighbours (passes of the mean of the known ones round each), the rest with the lowest.
void fillNoData(Dem& d) {
    if (!d.hasNoData) return;
    const float nd = (float)d.noData;
    auto isNd = [&](float v) { return v == nd || !(v == v) || v < -1e30f || v > 1e30f; };
    std::vector<uint8_t> known((size_t)d.w * d.h);
    size_t missing = 0;
    float lo = 1e30f;
    for (size_t i = 0; i < known.size(); i++) {
        known[i] = !isNd(d.v[i]);
        if (known[i]) lo = std::min(lo, d.v[i]);
        else missing++;
    }
    if (!missing) return;
    if (lo > 1e29f) lo = 0;
    for (int pass = 0; pass < 64 && missing; pass++) {
        std::vector<uint8_t> nk = known;
        for (int y = 0; y < d.h; y++)
            for (int x = 0; x < d.w; x++) {
                const size_t i = (size_t)y * d.w + x;
                if (known[i]) continue;
                double s = 0;
                int n = 0;
                for (int oy = -1; oy <= 1; oy++)
                    for (int ox = -1; ox <= 1; ox++) {
                        const int xx = x + ox, yy = y + oy;
                        if (xx < 0 || yy < 0 || xx >= d.w || yy >= d.h) continue;
                        const size_t j = (size_t)yy * d.w + xx;
                        if (known[j]) s += d.v[j], n++;
                    }
                if (n) d.v[i] = (float)(s / n), nk[i] = 1, missing--;
            }
        known.swap(nk);
    }
    for (size_t i = 0; i < known.size(); i++)
        if (!known[i]) d.v[i] = lo;
}

void crop(Dem& d, Window win) {
    if (win.empty()) return;
    win.x0 = std::max(0, win.x0), win.y0 = std::max(0, win.y0), win.x1 = std::min(d.w, win.x1), win.y1 = std::min(d.h, win.y1);
    if (win.empty()) return;
    std::vector<float> v((size_t)(win.x1 - win.x0) * (win.y1 - win.y0));
    for (int y = win.y0; y < win.y1; y++)
        std::memcpy(&v[(size_t)(y - win.y0) * (win.x1 - win.x0)], &d.v[(size_t)y * d.w + win.x0], sizeof(float) * (win.x1 - win.x0));
    d.v.swap(v);
    d.x0 += win.x0 * d.dx, d.y0 -= win.y0 * d.dy;
    d.w = win.x1 - win.x0, d.h = win.y1 - win.y0;
}

// ---- Checksums and deflate (stored blocks) ---------------------------------------------------------------------------------
uint32_t crc32(const uint8_t* p, size_t n, uint32_t c = 0) {
    static uint32_t table[256];
    static bool made = false;
    if (!made) {
        for (uint32_t i = 0; i < 256; i++) {
            uint32_t k = i;
            for (int b = 0; b < 8; b++) k = k & 1 ? 0xEDB88320u ^ (k >> 1) : k >> 1;
            table[i] = k;
        }
        made = true;
    }
    c = ~c;
    for (size_t i = 0; i < n; i++) c = table[(c ^ p[i]) & 255] ^ (c >> 8);
    return ~c;
}
std::vector<uint8_t> zlibStored(const std::vector<uint8_t>& raw) {
    std::vector<uint8_t> o = {0x78, 0x01};
    size_t at = 0;
    do {
        const size_t n = std::min<size_t>(65535, raw.size() - at);
        const bool last = at + n >= raw.size();
        o.push_back(last ? 1 : 0);
        o.push_back((uint8_t)(n & 255)), o.push_back((uint8_t)(n >> 8));
        o.push_back((uint8_t)(~n & 255)), o.push_back((uint8_t)((~n >> 8) & 255));
        o.insert(o.end(), raw.begin() + (long)at, raw.begin() + (long)(at + n));
        at += n;
    } while (at < raw.size());
    uint32_t a = 1, b = 0;
    for (uint8_t c : raw) a = (a + c) % 65521, b = (b + a) % 65521;
    const uint32_t ad = (b << 16) | a;
    for (int s = 24; s >= 0; s -= 8) o.push_back((uint8_t)(ad >> s));
    return o;
}
bool inflate(const uint8_t* p, size_t n, size_t expect, std::vector<uint8_t>& out) {
    out.assign(expect, 0);
    if (n > 0x7fffffff || expect > 0x7fffffff) return false;
    const int got = stbi_zlib_decode_buffer((char*)out.data(), (int)expect, (const char*)p, (int)n);
    if (got < 0) return false;
    out.resize((size_t)got);
    return true;
}

// ---- TIFF LZW ---------------------------------------------------------------------------------------------------------------
bool lzwDecode(const uint8_t* p, size_t n, size_t expect, std::vector<uint8_t>& out) {
    out.clear();
    out.reserve(expect);
    std::vector<uint16_t> prefix(4096);
    std::vector<uint8_t> suffix(4096), first(4096);
    std::vector<uint16_t> length(4096);
    for (int i = 0; i < 256; i++) prefix[i] = 0xffff, suffix[i] = (uint8_t)i, first[i] = (uint8_t)i, length[i] = 1;
    int width = 9, next = 258, prev = -1;
    size_t bitPos = 0;
    std::vector<uint8_t> tmp;
    auto emit = [&](int code) {
        const int L = length[code];
        const size_t at = out.size();
        out.resize(at + L);
        for (int c = code, k = L - 1; k >= 0; k--) out[at + k] = suffix[c], c = prefix[c];
    };
    while (out.size() < expect) {
        if (bitPos + width > n * 8) break;
        int code = 0;
        for (int b = 0; b < width; b++, bitPos++) code = (code << 1) | ((p[bitPos >> 3] >> (7 - (bitPos & 7))) & 1);
        if (code == 257) break;
        if (code == 256) {
            width = 9, next = 258, prev = -1;
            continue;
        }
        if (prev < 0) {
            if (code > 255) return false;
            emit(code);
            prev = code;
            continue;
        }
        if (code < next) {
            emit(code);
            if (next < 4096) prefix[next] = (uint16_t)prev, suffix[next] = first[code], first[next] = first[prev], length[next] = (uint16_t)(length[prev] + 1), next++;
        } else if (code == next && next < 4096) {
            prefix[next] = (uint16_t)prev, suffix[next] = first[prev], first[next] = first[prev], length[next] = (uint16_t)(length[prev] + 1), next++;
            emit(code);
        } else {
            return false;
        }
        prev = code;
        if (next + 1 >= (1 << width) && width < 12) width++;
    }
    if (out.size() > expect) out.resize(expect);
    return out.size() == expect;
}
std::vector<uint8_t> lzwEncode(const std::vector<uint8_t>& in) {
    std::vector<uint8_t> o;
    uint32_t acc = 0;
    int bits = 0, width = 9, freeEnt = 258;
    auto put = [&](int code) {
        acc = (acc << width) | (uint32_t)code, bits += width;
        while (bits >= 8) o.push_back((uint8_t)(acc >> (bits - 8))), bits -= 8;
        acc &= (1u << bits) - 1;
    };
    std::map<uint32_t, int> dict;
    put(256);
    if (in.empty()) {
        put(257);
        if (bits) o.push_back((uint8_t)(acc << (8 - bits)));
        return o;
    }
    int ent = in[0];
    auto grow = [&] {
        freeEnt++;
        if (freeEnt > (1 << width) - 1 && width < 12) width++;
    };
    for (size_t i = 1; i < in.size(); i++) {
        const uint32_t key = ((uint32_t)ent << 8) | in[i];
        auto it = dict.find(key);
        if (it != dict.end()) {
            ent = it->second;
            continue;
        }
        put(ent);
        dict[key] = freeEnt;
        grow();
        if (freeEnt >= 4093) {
            put(256);
            dict.clear(), width = 9, freeEnt = 258;
        }
        ent = in[i];
    }
    put(ent);
    grow();
    put(257);
    if (bits) o.push_back((uint8_t)(acc << (8 - bits)));
    return o;
}

// ---- TIFF reading -----------------------------------------------------------------------------------------------------------
struct TiffFile {
    std::ifstream f;
    bool be = false, big = false;
    uint64_t size = 0;
    struct Entry {
        uint16_t tag = 0, type = 0;
        uint64_t count = 0, value = 0;  // value: the offset, or the value itself packed (as stored)
        uint8_t raw[8] = {0};
    };
    std::map<int, Entry> tags;
    bool read(uint64_t at, void* dst, size_t n) {
        if (at + n > size) return false;
        f.seekg((std::streamoff)at);
        f.read((char*)dst, (std::streamsize)n);
        return (bool)f;
    }
    uint64_t un(const uint8_t* p, int n) const {
        uint64_t v = 0;
        for (int i = 0; i < n; i++) v |= (uint64_t)p[be ? n - 1 - i : i] << (8 * i);
        return v;
    }
    static int typeSize(int t) {
        switch (t) {
            case 1: case 2: case 6: case 7: return 1;
            case 3: case 8: return 2;
            case 4: case 9: case 11: case 13: return 4;
            case 5: case 10: case 12: case 16: case 17: case 18: return 8;
        }
        return 0;
    }
    bool open(const std::string& path, std::string* err) {
        f.open(path, std::ios::binary);
        if (!f) return fail(err, "can't open " + path);
        f.seekg(0, std::ios::end);
        size = (uint64_t)f.tellg();
        uint8_t h[16];
        if (!read(0, h, 8)) return fail(err, "not a TIFF (too short)");
        if (h[0] == 'I' && h[1] == 'I') be = false;
        else if (h[0] == 'M' && h[1] == 'M') be = true;
        else return fail(err, "not a TIFF");
        const int magic = (int)un(h + 2, 2);
        uint64_t ifd = 0;
        if (magic == 42) ifd = un(h + 4, 4);
        else if (magic == 43) {
            big = true;
            if (!read(0, h, 16)) return fail(err, "bad BigTIFF header");
            ifd = un(h + 8, 8);
        } else
            return fail(err, "not a TIFF (magic)");
        uint8_t cb[8];
        if (!read(ifd, cb, big ? 8 : 2)) return fail(err, "bad TIFF directory");
        const uint64_t n = un(cb, big ? 8 : 2);
        if (n > 4096) return fail(err, "bad TIFF directory size");
        const int es = big ? 20 : 12;
        std::vector<uint8_t> e((size_t)n * es);
        if (!read(ifd + (big ? 8 : 2), e.data(), e.size())) return fail(err, "bad TIFF directory");
        for (uint64_t i = 0; i < n; i++) {
            const uint8_t* p = &e[i * es];
            Entry en;
            en.tag = (uint16_t)un(p, 2), en.type = (uint16_t)un(p + 2, 2);
            en.count = big ? un(p + 4, 8) : un(p + 4, 4);
            std::memcpy(en.raw, p + (big ? 12 : 8), big ? 8 : 4);
            en.value = big ? un(p + 12, 8) : un(p + 8, 4);
            tags[en.tag] = en;
        }
        return true;
    }
    bool has(int tag) const { return tags.count(tag) != 0; }
    // A tag's values as doubles (and as integers, exactly).
    bool values(int tag, std::vector<double>* d, std::vector<uint64_t>* u = nullptr) {
        auto it = tags.find(tag);
        if (it == tags.end()) return false;
        const Entry& en = it->second;
        const int ts = typeSize(en.type);
        if (!ts || en.count > (1u << 28)) return false;
        const uint64_t bytes = en.count * (uint64_t)ts * (en.type == 5 || en.type == 10 ? 1 : 1);
        std::vector<uint8_t> b((size_t)bytes);
        if (bytes <= (uint64_t)(big ? 8 : 4)) std::memcpy(b.data(), en.raw, (size_t)bytes);
        else if (!read(en.value, b.data(), (size_t)bytes)) return false;
        if (d) d->clear();
        if (u) u->clear();
        for (uint64_t i = 0; i < en.count; i++) {
            const uint8_t* p = &b[(size_t)(i * ts)];
            double v = 0;
            uint64_t iv = 0;
            switch (en.type) {
                case 1: case 7: iv = p[0], v = (double)iv; break;
                case 6: v = (double)(int8_t)p[0], iv = (uint64_t)(int64_t)v; break;
                case 3: iv = un(p, 2), v = (double)iv; break;
                case 8: v = (double)(int16_t)un(p, 2), iv = (uint64_t)(int64_t)v; break;
                case 4: case 13: iv = un(p, 4), v = (double)iv; break;
                case 9: v = (double)(int32_t)un(p, 4), iv = (uint64_t)(int64_t)v; break;
                case 16: case 18: iv = un(p, 8), v = (double)iv; break;
                case 17: v = (double)(int64_t)un(p, 8), iv = un(p, 8); break;
                case 11: { const uint32_t x = (uint32_t)un(p, 4); float fl; std::memcpy(&fl, &x, 4); v = fl; break; }
                case 12: { const uint64_t x = un(p, 8); std::memcpy(&v, &x, 8); break; }
                case 5: { const double a = (double)un(p, 4), c = (double)un(p + 4, 4); v = c ? a / c : 0; break; }
                case 10: { const double a = (double)(int32_t)un(p, 4), c = (double)(int32_t)un(p + 4, 4); v = c ? a / c : 0; break; }
                default: break;
            }
            if (d) d->push_back(v);
            if (u) u->push_back(iv);
        }
        return true;
    }
    int one(int tag, int def) {
        std::vector<double> v;
        return values(tag, &v) && !v.empty() ? (int)v[0] : def;
    }
    std::string ascii(int tag) {
        auto it = tags.find(tag);
        if (it == tags.end() || it->second.type != 2 || it->second.count > 4096) return "";
        std::string s((size_t)it->second.count, '\0');
        if (it->second.count <= (uint64_t)(big ? 8 : 4)) std::memcpy(&s[0], it->second.raw, s.size());
        else if (!read(it->second.value, &s[0], s.size())) return "";
        while (!s.empty() && (s.back() == '\0' || s.back() == ' ')) s.pop_back();
        return s;
    }
};

bool readTiffInfo(TiffFile& t, TiffInfo& in, std::string* err) {
    in.bigTiff = t.big, in.bigEndian = t.be;
    in.w = t.one(256, 0), in.h = t.one(257, 0);
    in.bits = t.one(258, 1), in.compression = t.one(259, 1), in.samples = t.one(277, 1);
    in.rowsPerStrip = t.one(278, in.h), in.planar = t.one(284, 1), in.predictor = t.one(317, 1), in.sampleFormat = t.one(339, 1);
    in.tiled = t.has(322);
    in.tileW = t.one(322, 0), in.tileH = t.one(323, 0);
    if (in.w <= 0 || in.h <= 0 || in.w > 200000 || in.h > 200000) return fail(err, "TIFF: bad size");
    if (in.samples < 1 || in.samples > 16) return fail(err, "TIFF: bad samples per pixel");
    // The geo tags.
    std::vector<double> scale, tie, keys;
    if (t.values(33550, &scale) && scale.size() >= 2 && t.values(33922, &tie) && tie.size() >= 6) {
        in.dx = scale[0], in.dy = scale[1];
        in.x0 = tie[3] - tie[0] * in.dx, in.y0 = tie[4] + tie[1] * in.dy;
    }
    if (t.values(34735, &keys) && keys.size() >= 4) {
        const int nk = (int)keys[3];
        for (int k = 0; k < nk && 4 + 4 * k + 3 < (int)keys.size(); k++) {
            const int id = (int)keys[4 + 4 * k], loc = (int)keys[4 + 4 * k + 1], val = (int)keys[4 + 4 * k + 3];
            if (loc != 0) continue;
            if (id == 1024) in.modelType = val;
            if (id == 1025) in.pixelIsPoint = val == 2;
            if (id == 2048 && !in.epsg) in.epsg = val;
            if (id == 3072) in.epsg = val;
        }
    }
    if (in.pixelIsPoint) in.x0 -= 0.5 * in.dx, in.y0 += 0.5 * in.dy;
    if (in.dx > 0 && in.dy > 0) {
        // Degrees when it says it's geographic, or when it says nothing and the numbers look like it.
        const bool deg = in.modelType == 2 || (in.modelType == 0 && in.dx < 0.1 && std::fabs(in.x0) <= 360 && std::fabs(in.y0) <= 90);
        in.geo = deg ? Dem::GEO_DEGREES : Dem::GEO_METRES;
    }
    const std::string nd = t.ascii(42113);
    if (!nd.empty()) in.hasNoData = true, in.noData = std::atof(nd.c_str());
    return true;
}

// Undoes a block's predictor (rows of `w` pixels, `spp` samples of `bps` bytes, in the file's byte order) and leaves the samples in
// little-endian order.
bool unpredict(std::vector<uint8_t>& b, int w, int rows, int spp, int bps, int predictor, bool be) {
    const size_t rowBytes = (size_t)w * spp * bps;
    if (b.size() < rowBytes * rows) b.resize(rowBytes * rows, 0);
    if (predictor == 3) {
        std::vector<uint8_t> tmp(rowBytes);
        for (int r = 0; r < rows; r++) {
            uint8_t* p = &b[r * rowBytes];
            for (size_t i = spp; i < rowBytes; i++) p[i] = (uint8_t)(p[i] + p[i - spp]);
            std::memcpy(tmp.data(), p, rowBytes);
            const size_t wc = (size_t)w * spp;
            for (size_t c = 0; c < wc; c++)
                for (int k = 0; k < bps; k++) p[c * bps + k] = tmp[(size_t)(bps - 1 - k) * wc + c];  // (the planes are MSB first)
        }
        return true;
    }
    if (be && bps > 1)
        for (size_t i = 0; i + bps <= b.size(); i += bps) std::reverse(b.begin() + (long)i, b.begin() + (long)(i + bps));
    if (predictor == 2) {
        for (int r = 0; r < rows; r++) {
            uint8_t* p = &b[r * rowBytes];
            for (int x = 1; x < w; x++)
                for (int s = 0; s < spp; s++) {
                    uint8_t* cur = p + ((size_t)x * spp + s) * bps;
                    const uint8_t* prv = cur - (size_t)spp * bps;
                    if (bps == 1) cur[0] = (uint8_t)(cur[0] + prv[0]);
                    else if (bps == 2) {
                        const uint16_t v = (uint16_t)((cur[0] | cur[1] << 8) + (prv[0] | prv[1] << 8));
                        cur[0] = (uint8_t)v, cur[1] = (uint8_t)(v >> 8);
                    } else if (bps == 4) {
                        uint32_t a, c;
                        std::memcpy(&a, cur, 4), std::memcpy(&c, prv, 4);
                        a += c;
                        std::memcpy(cur, &a, 4);
                    } else
                        return false;
                }
        }
    } else if (predictor != 1)
        return false;
    return true;
}
double sampleAt(const uint8_t* p, int bps, int fmt) {
    if (fmt == 3 && bps == 4) {
        float f;
        std::memcpy(&f, p, 4);
        return f;
    }
    if (fmt == 3 && bps == 8) {
        double d;
        std::memcpy(&d, p, 8);
        return d;
    }
    if (bps == 2) {
        const uint16_t u = (uint16_t)(p[0] | p[1] << 8);
        return fmt == 2 ? (double)(int16_t)u : (double)u;
    }
    if (bps == 4) {
        uint32_t u;
        std::memcpy(&u, p, 4);
        return fmt == 2 ? (double)(int32_t)u : (double)u;
    }
    if (bps == 1) return fmt == 2 ? (double)(int8_t)p[0] : (double)p[0];
    return 0;
}
}  // namespace

// ---- Loaders ------------------------------------------------------------------------------------------------------------------
bool tiffInfo(const std::string& path, TiffInfo& out, std::string* err) {
    TiffFile t;
    if (!t.open(path, err)) return false;
    return readTiffInfo(t, out, err);
}

bool loadGeoTiff(const std::string& path, Dem& out, std::string* err, Window win) {
    TiffFile t;
    if (!t.open(path, err)) return false;
    TiffInfo in;
    if (!readTiffInfo(t, in, err)) return false;
    const int bps = in.bits / 8;
    if (in.bits % 8 || !(bps == 1 || bps == 2 || bps == 4 || bps == 8)) return fail(err, "TIFF: " + std::to_string(in.bits) + "-bit samples");
    if (!(in.sampleFormat >= 1 && in.sampleFormat <= 3)) return fail(err, "TIFF: sample format " + std::to_string(in.sampleFormat));
    if (!(in.compression == 1 || in.compression == 5 || in.compression == 8 || in.compression == 32946))
        return fail(err, "TIFF: compression " + std::to_string(in.compression) + " (only none, LZW and DEFLATE)");
    if (win.empty()) win = Window{0, 0, in.w, in.h};
    win.x0 = std::max(0, win.x0), win.y0 = std::max(0, win.y0), win.x1 = std::min(in.w, win.x1), win.y1 = std::min(in.h, win.y1);
    if (win.empty()) return fail(err, "TIFF: the crop is off the image");
    std::vector<uint64_t> offs, counts;
    std::vector<double> dummy;
    if (!t.values(in.tiled ? 324 : 273, &dummy, &offs) || !t.values(in.tiled ? 325 : 279, &dummy, &counts) || offs.size() != counts.size())
        return fail(err, "TIFF: no strips or tiles");
    const int bw = in.tiled ? in.tileW : in.w, bh = in.tiled ? in.tileH : std::max(1, std::min(in.rowsPerStrip, in.h));
    if (bw <= 0 || bh <= 0) return fail(err, "TIFF: bad tile size");
    const int across = (in.w + bw - 1) / bw, down = (in.h + bh - 1) / bh;
    const int spp = in.planar == 2 ? 1 : in.samples;  // (planar: the first plane is the first across * down blocks)
    if ((size_t)across * down > offs.size()) return fail(err, "TIFF: too few strips or tiles");
    out = Dem();
    out.format = "geotiff";
    out.w = win.x1 - win.x0, out.h = win.y1 - win.y0;
    out.v.assign((size_t)out.w * out.h, 0.0f);
    out.geo = in.geo, out.dx = in.dx, out.dy = in.dy;
    out.x0 = in.x0 + win.x0 * in.dx, out.y0 = in.y0 - win.y0 * in.dy;
    out.hasNoData = in.hasNoData, out.noData = in.noData;
    std::vector<uint8_t> comp, raw;
    for (int by = win.y0 / bh; by <= (win.y1 - 1) / bh; by++)
        for (int bx = win.x0 / bw; bx <= (win.x1 - 1) / bw; bx++) {
            const size_t k = (size_t)by * across + bx;
            const int rows = in.tiled ? bh : std::min(bh, in.h - by * bh);
            const size_t expect = (size_t)bw * rows * spp * bps;
            if (counts[k] > (uint64_t)1 << 31) return fail(err, "TIFF: a block too big");
            comp.resize((size_t)counts[k]);
            if (!t.read(offs[k], comp.data(), comp.size())) return fail(err, "TIFF: a block past the end of the file");
            if (in.compression == 1) raw = comp;
            else if (in.compression == 5) {
                if (!lzwDecode(comp.data(), comp.size(), expect, raw)) return fail(err, "TIFF: bad LZW data");
            } else if (!inflate(comp.data(), comp.size(), expect, raw))
                return fail(err, "TIFF: bad DEFLATE data");
            if (raw.size() < expect) raw.resize(expect, 0);
            if (!unpredict(raw, bw, rows, spp, bps, in.predictor, in.bigEndian)) return fail(err, "TIFF: predictor " + std::to_string(in.predictor));
            for (int r = 0; r < rows; r++) {
                const int y = by * bh + r;
                if (y < win.y0 || y >= win.y1) continue;
                for (int c = 0; c < bw; c++) {
                    const int x = bx * bw + c;
                    if (x < win.x0 || x >= win.x1) continue;
                    out.v[(size_t)(y - win.y0) * out.w + (x - win.x0)] = (float)sampleAt(&raw[((size_t)r * bw + c) * spp * bps], bps, in.sampleFormat);
                }
            }
        }
    return true;
}

bool loadHgt(const std::string& path, Dem& out, std::string* err) {
    std::vector<uint8_t> b;
    if (!readAll(path, b)) return fail(err, "can't read " + path);
    const int side = (int)std::lround(std::sqrt((double)(b.size() / 2)));
    if (side < 2 || (size_t)side * side * 2 != b.size()) return fail(err, ".hgt: not a square of 16-bit samples (" + std::to_string(b.size()) + " bytes)");
    out = Dem();
    out.format = "hgt";
    out.w = out.h = side;
    out.v.resize((size_t)side * side);
    for (size_t i = 0; i < out.v.size(); i++) out.v[i] = (float)(int16_t)(uint16_t)(b[2 * i] << 8 | b[2 * i + 1]);
    out.hasNoData = true, out.noData = -32768;
    // Its corner from the name (N38W110: the south-west corner; samples sit on whole degrees, so pixel (0, 0) is centred on the
    // north-west corner).
    const std::string n = upper(baseName(path));
    int lat = 0, lon = 0;
    char ns = 0, ew = 0;
    if (std::sscanf(n.c_str(), "%c%d%c%d", &ns, &lat, &ew, &lon) == 4 && (ns == 'N' || ns == 'S') && (ew == 'E' || ew == 'W')) {
        const double d = 1.0 / (side - 1);
        const double south = ns == 'S' ? -lat : lat, west = ew == 'W' ? -lon : lon;
        out.geo = Dem::GEO_DEGREES;
        out.dx = out.dy = d;
        out.x0 = west - 0.5 * d, out.y0 = south + 1 + 0.5 * d;
    }
    return true;
}

bool loadPng16(const std::string& path, Dem& out, std::string* err) {
    std::vector<uint8_t> b;
    if (!readAll(path, b)) return fail(err, "can't read " + path);
    if (b.size() > 0x7fffffff) return fail(err, "PNG too big");
    int w = 0, h = 0, comp = 0;
    stbi_us* px = stbi_load_16_from_memory(b.data(), (int)b.size(), &w, &h, &comp, 1);
    if (!px) return fail(err, std::string("PNG: ") + stbi_failure_reason());
    out = Dem();
    out.format = "png16";
    out.w = w, out.h = h;
    out.v.resize((size_t)w * h);
    for (size_t i = 0; i < out.v.size(); i++) out.v[i] = (float)px[i];
    stbi_image_free(px);
    return true;
}

bool loadDem(const std::string& path, Dem& out, std::string* err, Window win) {
    const std::string e = extOf(path);
    bool ok;
    if (e == "hgt") ok = loadHgt(path, out, err), crop(out, win);
    else if (e == "png") ok = loadPng16(path, out, err), crop(out, win);
    else if (e == "tif" || e == "tiff") ok = loadGeoTiff(path, out, err, win);
    else return fail(err, "unknown DEM format: " + path + " (.hgt, .png or .tif)");
    if (ok) fillNoData(out);
    return ok;
}

bool demHeader(const std::string& path, Dem& out, std::string* err) {
    const std::string e = extOf(path);
    if (e == "tif" || e == "tiff") {
        TiffInfo in;
        if (!tiffInfo(path, in, err)) return false;
        out = Dem();
        out.format = "geotiff", out.w = in.w, out.h = in.h, out.geo = in.geo, out.x0 = in.x0, out.y0 = in.y0, out.dx = in.dx, out.dy = in.dy;
        out.hasNoData = in.hasNoData, out.noData = in.noData;
        return true;
    }
    if (!loadDem(path, out, err)) return false;
    return true;
}

// ---- Writers --------------------------------------------------------------------------------------------------------------------
bool writeHgt(const std::string& path, int side, const std::vector<int16_t>& v) {
    if ((size_t)side * side != v.size()) return false;
    std::vector<uint8_t> b(v.size() * 2);
    for (size_t i = 0; i < v.size(); i++) b[2 * i] = (uint8_t)((uint16_t)v[i] >> 8), b[2 * i + 1] = (uint8_t)((uint16_t)v[i] & 255);
    return writeAll(path, b);
}

bool writePng16(const std::string& path, int w, int h, const std::vector<uint16_t>& v) {
    if ((size_t)w * h != v.size()) return false;
    std::vector<uint8_t> o = {0x89, 'P', 'N', 'G', '\r', '\n', 0x1a, '\n'};
    auto be32 = [](std::vector<uint8_t>& b, uint32_t x) {
        for (int s = 24; s >= 0; s -= 8) b.push_back((uint8_t)(x >> s));
    };
    auto chunk = [&](const char* type, const std::vector<uint8_t>& data) {
        be32(o, (uint32_t)data.size());
        const size_t at = o.size();
        o.insert(o.end(), type, type + 4);
        o.insert(o.end(), data.begin(), data.end());
        be32(o, crc32(&o[at], o.size() - at));
    };
    std::vector<uint8_t> ihdr;
    be32(ihdr, (uint32_t)w), be32(ihdr, (uint32_t)h);
    ihdr.insert(ihdr.end(), {16, 0, 0, 0, 0});
    chunk("IHDR", ihdr);
    std::vector<uint8_t> raw;
    raw.reserve((size_t)h * (1 + 2 * w));
    for (int y = 0; y < h; y++) {
        raw.push_back(0);
        for (int x = 0; x < w; x++) raw.push_back((uint8_t)(v[(size_t)y * w + x] >> 8)), raw.push_back((uint8_t)(v[(size_t)y * w + x] & 255));
    }
    chunk("IDAT", zlibStored(raw));
    chunk("IEND", {});
    return writeAll(path, o);
}

bool writeGeoTiff(const std::string& path, const Dem& dem, const TiffOptions& opt) {
    if (dem.w <= 0 || dem.h <= 0 || dem.v.size() != (size_t)dem.w * dem.h) return false;
    const bool be = opt.bigEndian;
    const int bps = opt.int16 ? 2 : 4;
    std::vector<uint8_t> o(8, 0);
    auto put = [&](std::vector<uint8_t>& b, uint64_t v, int n) {
        for (int i = 0; i < n; i++) b.push_back((uint8_t)(v >> (8 * (be ? n - 1 - i : i))));
    };
    o.clear();
    o.push_back(be ? 'M' : 'I'), o.push_back(be ? 'M' : 'I');
    put(o, 42, 2), put(o, 0, 4);  // (the directory's offset, filled in at the end)
    const int bw = opt.tiled ? opt.tile : dem.w, bh = opt.tiled ? opt.tile : std::max(1, std::min(dem.h, 16));
    const int across = (dem.w + bw - 1) / bw, down = (dem.h + bh - 1) / bh;
    std::vector<uint64_t> offs, counts;
    for (int by = 0; by < down; by++)
        for (int bx = 0; bx < across; bx++) {
            const int rows = opt.tiled ? bh : std::min(bh, dem.h - by * bh);
            const size_t rowBytes = (size_t)bw * bps;
            std::vector<uint8_t> raw(rowBytes * rows, 0);
            for (int r = 0; r < rows; r++)
                for (int c = 0; c < bw; c++) {
                    const int x = bx * bw + c, y = by * bh + r;
                    const float fv = x < dem.w && y < dem.h ? dem.v[(size_t)y * dem.w + x] : 0.0f;
                    uint32_t u;
                    if (opt.int16) u = (uint16_t)(int16_t)std::lround(fv);
                    else std::memcpy(&u, &fv, 4);
                    uint8_t* p = &raw[r * rowBytes + (size_t)c * bps];
                    for (int k = 0; k < bps; k++) p[k] = (uint8_t)(u >> (8 * (be || opt.predictor == 3 ? bps - 1 - k : k)));
                }
            if (opt.predictor == 3) {  // byte planes, most significant first, then differenced along the row
                for (int r = 0; r < rows; r++) {
                    uint8_t* p = &raw[r * rowBytes];
                    std::vector<uint8_t> t(p, p + rowBytes);
                    for (int c = 0; c < bw; c++)
                        for (int k = 0; k < bps; k++) p[(size_t)k * bw + c] = t[(size_t)c * bps + k];
                    for (size_t i = rowBytes - 1; i >= 1; i--) p[i] = (uint8_t)(p[i] - p[i - 1]);
                }
            } else if (opt.predictor == 2) {
                for (int r = 0; r < rows; r++)
                    for (int c = bw - 1; c >= 1; c--) {
                        uint8_t* cur = &raw[r * rowBytes + (size_t)c * bps];
                        const uint8_t* prv = cur - bps;
                        uint64_t a = 0, b = 0;
                        for (int k = 0; k < bps; k++) a |= (uint64_t)cur[k] << (8 * (be ? bps - 1 - k : k)), b |= (uint64_t)prv[k] << (8 * (be ? bps - 1 - k : k));
                        const uint64_t d = (a - b) & (bps == 2 ? 0xffffu : 0xffffffffu);
                        for (int k = 0; k < bps; k++) cur[k] = (uint8_t)(d >> (8 * (be ? bps - 1 - k : k)));
                    }
            }
            std::vector<uint8_t> enc = opt.compression == 5 ? lzwEncode(raw) : opt.compression == 8 ? zlibStored(raw) : raw;
            offs.push_back(o.size()), counts.push_back(enc.size());
            o.insert(o.end(), enc.begin(), enc.end());
            if (o.size() & 1) o.push_back(0);
        }
    // Extra data the directory points to.
    struct Tag {
        int tag, type;
        uint64_t count;
        std::vector<uint8_t> data;
    };
    std::vector<Tag> tags;
    auto shorts = [&](int tag, std::vector<uint64_t> v) {
        Tag t{tag, 3, v.size(), {}};
        for (uint64_t x : v) put(t.data, x, 2);
        tags.push_back(t);
    };
    auto longs = [&](int tag, std::vector<uint64_t> v) {
        Tag t{tag, 4, v.size(), {}};
        for (uint64_t x : v) put(t.data, x, 4);
        tags.push_back(t);
    };
    auto doubles = [&](int tag, std::vector<double> v) {
        Tag t{tag, 12, v.size(), {}};
        for (double x : v) {
            uint64_t u;
            std::memcpy(&u, &x, 8);
            put(t.data, u, 8);
        }
        tags.push_back(t);
    };
    longs(256, {(uint64_t)dem.w}), longs(257, {(uint64_t)dem.h});
    shorts(258, {(uint64_t)bps * 8}), shorts(259, {(uint64_t)opt.compression}), shorts(262, {1});
    if (!opt.tiled) longs(273, offs);
    shorts(277, {1});
    if (!opt.tiled) longs(278, {(uint64_t)bh}), longs(279, counts);
    shorts(284, {1});
    if (opt.predictor != 1) shorts(317, {(uint64_t)opt.predictor});
    if (opt.tiled) longs(322, {(uint64_t)bw}), longs(323, {(uint64_t)bh}), longs(324, offs), longs(325, counts);
    shorts(339, {(uint64_t)(opt.int16 ? 2 : 3)});
    if (dem.geo != Dem::GEO_NONE) {
        doubles(33550, {dem.dx, dem.dy, 0});
        doubles(33922, {0, 0, 0, dem.x0, dem.y0, 0});
        const bool deg = dem.geo == Dem::GEO_DEGREES;
        shorts(34735, {1, 1, 0, 3, 1024, 0, 1, (uint64_t)(deg ? 2 : 1), 1025, 0, 1, 1, (uint64_t)(deg ? 2048 : 3072), 0, 1, (uint64_t)(deg ? 4269 : 32767)});
    }
    if (dem.hasNoData) {
        char b[64];
        std::snprintf(b, sizeof b, "%.10g", dem.noData);
        Tag t{42113, 2, std::strlen(b) + 1, std::vector<uint8_t>(b, b + std::strlen(b) + 1)};
        tags.push_back(t);
    }
    std::sort(tags.begin(), tags.end(), [](const Tag& a, const Tag& b) { return a.tag < b.tag; });
    for (Tag& t : tags)
        if (t.data.size() > 4) {
            const uint64_t at = o.size();
            o.insert(o.end(), t.data.begin(), t.data.end());
            if (o.size() & 1) o.push_back(0);
            t.data.clear();
            put(t.data, at, 4);
            t.type = -t.type;  // (marks: an offset now)
        }
    const uint64_t ifd = o.size();
    std::vector<uint8_t> hdr;
    put(hdr, ifd, 4);
    std::memcpy(&o[4], hdr.data(), 4);
    put(o, tags.size(), 2);
    for (const Tag& t : tags) {
        put(o, (uint64_t)t.tag, 2), put(o, (uint64_t)std::abs(t.type), 2), put(o, t.count, 4);
        std::vector<uint8_t> v = t.data;
        if (t.type > 0 && v.size() < 4) {  // (a short value sits at the low-address end of the field)
            v.resize(4, 0);
        }
        o.insert(o.end(), v.begin(), v.begin() + 4);
    }
    put(o, 0, 4);
    return writeAll(path, o);
}

// ---- Arithmetic that's the same everywhere -----------------------------------------------------------------------------------
double cosDeg(double deg) {
    // Into [-180, 180], then [0, 90] by symmetry, then the series.
    double a = deg - 360.0 * std::floor((deg + 180.0) / 360.0);
    if (a < 0) a = -a;
    double sign = 1;
    if (a > 90) a = 180 - a, sign = -1;
    const double x = a * (M_PI / 180.0), x2 = x * x;
    double term = 1, sum = 1;
    for (int k = 1; k < 14; k++) {
        term *= -x2 / ((2 * k - 1) * (2 * k));
        sum += term;
    }
    return sign * sum;
}
double metresPerDegLat(double lat) { return 111132.954 - 559.822 * cosDeg(2 * lat) + 1.175 * cosDeg(4 * lat); }
double metresPerDegLon(double lat) { return 111412.84 * cosDeg(lat) - 93.5 * cosDeg(3 * lat) + 0.118 * cosDeg(5 * lat); }

// ---- The sidecar ----------------------------------------------------------------------------------------------------------------
bool parseSpec(const std::string& text, const std::string& dir, Spec& s, std::string* err) {
    s = Spec();
    s.dir = dir, s.text = text;
    std::istringstream in(text);
    std::string line;
    int ln = 0;
    while (std::getline(in, line)) {
        ln++;
        const size_t hash = line.find('#');
        if (hash != std::string::npos) line.resize(hash);
        std::istringstream ls(line);
        std::string key;
        if (!(ls >> key)) continue;
        key = lower(key);
        std::vector<std::string> a;
        for (std::string w; ls >> w;) a.push_back(w);
        auto num = [&](size_t i) { return i < a.size() ? std::atof(a[i].c_str()) : 0.0; };
        auto need = [&](size_t n) {
            if (a.size() >= n) return true;
            if (err) *err = "line " + std::to_string(ln) + ": " + key + " needs " + std::to_string(n) + " value(s)";
            return false;
        };
        if (key == "name") {
            std::string n;
            for (const std::string& w : a) n += (n.empty() ? "" : " ") + w;
            s.name = upper(n.substr(0, 24));
        } else if (key == "source" || key == "file" || key == "dem") {
            if (!need(1)) return false;
            s.source = a[0];
        } else if (key == "metres_per_pixel" || key == "meters_per_pixel" || key == "mpp") {
            if (!need(1)) return false;
            s.metresPerPixel = num(0);
        } else if (key == "vertical_scale") s.verticalScale = num(0);
        else if (key == "vertical_offset") s.verticalOffset = num(0);
        else if (key == "exaggeration") s.exaggeration = std::max(0.1, num(0));
        else if (key == "sea_level" || key == "water_level") s.seaLevel = num(0);
        else if (key == "crop" || key == "crop_px") {
            if (!need(4)) return false;
            s.cropPx = Window{(int)num(0), (int)num(1), (int)num(2), (int)num(3)};
        } else if (key == "bounds" || key == "crop_latlon") {
            if (!need(4)) return false;
            s.hasBounds = true, s.south = num(0), s.west = num(1), s.north = num(2), s.east = num(3);
        } else if (key == "centre" || key == "center") {
            if (!need(2)) return false;
            s.hasCentre = true, s.centreLat = num(0), s.centreLon = num(1);
        } else if (key == "size") s.size = num(0);
        else if (key == "smooth") s.smooth = std::max(0.0, num(0));
        else if (key == "detail") s.detail = num(0);
        else if (key == "climate") {
            const std::string c = a.empty() ? "" : lower(a[0]);
            s.climate = c == "desert" ? CLIMATE_DESERT : c == "alpine" ? CLIMATE_ALPINE : CLIMATE_TEMPERATE;
        } else if (key == "snow_line") s.snowLine = num(0);
        else if (key == "tree_line") s.treeLine = num(0);
        else if (key == "trees") s.trees = std::max(0.0, num(0));
        else if (key == "bushes") s.bushes = std::max(0.0, num(0));
        else if (key == "rocks") s.rocks = std::max(0.0, num(0));
        else if (key == "ledges") s.ledgeStep = std::max(0.0, num(0)), s.ledgeStrength = a.size() > 1 ? clampd(num(1), 0, 1) : 0.8;
        else if (key == "river_area") s.riverArea = num(0);
        else if (key == "wash_area") s.washArea = num(0);
        else if (key == "lake_area") s.lakeArea = std::max(0.0, num(0));
        else if (key == "outposts") s.outposts = (int)num(0);
        else if (key == "seed") s.seed = (uint32_t)num(0);
        else {
            if (err) *err = "line " + std::to_string(ln) + ": unknown key '" + key + "'";
            return false;
        }
    }
    if (s.source.empty()) return fail(err, "no 'source' (the DEM file)");
    if (s.riverArea < 0) s.riverArea = s.climate == CLIMATE_DESERT ? 40 : s.climate == CLIMATE_ALPINE ? 1.5 : 2.0;
    if (s.washArea < 0) s.washArea = s.climate == CLIMATE_DESERT ? 0.25 : s.climate == CLIMATE_ALPINE ? 0.3 : 0.4;
    const bool abs = !s.source.empty() && (s.source[0] == '/' || s.source[0] == '\\' || (s.source.size() > 1 && s.source[1] == ':'));
    s.sourcePath = abs || dir.empty() ? s.source : dir + "/" + s.source;
    return true;
}

uint64_t hashFile(const std::string& path, bool* ok) {
    std::ifstream f(path, std::ios::binary);
    if (ok) *ok = (bool)f;
    uint64_t h = 1469598103934665603ull;
    if (!f) return 0;
    std::vector<char> buf(1 << 20);
    // (FNV-1a over 8-byte words: the same on any machine, and fast enough for a 500 MB tile.)
    while (f) {
        f.read(buf.data(), (std::streamsize)buf.size());
        const size_t n = (size_t)f.gcount();
        size_t i = 0;
        for (; i + 8 <= n; i += 8) {
            uint64_t w = 0;
            for (int k = 0; k < 8; k++) w |= (uint64_t)(uint8_t)buf[i + k] << (8 * k);
            h = (h ^ w) * 1099511628211ull;
        }
        for (; i < n; i++) h = (h ^ (uint8_t)buf[i]) * 1099511628211ull;
    }
    return h;
}

bool loadSpec(const std::string& path, Spec& out, std::string* err) {
    namespace fs = std::filesystem;
    std::error_code ec;
    std::string file = path, dir;
    if (fs::is_directory(path, ec)) {
        dir = path;
        file.clear();
        if (fs::exists(path + "/terrain.txt", ec)) file = path + "/terrain.txt";
        else {
            std::vector<std::string> found;
            for (const auto& e : fs::directory_iterator(path, ec)) {
                const std::string n = e.path().filename().string();
                if (n.size() > 12 && n.compare(n.size() - 12, 12, ".terrain.txt") == 0) found.push_back(e.path().string());
            }
            std::sort(found.begin(), found.end());
            if (!found.empty()) file = found[0];
        }
        if (file.empty()) return fail(err, "no terrain.txt in " + path);
    } else {
        const size_t s = path.find_last_of("/\\");
        dir = s == std::string::npos ? "." : path.substr(0, s);
    }
    std::ifstream f(file);
    if (!f) return fail(err, "can't read " + file);
    std::stringstream ss;
    ss << f.rdbuf();
    if (!parseSpec(ss.str(), dir, out, err)) return false;
    if (out.name == "REAL") {  // (no name: the folder's)
        std::string n = baseName(dir == "." ? fs::current_path(ec).string() : dir);
        if (!n.empty()) out.name = upper(n.substr(0, 24));
    }
    bool ok = false;
    const uint64_t fh = hashFile(out.sourcePath, &ok);
    if (!ok) return fail(err, "can't read the DEM " + out.sourcePath);
    uint64_t h = fh;
    for (char c : out.text) h = (h ^ (uint8_t)c) * 1099511628211ull;
    out.hash = h ? h : 1;
    return true;
}

// ---- The prepared land ---------------------------------------------------------------------------------------------------------
std::shared_ptr<const Terrain> prepare(const Spec& spec, std::string* err) {
    auto t = std::make_shared<Terrain>();
    t->spec = spec;
    Dem head;
    if (!demHeader(spec.sourcePath, head, err)) return nullptr;
    // Metres per pixel, at the middle of what's used.
    double lat = 0;
    if (head.geo == Dem::GEO_DEGREES) lat = spec.hasCentre ? spec.centreLat : spec.hasBounds ? 0.5 * (spec.south + spec.north) : head.y0 - 0.5 * head.h * head.dy;
    double mx = spec.metresPerPixel, my = spec.metresPerPixel;
    if (mx <= 0) {
        if (head.geo == Dem::GEO_DEGREES) mx = head.dx * metresPerDegLon(lat), my = head.dy * metresPerDegLat(lat);
        else if (head.geo == Dem::GEO_METRES) mx = head.dx, my = head.dy;
        else {
            if (err) *err = "the file doesn't say how big a pixel is: give metres_per_pixel";
            return nullptr;
        }
    }
    t->mppX = mx, t->mppY = my;
    // The crop, in pixels (with 3 pixels' margin for the bicubic).
    Window win = spec.cropPx;
    double cxAbs = -1, cyAbs = -1;  // the map's middle (pixel, centre convention), in the whole file
    auto toPx = [&](double lon, double la, double& px, double& py) {
        px = (lon - head.x0) / head.dx - 0.5, py = (head.y0 - la) / head.dy - 0.5;
    };
    if (spec.hasCentre) {
        if (head.geo != Dem::GEO_DEGREES) {
            if (err) *err = "'centre' needs a file in degrees (use crop for others)";
            return nullptr;
        }
        toPx(spec.centreLon, spec.centreLat, cxAbs, cyAbs);
        const double size = spec.size > 0 ? spec.size : 6000;
        const int rx = (int)std::ceil(0.5 * size / mx) + 3, ry = (int)std::ceil(0.5 * size / my) + 3;
        win = Window{(int)std::floor(cxAbs) - rx, (int)std::floor(cyAbs) - ry, (int)std::floor(cxAbs) + rx + 2, (int)std::floor(cyAbs) + ry + 2};
    } else if (spec.hasBounds) {
        if (head.geo != Dem::GEO_DEGREES) {
            if (err) *err = "'bounds' needs a file in degrees (use crop for others)";
            return nullptr;
        }
        double ax, ay, bx, by;
        toPx(spec.west, spec.north, ax, ay), toPx(spec.east, spec.south, bx, by);
        win = Window{(int)std::floor(ax) - 3, (int)std::floor(ay) - 3, (int)std::ceil(bx) + 4, (int)std::ceil(by) + 4};
        cxAbs = 0.5 * (ax + bx), cyAbs = 0.5 * (ay + by);
    }
    if (!win.empty()) {
        win.x0 = std::max(0, win.x0), win.y0 = std::max(0, win.y0), win.x1 = std::min(head.w, win.x1), win.y1 = std::min(head.h, win.y1);
        if (win.empty() || win.x1 - win.x0 < 4 || win.y1 - win.y0 < 4) {
            if (err) *err = "the crop is off the file";
            return nullptr;
        }
    }
    t->window = win;
    if (!loadDem(spec.sourcePath, t->dem, err, win)) return nullptr;
    Dem& d = t->dem;
    const int ox = win.empty() ? 0 : win.x0, oy = win.empty() ? 0 : win.y0;
    t->cx = cxAbs >= 0 ? cxAbs - ox : 0.5 * (d.w - 1);
    t->cy = cyAbs >= 0 ? cyAbs - oy : 0.5 * (d.h - 1);
    // Into metres over the lowest point, stretched.
    double lo = 1e30;
    for (float& v : d.v) {
        v = (float)(v * spec.verticalScale + spec.verticalOffset);
        lo = std::min(lo, (double)v);
    }
    t->base = lo;
    for (float& v : d.v) v = (float)((v - lo) * spec.exaggeration);
    // A light smoothing (1-2-1 each way), pass by pass.
    for (int pass = 0; pass < (int)std::lround(spec.smooth); pass++) {
        std::vector<float> a(d.v.size());
        for (int y = 0; y < d.h; y++)
            for (int x = 0; x < d.w; x++) a[(size_t)y * d.w + x] = (float)(0.25 * d.at(x - 1, y) + 0.5 * d.at(x, y) + 0.25 * d.at(x + 1, y));
        for (int y = 0; y < d.h; y++)
            for (int x = 0; x < d.w; x++) {
                auto A = [&](int yy) { yy = yy < 0 ? 0 : yy >= d.h ? d.h - 1 : yy; return (double)a[(size_t)yy * d.w + x]; };
                d.v[(size_t)y * d.w + x] = (float)(0.25 * A(y - 1) + 0.5 * A(y) + 0.25 * A(y + 1));
            }
    }
    // The map's size: as asked, else as much as the crop holds round its middle; whole metres, 150 m to 6144 m a half.
    double half = spec.size > 0 ? 0.5 * spec.size : std::min(std::min(t->cx, d.w - 1 - t->cx) * mx, std::min(t->cy, d.h - 1 - t->cy) * my) - 2 * std::max(mx, my);
    half = std::floor(clampd(half, 150, 6144));  // (12 km at most: 150 M cells, about twice the 8 km big map's memory)
    t->half = half;
    const double mpp = std::max(mx, my);
    t->detailAmp = spec.detail >= 0 ? spec.detail : clampd(0.06 * mpp, 0.08, 1.0);
    return t;
}

double Terrain::demAt(double x, double z) const {
    const double px = cx + x / mppX, py = cy - z / mppY;
    const double fx = std::floor(px), fy = std::floor(py);
    const int ix = (int)fx, iy = (int)fy;
    const double tx = px - fx, ty = py - fy;
    auto w4 = [](double t, double* w) {
        const double t2 = t * t, t3 = t2 * t;
        w[0] = 0.5 * (-t3 + 2 * t2 - t), w[1] = 0.5 * (3 * t3 - 5 * t2 + 2), w[2] = 0.5 * (-3 * t3 + 4 * t2 + t), w[3] = 0.5 * (t3 - t2);
    };
    double wx[4], wy[4];
    w4(tx, wx), w4(ty, wy);
    double s = 0;
    for (int j = 0; j < 4; j++) {
        double r = 0;
        for (int i = 0; i < 4; i++) r += wx[i] * dem.at(ix - 1 + i, iy - 1 + j);
        s += wy[j] * r;
    }
    return s;
}
double Terrain::landAt(double x, double z) const {
    const double a = detailAmp;
    return demAt(x, z) + a * (fbmNoise(x * 0.09 + 5.0, z * 0.09 + 1.0, 2) - 0.5) + 0.5 * a * (valueNoise(x * 0.31, z * 0.27) - 0.5);
}
double Terrain::rimAt(double x, double z) const {
    const double ex = std::max(0.0, std::fabs(x) - (half - 130)), ez = std::max(0.0, std::fabs(z) - (half - 130));
    const double d = std::sqrt(ex * ex + ez * ez);
    return 0.0045 * d * d + 0.02 * d;
}
double Terrain::heightAt(double x, double z) const { return landAt(x, z) + rimAt(x, z); }

// ---- The active real map --------------------------------------------------------------------------------------------------------
namespace {
std::mutex gLock;
std::shared_ptr<const Terrain> gActive;
std::vector<std::shared_ptr<const Terrain>> gKept;  // (every terrain made active stays: its places are handed out by reference)
bool gDiscovered = false;
std::string gDisplay = "REAL TERRAIN";

thread_local bool tlSet = false;
thread_local std::shared_ptr<const Terrain> tlActive;
thread_local std::vector<std::string> tlRoots;
std::vector<std::string> searchRoots() {
    if (tlSet) return tlRoots;
    std::vector<std::string> out;
    for (const char* rel : {"maps", "data/real"}) {
        const std::string d = findAssetDir(rel);
        if (!d.empty()) out.push_back(d);
    }
    return out;
}
}  // namespace

std::vector<std::string> discover() {
    namespace fs = std::filesystem;
    std::vector<std::string> out;
    std::error_code ec;
    for (const std::string& root : searchRoots()) {
        std::vector<std::string> dirs;
        for (const auto& e : fs::directory_iterator(root, ec))
            if (e.is_directory(ec)) dirs.push_back(e.path().string());
        std::sort(dirs.begin(), dirs.end());
        for (const std::string& d : dirs) {
            bool has = fs::exists(d + "/terrain.txt", ec);
            if (!has)
                for (const auto& e : fs::directory_iterator(d, ec)) {
                    const std::string n = e.path().filename().string();
                    if (n.size() > 12 && n.compare(n.size() - 12, 12, ".terrain.txt") == 0) has = true;
                }
            if (has) out.push_back(d);
        }
    }
    return out;
}

std::shared_ptr<const Terrain> active() {
    if (tlSet) return tlActive;
    std::lock_guard<std::mutex> lk(gLock);
    return gActive;
}
ScopedTerrain::ScopedTerrain(std::shared_ptr<const Terrain> t, std::vector<std::string> roots) : prev_(tlActive), prevSet_(tlSet), prevRoots_(tlRoots) {
    tlSet = true, tlActive = std::move(t), tlRoots = std::move(roots);
}
ScopedTerrain::~ScopedTerrain() { tlSet = prevSet_, tlActive = prev_, tlRoots = prevRoots_; }
void setActive(std::shared_ptr<const Terrain> t) {
    if (tlSet) {
        tlActive = t;
        std::lock_guard<std::mutex> lk(gLock);
        if (t) gKept.push_back(t);
        return;
    }
    std::lock_guard<std::mutex> lk(gLock);
    gActive = t;
    if (t) gKept.push_back(t);
    gDisplay = t ? "REAL: " + t->spec.name : "REAL TERRAIN";
}
bool selectPath(const std::string& path, std::string* err) {
    Spec s;
    if (!loadSpec(path, s, err)) return false;
    std::shared_ptr<const Terrain> t = prepare(s, err);
    if (!t) return false;
    setActive(t);
    return true;
}
bool selectByHash(uint64_t hash, const std::string& nameHint) {
    if (activeHash() == hash) return true;
    std::vector<std::string> ds = discover();
    // (The one named like it first: then nothing else needs hashing.)
    std::stable_sort(ds.begin(), ds.end(), [&](const std::string& a, const std::string& b) {
        return (upper(baseName(a)) == nameHint) > (upper(baseName(b)) == nameHint);
    });
    for (const std::string& d : ds) {
        Spec s;
        if (loadSpec(d, s) && s.hash == hash) {
            std::shared_ptr<const Terrain> t = prepare(s);
            if (!t) return false;
            setActive(t);
            return true;
        }
    }
    return false;
}
bool available() {
    if (tlSet) return tlActive != nullptr;
    {
        std::lock_guard<std::mutex> lk(gLock);
        if (gActive) return true;
        if (gDiscovered) return false;
        gDiscovered = true;
    }
    for (const std::string& d : discover())
        if (selectPath(d)) return true;
    return false;
}
uint64_t activeHash() {
    std::shared_ptr<const Terrain> t = active();
    return t ? t->spec.hash : 0;
}
std::string activeName() {
    std::shared_ptr<const Terrain> t = active();
    return t ? t->spec.name : "";
}
double activeHalf() {
    std::shared_ptr<const Terrain> t = active();
    return t ? t->half : World::kDefaultHalfSize;
}
const char* displayName() {
    std::lock_guard<std::mutex> lk(gLock);
    const std::string want = tlSet ? (tlActive ? "REAL: " + tlActive->spec.name : std::string("REAL TERRAIN")) : gDisplay;
    static std::string shown[64];  // (stable pointers: one per name ever shown)
    static int used = 0;
    for (int i = 0; i < used; i++)
        if (shown[i] == want) return shown[i].c_str();
    if (used < 64) shown[used] = want;
    return shown[used < 64 ? used++ : 63].c_str();
}
const std::vector<MapPlace>& places() {
    static const std::vector<MapPlace> none;
    std::shared_ptr<const Terrain> t = active();
    return t ? t->plan().places : none;
}

}  // namespace realterrain

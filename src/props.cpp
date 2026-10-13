#include "props.h"

#include <cmath>
#include <cstdio>
#include <cstring>

#ifdef _WIN32
#include <windows.h>
#else
#include <unistd.h>
#endif

namespace props {

namespace {
const char kMagic[4] = {'C', 'Z', 'P', '2'};

struct File {
    FILE* f = nullptr;
    explicit File(const std::string& path, const char* mode) { f = std::fopen(path.c_str(), mode); }
    ~File() {
        if (f) std::fclose(f);
    }
    bool ok() const { return f != nullptr; }
    template <class T>
    void put(const T& v) {
        std::fwrite(&v, sizeof(T), 1, f);
    }
    template <class T>
    bool get(T& v) {
        return std::fread(&v, sizeof(T), 1, f) == 1;
    }
    void putStr(const std::string& s) {
        const uint16_t n = (uint16_t)s.size();
        put(n);
        std::fwrite(s.data(), 1, n, f);
    }
    bool getStr(std::string& s) {
        uint16_t n = 0;
        if (!get(n)) return false;
        s.resize(n);
        return n == 0 || std::fread(&s[0], 1, n, f) == n;
    }
};

int8_t q8(float v) { return (int8_t)std::lround(std::fmax(-1.0f, std::fmin(1.0f, v)) * 127.0f); }
}  // namespace

bool saveModel(const std::string& path, const Model& m) {
    File out(path, "wb");
    if (!out.ok()) return false;
    // Quantisation bounds: every position of every LOD.
    float qmin[3] = {1e30f, 1e30f, 1e30f}, qmax[3] = {-1e30f, -1e30f, -1e30f};
    for (const Lod& l : m.lods)
        for (const Sub& s : l.subs)
            for (const Vert& v : s.v)
                for (int k = 0; k < 3; k++) qmin[k] = std::fmin(qmin[k], v.p[k]), qmax[k] = std::fmax(qmax[k], v.p[k]);
    for (int k = 0; k < 3; k++)
        if (qmax[k] - qmin[k] < 1e-6f) qmax[k] = qmin[k] + 1e-6f;
    std::fwrite(kMagic, 1, 4, out.f);
    out.putStr(m.name);
    out.put((uint8_t)m.kind);
    for (int k = 0; k < 3; k++) out.put(m.bmin[k]);
    for (int k = 0; k < 3; k++) out.put(m.bmax[k]);
    for (int k = 0; k < 3; k++) out.put(qmin[k]);
    for (int k = 0; k < 3; k++) out.put(qmax[k]);
    out.put((uint8_t)m.mats.size());
    for (const Material& mt : m.mats) {
        out.put((uint8_t)mt.alphaMode);
        out.put((uint8_t)((mt.doubleSided ? 1 : 0) | (mt.foliage ? 2 : 0)));
        out.put(mt.wind);
        out.put(mt.cutoff);
        for (int k = 0; k < 3; k++) out.putStr(mt.tex[k]);
    }
    out.put((uint8_t)m.lods.size());
    for (const Lod& l : m.lods) {
        out.put(l.maxDist);
        out.put((uint8_t)l.subs.size());
        for (const Sub& s : l.subs) {
            out.put((uint8_t)s.mat);
            out.put((uint32_t)s.v.size());
            out.put((uint32_t)s.idx.size());
            for (const Vert& v : s.v) {
                for (int k = 0; k < 3; k++) {
                    const double t = (v.p[k] - qmin[k]) / (qmax[k] - qmin[k]);
                    out.put((int16_t)std::lround(t * 65535.0 - 32768.0));
                }
                for (int k = 0; k < 3; k++) out.put(q8(v.n[k]));
                for (int k = 0; k < 2; k++) {
                    const double t = std::fmax(-7.99, std::fmin(7.99, (double)v.uv[k]));
                    out.put((int16_t)std::lround(t * 4096.0));
                }
                for (int k = 0; k < 4; k++) out.put(q8(v.t[k]));
            }
            std::fwrite(s.idx.data(), sizeof(uint16_t), s.idx.size(), out.f);
        }
    }
    return true;
}

bool loadModel(const std::string& path, Model& m) {
    File in(path, "rb");
    if (!in.ok()) return false;
    char magic[4];
    if (std::fread(magic, 1, 4, in.f) != 4 || std::memcmp(magic, kMagic, 4) != 0) return false;
    m = Model();
    uint8_t u8 = 0;
    float qmin[3], qmax[3];
    if (!in.getStr(m.name) || !in.get(u8)) return false;
    m.kind = u8;
    for (int k = 0; k < 3; k++) in.get(m.bmin[k]);
    for (int k = 0; k < 3; k++) in.get(m.bmax[k]);
    for (int k = 0; k < 3; k++) in.get(qmin[k]);
    for (int k = 0; k < 3; k++) in.get(qmax[k]);
    uint8_t nMat = 0, nLod = 0;
    if (!in.get(nMat)) return false;
    m.mats.resize(nMat);
    for (Material& mt : m.mats) {
        uint8_t a = 0, fl = 0;
        in.get(a);
        in.get(fl);
        mt.alphaMode = a;
        mt.doubleSided = fl & 1;
        mt.foliage = (fl & 2) != 0;
        in.get(mt.wind);
        in.get(mt.cutoff);
        for (int k = 0; k < 3; k++)
            if (!in.getStr(mt.tex[k])) return false;
    }
    if (!in.get(nLod)) return false;
    m.lods.resize(nLod);
    for (Lod& l : m.lods) {
        uint8_t nSub = 0;
        in.get(l.maxDist);
        if (!in.get(nSub)) return false;
        l.subs.resize(nSub);
        for (Sub& s : l.subs) {
            uint8_t mat = 0;
            uint32_t nv = 0, ni = 0;
            in.get(mat);
            in.get(nv);
            if (!in.get(ni) || nv > 65536 || ni > (1u << 24)) return false;
            s.mat = mat;
            s.v.resize(nv);
            for (Vert& v : s.v) {
                for (int k = 0; k < 3; k++) {
                    int16_t q = 0;
                    in.get(q);
                    v.p[k] = qmin[k] + (float)((q + 32768.0) / 65535.0) * (qmax[k] - qmin[k]);
                }
                for (int k = 0; k < 3; k++) {
                    int8_t q = 0;
                    in.get(q);
                    v.n[k] = q / 127.0f;
                }
                for (int k = 0; k < 2; k++) {
                    int16_t q = 0;
                    in.get(q);
                    v.uv[k] = q / 4096.0f;
                }
                for (int k = 0; k < 4; k++) {
                    int8_t q = 0;
                    in.get(q);
                    v.t[k] = q / 127.0f;
                }
            }
            s.idx.resize(ni);
            if (ni && std::fread(s.idx.data(), sizeof(uint16_t), ni, in.f) != ni) return false;
        }
    }
    return true;
}

std::string assetDir() {
    static bool done = false;
    static std::string found;
    if (done) return found;
    done = true;
    std::vector<std::string> bases = {"."};
    char buf[4096];
#ifdef _WIN32
    const DWORD n = GetModuleFileNameA(nullptr, buf, sizeof buf - 1);
    if (n > 0 && n < sizeof buf) {
        buf[n] = 0;
        std::string exe = buf;
        const size_t slash = exe.find_last_of("\\/");
        if (slash != std::string::npos) bases.push_back(exe.substr(0, slash));
    }
#else
    const ssize_t n = readlink("/proc/self/exe", buf, sizeof buf - 1);
    if (n > 0) {
        buf[n] = 0;
        std::string exe = buf;
        const size_t slash = exe.find_last_of('/');
        if (slash != std::string::npos) bases.push_back(exe.substr(0, slash));
    }
#endif
    const char* rel[] = {"/assets/props", "/../assets/props", "/../../assets/props", "/../../../assets/props"};
    for (const std::string& b : bases)
        for (const char* r : rel) {
            const std::string dir = b + r;
            if (FILE* f = std::fopen((dir + "/rocks.hull").c_str(), "rb")) {
                std::fclose(f);
                found = dir;
                return found;
            }
        }
    return found;
}

bool HullLibrary::load(const std::string& dir) {
    hulls.clear();
    FILE* f = std::fopen((dir + "/rocks.hull").c_str(), "rb");
    if (!f) return false;
    int count = 0;
    if (std::fscanf(f, "CZHULL 1 %d", &count) != 1 || count <= 0 || count > 4096) {
        std::fclose(f);
        return false;
    }
    for (int i = 0; i < count; i++) {
        RockHull h;
        char name[128];
        int nv = 0, nf = 0;
        if (std::fscanf(f, " %127s %d %d", name, &nv, &nf) != 3 || nv < 4 || nv > 200 || nf < 4 || nf > 400) {
            hulls.clear();
            std::fclose(f);
            return false;
        }
        h.name = name;
        for (int v = 0; v < nv; v++) {
            double x, y, z;
            if (std::fscanf(f, "%lf %lf %lf", &x, &y, &z) != 3) {
                hulls.clear();
                std::fclose(f);
                return false;
            }
            h.v.push_back(V3(x, y, z));
        }
        for (int t = 0; t < nf; t++) {
            int a, b, c;
            if (std::fscanf(f, "%d %d %d", &a, &b, &c) != 3 || a < 0 || b < 0 || c < 0 || a >= nv || b >= nv || c >= nv) {
                hulls.clear();
                std::fclose(f);
                return false;
            }
            h.f.push_back({a, b, c});
        }
        for (int k = 0; k < 3; k++) h.bmin[k] = 1e30f, h.bmax[k] = -1e30f;
        for (const V3& p : h.v) {
            const double c3[3] = {p.x, p.y, p.z};
            for (int k = 0; k < 3; k++) h.bmin[k] = std::fmin(h.bmin[k], (float)c3[k]), h.bmax[k] = std::fmax(h.bmax[k], (float)c3[k]);
        }
        hulls.push_back(std::move(h));
    }
    std::fclose(f);
    return true;
}

bool HullLibrary::save(const std::string& path) const {
    FILE* f = std::fopen(path.c_str(), "wb");
    if (!f) return false;
    std::fprintf(f, "CZHULL 1 %d\n", (int)hulls.size());
    for (const RockHull& h : hulls) {
        std::fprintf(f, "%s %d %d\n", h.name.c_str(), (int)h.v.size(), (int)h.f.size());
        for (const V3& p : h.v) std::fprintf(f, "%.5f %.5f %.5f\n", p.x, p.y, p.z);
        for (const auto& t : h.f) std::fprintf(f, "%d %d %d\n", t[0], t[1], t[2]);
    }
    std::fclose(f);
    return true;
}

}  // namespace props

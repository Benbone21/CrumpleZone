#include "meshcache.h"

#include <atomic>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <map>
#include <mutex>
#include <sys/stat.h>

#include "platform.h"

namespace meshcache {
namespace {
constexpr uint32_t kMagic = 0x424d5a43;  // "CZMB"
constexpr uint32_t kFormat = 1;

struct State {
    std::mutex lock;
    std::string dir;
    bool haveDir = false, haveVersion = false, loaded = false, dirty = false, off = false, haveOff = false;
    uint64_t version = 0;
    std::map<uint64_t, std::vector<uint8_t>> entries;
    Stats stats;
};
State& st() {
    static State s;
    return s;
}

uint64_t exeStamp() {
    Hasher h;
    h.u64(kFormat);
    const std::string exe = exePath();
    struct stat sb;
    if (!exe.empty() && stat(exe.c_str(), &sb) == 0) h.u64((uint64_t)sb.st_size), h.u64((uint64_t)sb.st_mtime);
    else h.str(__DATE__ " " __TIME__);  // (no executable to look at: the compile time of this file will do)
    return h.h;
}

uint64_t checksum(const uint8_t* p, size_t n) {
    Hasher h;
    h.bytes(p, n);
    return h.h;
}

std::string dirLocked(State& s) {
    if (s.haveDir) return s.dir;
    const char* env = std::getenv("CZ_CACHE_DIR");  // (testing: somewhere that isn't the player's config)
    return env && *env ? std::string(env) : configDir() + "/cache";
}
std::string pathLocked(State& s) { return dirLocked(s) + "/meshes.bin"; }
uint64_t versionLocked(State& s) {
    if (!s.haveVersion) s.version = exeStamp(), s.haveVersion = true;
    return s.version;
}

// Bundle: u32 magic, u32 format, u64 code version, u32 count, u64 checksum of everything after the header,
// then count x {u64 key, u32 size}, then the blobs in the same order.
struct Header {
    uint32_t magic, format;
    uint64_t version;
    uint32_t count;
    uint64_t sum;
};

void loadLocked(State& s) {
    if (s.loaded) return;
    s.loaded = true;
    s.entries.clear();
    FILE* f = std::fopen(pathLocked(s).c_str(), "rb");
    if (!f) return;
    std::fseek(f, 0, SEEK_END);
    const long size = std::ftell(f);
    std::fseek(f, 0, SEEK_SET);
    std::vector<uint8_t> all(size > 0 ? (size_t)size : 0);
    const bool read = size > 0 && std::fread(all.data(), 1, all.size(), f) == all.size();  // (the one read)
    std::fclose(f);
    s.stats.fileBytes = all.size();
    Header h;
    if (!read || all.size() < sizeof h) {
        s.stats.stale++;
        return;
    }
    std::memcpy(&h, all.data(), sizeof h);
    if (h.magic != kMagic || h.format != kFormat || h.version != versionLocked(s) || h.count > (1u << 20) ||
        h.sum != checksum(all.data() + sizeof h, all.size() - sizeof h)) {
        s.stats.stale++;  // another build's (or damaged): thrown away, the meshes are made again
        return;
    }
    size_t at = sizeof h, blob = sizeof h + (size_t)h.count * 12;
    if (blob > all.size()) {
        s.stats.stale++;
        return;
    }
    for (uint32_t i = 0; i < h.count; i++, at += 12) {
        uint64_t key;
        uint32_t n;
        std::memcpy(&key, &all[at], 8);
        std::memcpy(&n, &all[at + 8], 4);
        if (blob + n > all.size()) {
            s.entries.clear();
            s.stats.stale++;
            return;
        }
        s.entries[key].assign(all.begin() + blob, all.begin() + blob + n);
        blob += n;
    }
}
}  // namespace

void setDir(const std::string& d) {
    State& s = st();
    std::lock_guard<std::mutex> lk(s.lock);
    s.dir = d, s.haveDir = true, s.loaded = false, s.dirty = false, s.entries.clear();
}
std::string dir() {
    State& s = st();
    std::lock_guard<std::mutex> lk(s.lock);
    return dirLocked(s);
}
void setCodeVersion(uint64_t v) {
    State& s = st();
    std::lock_guard<std::mutex> lk(s.lock);
    s.version = v, s.haveVersion = true, s.loaded = false, s.entries.clear();
}
uint64_t codeVersion() {
    State& s = st();
    std::lock_guard<std::mutex> lk(s.lock);
    return versionLocked(s);
}
bool enabled() {
    State& s = st();
    std::lock_guard<std::mutex> lk(s.lock);
    if (!s.haveOff) s.off = std::getenv("CZ_NO_MESH_CACHE") != nullptr, s.haveOff = true;
    return !s.off;
}
namespace {
std::atomic<double> gMinCost{1.0};
}
void setMinCostMs(double ms) { gMinCost = ms; }
double minCostMs() { return gMinCost; }
void setEnabled(bool on) {
    State& s = st();
    std::lock_guard<std::mutex> lk(s.lock);
    s.off = !on, s.haveOff = true;
}

bool get(uint64_t key, std::vector<uint8_t>& out) {
    if (!enabled()) return false;
    State& s = st();
    std::lock_guard<std::mutex> lk(s.lock);
    loadLocked(s);
    auto it = s.entries.find(key);
    if (it == s.entries.end()) return s.stats.misses++, false;
    out = it->second;
    s.stats.hits++;
    return true;
}

void put(uint64_t key, std::vector<uint8_t> bytes) {
    if (!enabled()) return;
    State& s = st();
    std::lock_guard<std::mutex> lk(s.lock);
    loadLocked(s);  // (so what was there stays)
    s.entries[key] = std::move(bytes);
    s.dirty = true;
    s.stats.stored++;
}

bool flush() {
    if (!enabled()) return true;
    State& s = st();
    std::lock_guard<std::mutex> lk(s.lock);
    if (!s.dirty) return true;
    std::vector<uint8_t> body;
    for (const auto& e : s.entries) {
        const uint32_t n = (uint32_t)e.second.size();
        body.resize(body.size() + 12);
        std::memcpy(&body[body.size() - 12], &e.first, 8);
        std::memcpy(&body[body.size() - 4], &n, 4);
    }
    for (const auto& e : s.entries) body.insert(body.end(), e.second.begin(), e.second.end());
    Header h{kMagic, kFormat, versionLocked(s), (uint32_t)s.entries.size(), checksum(body.data(), body.size())};
    const std::string d = dirLocked(s);
    makeDir(d.substr(0, d.find_last_of('/')));  // (the config folder, if it isn't there yet)
    makeDir(d);
    const std::string path = pathLocked(s), tmp = path + ".tmp";
    FILE* f = std::fopen(tmp.c_str(), "wb");
    if (!f) return false;
    const bool ok = std::fwrite(&h, sizeof h, 1, f) == 1 && std::fwrite(body.data(), 1, body.size(), f) == body.size();
    if (std::fclose(f) != 0 || !ok) return std::remove(tmp.c_str()), false;
    std::remove(path.c_str());  // (Windows won't rename over a file)
    if (std::rename(tmp.c_str(), path.c_str()) != 0) return false;
    s.dirty = false;
    return true;
}

bool getBlob(uint64_t key, std::vector<uint8_t>& out) {
    if (!enabled()) return false;
    std::string path;
    {
        State& s = st();
        std::lock_guard<std::mutex> lk(s.lock);
        char name[40];
        std::snprintf(name, sizeof name, "/blob-%016llx.bin", (unsigned long long)key);
        path = dirLocked(s) + name;
    }
    FILE* f = std::fopen(path.c_str(), "rb");
    if (!f) return false;
    std::fseek(f, 0, SEEK_END);
    const long size = std::ftell(f);
    std::fseek(f, 0, SEEK_SET);
    std::vector<uint8_t> all(size > 0 ? (size_t)size : 0);
    const bool read = size > 0 && std::fread(all.data(), 1, all.size(), f) == all.size();
    std::fclose(f);
    uint64_t sum;
    if (!read || all.size() < 8) return false;
    std::memcpy(&sum, all.data(), 8);
    if (sum != checksum(all.data() + 8, all.size() - 8)) return false;
    out.assign(all.begin() + 8, all.end());
    return true;
}

bool putBlob(uint64_t key, const std::vector<uint8_t>& bytes) {
    if (!enabled()) return true;
    std::string d, path;
    {
        State& s = st();
        std::lock_guard<std::mutex> lk(s.lock);
        char name[40];
        std::snprintf(name, sizeof name, "/blob-%016llx.bin", (unsigned long long)key);
        d = dirLocked(s), path = d + name;
    }
    makeDir(d.substr(0, d.find_last_of('/')));
    makeDir(d);
    const std::string tmp = path + ".tmp" + std::to_string((unsigned long long)(uintptr_t)&bytes);  // (several threads write at once)
    FILE* f = std::fopen(tmp.c_str(), "wb");
    if (!f) return false;
    const uint64_t sum = checksum(bytes.data(), bytes.size());
    const bool ok = std::fwrite(&sum, 8, 1, f) == 1 && std::fwrite(bytes.data(), 1, bytes.size(), f) == bytes.size();
    if (std::fclose(f) != 0 || !ok) return std::remove(tmp.c_str()), false;
    std::remove(path.c_str());
    return std::rename(tmp.c_str(), path.c_str()) == 0;
}

std::vector<uint8_t> packEncoded(const texenc::Encoded& e) {
    std::vector<uint8_t> o;
    auto put = [&](const void* p, size_t n) { o.insert(o.end(), (const uint8_t*)p, (const uint8_t*)p + n); };
    const int32_t head[4] = {e.w, e.h, (int32_t)e.kind, (int32_t)e.levels.size()};
    const uint8_t flags[3] = {e.srgb, e.compressed, e.swizzle};
    put(head, sizeof head), put(flags, sizeof flags);
    for (const auto& l : e.levels) {
        const uint64_t n = l.size();
        put(&n, 8), put(l.data(), l.size());
    }
    return o;
}

bool unpackEncoded(const std::vector<uint8_t>& o, texenc::Encoded& e) {
    if (o.size() < 19) return false;
    int32_t head[4];
    std::memcpy(head, o.data(), sizeof head);
    e.w = head[0], e.h = head[1], e.kind = (texenc::Kind)head[2];
    e.srgb = o[16], e.compressed = o[17], e.swizzle = o[18];
    if (head[3] < 0 || head[3] > 32) return false;
    size_t at = 19;
    e.levels.assign(head[3], {});
    for (auto& l : e.levels) {
        uint64_t n;
        if (at + 8 > o.size()) return false;
        std::memcpy(&n, &o[at], 8);
        at += 8;
        if (n > o.size() - at) return false;
        l.assign(o.begin() + at, o.begin() + at + n);
        at += n;
    }
    return at == o.size();
}

uint64_t fileStamp(const std::string& path) {
    struct stat sb;
    if (stat(path.c_str(), &sb) != 0) return 0;
    Hasher h;
    h.str(path), h.u64((uint64_t)sb.st_size), h.u64((uint64_t)sb.st_mtime);
    return h.h | 1;
}

void reset() {
    State& s = st();
    std::lock_guard<std::mutex> lk(s.lock);
    s.loaded = false, s.dirty = false, s.entries.clear();
}

Stats stats() {
    State& s = st();
    std::lock_guard<std::mutex> lk(s.lock);
    return s.stats;
}
void clearStats() {
    State& s = st();
    std::lock_guard<std::mutex> lk(s.lock);
    s.stats = Stats();
}

}  // namespace meshcache

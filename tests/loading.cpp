// Loading and the mesh cache (meshcache.h): a cached mesh is the generated one byte for byte, a missing, stale or damaged cache is
// rebuilt, and several threads building and caching meshes together agree with one thread alone.
#include <cstdio>
#include <string>
#include <mutex>
#include <thread>
#include <vector>

#include "../src/meshcache.h"
#include "../src/modelkit.h"
#include "../src/platform.h"
#include "../src/texenc.h"
#include "harness.h"

namespace {
const char* kDir = "build/test-mesh-cache";
const uint64_t kPinnedEncoderHash = 6923209209361431738ull;  // texenc output for the test texture, at kEncoderVersion 1
std::mutex serial;  // (the cache is one per process, and scenarios run side by side)

struct CacheEnv {  // a private folder, and the real settings put back afterwards
    CacheEnv(uint64_t version) {
        meshcache::setDir(kDir);
        meshcache::setCodeVersion(version);
        meshcache::setEnabled(true);
        meshcache::setMinCostMs(0);  // (every mesh, however quick: the checks need them stored)
        meshcache::clearStats();
    }
    ~CacheEnv() {
        meshcache::setMinCostMs(1.0);
        meshcache::setEnabled(false);  // (other tests generate fresh, and write nothing)
        meshcache::reset();
    }
};
void wipe() {
    meshcache::setDir(kDir);
    makeDir("build");
    makeDir(kDir);
    std::remove((std::string(kDir) + "/meshes.bin").c_str());
    meshcache::reset();
}

std::vector<const mk::ModelDef*> sample(size_t n) {
    std::vector<const mk::ModelDef*> out;
    for (const mk::ModelDef& d : mk::catalog())
        if (!d.variants.empty() && out.size() < n) out.push_back(&d);
    return out;
}
std::vector<uint8_t> built(const mk::ModelDef& d) {
    mk::Mesh m;
    mk::build(d, 0, mk::defaults(d.variants[0]), m);
    return mk::packMesh(m);
}
std::vector<std::vector<uint8_t>> freshMeshes(const std::vector<const mk::ModelDef*>& defs) {
    meshcache::setEnabled(false);
    std::vector<std::vector<uint8_t>> out;
    for (const mk::ModelDef* d : defs) out.push_back(built(*d));
    meshcache::setEnabled(true);
    return out;
}
}  // namespace

SCENARIO(loadingCacheRoundTrip, "loading/cache-hit-miss", "A mesh made once is stored and the next run reads it back: byte for byte what generating gives") {
    std::lock_guard<std::mutex> only(serial);
    const auto defs = sample(60);
    const auto fresh = freshMeshes(defs);
    wipe();
    CacheEnv env(1001);
    for (const mk::ModelDef* d : defs) built(*d);  // first run: all misses, all stored
    meshcache::Stats first = meshcache::stats();
    ctx.check("first run: every model missed and was stored", first.hits == 0 && first.misses >= (int)defs.size() && first.stored >= (int)defs.size());
    ctx.check("the bundle was written", meshcache::flush());
    meshcache::reset();  // (a new run: nothing in memory)
    meshcache::clearStats();
    bool same = true;
    for (size_t i = 0; i < defs.size(); i++) same &= built(*defs[i]) == fresh[i];
    meshcache::Stats second = meshcache::stats();
    ctx.check("second run: every model came from the cache", second.hits >= (int)defs.size() && second.misses == 0 && second.stored == 0);
    ctx.check("cached meshes are byte-identical to generated ones", same);
    ctx.metric("bundle size", second.fileBytes / 1024.0, "KB", 1, 200000);
}

SCENARIO(loadingCacheStale, "loading/cache-stale", "A bundle from another build, or a damaged one, is ignored and rebuilt (and the new one is good)") {
    std::lock_guard<std::mutex> only(serial);
    const auto defs = sample(20);
    const auto fresh = freshMeshes(defs);
    wipe();
    {
        CacheEnv env(2001);
        for (const mk::ModelDef* d : defs) built(*d);
        meshcache::flush();
    }
    {  // another build of the game
        CacheEnv env(2002);
        meshcache::reset();
        bool same = true;
        for (size_t i = 0; i < defs.size(); i++) same &= built(*defs[i]) == fresh[i];
        meshcache::Stats s = meshcache::stats();
        ctx.check("a bundle from another code version is stale: nothing hit", s.stale == 1 && s.hits == 0);
        ctx.check("and the rebuilt meshes are the generated ones", same);
        meshcache::flush();
        meshcache::reset();
        meshcache::clearStats();
        for (const mk::ModelDef* d : defs) built(*d);
        s = meshcache::stats();
        ctx.check("the rebuilt bundle serves this build", s.hits >= (int)defs.size() && s.stale == 0);
    }
    {  // damage: a flipped byte in the middle
        const std::string path = std::string(kDir) + "/meshes.bin";
        FILE* f = std::fopen(path.c_str(), "r+b");
        ctx.check("the bundle is there to damage", f != nullptr);
        if (f) {
            std::fseek(f, 0, SEEK_END);
            const long mid = std::ftell(f) / 2;
            std::fseek(f, mid, SEEK_SET);
            int c = std::fgetc(f);
            std::fseek(f, mid, SEEK_SET);
            std::fputc(c ^ 0x5a, f);
            std::fclose(f);
        }
        CacheEnv env(2002);
        meshcache::reset();
        bool same = true;
        for (size_t i = 0; i < defs.size(); i++) same &= built(*defs[i]) == fresh[i];
        ctx.check("a damaged bundle is stale", meshcache::stats().stale == 1 && meshcache::stats().hits == 0);
        ctx.check("and the meshes still come out right", same);
    }
    {  // a different pick (parameters) is a different key
        CacheEnv env(2002);
        meshcache::reset();
        const mk::ModelDef* d = defs[0];
        mk::Mesh a, b;
        mk::P p = mk::defaults(d->variants[0]);
        mk::build(*d, 0, p, a);
        p.v["__other"] = 3;  // (not a parameter the model reads, but part of the key)
        meshcache::clearStats();
        mk::build(*d, 0, p, b);
        ctx.check("other parameters are a miss, not the old mesh", meshcache::stats().misses == 1 && mk::packMesh(a) == mk::packMesh(b));
    }
}

SCENARIO(loadingConcurrent, "loading/concurrent", "Several threads building and caching the same models at once agree with one thread (no lost or torn meshes)") {
    std::lock_guard<std::mutex> only(serial);
    const auto defs = sample(40);
    const auto fresh = freshMeshes(defs);
    wipe();
    CacheEnv env(3001);
    const int kThreads = 8;
    std::vector<int> bad(kThreads, 0);
    std::vector<std::thread> pool;
    for (int t = 0; t < kThreads; t++)
        pool.emplace_back([&, t] {
            for (int round = 0; round < 3; round++)
                for (size_t k = 0; k < defs.size(); k++) {
                    const size_t i = (k + t * 5) % defs.size();  // (each thread starts somewhere else)
                    if (built(*defs[i]) != fresh[i]) bad[t]++;
                    if (mk::packMesh(mk::modelMesh(defs[i]->id)) .empty() && !mk::modelMesh(defs[i]->id).v.empty()) bad[t]++;
                }
            meshcache::flush();
        });
    for (std::thread& t : pool) t.join();
    int wrong = 0;
    for (int b : bad) wrong += b;
    ctx.check("every thread got the generated meshes", wrong == 0, std::to_string(wrong) + " wrong");
    ctx.check("the bundle written under contention reads back whole", meshcache::flush());
    meshcache::reset();
    meshcache::clearStats();
    bool same = true;
    for (size_t i = 0; i < defs.size(); i++) same &= built(*defs[i]) == fresh[i];
    ctx.check("and serves the same meshes", same && meshcache::stats().hits >= (int)defs.size() && meshcache::stats().stale == 0);
}

namespace {
std::vector<uint8_t> gradient(int w, int h) {
    std::vector<uint8_t> px((size_t)w * h * 4);
    for (int y = 0; y < h; y++)
        for (int x = 0; x < w; x++) {
            uint8_t* p = &px[((size_t)y * w + x) * 4];
            p[0] = (uint8_t)(x * 255 / (w - 1)), p[1] = (uint8_t)(y * 255 / (h - 1)), p[2] = (uint8_t)((x * 7 + y * 13) & 255), p[3] = (uint8_t)(((x / 8 + y / 8) & 1) ? 255 : 90);
        }
    return px;
}
uint64_t hashEncoded(const texenc::Encoded& e) {
    meshcache::Hasher h;
    for (const auto& l : e.levels) h.bytes(l.data(), l.size());
    return h.h;
}
}  // namespace

SCENARIO(loadingTextureBlobs, "loading/texture-cache", "Encoded textures kept on disk come back exactly as encoded; a damaged file is a miss; the encoder's output is pinned so a change must bump its version") {
    std::lock_guard<std::mutex> only(serial);
    wipe();
    CacheEnv env(4001);
    const int w = 64, h = 64;
    const auto px = gradient(w, h);
    const auto chain = texenc::mipChain(px.data(), w, h, true);
    const texenc::Encoded fresh = texenc::encode(texenc::Kind::ColorAlpha, true, chain, w, h, true);
    ctx.check("the texture was compressed", fresh.compressed && !fresh.levels.empty());
    const uint64_t key = 0xabcdef12345ull;
    ctx.check("a missing blob is a miss", !meshcache::getBlob(key, *std::make_unique<std::vector<uint8_t>>()));
    ctx.check("stored", meshcache::putBlob(key, meshcache::packEncoded(fresh)));
    std::vector<uint8_t> blob;
    texenc::Encoded back;
    ctx.check("read back", meshcache::getBlob(key, blob) && meshcache::unpackEncoded(blob, back));
    ctx.check("blocks identical to the encoder's", back.levels == fresh.levels && back.w == fresh.w && back.h == fresh.h && back.kind == fresh.kind &&
                                                   back.srgb == fresh.srgb && back.compressed == fresh.compressed && back.swizzle == fresh.swizzle);
    // Damage it.
    const std::string path = std::string(kDir) + "/blob-00000abcdef12345.bin";
    FILE* f = std::fopen(path.c_str(), "r+b");
    ctx.check("the blob file is there", f != nullptr);
    if (f) {
        std::fseek(f, 40, SEEK_SET);
        std::fputc(0x77, f);
        std::fclose(f);
    }
    std::vector<uint8_t> bad;
    ctx.check("a damaged blob is a miss", !meshcache::getBlob(key, bad));
    // The encoder's output, pinned. If this fails the encoder changed: raise texenc::kEncoderVersion (the cache would otherwise serve
    // the old blocks) and put the new hash here.
    ctx.check("encoder output matches the pinned hash for version " + std::to_string(texenc::kEncoderVersion), hashEncoded(fresh) == kPinnedEncoderHash, std::to_string(hashEncoded(fresh)));
}

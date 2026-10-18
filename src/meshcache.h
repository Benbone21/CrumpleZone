// A binary cache of the meshes the game generates from code (the model kit's, the trees'): the second run loads them with
// one read instead of building them again. Entries are keyed by a hash of the generator's code version and its parameters
// (so a rebuilt game, or a changed pick in the model lab, never finds an old one); a cache that is missing, from another
// build or damaged is simply ignored and the meshes are generated as before. Bundle file: <dir>/meshes.bin, written by
// flush() (atomically: a temp file, then a rename). Nothing here touches OpenGL; every function is safe from any thread.
#pragma once
#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

namespace meshcache {

// FNV-1a over values; the key of an entry is built with one of these.
struct Hasher {
    uint64_t h = 1469598103934665603ull;
    void bytes(const void* p, size_t n) {
        const unsigned char* c = (const unsigned char*)p;
        for (size_t i = 0; i < n; i++) h = (h ^ c[i]) * 1099511628211ull;
    }
    void u64(uint64_t v) { bytes(&v, sizeof v); }
    void num(double v) { bytes(&v, sizeof v); }
    void str(const std::string& s) { bytes(s.data(), s.size()), u64(s.size()); }
};

// Where the bundle lives: configDir()/cache unless set (the tests use a folder of their own).
void setDir(const std::string& dir);
std::string dir();
// What the generators were when the bundle was made: a hash of the running executable's size and time stamp, unless set.
void setCodeVersion(uint64_t v);
uint64_t codeVersion();
// Only meshes that took at least this long (ms) to generate are stored: a cheap one is faster to make than to read back, and the
// bundle stays small. 1 ms unless set.
void setMinCostMs(double ms);
double minCostMs();
bool enabled();                 // false with CZ_NO_MESH_CACHE set (or setEnabled(false))
void setEnabled(bool on);

// The bytes stored under `key` (false: not cached). The bundle is read, whole, on the first call.
bool get(uint64_t key, std::vector<uint8_t>& out);
// Remember `bytes` under `key` (written out by the next flush()).
void put(uint64_t key, std::vector<uint8_t> bytes);
// Writes the bundle if anything was added. False if it could not be written (the game goes on without it).
bool flush();
// Forgets everything held in memory (the next get() reads the file again).
void reset();

struct Stats {
    int hits = 0, misses = 0, stored = 0;
    int stale = 0;              // bundles thrown away: another build's, or damaged
    size_t fileBytes = 0;       // size of the bundle as read
};
Stats stats();
void clearStats();

}  // namespace meshcache

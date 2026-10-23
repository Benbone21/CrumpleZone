#include "bytepack.h"

#include <algorithm>
#include <cstring>

#include "parallel.h"

namespace bytepack {
namespace {
constexpr size_t kBlock = 4u << 20;
constexpr int kMinMatch = 4, kHashBits = 16;
const uint8_t kMagic[4] = {'C', 'Z', 'P', '1'};

void putVar(std::vector<uint8_t>& o, uint64_t v) {
    while (v >= 0x80) o.push_back((uint8_t)(v | 0x80)), v >>= 7;
    o.push_back((uint8_t)v);
}
bool getVar(const uint8_t*& p, const uint8_t* end, uint64_t& v) {
    v = 0;
    for (int s = 0; s < 64; s += 7) {
        if (p >= end) return false;
        const uint8_t b = *p++;
        v |= (uint64_t)(b & 0x7f) << s;
        if (!(b & 0x80)) return true;
    }
    return false;
}
uint32_t read32(const uint8_t* p) {
    uint32_t v;
    std::memcpy(&v, p, 4);
    return v;
}

// One block: (literal count, literals, match length - kMinMatch + 1 (0: the end), offset) repeated.
std::vector<uint8_t> packBlock(const uint8_t* src, size_t n) {
    std::vector<uint8_t> o;
    o.reserve(n / 2 + 16);
    std::vector<uint32_t> table((size_t)1 << kHashBits, 0xffffffffu);
    size_t i = 0, lit = 0;
    auto flush = [&](size_t matchLen, size_t offset) {
        putVar(o, i - lit);
        o.insert(o.end(), src + lit, src + i);
        putVar(o, matchLen ? matchLen - kMinMatch + 1 : 0);
        if (matchLen) putVar(o, offset);
    };
    while (i + kMinMatch <= n) {
        const uint32_t v = read32(src + i);
        const uint32_t h = (v * 2654435761u) >> (32 - kHashBits);
        const uint32_t cand = table[h];
        table[h] = (uint32_t)i;
        if (cand != 0xffffffffu && read32(src + cand) == v) {
            size_t len = kMinMatch;
            while (i + len < n && src[cand + len] == src[i + len]) len++;
            flush(len, i - cand);
            for (size_t k = i + 1; k < i + len && k + kMinMatch <= n; k += 2) table[(read32(src + k) * 2654435761u) >> (32 - kHashBits)] = (uint32_t)k;
            i += len, lit = i;
        } else {
            i++;
        }
    }
    i = n;
    flush(0, 0);
    return o;
}

bool unpackBlock(const uint8_t* p, const uint8_t* end, uint8_t* out, size_t n) {
    size_t at = 0;
    while (true) {
        uint64_t lit, m;
        if (!getVar(p, end, lit) || lit > (uint64_t)(end - p) || at + lit > n) return false;
        std::memcpy(out + at, p, lit), p += lit, at += lit;
        if (!getVar(p, end, m)) return false;
        if (m == 0) return at == n && p == end;
        uint64_t off;
        const uint64_t len = m - 1 + kMinMatch;
        if (!getVar(p, end, off) || off == 0 || off > at || at + len > n) return false;
        for (uint64_t k = 0; k < len; k++) out[at + k] = out[at - off + k];  // (overlapping: a byte at a time)
        at += len;
    }
}
}  // namespace

std::vector<uint8_t> pack(const std::vector<uint8_t>& raw) {
    const size_t blocks = (raw.size() + kBlock - 1) / kBlock;
    std::vector<std::vector<uint8_t>> out(blocks);
    parallelRows((int)blocks, [&](int b) {
        const size_t at = (size_t)b * kBlock;
        out[b] = packBlock(raw.data() + at, std::min(kBlock, raw.size() - at));
    }, 1);
    std::vector<uint8_t> o(kMagic, kMagic + 4);
    putVar(o, raw.size()), putVar(o, blocks);
    for (const auto& b : out) putVar(o, b.size());
    for (const auto& b : out) o.insert(o.end(), b.begin(), b.end());
    return o;
}

bool unpack(const std::vector<uint8_t>& packed, std::vector<uint8_t>& out) {
    out.clear();
    if (packed.size() < 4 || std::memcmp(packed.data(), kMagic, 4) != 0) return false;
    const uint8_t *p = packed.data() + 4, *end = packed.data() + packed.size();
    uint64_t size, blocks;
    if (!getVar(p, end, size) || !getVar(p, end, blocks) || blocks != (size + kBlock - 1) / kBlock || size > ((uint64_t)1 << 36)) return false;
    std::vector<uint64_t> len(blocks), start(blocks);
    uint64_t total = 0;
    for (uint64_t b = 0; b < blocks; b++) {
        if (!getVar(p, end, len[b])) return false;
        start[b] = total, total += len[b];
    }
    if (total != (uint64_t)(end - p)) return false;
    out.resize(size);
    std::vector<char> ok(blocks, 0);
    parallelRows((int)blocks, [&](int b) {
        const size_t at = (size_t)b * kBlock;
        ok[b] = unpackBlock(p + start[b], p + start[b] + len[b], out.data() + at, std::min(kBlock, (size_t)size - at));
    }, 1);
    for (char k : ok)
        if (!k) return out.clear(), false;
    return true;
}

void deltaShuffle32(std::vector<uint8_t>& bytes, size_t at, size_t count) {
    std::vector<uint8_t> t(count * 4);
    uint32_t prev = 0;
    for (size_t k = 0; k < count; k++) {
        uint32_t v;
        std::memcpy(&v, &bytes[at + k * 4], 4);
        const uint32_t d = v - prev;
        prev = v;
        for (int b = 0; b < 4; b++) t[b * count + k] = (uint8_t)(d >> (8 * b));
    }
    std::memcpy(&bytes[at], t.data(), t.size());
}

void unshuffleDelta32(std::vector<uint8_t>& bytes, size_t at, size_t count) {
    std::vector<uint8_t> t(count * 4);
    uint32_t prev = 0;
    for (size_t k = 0; k < count; k++) {
        uint32_t d = 0;
        for (int b = 0; b < 4; b++) d |= (uint32_t)bytes[at + b * count + k] << (8 * b);
        prev += d;
        std::memcpy(&t[k * 4], &prev, 4);
    }
    std::memcpy(&bytes[at], t.data(), t.size());
}

}  // namespace bytepack

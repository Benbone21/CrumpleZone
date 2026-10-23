#pragma once
// Lossless compression for the caches (worldcache.h): a small LZ77 (byte matches against what came before, LZ4-like tokens),
// in independent 4 MB blocks packed and unpacked on a few threads. No dependencies; the same bytes back, exactly.
#include <cstddef>
#include <cstdint>
#include <vector>

namespace bytepack {

std::vector<uint8_t> pack(const std::vector<uint8_t>& raw);
// False: not something pack() made (or damaged); `out` is then left empty.
bool unpack(const std::vector<uint8_t>& packed, std::vector<uint8_t>& out);

// Reversible transforms that make numbers compress better (in place): 32-bit values as the difference from the one before,
// then their bytes regrouped by position (all the lowest bytes, then the next...).
void deltaShuffle32(std::vector<uint8_t>& bytes, size_t at, size_t count);
void unshuffleDelta32(std::vector<uint8_t>& bytes, size_t at, size_t count);

}  // namespace bytepack

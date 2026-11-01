// SHA-256 and SHA-512 (FIPS 180-4), for the updater: SHA-256 checks a downloaded file against the signed manifest,
// SHA-512 is the hash inside Ed25519 (ed25519.h). Plain portable C++, no tables beyond the standard constants.
// Tests: update/sha256, update/sha512 (the NIST examples).
#pragma once
#include <cstddef>
#include <cstdint>
#include <string>

namespace sha2 {

class Sha256 {
public:
    Sha256();
    void add(const void* data, size_t n);
    void finish(uint8_t out[32]);  // (the object is spent afterwards)
private:
    uint32_t h_[8];
    uint8_t buf_[64];
    uint64_t total_ = 0;
    size_t fill_ = 0;
    void block(const uint8_t* p);
};

class Sha512 {
public:
    Sha512();
    void add(const void* data, size_t n);
    void finish(uint8_t out[64]);
private:
    uint64_t h_[8];
    uint8_t buf_[128];
    uint64_t total_ = 0;
    size_t fill_ = 0;
    void block(const uint8_t* p);
};

void sha256(const void* data, size_t n, uint8_t out[32]);
void sha512(const void* data, size_t n, uint8_t out[64]);
std::string sha256Hex(const std::string& data);  // lowercase hex
// Lowercase hex of the file's SHA-256; "" when it can't be read.
std::string sha256File(const std::string& path);

std::string toHex(const uint8_t* p, size_t n);
// Hex (either case) to bytes; false if the length is not 2*n or a character is not hex.
bool fromHex(const std::string& hex, uint8_t* out, size_t n);

}  // namespace sha2

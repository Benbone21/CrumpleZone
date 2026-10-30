// Ed25519 signatures (RFC 8032), for the updater: the game checks the release manifest's signature against the public key
// compiled into it (updatecore.h); tools/cz_update.cpp makes keys and signs (CI holds the private key as a secret).
// A port of TweetNaCl's crypto_sign (Bernstein, Janssen, Lange, Schwabe; public domain), with the SHA-512 from sha2.h and one
// addition: a signature whose S is not below the group order L is refused (RFC 8032 5.1.7), so a signature can't be reshaped.
// Not constant-time beyond what TweetNaCl gives; signing only happens in the CI tool. Tests: update/ed25519 (RFC 8032 section 7.1).
#pragma once
#include <cstddef>
#include <cstdint>

namespace ed25519 {

// The 32-byte private key ("seed") gives the 32-byte public key.
void publicKey(const uint8_t seed[32], uint8_t pub[32]);
void sign(uint8_t sig[64], const uint8_t* msg, size_t n, const uint8_t seed[32]);
bool verify(const uint8_t sig[64], const uint8_t* msg, size_t n, const uint8_t pub[32]);

}  // namespace ed25519

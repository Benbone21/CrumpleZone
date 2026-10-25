// HTTP(S) GET for the updater, with no new library linked into the game:
//  - Windows: WinHTTP (part of Windows; TLS and certificates from the system).
//  - Linux and macOS: libcurl loaded at run time (dlopen libcurl.so.4 / libcurl-gnutls.so.4 / macOS's own /usr/lib/libcurl.4.dylib),
//    like SDL is loaded by soname, so the game still starts without it; else the `curl` program (macOS always has /usr/bin/curl;
//    most Linux desktops have one of the two). TLS and certificate checks are curl's, with the system's CA store.
// Whatever comes over the wire is checked anyway: the manifest's Ed25519 signature, then each file's size and SHA-256 (updatecore.h).
// Errors come back in plain words for the update card. Tests: update/http-* (a local HTTP server on 127.0.0.1).
#pragma once
#include <atomic>
#include <cstdint>
#include <string>

namespace http {

struct Progress {
    std::atomic<uint64_t> got{0}, total{0};  // bytes so far; the size when the server says it (else 0)
    std::atomic<bool> cancel{false};         // set from another thread to stop
};

enum class Backend { Auto, LibCurl, CurlTool, WinHttp };
bool available(Backend b);
const char* backendName(Backend b);
Backend autoBackend();  // what Auto picks here

// GET url into the file at path (replaced). False with a plain reason; a partial file is removed.
bool download(const std::string& url, const std::string& path, Progress* progress, std::string& why, Backend b = Backend::Auto);
// GET a small text (at most maxBytes) into memory.
bool fetch(const std::string& url, size_t maxBytes, std::string& body, std::string& why, Backend b = Backend::Auto);

}  // namespace http

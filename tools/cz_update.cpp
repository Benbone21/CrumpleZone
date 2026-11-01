// cz-update: the release signing and checking tool for the self-updater (src/updatecore.h).
//   build/cz-update --keygen FILE              a new key pair: the private key (hex) into FILE (mode 600, never printed), the
//                                              public key printed (it goes into src/updatekeys.h). The owner runs this once.
//   build/cz-update --pubkey FILE              the public key of the private key in FILE
//   build/cz-update --sign OUT --version V [--net N] [--date D] [--notes URL] --file PLATFORM URL LOCALFILE [--file ...]
//                                              writes the signed manifest; size and SHA-256 are taken from each LOCALFILE. The key
//                                              comes from the environment variable CZ_UPDATE_SIGNING_KEY (hex; CI's secret) or
//                                              --key-file FILE. --require-builtin: fail unless the key matches one compiled in.
//   build/cz-update --verify FILE|URL [--key HEX] [--files DIR] [--fetch]
//                                              checks the signature (keys compiled in, or --key), prints the manifest; with --files
//                                              also checks each file (by its name) in DIR; with --fetch (a URL) downloads each file from
//                                              where the game would and checks it. Exit status 0 only when all is good.
//   build/cz-update --hash FILE                size and SHA-256
// See docs/update-signing-keys.md and docs/server-updates-setup.md.
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <string>
#include <vector>

#include "../src/ed25519.h"
#include "../src/httpget.h"
#include "../src/net.h"
#include "../src/sha2.h"
#include "../src/updatecore.h"

#ifndef _WIN32
#include <fcntl.h>
#include <sys/stat.h>
#include <unistd.h>
#endif

namespace fs = std::filesystem;
using namespace update;

namespace {

int usage() {
    std::fprintf(stderr,
                 "usage: cz-update --keygen FILE | --pubkey FILE | --hash FILE\n"
                 "       cz-update --sign OUT --version V [--net N] [--date D] [--notes URL] [--require-builtin] [--key-file FILE]\n"
                 "                 --file PLATFORM URL LOCALFILE [--file ...]     (key: $CZ_UPDATE_SIGNING_KEY, hex)\n"
                 "       cz-update --verify FILE|URL [--key HEX] [--files DIR] [--fetch]\n");
    return 2;
}

std::string readAll(const std::string& path) {
    std::ifstream f(path, std::ios::binary);
    if (!f) return "";
    return std::string((std::istreambuf_iterator<char>(f)), std::istreambuf_iterator<char>());
}

std::string trim(std::string s) {
    while (!s.empty() && (s.back() == '\n' || s.back() == '\r' || s.back() == ' ')) s.pop_back();
    size_t i = 0;
    while (i < s.size() && s[i] == ' ') i++;
    return s.substr(i);
}

bool loadSeed(const std::string& hex, uint8_t seed[32]) { return sha2::fromHex(trim(hex), seed, 32); }

void wipe(void* p, size_t n) {
    volatile uint8_t* v = (volatile uint8_t*)p;
    while (n--) *v++ = 0;
}

int keygen(const std::string& path) {
#ifdef _WIN32
    (void)path;
    std::fprintf(stderr, "cz-update: --keygen runs on Linux or macOS\n");
    return 1;
#else
    if (fs::exists(path)) {
        std::fprintf(stderr, "cz-update: %s already exists; not overwriting a key\n", path.c_str());
        return 1;
    }
    uint8_t seed[32];
    std::FILE* r = std::fopen("/dev/urandom", "rb");
    if (!r || std::fread(seed, 1, 32, r) != 32) {
        std::fprintf(stderr, "cz-update: can't read /dev/urandom\n");
        if (r) std::fclose(r);
        return 1;
    }
    std::fclose(r);
    const int fd = open(path.c_str(), O_WRONLY | O_CREAT | O_EXCL, 0600);
    if (fd < 0) {
        std::fprintf(stderr, "cz-update: can't create %s\n", path.c_str());
        return 1;
    }
    const std::string hex = sha2::toHex(seed, 32) + "\n";
    const bool ok = write(fd, hex.data(), hex.size()) == (ssize_t)hex.size() && fsync(fd) == 0;
    close(fd);
    uint8_t pub[32];
    ed25519::publicKey(seed, pub);
    wipe(seed, sizeof seed);
    if (!ok) {
        std::fprintf(stderr, "cz-update: writing %s failed\n", path.c_str());
        return 1;
    }
    std::printf("private key written to %s (keep it secret: it goes only into the GitHub secret UPDATE_SIGNING_KEY)\n", path.c_str());
    std::printf("public key (goes into src/updatekeys.h):\n%s\n", sha2::toHex(pub, 32).c_str());
    return 0;
#endif
}

int pubkey(const std::string& path) {
    uint8_t seed[32], pub[32];
    if (!loadSeed(readAll(path), seed)) {
        std::fprintf(stderr, "cz-update: %s is not a private key (64 hex characters)\n", path.c_str());
        return 1;
    }
    ed25519::publicKey(seed, pub);
    wipe(seed, sizeof seed);
    std::printf("%s\n", sha2::toHex(pub, 32).c_str());
    return 0;
}

int hashFile(const std::string& path) {
    std::error_code ec;
    const uint64_t size = fs::file_size(path, ec);
    const std::string h = sha2::sha256File(path);
    if (ec || h.empty()) {
        std::fprintf(stderr, "cz-update: can't read %s\n", path.c_str());
        return 1;
    }
    std::printf("%llu %s\n", (unsigned long long)size, h.c_str());
    return 0;
}

int signCmd(int argc, char** argv) {
    std::string out, keyFile;
    bool requireBuiltin = false;
    Manifest m;
    m.net = net::kVersion;
    std::vector<std::string> locals;
    for (int i = 1; i < argc; i++) {
        const std::string a = argv[i];
        auto next = [&]() -> std::string { return i + 1 < argc ? argv[++i] : ""; };
        if (a == "--sign") out = next();
        else if (a == "--version") m.version = next();
        else if (a == "--net") m.net = std::atoi(next().c_str());
        else if (a == "--date") m.date = next();
        else if (a == "--notes") m.notes = next();
        else if (a == "--key-file") keyFile = next();
        else if (a == "--require-builtin") requireBuiltin = true;
        else if (a == "--file" && i + 3 < argc) {
            FileEntry f;
            f.platform = argv[++i];
            f.url = argv[++i];
            locals.push_back(argv[++i]);
            m.files.push_back(f);
        } else return usage();
    }
    if (out.empty() || !validVersion(m.version) || m.files.empty()) return usage();
    uint8_t seed[32];
    std::string hex = keyFile.empty() ? (std::getenv("CZ_UPDATE_SIGNING_KEY") ? std::getenv("CZ_UPDATE_SIGNING_KEY") : "") : readAll(keyFile);
    const bool haveKey = loadSeed(hex, seed);
    wipe(&hex[0], hex.size());
    if (!haveKey) {
        std::fprintf(stderr, "cz-update: no signing key (set CZ_UPDATE_SIGNING_KEY to the 64-hex private key, or pass --key-file)\n");
        return 1;
    }
    for (size_t i = 0; i < m.files.size(); i++) {
        std::error_code ec;
        m.files[i].size = fs::file_size(locals[i], ec);
        m.files[i].sha256 = sha2::sha256File(locals[i]);
        if (ec || m.files[i].sha256.empty()) {
            std::fprintf(stderr, "cz-update: can't read %s\n", locals[i].c_str());
            wipe(seed, sizeof seed);
            return 1;
        }
        if (resolveUrl("https://example.invalid/m.txt", m.files[i].url).empty()) {
            std::fprintf(stderr, "cz-update: %s is not an allowed file URL (relative, or https://)\n", m.files[i].url.c_str());
            wipe(seed, sizeof seed);
            return 1;
        }
    }
    PublicKey pub;
    ed25519::publicKey(seed, pub.data());
    const std::string text = signManifest(m, seed);
    wipe(seed, sizeof seed);
    bool builtin = false;
    for (const PublicKey& k : builtinKeys()) builtin = builtin || k == pub;
    if (requireBuiltin && !builtin) {
        std::fprintf(stderr, "cz-update: the signing key's public key %s is not in src/updatekeys.h: games built from this source would refuse "
                             "the manifest\n", sha2::toHex(pub.data(), 32).c_str());
        return 1;
    }
    Manifest back;
    std::string why;
    if (!readManifest(text, {pub}, back, why)) {
        std::fprintf(stderr, "cz-update: the manifest doesn't read back: %s\n", why.c_str());
        return 1;
    }
    std::ofstream o(out, std::ios::binary | std::ios::trunc);
    o << text;
    o.close();
    if (!o) {
        std::fprintf(stderr, "cz-update: can't write %s\n", out.c_str());
        return 1;
    }
    std::printf("signed %s: version %s, %zu files, key %s%s\n", out.c_str(), m.version.c_str(), m.files.size(), sha2::toHex(pub.data(), 32).c_str(),
                builtin ? " (compiled into this source)" : " (NOT the key in src/updatekeys.h)");
    return 0;
}

int verifyCmd(int argc, char** argv) {
    std::string src, keyHex, dir;
    bool fetchAll = false;
    for (int i = 1; i < argc; i++) {
        const std::string a = argv[i];
        auto next = [&]() -> std::string { return i + 1 < argc ? argv[++i] : ""; };
        if (a == "--verify") src = next();
        else if (a == "--key") keyHex = next();
        else if (a == "--files") dir = next();
        else if (a == "--fetch") fetchAll = true;
        else return usage();
    }
    if (src.empty()) return usage();
    std::vector<PublicKey> keys;
    if (!keyHex.empty()) {
        PublicKey k;
        if (!sha2::fromHex(keyHex, k.data(), 32)) {
            std::fprintf(stderr, "cz-update: --key needs 64 hex characters\n");
            return 2;
        }
        keys.push_back(k);
    } else {
        keys = builtinKeys();
    }
    std::string text, why;
    const bool isUrl = src.find("://") != std::string::npos;
    if (isUrl) {
        if (!http::fetch(src, 64 * 1024, text, why)) {
            std::fprintf(stderr, "FAIL fetching %s: %s\n", src.c_str(), why.c_str());
            return 1;
        }
    } else {
        text = readAll(src);
    }
    Manifest m;
    if (!readManifest(text, keys, m, why)) {
        std::fprintf(stderr, "FAIL %s: %s\n", src.c_str(), why.c_str());
        return 1;
    }
    std::printf("OK signature good: version %s, net %d, date %s\n", m.version.c_str(), m.net, m.date.c_str());
    bool all = true;
    for (const FileEntry& f : m.files) {
        const std::string full = fileUrl(isUrl ? src : "https://example.invalid/manifest.txt", m, f);
        std::printf("  %-8s %12llu %s  %s\n", f.platform.c_str(), (unsigned long long)f.size, f.sha256.c_str(), isUrl ? full.c_str() : f.url.c_str());
        if (full.empty()) {
            std::printf("  FAIL %s: not an allowed URL\n", f.url.c_str());
            all = false;
        }
        if (fetchAll && !full.empty()) {  // (a published release: download each file where the game would, following redirects)
            const std::string tmp = (fs::temp_directory_path() / ("cz-update-check-" + f.platform)).string();
            std::string werr;
            const bool got = http::download(full, tmp, nullptr, werr);
            std::error_code ec;
            const bool good = got && fs::file_size(tmp, ec) == f.size && !ec && sha2::sha256File(tmp) == f.sha256;
            std::printf("  %s download %s\n", good ? "OK  " : "FAIL", got ? (good ? "matches" : "doesn't match the signed size/SHA-256") : werr.c_str());
            fs::remove(tmp, ec);
            all = all && good;
        }
        if (!dir.empty()) {
            const std::string local = dir + "/" + fs::path(f.url).filename().string();
            std::error_code ec;
            const uint64_t size = fs::file_size(local, ec);
            const bool good = !ec && size == f.size && sha2::sha256File(local) == f.sha256;
            std::printf("  %s %s\n", good ? "OK  " : "FAIL", local.c_str());
            all = all && good;
        }
    }
    return all ? 0 : 1;
}

}  // namespace

int main(int argc, char** argv) {
    if (argc < 3) return usage();
    const std::string cmd = argv[1];
    if (cmd == "--keygen") return keygen(argv[2]);
    if (cmd == "--pubkey") return pubkey(argv[2]);
    if (cmd == "--hash") return hashFile(argv[2]);
    if (cmd == "--sign") return signCmd(argc, argv);
    if (cmd == "--verify") return verifyCmd(argc, argv);
    return usage();
}

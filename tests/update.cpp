// The self-updater (updatecore.h, httpget.h, updater.h) without a window: SHA-256 / SHA-512 (NIST), Ed25519 (RFC 8032 7.1), the
// signed manifest (and every way of tampering with it), versions, the decision for each kind of install, URLs, unpacking a zip
// made by Info-ZIP (as CI makes them) and hostile ones, the folder swap and rollback for each layout, the after-swap start counting,
// the Windows helper's logic, and downloads from a local HTTP server through each backend there is on this machine.
#include <atomic>
#include <chrono>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <map>
#include <mutex>
#include <thread>

#include "../src/ed25519.h"
#include "../src/httpget.h"
#include "../src/sha2.h"
#include "../src/updatecore.h"
#include "../src/updater.h"
#include "harness.h"

#ifdef _WIN32
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <winsock2.h>
#include <ws2tcpip.h>
typedef int socklen_t;
#else
#include <arpa/inet.h>
#include <netinet/in.h>
#include <sys/socket.h>
#include <unistd.h>
#endif

namespace fs = std::filesystem;
using namespace update;

namespace {

std::string hexOf(const uint8_t* p, size_t n) { return sha2::toHex(p, n); }
std::vector<uint8_t> unhex(const std::string& h) {
    std::vector<uint8_t> v(h.size() / 2);
    sha2::fromHex(h, v.data(), v.size());
    return v;
}

// A throwaway key for the tests only (the seed is a hash of a public phrase: never use it for anything real).
void testSeed(uint8_t seed[32], const char* phrase = "crumple zone updater test key, not a real key") { sha2::sha256(phrase, std::strlen(phrase), seed); }
PublicKey pubOf(const uint8_t seed[32]) {
    PublicKey k;
    ed25519::publicKey(seed, k.data());
    return k;
}

// A temporary folder of the test's own, removed at the end.
struct TempDir {
    std::string path;
    explicit TempDir(const std::string& tag) {
        static std::atomic<int> n{0};
        path = (fs::temp_directory_path() / ("cz-updtest-" + tag + "-" + std::to_string(n++) + "-" + std::to_string((long)std::hash<std::thread::id>()(std::this_thread::get_id()) % 100000)))
                   .string();
        fs::remove_all(path);
        fs::create_directories(path);
    }
    ~TempDir() {
        std::error_code ec;
        fs::remove_all(path, ec);
    }
};
void put(const std::string& path, const std::string& text) {
    fs::create_directories(fs::path(path).parent_path());
    std::ofstream(path, std::ios::binary) << text;
}
std::string get(const std::string& path) {
    std::ifstream f(path, std::ios::binary);
    return std::string((std::istreambuf_iterator<char>(f)), std::istreambuf_iterator<char>());
}

// A zip made by Info-ZIP 3.0 (`zip -qry -X`): crumplezone/{crumplezone (0755), cz-install.txt, assets/a.txt (deflated), link -> assets/a.txt}.
const char* kFixtureZip =
"504b03040a0000000000455b595d0000000000000000000000000c0000006372756d706c657a6f6e652f504b03040a0000000000455b595d12cac1a0"
    "0c0000000c000000100000006372756d706c657a6f6e652f6c696e6b6173736574732f612e747874504b03040a0000000000455b595dd1fc3eb50d00"
    "00000d0000001a0000006372756d706c657a6f6e652f637a2d696e7374616c6c2e7478746c61796f7574206c696e75780a504b03040a000000000045"
    "5b595dca302da61200000012000000170000006372756d706c657a6f6e652f6372756d706c657a6f6e6523212f62696e2f73680a6563686f2076320a"
    "504b03040a0000000000455b595d000000000000000000000000130000006372756d706c657a6f6e652f6173736574732f504b030414000000080045"
    "5b595d2afc3cd44b000000a0140000180000006372756d706c657a6f6e652f6173736574732f612e747874edcac91180201404d15426354514d78f08"
    "6ed16b1983c73e76f5cbc16b2dbd1b55273b16b5766a2873dc64bb4fcaef9eaafb5263dd17582c168bc562b1582c168bc562b1582c16fb977d00504b"
    "01021e030a0000000000455b595d0000000000000000000000000c0000000000000000001000fd41000000006372756d706c657a6f6e652f504b0102"
    "1e030a0000000000455b595d12cac1a00c0000000c000000100000000000000000000000ffa12a0000006372756d706c657a6f6e652f6c696e6b504b"
    "01021e030a0000000000455b595dd1fc3eb50d0000000d0000001a0000000000000001000000b481640000006372756d706c657a6f6e652f637a2d69"
    "6e7374616c6c2e747874504b01021e030a0000000000455b595dca302da61200000012000000170000000000000001000000ed81a90000006372756d"
    "706c657a6f6e652f6372756d706c657a6f6e65504b01021e030a0000000000455b595d000000000000000000000000130000000000000000001000fd"
    "41f00000006372756d706c657a6f6e652f6173736574732f504b01021e03140000000800455b595d2afc3cd44b000000a01400001800000000000000"
    "01000000a481210100006372756d706c657a6f6e652f6173736574732f612e747874504b050600000000060006008c010000a20100000000";

// A small zip writer for the hostile cases (stored entries only).
uint32_t crc(const std::string& s) {
    uint32_t c = 0xFFFFFFFFu;
    for (unsigned char ch : s) {
        c ^= ch;
        for (int k = 0; k < 8; k++) c = c & 1 ? 0xEDB88320u ^ (c >> 1) : c >> 1;
    }
    return c ^ 0xFFFFFFFFu;
}
struct ZEntry {
    std::string name, data;
    uint32_t mode = 0100644;
    bool badCrc = false;
};
std::string makeZip(const std::vector<ZEntry>& es) {
    std::string out, cd;
    auto u16 = [](std::string& s, unsigned v) { s += (char)(v & 255), s += (char)(v >> 8 & 255); };
    auto u32 = [](std::string& s, uint32_t v) { for (int i = 0; i < 4; i++) s += (char)(v >> (8 * i) & 255); };
    for (const ZEntry& e : es) {
        const uint32_t off = (uint32_t)out.size(), c = e.badCrc ? crc(e.data) ^ 1 : crc(e.data);
        u32(out, 0x04034b50), u16(out, 10), u16(out, 0), u16(out, 0), u16(out, 0), u16(out, 0), u32(out, c), u32(out, (uint32_t)e.data.size()),
            u32(out, (uint32_t)e.data.size()), u16(out, (unsigned)e.name.size()), u16(out, 0);
        out += e.name + e.data;
        u32(cd, 0x02014b50), u16(cd, 3 << 8 | 30), u16(cd, 10), u16(cd, 0), u16(cd, 0), u16(cd, 0), u16(cd, 0), u32(cd, c), u32(cd, (uint32_t)e.data.size()),
            u32(cd, (uint32_t)e.data.size()), u16(cd, (unsigned)e.name.size()), u16(cd, 0), u16(cd, 0), u16(cd, 0), u16(cd, 0), u32(cd, e.mode << 16), u32(cd, off);
        cd += e.name;
    }
    const uint32_t cdOff = (uint32_t)out.size();
    out += cd;
    u32(out, 0x06054b50), u16(out, 0), u16(out, 0), u16(out, (unsigned)es.size()), u16(out, (unsigned)es.size()), u32(out, (uint32_t)cd.size()), u32(out, cdOff),
        u16(out, 0);
    return out;
}

Manifest sampleManifest() {
    Manifest m;
    m.version = "0.4.1";
    m.net = 8;
    m.date = "2026-10-25";
    m.notes = "https://github.com/Benbone21/crumplezone/releases/tag/v0.4.1";
    m.files = {{"windows", "v0.4.1/CrumpleZone-0.4.1-windows.zip", 123456789, std::string(64, 'a')},
               {"linux", "v0.4.1/CrumpleZone-0.4.1-linux.zip", 1000, std::string(64, 'b')},
               {"deb", "v0.4.1/CrumpleZone-0.4.1-linux.deb", 2000, std::string(64, 'c')}};
    return m;
}

// ---- a tiny HTTP server on 127.0.0.1 ---------------------------------------------------------------------------------
struct HttpServer {
    int port = 0;
    std::atomic<bool> stop{false};
    std::thread th;
    std::map<std::string, std::string> files;  // path -> body
    std::map<std::string, std::string> redirects;  // path -> Location (302), like GitHub's /releases/latest/download/ and its asset links
                                                   // ("abs:/x": the absolute http://127.0.0.1:port/x, like GitHub's to its file host)
    bool shortBody = false;                     // send fewer bytes than Content-Length says
    std::mutex m;                               // (files and redirects once it runs: set())
    void set(const std::string& path, const std::string& body) {
        std::lock_guard<std::mutex> g(m);
        files[path] = body;
    }
#ifdef _WIN32
    SOCKET ls = INVALID_SOCKET;
#else
    int ls = -1;
#endif
    bool start() {
#ifdef _WIN32
        WSADATA d;
        WSAStartup(MAKEWORD(2, 2), &d);
#endif
        ls = socket(AF_INET, SOCK_STREAM, 0);
        sockaddr_in a;
        std::memset(&a, 0, sizeof a);
        a.sin_family = AF_INET;
        a.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
        a.sin_port = 0;
        if (bind(ls, (sockaddr*)&a, sizeof a) != 0 || listen(ls, 8) != 0) return false;
        socklen_t len = sizeof a;
        getsockname(ls, (sockaddr*)&a, &len);
        port = ntohs(a.sin_port);
        th = std::thread([this] { loop(); });
        return true;
    }
    void loop() {
        while (!stop) {
            fd_set rd;
            FD_ZERO(&rd);
            FD_SET(ls, &rd);
            timeval tv{0, 100000};
            if (select((int)ls + 1, &rd, nullptr, nullptr, &tv) <= 0) continue;
            auto c = accept(ls, nullptr, nullptr);
            std::string req;
            char buf[2048];
            while (req.find("\r\n\r\n") == std::string::npos) {
                const int n = (int)recv(c, buf, sizeof buf, 0);
                if (n <= 0) break;
                req.append(buf, n);
            }
            const size_t sp = req.find(' '), sp2 = req.find(' ', sp + 1);
            std::string path = sp == std::string::npos ? "" : req.substr(sp + 1, sp2 - sp - 1);
            path = path.substr(0, path.find('?'));  // (a signed file-host link's query)
            std::string resp;
            std::lock_guard<std::mutex> g(m);
            auto it = files.find(path);
            auto re = redirects.find(path);
            if (re != redirects.end())
                resp = "HTTP/1.1 302 Found\r\nLocation: " + (re->second.compare(0, 4, "abs:") == 0 ? url(re->second.substr(4)) : re->second) + "\r\nContent-Length: 0\r\nConnection: close\r\n\r\n";
            else if (it == files.end()) resp = "HTTP/1.1 404 Not Found\r\nContent-Length: 9\r\nConnection: close\r\n\r\nnot found";
            else {
                const std::string& body = it->second;
                resp = "HTTP/1.1 200 OK\r\nContent-Length: " + std::to_string(body.size()) + "\r\nConnection: close\r\n\r\n" +
                       (shortBody ? body.substr(0, body.size() / 2) : body);
            }
            size_t sent = 0;
            while (sent < resp.size()) {
                const int n = (int)send(c, resp.data() + sent, (int)(resp.size() - sent), 0);
                if (n <= 0) break;
                sent += n;
            }
#ifdef _WIN32
            shutdown(c, SD_SEND);
            closesocket(c);
#else
            shutdown(c, SHUT_WR);
            close(c);
#endif
        }
    }
    std::string url(const std::string& path) const { return "http://127.0.0.1:" + std::to_string(port) + path; }
    ~HttpServer() {
        stop = true;
        if (th.joinable()) th.join();
#ifdef _WIN32
        closesocket(ls);
#else
        close(ls);
#endif
    }
};

}  // namespace

SCENARIO(updSha256, "update/sha256", "SHA-256 gives the FIPS 180-4 / NIST answers, in one piece or fed in uneven pieces") {
    ctx.check("empty", sha2::sha256Hex("") == "e3b0c44298fc1c149afbf4c8996fb92427ae41e4649b934ca495991b7852b855");
    ctx.check("abc", sha2::sha256Hex("abc") == "ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad");
    ctx.check("448 bits", sha2::sha256Hex("abcdbcdecdefdefgefghfghighijhijkijkljklmklmnlmnomnopnopq") == "248d6a61d20638b8e5c026930c3e6039a33ce45964ff2167f6ecedd419db06c1");
    sha2::Sha256 s;
    const std::string chunk(1000, 'a');
    for (int i = 0; i < 1000; i++) s.add(chunk.data(), chunk.size());
    uint8_t d[32];
    s.finish(d);
    ctx.check("a million a's", hexOf(d, 32) == "cdc76e5c9914fb9281a1c7e284d73e67f1809a48a497200e046d39ccc7112cd0");
    std::string text;
    for (int i = 0; i < 300; i++) text += (char)(i * 7 + 3);
    bool same = true;
    for (size_t cut : {1, 55, 56, 63, 64, 65, 127, 200}) {
        sha2::Sha256 p;
        p.add(text.data(), cut);
        p.add(text.data() + cut, text.size() - cut);
        p.finish(d);
        same = same && hexOf(d, 32) == sha2::sha256Hex(text);
    }
    ctx.check("split anywhere, same hash", same);
    TempDir t("sha");
    put(t.path + "/f", text);
    ctx.check("a file", sha2::sha256File(t.path + "/f") == sha2::sha256Hex(text) && sha2::sha256File(t.path + "/missing").empty());
}

SCENARIO(updSha512, "update/sha512", "SHA-512 (the hash inside Ed25519) gives the NIST answers") {
    uint8_t d[64];
    sha2::sha512("", 0, d);
    ctx.check("empty", hexOf(d, 64) == "cf83e1357eefb8bdf1542850d66d8007d620e4050b5715dc83f4a921d36ce9ce47d0d13c5d85f2b0ff8318d2877eec2f63b931bd47417a81a538327af927da3e");
    sha2::sha512("abc", 3, d);
    ctx.check("abc", hexOf(d, 64) == "ddaf35a193617abacc417349ae20413112e6fa4e89a97ea20a9eeee64b55d39a2192992a274fc1a836ba3c23a3feebbd454d4423643ce80e2a9ac94fa54ca49f");
    const char* m = "abcdefghbcdefghicdefghijdefghijkefghijklfghijklmghijklmnhijklmnoijklmnopjklmnopqklmnopqrlmnopqrsmnopqrstnopqrstu";
    sha2::sha512(m, std::strlen(m), d);
    ctx.check("896 bits", hexOf(d, 64) == "8e959b75dae313da8cf4f72814fc143f8f7779c6eb9f7fa17299aeadb6889018501d289e4900f7e4331b99dec4b5433ac7d329eeb6dd26545e96e55b874be909");
}

SCENARIO(updEd25519, "update/ed25519", "Ed25519 matches RFC 8032 section 7.1 (tests 1-3) and refuses changed messages, signatures and keys") {
    struct V {
        const char *sk, *pk, *msg, *sig;
    };
    const V vs[] = {
        {"9d61b19deffd5a60ba844af492ec2cc44449c5697b326919703bac031cae7f60", "d75a980182b10ab7d54bfed3c964073a0ee172f3daa62325af021a68f707511a", "",
         "e5564300c360ac729086e2cc806e828a84877f1eb8e5d974d873e065224901555fb8821590a33bacc61e39701cf9b46bd25bf5f0595bbe24655141438e7a100b"},
        {"4ccd089b28ff96da9db6c346ec114e0f5b8a319f35aba624da8cf6ed4fb8a6fb", "3d4017c3e843895a92b70aa74d1b7ebc9c982ccf2ec4968cc0cd55f12af4660c", "72",
         "92a009a9f0d4cab8720e820b5f642540a2b27b5416503f8fb3762223ebdb69da085ac1e43e15996e458f3613d0f11d8c387b2eaeb4302aeeb00d291612bb0c00"},
        {"c5aa8df43f9f837bedb7442f31dcb7b166d38535076f094b85ce3a2e0b4458f7", "fc51cd8e6218a1a38da47ed00230f0580816ed13ba3303ac5deb911548908025", "af82",
         "6291d657deec24024827e69c3abe01a30ce548a284743a445e3680d7db5ac3ac18ff9b538d16f290ae67f760984dc6594a7c15e9716ed28dc027beceea1ec40a"},
    };
    int n = 1;
    for (const V& v : vs) {
        const std::vector<uint8_t> sk = unhex(v.sk), msg = unhex(v.msg), want = unhex(v.sig);
        uint8_t pk[32], sig[64];
        ed25519::publicKey(sk.data(), pk);
        ed25519::sign(sig, msg.data(), msg.size(), sk.data());
        const std::string t = "test " + std::to_string(n++);
        ctx.check(t + ": public key", hexOf(pk, 32) == v.pk);
        ctx.check(t + ": signature", hexOf(sig, 64) == v.sig);
        ctx.check(t + ": verifies", ed25519::verify(want.data(), msg.data(), msg.size(), pk));
        std::vector<uint8_t> m2 = msg;
        m2.push_back(0);
        ctx.check(t + ": a longer message fails", !ed25519::verify(want.data(), m2.data(), m2.size(), pk));
        bool allFlips = true;
        for (int bit = 0; bit < 512; bit += 37) {
            std::vector<uint8_t> s2 = want;
            s2[bit / 8] ^= (uint8_t)(1 << (bit % 8));
            allFlips = allFlips && !ed25519::verify(s2.data(), msg.data(), msg.size(), pk);
        }
        ctx.check(t + ": any flipped signature bit fails", allFlips);
        uint8_t pk2[32];
        std::memcpy(pk2, pk, 32);
        pk2[5] ^= 1;
        ctx.check(t + ": another key fails", !ed25519::verify(want.data(), msg.data(), msg.size(), pk2));
    }
    {  // S + L: the same point, a non-canonical S (RFC 8032 5.1.7 says reject)
        const std::vector<uint8_t> sk = unhex(vs[0].sk);
        std::vector<uint8_t> sig = unhex(vs[0].sig);
        uint8_t pk[32];
        ed25519::publicKey(sk.data(), pk);
        const uint8_t L[32] = {0xed, 0xd3, 0xf5, 0x5c, 0x1a, 0x63, 0x12, 0x58, 0xd6, 0x9c, 0xf7, 0xa2, 0xde, 0xf9, 0xde, 0x14, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0x10};
        int carry = 0;
        for (int i = 0; i < 32; i++) {
            const int v = sig[32 + i] + L[i] + carry;
            sig[32 + i] = (uint8_t)v;
            carry = v >> 8;
        }
        ctx.check("S + L is refused", !ed25519::verify(sig.data(), nullptr, 0, pk));
    }
    uint8_t seed[32], sig[64];
    testSeed(seed);
    const PublicKey pk = pubOf(seed);
    bool round = true;
    std::string msg;
    for (int len = 0; len < 300; len += 13) {
        while ((int)msg.size() < len) msg += (char)(msg.size() * 31 + 7);
        ed25519::sign(sig, (const uint8_t*)msg.data(), msg.size(), seed);
        round = round && ed25519::verify(sig, (const uint8_t*)msg.data(), msg.size(), pk.data());
    }
    ctx.check("sign then verify, lengths 0 to 299", round);
}

SCENARIO(updManifest, "update/manifest", "a signed manifest reads back exactly; any change to it, a missing or foreign signature, or a bad line is refused") {
    uint8_t seed[32], other[32];
    testSeed(seed);
    testSeed(other, "another throwaway test key");
    const PublicKey k = pubOf(seed), k2 = pubOf(other);
    const std::string text = signManifest(sampleManifest(), seed);
    Manifest m;
    std::string why;
    ctx.check("reads", readManifest(text, {k}, m, why), why);
    ctx.check("the fields", m.version == "0.4.1" && m.net == 8 && m.files.size() == 3 && m.file("linux") && m.file("linux")->size == 1000 &&
                                m.file("deb")->sha256 == std::string(64, 'c') && !m.file("mac"));
    ctx.check("either of two keys", readManifest(text, {k2, k}, m, why));
    ctx.check("no keys in the build: refused with a reason", !readManifest(text, {}, m, why) && why.find("NO UPDATE KEY") != std::string::npos);
    ctx.check("another key: refused", !readManifest(text, {k2}, m, why) && why.find("DOESN'T MATCH") != std::string::npos);
    auto tampered = [&](const std::string& from, const std::string& to) {
        std::string t = text;
        const size_t at = t.find(from);
        if (at == std::string::npos) return false;
        t.replace(at, from.size(), to);
        Manifest x;
        std::string w;
        return !readManifest(t, {k}, x, w) && w.find("DOESN'T MATCH") != std::string::npos;
    };
    ctx.check("a changed version", tampered("version 0.4.1", "version 9.9.9"));
    ctx.check("a changed size", tampered(" 1000 ", " 1001 "));
    ctx.check("a changed hash", tampered(std::string(64, 'b'), std::string(63, 'b') + "d"));
    ctx.check("a changed URL", tampered("v0.4.1/CrumpleZone-0.4.1-windows.zip", "v0.4.1/evil.zip"));
    ctx.check("an added line", tampered("date ", "file mac https://evil.example/x.zip 1 " + std::string(64, 'e') + "\ndate "));
    ctx.check("CRLF line ends", tampered("\n", "\r\n"));
    {
        Manifest x;
        const std::string body = manifestBody(sampleManifest());
        ctx.check("no signature", !readManifest(body, {k}, x, why) && why.find("NOT SIGNED") != std::string::npos);
        ctx.check("a damaged signature", !readManifest(body + "signature 12zz\n", {k}, x, why) && why.find("DAMAGED") != std::string::npos);
        ctx.check("empty", !readManifest("", {k}, x, why));
    }
    auto signedText = [&](const std::string& body) {
        uint8_t sig[64];
        ed25519::sign(sig, (const uint8_t*)body.data(), body.size(), seed);
        return body + "signature " + sha2::toHex(sig, 64) + "\n";
    };
    ctx.check("signed but a newer format: refused", !readManifest(signedText("crumplezone-update 2\nversion 1.0.0\n"), {k}, m, why) && why.find("NEWER FORMAT") != std::string::npos);
    ctx.check("signed but not ours", !readManifest(signedText("something-else 1\nversion 1.0.0\n"), {k}, m, why));
    ctx.check("signed but a bad file line", !readManifest(signedText("crumplezone-update 1\nversion 1.0.0\nfile linux x.zip 12 nothex\n"), {k}, m, why));
    ctx.check("signed but no version", !readManifest(signedText("crumplezone-update 1\nnet 8\n"), {k}, m, why));
    ctx.check("signed, unknown lines ignored", readManifest(signedText("crumplezone-update 1\nversion 1.0.0\nchannel beta\n"), {k}, m, why) && m.version == "1.0.0");
    ctx.check("no real key is compiled in by this branch (the owner adds his)", builtinKeys().empty() || builtinKeys().size() >= 1);
}

SCENARIO(updVersions, "update/versions", "release versions compare by number, a -suffix sorts below the release, junk below everything") {
    ctx.check("0.4.1 > 0.4.0", compareVersions("0.4.1", "0.4.0") > 0);
    ctx.check("0.10.0 > 0.9.9 (numbers, not text)", compareVersions("0.10.0", "0.9.9") > 0);
    ctx.check("1.0 == 1.0.0", compareVersions("1.0", "1.0.0") == 0);
    ctx.check("0.4.1-rc1 < 0.4.1", compareVersions("0.4.1-rc1", "0.4.1") < 0 && compareVersions("0.4.1-rc1", "0.4.0") > 0);
    ctx.check("0.0.0-dev < 0.4.0", compareVersions("0.0.0-dev", "0.4.0") < 0);
    ctx.check("junk < anything", compareVersions("x.y", "0.0.1") < 0 && compareVersions("0.0.1", "") > 0);
    ctx.check("valid", validVersion("0.4.1") && validVersion("1.2.3-beta.2") && !validVersion("v0.4.1") && !validVersion("0..1") && !validVersion("1.2.3-") &&
                           !validVersion("1.2.3.4.5"));
}

SCENARIO(updUrls, "update/urls", "only https (or the tests' loopback http); file paths resolve beside the manifest and can't climb out") {
    const std::string mu = "https://updates.example.net/crumplezone/manifest.txt";
    ctx.check("relative", resolveUrl(mu, "v0.4.1/a.zip") == "https://updates.example.net/crumplezone/v0.4.1/a.zip");
    ctx.check("absolute https", resolveUrl(mu, "https://cdn.example.org/a.zip") == "https://cdn.example.org/a.zip");
    ctx.check("absolute http: refused", resolveUrl(mu, "http://cdn.example.org/a.zip").empty());
    ctx.check("climbing out: refused", resolveUrl(mu, "../a.zip").empty() && resolveUrl(mu, "v1/../../a.zip").empty() && resolveUrl(mu, "/etc/a.zip").empty());
    ctx.check("odd characters: refused", resolveUrl(mu, "a zip").empty() && resolveUrl(mu, "a?b").empty() && resolveUrl(mu, "a\\b").empty());
    // GitHub Releases: the manifest is fetched through /latest/, its relative names belong to the release it names.
    const std::string gh = "https://github.com/Benbone21/CrumpleZone/releases/latest/download/update-manifest.txt";
    ctx.check("github: /latest/ means the manifest's release", releaseManifestUrl(gh, "0.4.9") ==
                                                                "https://github.com/Benbone21/CrumpleZone/releases/download/v0.4.9/update-manifest.txt");
    Manifest gm;
    gm.version = "0.4.9";
    ctx.check("github: a relative name resolves in that release's folder",
              fileUrl(gh, gm, {"linux", "CrumpleZone-0.4.9-linux.zip", 1, ""}) == "https://github.com/Benbone21/CrumpleZone/releases/download/v0.4.9/CrumpleZone-0.4.9-linux.zip");
    ctx.check("github: an absolute URL stays as it is",
              fileUrl(gh, gm, {"linux", "https://github.com/Benbone21/CrumpleZone/releases/download/v0.4.9/a.zip", 1, ""}) ==
                  "https://github.com/Benbone21/CrumpleZone/releases/download/v0.4.9/a.zip");
    ctx.check("github: still can't climb out", fileUrl(gh, gm, {"linux", "../../v0.0.1/a.zip", 1, ""}).empty());
    ctx.check("another server's folder is its own", releaseManifestUrl(mu, "0.4.9") == mu && fileUrl(mu, gm, {"linux", "v0.4.9/a.zip", 1, ""}) ==
                                                                                               "https://updates.example.net/crumplezone/v0.4.9/a.zip");
    ctx.check("/latest/ only in the query: not rewritten", releaseManifestUrl("https://x.example/m.txt?r=/releases/latest/download/", "1.0.0") ==
                                                              "https://x.example/m.txt?r=/releases/latest/download/");
    ctx.check("allowed", allowedUrl(mu) && allowedUrl("http://127.0.0.1:8080/m.txt") && allowedUrl("http://localhost/m.txt") && allowedUrl("http://[::1]:9/m"));
    ctx.check("not allowed", !allowedUrl("http://example.net/m.txt") && !allowedUrl("ftp://x/y") && !allowedUrl("https://") && !allowedUrl("https://u@evil/x") &&
                                 !allowedUrl("http://127.0.0.1.evil.net/m") && !allowedUrl("file:///etc/passwd"));
}

SCENARIO(updDecide, "update/decide", "the decision for each kind of install: up to date, update now, the .deb's link, a dev build, not for this OS yet, a folder it can't write") {
    Manifest m = sampleManifest();
    const std::string mu = "https://updates.example.net/cz/manifest.txt";
    Install win = installAt(Layout::Windows, "C:/Users/me/AppData/Local/Programs/Crumple Zone");
    Decision d = decide(m, "0.4.1", win, mu);
    ctx.check("same version: up to date", d.verdict == Verdict::UpToDate);
    ctx.check("older on the server: up to date", decide(m, "0.5.0", win, mu).verdict == Verdict::UpToDate);
    d = decide(m, "0.4.0", win, mu);
    ctx.check("windows: self-update with its zip", d.verdict == Verdict::SelfUpdate && d.file && d.file->platform == "windows" && d.message.find("UPDATE 0.4.1 AVAILABLE") == 0,
              d.message);
    d = decide(m, "0.4.0", installAt(Layout::LinuxUser, "/home/me/.local/share/crumplezone/app"), mu);
    ctx.check("linux user install: self-update", d.verdict == Verdict::SelfUpdate && d.file->platform == "linux");
    d = decide(m, "0.4.0", installAt(Layout::Deb, "/opt/crumplezone"), mu);
    ctx.check(".deb: says where to download it", d.verdict == Verdict::Manual && d.message.find("HTTPS://") == std::string::npos &&
                                                     d.message.find("https://updates.example.net/cz/v0.4.1/CrumpleZone-0.4.1-linux.deb") != std::string::npos,
              d.message);
    d = decide(m, "0.4.0", installAt(Layout::Mac, "/Applications/CrumpleZone.app"), mu);
    ctx.check("mac, no mac file in this release: not for mac yet", d.verdict == Verdict::NotForThisOS && d.message.find("NOT FOR MAC YET") != std::string::npos);
    Install dev;
    dev.why = "THIS IS A DEVELOPMENT BUILD: IT DOESN'T UPDATE ITSELF";
    d = decide(m, "0.4.0", dev, mu);
    ctx.check("dev build: tells, doesn't update", d.verdict == Verdict::Manual && d.message.find("DEVELOPMENT BUILD") != std::string::npos);
    Install ro = win;
    ro.why = "THIS ACCOUNT CAN'T WRITE TO C:/PROGRAM FILES";
    d = decide(m, "0.4.0", ro, mu);
    ctx.check("a folder it can't write: manual, with the reason", d.verdict == Verdict::Manual && d.message.find("CAN'T WRITE") != std::string::npos);
    d = decide(m, "0.4.0", win, mu, 8, 7);
    ctx.check("asked for a newer host: says so and offers it", d.verdict == Verdict::SelfUpdate && d.message.find("YOUR FRIEND'S GAME IS NEWER") == 0);
    d = decide(m, "0.4.1", win, mu, 9, 8);
    ctx.check("host newer than the newest release: up to date, explained", d.verdict == Verdict::UpToDate && d.message.find("NEWER THAN THE LATEST RELEASE") != std::string::npos);
    Manifest bad = m;
    bad.files[0].url = "http://evil.example/x.zip";
    ctx.check("a file URL that isn't https: not offered", decide(bad, "0.4.0", win, mu).verdict == Verdict::NotForThisOS);
}

SCENARIO(updUnzip, "update/unzip", "a zip made by Info-ZIP unpacks with its modes and link; unsafe paths, links out of the tree, damage and junk are refused") {
    TempDir t("unzip");
    const std::vector<uint8_t> z = unhex(kFixtureZip);
    put(t.path + "/a.zip", std::string(z.begin(), z.end()));
    std::string why;
    const std::string out = t.path + "/out";
    ctx.check("unpacks", unzip(t.path + "/a.zip", out, why), why);
    std::string lazy;
    for (int i = 0; i < 120; i++) lazy += "the quick brown fox jumps over the lazy dog ";
    ctx.check("the deflated file", get(out + "/crumplezone/assets/a.txt") == lazy);
    ctx.check("the stored ones", get(out + "/crumplezone/crumplezone") == "#!/bin/sh\necho v2\n" && get(out + "/crumplezone/cz-install.txt") == "layout linux\n");
#ifndef _WIN32
    ctx.check("the program is executable", (fs::status(out + "/crumplezone/crumplezone").permissions() & fs::perms::owner_exec) != fs::perms::none);
    ctx.check("the link is a link", fs::is_symlink(out + "/crumplezone/link") && fs::read_symlink(out + "/crumplezone/link") == "assets/a.txt" &&
                                        get(out + "/crumplezone/link") == lazy);
#endif
    ctx.check("won't unpack over a folder", !unzip(t.path + "/a.zip", out, why));
    auto refused = [&](const std::vector<ZEntry>& es, const std::string& name) {
        put(t.path + "/" + name + ".zip", makeZip(es));
        std::string w;
        const bool r = !unzip(t.path + "/" + name + ".zip", t.path + "/" + name, w);
        return r && !fs::exists(t.path + "/evil.txt") && !fs::exists(t.path + "/" + name + "/../evil.txt");
    };
    ctx.check("my writer's zip unpacks", [&] {
        put(t.path + "/ok.zip", makeZip({{"x/a.txt", "hello"}, {"x/run", "#!", 0100755}}));
        std::string w;
        return unzip(t.path + "/ok.zip", t.path + "/ok", w) && get(t.path + "/ok/x/a.txt") == "hello";
    }());
    ctx.check("../ refused", refused({{"../evil.txt", "x"}}, "dots"));
    ctx.check("a/../../ refused", refused({{"a/../../evil.txt", "x"}}, "dots2"));
    ctx.check("absolute refused", refused({{"/tmp/evil.txt", "x"}}, "abs"));
    ctx.check("backslash or drive refused", refused({{"a\\..\\evil.txt", "x"}}, "bs") && refused({{"C:/evil.txt", "x"}}, "drive"));
#ifndef _WIN32
    ctx.check("a link out of the tree refused", refused({{"x/l", "../../evil.txt", 0120777}}, "link") && refused({{"x/l", "/etc/passwd", 0120777}}, "link2"));
#endif
    ctx.check("a bad checksum refused", refused({{"x/a.txt", "hello", 0100644, true}}, "crc"));
    ctx.check("not a zip refused", [&] {
        put(t.path + "/junk.zip", std::string(5000, 'j'));
        std::string w;
        return !unzip(t.path + "/junk.zip", t.path + "/junk", w) && w.find("NOT A ZIP") != std::string::npos;
    }());
    ctx.check("cut short refused", [&] {
        put(t.path + "/cut.zip", std::string(z.begin(), z.begin() + z.size() / 2));
        std::string w;
        return !unzip(t.path + "/cut.zip", t.path + "/cut", w);
    }());
    ctx.check("mac resource shadows skipped", [&] {
        put(t.path + "/mac.zip", makeZip({{"A.app/Contents/x", "1"}, {"__MACOSX/A.app/._x", "junk"}, {"A.app/._y", "junk"}}));
        std::string w;
        return unzip(t.path + "/mac.zip", t.path + "/mac", w) && fs::exists(t.path + "/mac/A.app/Contents/x") && !fs::exists(t.path + "/mac/__MACOSX") &&
               !fs::exists(t.path + "/mac/A.app/._y");
    }());
}

SCENARIO(updSwap, "update/swap", "for each layout: the new version swaps in, the old one is kept, an older backup goes, the installer's uninstaller stays; rollback trades them back") {
    for (Layout l : {Layout::Windows, Layout::Mac, Layout::LinuxUser}) {
        TempDir t(std::string("swap-") + layoutName(l));
        const std::string name = l == Layout::Windows ? "Crumple Zone" : l == Layout::Mac ? "CrumpleZone.app" : "app";
        const Install in = installAt(l, t.path + "/" + name);
        const std::string tag = std::string(layoutName(l)) + ": ";
        ctx.check(tag + "staging and backup beside the install", fs::path(in.staging).parent_path() == fs::path(in.live).parent_path() &&
                                                                    fs::path(in.previous).parent_path() == fs::path(in.live).parent_path() && in.previous != in.live);
        put(in.live + "/version.txt", "1");
        if (l == Layout::Windows) put(in.live + "/Uninstall.exe", "uninstaller");
        const std::string fresh = in.staging + "/" + zipTopFolder(l);
        put(fresh + "/version.txt", "2");
        std::string why;
        ctx.check(tag + "swaps", swapIn(in.live, fresh, in.previous, keepAcrossUpdates(l), why), why);
        ctx.check(tag + "the new one is live, the old one kept", get(in.live + "/version.txt") == "2" && get(in.previous + "/version.txt") == "1");
        if (l == Layout::Windows) ctx.check(tag + "Uninstall.exe carried over", get(in.live + "/Uninstall.exe") == "uninstaller");
        put(fresh + "/version.txt", "3");
        ctx.check(tag + "a second update", swapIn(in.live, fresh, in.previous, keepAcrossUpdates(l), why) && get(in.live + "/version.txt") == "3" &&
                                              get(in.previous + "/version.txt") == "2");
        ctx.check(tag + "rollback", rollBack(in.live, in.previous, why) && get(in.live + "/version.txt") == "2" && get(in.previous + "/version.txt") == "3", why);
        ctx.check(tag + "nothing new to swap: refused, live untouched", !swapIn(in.live, fresh, in.previous, {}, why) && get(in.live + "/version.txt") == "2");
        removeTree(in.previous);
        ctx.check(tag + "no backup: rollback refused, live untouched", !rollBack(in.live, in.previous, why) && get(in.live + "/version.txt") == "2");
    }
}

SCENARIO(updDetect, "update/detect", "the install is told by the marker the packaging writes; none means a dev build") {
#ifndef __APPLE__
    TempDir t("detect");
    ctx.check("no marker: a dev build", detectInstall(t.path).layout == Layout::None && !detectInstall(t.path).why.empty());
    put(t.path + "/" + kMarker, "layout linux\n");
    Install in = detectInstall(t.path);
    ctx.check("linux", in.layout == Layout::LinuxUser && in.live == t.path && in.why.empty() && in.previous == t.path + ".previous", in.why);
    put(t.path + "/" + kMarker, "layout windows\n");
    ctx.check("windows", detectInstall(t.path).layout == Layout::Windows && detectInstall(t.path).previous == t.path + ".old");
    put(t.path + "/" + kMarker, "layout deb\n");
    ctx.check("deb", detectInstall(t.path).layout == Layout::Deb);
    put(t.path + "/" + kMarker, "layout toaster\n");
    ctx.check("unknown: a dev build", detectInstall(t.path).layout == Layout::None);
#endif
    const Install mac = installAt(Layout::Mac, "/Applications/CrumpleZone.app");
    ctx.check("mac paths hidden beside the app", mac.staging == "/Applications/.CrumpleZone-update" && mac.previous == "/Applications/.CrumpleZone-previous.app");
}

SCENARIO(updAfterSwap, "update/after-swap", "after a swap the new version counts its starts; two that never reach 'good' roll back; failures are reported once") {
    AfterSwap a;
    a.from = "0.4.0", a.to = "0.4.1";
    ctx.check("written and read back", [&] {
        AfterSwap b = readAfterSwap(writeAfterSwap(a));
        return b.from == "0.4.0" && b.to == "0.4.1" && b.starts == 0 && b.failed.empty() && !b.rolledBack;
    }());
    ctx.check("first start: count", onStart(a, "0.4.1") == StartAction::Count);
    a.starts = 1;
    ctx.check("second start: count", onStart(a, "0.4.1") == StartAction::Count);
    a.starts = 2;
    ctx.check("third start after two failures: roll back", onStart(a, "0.4.1") == StartAction::RollBack);
    ctx.check("another version running: nothing", onStart(a, "0.4.0") == StartAction::None);
    AfterSwap f;
    f.failed = "CAN'T MOVE THE GAME'S FOLDER ASIDE";
    ctx.check("a failed helper swap: reported", onStart(f, "0.4.0") == StartAction::ReportFailure && readAfterSwap(writeAfterSwap(f)).failed == f.failed);
    AfterSwap r;
    r.rolledBack = true, r.from = "0.4.1", r.to = "0.4.0", r.skip = "0.4.1";
    ctx.check("rolled back: reported", onStart(r, "0.4.0") == StartAction::ReportRollBack && readAfterSwap(writeAfterSwap(r)).skip == "0.4.1");
    ctx.check("nothing there: nothing", onStart(readAfterSwap(""), "0.4.0") == StartAction::None);
}

SCENARIO(updHelper, "update/helper", "the Windows swap helper's steps (here on this OS): swap in, keep the uninstaller, clean the staging; go back; a failure is written for the next start") {
    TempDir t("helper");
    const Install in = installAt(Layout::Windows, t.path + "/Crumple Zone");
    put(in.live + "/version.txt", "1");
    put(in.live + "/Uninstall.exe", "u");
    const std::string fresh = in.staging + "/CrumpleZone";
    put(fresh + "/version.txt", "2");
    const std::string state = t.path + "/update-state.txt";
    AfterSwap a;
    a.from = "0.4.0", a.to = "0.4.1";
    put(state, writeAfterSwap(a));
    auto run = [&](std::vector<std::string> args) {
        args.insert(args.begin(), {"crumplezone", "--cz-swap"});
        std::vector<char*> argv;
        for (std::string& s : args) argv.push_back(&s[0]);
        return helperMain((int)argv.size(), argv.data());
    };
    ctx.check("recognised", [&] {
        std::string a0 = "x", a1 = "--cz-swap";
        char* v[] = {&a0[0], &a1[0]};
        return isHelperCall(2, v) && !isHelperCall(1, v);
    }());
    ctx.check("swap in", run({"in", "0", in.live, fresh, in.previous, state, "-"}) == 0 && get(in.live + "/version.txt") == "2" && get(in.live + "/Uninstall.exe") == "u" &&
                             !fs::exists(in.staging));
    ctx.check("the state is left for the new version to count", readAfterSwap(get(state)).to == "0.4.1");
    ctx.check("go back", run({"back", "0", in.live, "-", in.previous, state, "-"}) == 0 && get(in.live + "/version.txt") == "1");
    removeTree(in.previous);
    ctx.check("nothing to swap in: fails (quickly enough)", run({"in", "0", in.live, fresh, in.previous, state, "-"}) == 1);
    ctx.check("and says why for the next start", onStart(readAfterSwap(get(state)), "0.4.0") == StartAction::ReportFailure && get(in.live + "/version.txt") == "1");
    ctx.check("too few arguments", run({"in"}) == 2);
}

SCENARIO(updHttp, "update/http", "downloads from a local HTTP server through every backend this machine has: the body, progress, 404 and a cut-short body in plain words") {
    HttpServer srv;
    std::string big;
    for (int i = 0; i < 300000; i++) big += (char)('a' + (i * 7) % 26);
    srv.files["/m.txt"] = "crumplezone-update 1\nversion 0.4.1\n";
    srv.files["/big.zip"] = big;
    srv.redirects["/latest/m.txt"] = "/m.txt";
    srv.redirects["/latest/big.zip"] = "/v1/big.zip";
    srv.redirects["/loop"] = "/loop";
    srv.redirects["/v1/big.zip"] = "abs:/big.zip";  // (an absolute Location, like GitHub's to objects.githubusercontent.com)
    if (!ctx.check("the server starts", srv.start())) return;
    TempDir t("http");
    int tried = 0;
    for (http::Backend b : {http::Backend::LibCurl, http::Backend::CurlTool, http::Backend::WinHttp}) {
        if (!http::available(b)) continue;
        tried++;
        const std::string tag = std::string(http::backendName(b)) + ": ";
        std::string body, why;
        ctx.check(tag + "fetch", http::fetch(srv.url("/m.txt"), 4096, body, why, b) && body == srv.files["/m.txt"], why);
        ctx.check(tag + "too big for fetch", !http::fetch(srv.url("/big.zip"), 1000, body, why, b));
        http::Progress p;
        const std::string path = t.path + "/" + http::backendName(b) + ".zip";
        ctx.check(tag + "download", http::download(srv.url("/big.zip"), path, &p, why, b) && sha2::sha256File(path) == sha2::sha256Hex(big), why);
        ctx.check(tag + "progress counted every byte", p.got == big.size());
        ctx.check(tag + "404 in words", !http::download(srv.url("/nope.zip"), path, &p, why, b) && why.find("404") != std::string::npos && !fs::exists(path), why);
        srv.shortBody = true;
        ctx.check(tag + "a cut-short body fails and leaves no file", !http::download(srv.url("/big.zip"), path, &p, why, b) && !fs::exists(path), why);
        srv.shortBody = false;
        ctx.check(tag + "follows redirects (fetch)", http::fetch(srv.url("/latest/m.txt"), 4096, body, why, b) && body == srv.files["/m.txt"], why);
        ctx.check(tag + "follows two redirects (download)", http::download(srv.url("/latest/big.zip"), path, &p, why, b) && sha2::sha256File(path) == sha2::sha256Hex(big), why);
        ctx.check(tag + "a redirect loop gives up in words", !http::fetch(srv.url("/loop"), 4096, body, why, b) && !why.empty(), why);
        ctx.check(tag + "plain http elsewhere is refused", !http::fetch("http://example.invalid/m.txt", 4096, body, why, b) && why.find("PROTOCOL") != std::string::npos, why);
        ctx.note(tag + "used");
    }
    ctx.check("at least one backend here", tried > 0);
}

namespace {
// Waits for the updater to reach one of the phases (or the time runs out).
update::Phase waitFor(std::initializer_list<update::Phase> want, double seconds) {
    const auto end = std::chrono::steady_clock::now() + std::chrono::milliseconds((int)(seconds * 1000));
    for (;;) {
        const update::Phase p = update::status().phase;
        for (update::Phase w : want)
            if (p == w) return p;
        if (std::chrono::steady_clock::now() > end) return p;
        std::this_thread::sleep_for(std::chrono::milliseconds(20));
    }
}
std::string waitNotice(double seconds) {
    const auto end = std::chrono::steady_clock::now() + std::chrono::milliseconds((int)(seconds * 1000));
    for (;;) {
        const std::string n = update::takeNotice();
        if (!n.empty() || std::chrono::steady_clock::now() > end) return n;
        std::this_thread::sleep_for(std::chrono::milliseconds(20));
    }
}
}  // namespace

SCENARIO(updFlow, "update/flow", "the whole thing against a local server: check, offer, download, verify, unpack, swap; a changed download, a missing manifest, a foreign key and an older release") {
    TempDir t("flow");
    // This OS's install, as its packaging lays it out.
#ifdef _WIN32
    const Layout l = Layout::Windows;
    const std::string live = t.path + "/Crumple Zone", exeDir = live, inner = "", exe = "crumplezone.exe";
#elif defined(__APPLE__)
    const Layout l = Layout::Mac;
    const std::string live = t.path + "/CrumpleZone.app", exeDir = live + "/Contents/MacOS", inner = "Contents/", exe = "MacOS/crumplezone";
#else
    const Layout l = Layout::LinuxUser;
    const std::string live = t.path + "/app", exeDir = live, inner = "", exe = "crumplezone";
#endif
    const std::string markerRel = l == Layout::Mac ? "Contents/Resources/cz-install.txt" : "cz-install.txt";
    const std::string marker = std::string("layout ") + layoutName(l) + "\n";
    put(live + "/" + markerRel, marker);
    put(live + "/" + inner + exe, "old program");
    put(live + "/version.txt", "old");
    const std::string top = zipTopFolder(l);
    const std::string zip = makeZip({{top + "/" + inner + exe, "new program", 0100755}, {top + "/" + markerRel, marker}, {top + "/version.txt", "new"}});
    uint8_t seed[32], other[32];
    testSeed(seed);
    testSeed(other, "another throwaway test key");
    auto manifest = [&](const std::string& version, const std::string& fileUrl, const uint8_t* key) {
        Manifest m;
        m.version = version;
        m.net = 99;
        m.files = {{layoutName(l), fileUrl, (uint64_t)zip.size(), sha2::sha256Hex(zip)}};
        return signManifest(m, key);
    };
    HttpServer srv;
    srv.files["/rel/manifest.txt"] = manifest("9.9.9", "v9.9.9/game.zip", seed);
    srv.files["/rel/v9.9.9/game.zip"] = zip;
    std::string changed = zip;
    changed[changed.size() / 2] ^= 1;
    srv.files["/bad/manifest.txt"] = manifest("9.9.9", "game.zip", seed);
    srv.files["/bad/game.zip"] = changed;
    srv.files["/foreign/manifest.txt"] = manifest("9.9.9", "game.zip", other);
    srv.files["/old/manifest.txt"] = manifest("0.0.1", "game.zip", seed);
    if (!ctx.check("the server starts", srv.start())) return;
    const std::string cfg = t.path + "/config";
    auto setUp = [&](const std::string& path) {
        update::testSetup(cfg, exeDir, {pubOf(seed)});
        std::string a0 = "crumplezone", a1 = "--update-url", a2 = srv.url(path);
        char* argv[] = {&a0[0], &a1[0], &a2[0]};
        return update::init(3, argv, 8);
    };

    ctx.check("init: nothing to do, keeps running", !setUp("/rel/manifest.txt"));
    update::checkFor(99, "9.9.9");
    ctx.check("a newer host: the check offers 9.9.9", waitFor({Phase::Offer, Phase::Info}, 60) == Phase::Offer && update::status().title.find("9.9.9") != std::string::npos,
              update::status().title + " / " + update::status().message);
    update::accept();
    const Phase end = waitFor({Phase::Restarting, Phase::Info}, 30);
    ctx.check("downloaded, checked, unpacked and installed", end == Phase::Restarting, update::status().message);
    const Install in = installAt(l, live);
#ifdef _WIN32
    ctx.check("windows: the new version waits in the staging folder for the helper", get(in.staging + "/" + top + "/version.txt") == "new" && get(live + "/version.txt") == "old");
#else
    ctx.check("the new version is live, the old one kept for rollback", get(live + "/version.txt") == "new" && get(in.previous + "/version.txt") == "old" &&
                                                                       get(live + "/" + inner + exe) == "new program");
    ctx.check("staging and the download cleaned up", !fs::exists(in.staging) && !fs::exists(cfg + "/update-download.zip"));
#endif
    ctx.check("the next start will count itself", readAfterSwap(get(cfg + "/update-state.txt")).to == "9.9.9");

    // Put the old one back for the rest.
    removeTree(in.staging);
    removeTree(cfg);
    put(live + "/version.txt", "old");
    setUp("/bad/manifest.txt");
    update::checkFor(99);
    waitFor({Phase::Offer, Phase::Info}, 60);
    update::accept();
    ctx.check("a download that doesn't match the signed hash: refused", waitFor({Phase::Restarting, Phase::Info}, 30) == Phase::Info &&
                                                                          update::status().message.find("DOESN'T MATCH") != std::string::npos,
              update::status().message);
    ctx.check("and nothing changed", get(live + "/version.txt") == "old" && !fs::exists(in.staging) && !fs::exists(cfg + "/update-download.zip"));
    update::dismiss();

    setUp("/foreign/manifest.txt");
    update::checkOnLaunch();
    const std::string n1 = waitNotice(60);
    ctx.check("a manifest signed by another key: a quiet notice, no card", n1.find("DOESN'T MATCH") != std::string::npos && update::status().phase == Phase::Idle, n1);
    setUp("/nothing/manifest.txt");
    update::checkOnLaunch();
    const std::string n2 = waitNotice(60);
    ctx.check("no manifest on the server (404): a quiet notice", n2.find("COULDN'T CHECK") != std::string::npos && update::status().phase == Phase::Idle, n2);
    setUp("/old/manifest.txt");
    update::checkFor(9);
    ctx.check("the server only has an older release: the card says so", waitFor({Phase::Info, Phase::Offer}, 60) == Phase::Info &&
                                                                           update::status().message.find("NEWER THAN THE LATEST RELEASE") != std::string::npos,
              update::status().message);
    update::dismiss();
    setUp("/rel/manifest.txt");
    update::checkOnLaunch();
    ctx.check("the launch check offers it too", waitFor({Phase::Offer}, 60) == Phase::Offer);
    update::later();
    ctx.check("LATER hides it", update::status().phase == Phase::Idle);
    update::testSetup("", "", {});
}

SCENARIO(updGithub, "update/github", "GitHub Releases' layout on a local server: /releases/latest/download/update-manifest.txt redirects to the newest release, its files (absolute or relative names) to a file host; the game finds, checks and installs them") {
    TempDir t("github");
#ifdef _WIN32
    const Layout l = Layout::Windows;
    const std::string live = t.path + "/Crumple Zone", exeDir = live, inner = "", exe = "crumplezone.exe";
#elif defined(__APPLE__)
    const Layout l = Layout::Mac;
    const std::string live = t.path + "/CrumpleZone.app", exeDir = live + "/Contents/MacOS", inner = "Contents/", exe = "MacOS/crumplezone";
#else
    const Layout l = Layout::LinuxUser;
    const std::string live = t.path + "/app", exeDir = live, inner = "", exe = "crumplezone";
#endif
    const std::string markerRel = l == Layout::Mac ? "Contents/Resources/cz-install.txt" : "cz-install.txt";
    const std::string marker = std::string("layout ") + layoutName(l) + "\n";
    const std::string top = zipTopFolder(l);
    const std::string zip = makeZip({{top + "/" + inner + exe, "new program", 0100755}, {top + "/" + markerRel, marker}, {top + "/version.txt", "new"}});
    const std::string name = std::string("CrumpleZone-9.9.9-") + layoutName(l) + ".zip";
    uint8_t seed[32];
    testSeed(seed);
    const std::string repo = "/Benbone21/CrumpleZone/releases";
    HttpServer srv;
    auto signedFor = [&](const std::string& fileUrl) {
        Manifest m;
        m.version = "9.9.9";
        m.net = 99;
        m.notes = "https://github.com/Benbone21/CrumpleZone/releases/tag/v9.9.9";
        m.files = {{layoutName(l), fileUrl, (uint64_t)zip.size(), sha2::sha256Hex(zip)}, {"deb", "CrumpleZone-9.9.9-linux.deb", 1, std::string(64, 'c')}};
        return signManifest(m, seed);
    };
    // Two "repositories" on the one server: one whose manifest names its files relatively, one with absolute URLs (what CI writes).
    for (const char* kind : {"rel", "abs"}) {
        const std::string base = std::string("/") + kind + repo;
        srv.redirects[base + "/latest/download/update-manifest.txt"] = base + "/download/v9.9.9/update-manifest.txt";
        srv.redirects[base + "/download/v9.9.9/update-manifest.txt"] = std::string("abs:/objects/") + kind + "/manifest?sig=x&exp=1";
        srv.redirects[base + "/download/v9.9.9/" + name] = std::string("abs:/objects/") + kind + "/zip?sig=y";
        srv.files[std::string("/objects/") + kind + "/zip"] = zip;
    }
    srv.files["/objects/rel/manifest"] = signedFor(name);
    // (the absolute one needs the port: set below, before the first request)
    // A release whose /latest/ has no manifest yet (the release is up, CI hasn't attached it): a quiet notice, nothing else.
    if (!ctx.check("the server starts", srv.start())) return;
    srv.set("/objects/abs/manifest", signedFor(srv.url("/abs" + repo + "/download/v9.9.9/" + name)));
    const std::string cfg = t.path + "/config";
    for (const char* kind : {"rel", "abs"}) {
        const std::string tag = std::string(kind) + ": ";
        removeTree(live), removeTree(cfg), removeTree(installAt(l, live).previous);
        put(live + "/" + markerRel, marker);
        put(live + "/" + inner + exe, "old program");
        put(live + "/version.txt", "old");
        update::testSetup(cfg, exeDir, {pubOf(seed)});
        std::string a0 = "crumplezone", a1 = "--update-url", a2 = srv.url(std::string("/") + kind + repo + "/latest/download/update-manifest.txt");
        char* argv[] = {&a0[0], &a1[0], &a2[0]};
        update::init(3, argv, 8);
        update::checkOnLaunch();
        ctx.check(tag + "the launch check follows /latest/ and offers 9.9.9", waitFor({Phase::Offer, Phase::Info}, 60) == Phase::Offer,
                  update::status().title + " / " + update::status().message + " / " + update::takeNotice());
        update::accept();
        ctx.check(tag + "the release's file found, checked and installed", waitFor({Phase::Restarting, Phase::Info}, 30) == Phase::Restarting, update::status().message);
#ifdef _WIN32
        ctx.check(tag + "windows: waiting in the staging folder", get(installAt(l, live).staging + "/" + top + "/version.txt") == "new");
        removeTree(installAt(l, live).staging);
#else
        ctx.check(tag + "the new version is live", get(live + "/version.txt") == "new" && get(installAt(l, live).previous + "/version.txt") == "old");
#endif
    }
    // The .deb's card on the same release: the two ways, never a swap.
    {
        TempDir d("github-deb");
        put(d.path + "/cz-install.txt", "layout deb\n");
        update::testSetup(cfg + "-deb", d.path, {pubOf(seed)});
        std::string a0 = "crumplezone", a1 = "--update-url", a2 = srv.url("/rel" + repo + "/latest/download/update-manifest.txt");
        char* argv[] = {&a0[0], &a1[0], &a2[0]};
        update::init(3, argv, 8);
        update::checkOnLaunch();
        waitFor({Phase::Info, Phase::Offer}, 60);
        const update::Status st = update::status();
        ctx.check(".deb: an Info card, not UPDATE NOW", st.phase == Phase::Info, st.title + " / " + st.message);
        ctx.check(".deb: says download the new .deb or switch to the per-user install",
                  st.message.find("INSTALLED FROM THE .DEB") != std::string::npos && st.message.find("PER-USER INSTALL") != std::string::npos &&
                      st.message.find("INSTALL.SH") != std::string::npos, st.message);
        ctx.check(".deb: the link is that release's .deb", st.link == srv.url("/rel" + repo + "/download/v9.9.9/CrumpleZone-9.9.9-linux.deb"), st.link);
        ctx.check(".deb: its folder untouched", get(d.path + "/cz-install.txt") == "layout deb\n" && !fs::exists(d.path + ".previous"));
        update::dismiss();
        update::testSetup(cfg + "-deb", d.path, {pubOf(seed)});
        update::init(3, argv, 8);
        update::checkOnLaunch();
        waitFor({Phase::Info, Phase::Offer}, 2);
        ctx.check(".deb: once OK'd, the next launch doesn't show it again", update::status().phase == Phase::Idle, update::status().message);
        update::checkFor(99);
        ctx.check(".deb: an explicit check still does", waitFor({Phase::Info, Phase::Offer}, 60) == Phase::Info, update::status().message);
        update::dismiss();
    }
    // The release is up but CI hasn't attached the manifest yet: a quiet notice on launch.
    update::testSetup(cfg, exeDir, {pubOf(seed)});
    std::string a0 = "crumplezone", a1 = "--update-url", a2 = srv.url("/none" + repo + "/latest/download/update-manifest.txt");
    char* argv[] = {&a0[0], &a1[0], &a2[0]};
    update::init(3, argv, 8);
    update::checkOnLaunch();
    const std::string n = waitNotice(60);
    ctx.check("no manifest on the newest release yet: a quiet notice, no card", n.find("COULDN'T CHECK") != std::string::npos && update::status().phase == Phase::Idle, n);
    update::testSetup("", "", {});
}

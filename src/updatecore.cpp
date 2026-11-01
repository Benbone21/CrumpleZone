#include "updatecore.h"

#include <cctype>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <sstream>

#include "ed25519.h"
#include "sha2.h"
#include "updatekeys.h"

#ifndef _WIN32
#include <unistd.h>
#endif

// stb_image's inflate (raw deflate for the zip entries), as static functions like groundtex.cpp's copy.
#define STB_IMAGE_STATIC
#define STB_IMAGE_IMPLEMENTATION
#define STBI_ONLY_PNG
#define STBI_NO_STDIO
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wunused-function"
#include "third_party/stb_image.h"
#pragma GCC diagnostic pop

namespace fs = std::filesystem;

namespace update {

// ---- versions --------------------------------------------------------------------------------------------------------
namespace {
struct Ver {
    bool ok = false;
    std::vector<long> nums;
    std::string suffix;
};
Ver parseVer(const std::string& tag) {
    Ver r;
    const std::string v = !tag.empty() && (tag[0] == 'v' || tag[0] == 'V') ? tag.substr(1) : tag;  // (a tag's name, v0.4.10, is that version)
    const size_t dash = v.find('-');
    const std::string head = v.substr(0, dash);
    r.suffix = dash == std::string::npos ? "" : v.substr(dash + 1);
    if (dash != std::string::npos && r.suffix.empty()) return r;
    for (char c : r.suffix)
        if (!std::isalnum((unsigned char)c) && c != '.') return r;
    size_t i = 0;
    while (i <= head.size()) {
        size_t j = head.find('.', i);
        if (j == std::string::npos) j = head.size();
        const std::string part = head.substr(i, j - i);
        if (part.empty() || part.size() > 9) return r;
        for (char c : part)
            if (!std::isdigit((unsigned char)c)) return r;
        r.nums.push_back(std::atol(part.c_str()));
        i = j + 1;
    }
    r.ok = !r.nums.empty() && r.nums.size() <= 4;
    return r;
}
}  // namespace

bool validVersion(const std::string& v) { return parseVer(v).ok; }

int compareVersions(const std::string& a, const std::string& b) {
    const Ver x = parseVer(a), y = parseVer(b);
    if (!x.ok || !y.ok) return (int)x.ok - (int)y.ok;
    for (size_t i = 0; i < std::max(x.nums.size(), y.nums.size()); i++) {
        const long p = i < x.nums.size() ? x.nums[i] : 0, q = i < y.nums.size() ? y.nums[i] : 0;
        if (p != q) return p < q ? -1 : 1;
    }
    if (x.suffix.empty() != y.suffix.empty()) return x.suffix.empty() ? 1 : -1;
    const int c = x.suffix.compare(y.suffix);
    return c < 0 ? -1 : c > 0 ? 1 : 0;
}

// ---- the manifest ----------------------------------------------------------------------------------------------------
const FileEntry* Manifest::file(const std::string& platform) const {
    for (const FileEntry& f : files)
        if (f.platform == platform) return &f;
    return nullptr;
}

std::string manifestBody(const Manifest& m) {
    std::ostringstream o;
    o << "crumplezone-update 1\n";
    o << "version " << m.version << "\n";
    if (m.net > 0) o << "net " << m.net << "\n";
    if (!m.date.empty()) o << "date " << m.date << "\n";
    if (!m.notes.empty()) o << "notes " << m.notes << "\n";
    for (const FileEntry& f : m.files) o << "file " << f.platform << " " << f.url << " " << f.size << " " << f.sha256 << "\n";
    return o.str();
}

std::string signManifest(const Manifest& m, const uint8_t seed[32]) {
    const std::string body = manifestBody(m);
    uint8_t sig[64];
    ed25519::sign(sig, (const uint8_t*)body.data(), body.size(), seed);
    return body + "signature " + sha2::toHex(sig, 64) + "\n";
}

namespace {
bool safeToken(const std::string& s) {
    if (s.empty()) return false;
    for (unsigned char c : s)
        if (c <= ' ' || c >= 127) return false;
    return true;
}
}  // namespace

bool readManifest(const std::string& text, const std::vector<PublicKey>& keys, Manifest& out, std::string& why) {
    out = Manifest();
    if (keys.empty()) {
        why = "THIS BUILD HAS NO UPDATE KEY, SO IT CAN'T CHECK AN UPDATE";
        return false;
    }
    if (text.empty()) {
        why = "THE UPDATE SERVER SENT AN EMPTY FILE";
        return false;
    }
    size_t at = text.rfind("\nsignature ");
    if (at == std::string::npos) {
        why = "THE UPDATE FILE IS NOT SIGNED";
        return false;
    }
    at++;
    const std::string body = text.substr(0, at);
    std::string sigHex = text.substr(at + 10);
    while (!sigHex.empty() && std::isspace((unsigned char)sigHex.back())) sigHex.pop_back();
    uint8_t sig[64];
    if (!sha2::fromHex(sigHex, sig, 64)) {
        why = "THE UPDATE FILE'S SIGNATURE IS DAMAGED";
        return false;
    }
    bool good = false;
    for (const PublicKey& k : keys)
        if (ed25519::verify(sig, (const uint8_t*)body.data(), body.size(), k.data())) good = true;
    if (!good) {
        why = "THE UPDATE FILE'S SIGNATURE DOESN'T MATCH: IT WAS CHANGED, OR NOT MADE BY THE GAME'S RELEASES";
        return false;
    }
    // Signed: now read it.
    std::istringstream in(body);
    std::string line;
    int n = 0;
    while (std::getline(in, line)) {
        if (!line.empty() && line.back() == '\r') line.pop_back();
        std::istringstream ls(line);
        std::string key;
        ls >> key;
        if (n++ == 0) {
            int format = 0;
            ls >> format;
            if (key != "crumplezone-update" || format < 1) {
                why = "THE UPDATE FILE IS NOT A CRUMPLE ZONE RELEASE";
                return false;
            }
            if (format > 1) {
                why = "THE UPDATE SERVER USES A NEWER FORMAT THAN THIS GAME KNOWS: DOWNLOAD THE NEW VERSION BY HAND";
                return false;
            }
            continue;
        }
        if (key == "version") ls >> out.version;
        else if (key == "net") ls >> out.net;
        else if (key == "date") ls >> out.date;
        else if (key == "notes") ls >> out.notes;
        else if (key == "file") {
            FileEntry f;
            std::string size;
            ls >> f.platform >> f.url >> size >> f.sha256;
            uint8_t h[32];
            bool digits = !size.empty() && size.size() < 16;
            for (char c : size) digits = digits && std::isdigit((unsigned char)c);
            if (!safeToken(f.platform) || !safeToken(f.url) || !digits || !sha2::fromHex(f.sha256, h, 32)) {
                why = "THE UPDATE FILE HAS A BAD LINE: " + line.substr(0, 60);
                return false;
            }
            f.size = std::strtoull(size.c_str(), nullptr, 10);
            f.sha256 = sha2::toHex(h, 32);
            out.files.push_back(f);
        }
    }
    if (!validVersion(out.version)) {
        why = "THE UPDATE FILE HAS NO VERSION";
        return false;
    }
    return true;
}

std::vector<PublicKey> builtinKeys() {
    std::vector<PublicKey> keys;
    for (const char* hex : kUpdatePublicKeys) {
        PublicKey k;
        if (hex && sha2::fromHex(hex, k.data(), 32)) keys.push_back(k);
    }
    return keys;
}

bool allowedUrl(const std::string& url) {
    for (unsigned char c : url)
        if (c <= ' ' || c >= 127 || c == '\\') return false;
    auto hostOf = [&](size_t start) {
        const size_t end = url.find('/', start);
        return url.substr(start, end == std::string::npos ? std::string::npos : end - start);
    };
    if (url.compare(0, 8, "https://") == 0) {
        const std::string host = hostOf(8);
        return !host.empty() && host.find('@') == std::string::npos;
    }
    if (url.compare(0, 7, "http://") == 0) {
        std::string host = hostOf(7);
        if (host.compare(0, 5, "[::1]") == 0) return host.size() == 5 || host[5] == ':';
        const size_t colon = host.find(':');
        if (colon != std::string::npos) host = host.substr(0, colon);
        return host == "127.0.0.1" || host == "localhost";
    }
    return false;
}

std::string resolveUrl(const std::string& manifestUrl, const std::string& fileUrl) {
    if (fileUrl.find("://") != std::string::npos) return allowedUrl(fileUrl) ? fileUrl : "";
    if (fileUrl.empty() || fileUrl[0] == '/' || !allowedUrl(manifestUrl)) return "";
    for (unsigned char c : fileUrl)
        if (!std::isalnum(c) && c != '.' && c != '_' && c != '-' && c != '/' && c != '+') return "";
    if (fileUrl == ".." || fileUrl.compare(0, 3, "../") == 0 || fileUrl.find("/../") != std::string::npos ||
        (fileUrl.size() >= 3 && fileUrl.compare(fileUrl.size() - 3, 3, "/..") == 0))
        return "";
    const size_t q = manifestUrl.find_first_of("?#");
    const std::string base = manifestUrl.substr(0, q);
    const size_t slash = base.rfind('/');
    if (slash == std::string::npos || slash < base.find("://") + 3) return base + "/" + fileUrl;
    return base.substr(0, slash + 1) + fileUrl;
}

std::string releaseManifestUrl(const std::string& manifestUrl, const std::string& version) {
    static const std::string latest = "/releases/latest/download/";
    const size_t q = manifestUrl.find_first_of("?#");
    const size_t at = manifestUrl.find(latest);
    if (at == std::string::npos || (q != std::string::npos && at > q) || !validVersion(version)) return manifestUrl;
    return manifestUrl.substr(0, at) + "/releases/download/v" + version + "/" + manifestUrl.substr(at + latest.size());
}

std::string fileUrl(const std::string& manifestUrl, const Manifest& m, const FileEntry& f) {
    return resolveUrl(releaseManifestUrl(manifestUrl, m.version), f.url);
}

// ---- installs --------------------------------------------------------------------------------------------------------
const char* layoutName(Layout l) {
    switch (l) {
        case Layout::Windows: return "windows";
        case Layout::Mac: return "mac";
        case Layout::LinuxUser: return "linux";
        case Layout::Deb: return "deb";
        default: return "";
    }
}

Layout layoutFromName(const std::string& s) {
    if (s == "windows") return Layout::Windows;
    if (s == "mac") return Layout::Mac;
    if (s == "linux") return Layout::LinuxUser;
    if (s == "deb") return Layout::Deb;
    return Layout::None;
}

const char* zipTopFolder(Layout l) {
    switch (l) {
        case Layout::Windows: return "CrumpleZone";
        case Layout::Mac: return "CrumpleZone.app";
        case Layout::LinuxUser: return "crumplezone";
        default: return "";
    }
}

std::vector<std::string> keepAcrossUpdates(Layout l) {
    if (l == Layout::Windows) return {"Uninstall.exe"};
    return {};
}

namespace {
std::string parentOf(const std::string& p) {
    std::string s = p;
    while (s.size() > 1 && (s.back() == '/' || s.back() == '\\')) s.pop_back();
    const size_t cut = s.find_last_of("/\\");
    return cut == std::string::npos ? "." : cut == 0 ? s.substr(0, 1) : s.substr(0, cut);
}
std::string baseOf(const std::string& p) {
    std::string s = p;
    while (s.size() > 1 && (s.back() == '/' || s.back() == '\\')) s.pop_back();
    const size_t cut = s.find_last_of("/\\");
    return cut == std::string::npos ? s : s.substr(cut + 1);
}
// Can we make (and remove) a file in this folder?
bool writableDir(const std::string& dir) {
    const std::string probe = dir + "/.cz-write-test";
    std::FILE* f = std::fopen(probe.c_str(), "wb");
    if (!f) return false;
    std::fclose(f);
    std::remove(probe.c_str());
    return true;
}
std::string readSmall(const std::string& path) {
    std::ifstream f(path, std::ios::binary);
    if (!f) return "";
    std::string s((std::istreambuf_iterator<char>(f)), std::istreambuf_iterator<char>());
    return s.size() > 4096 ? s.substr(0, 4096) : s;
}
}  // namespace

Install installAt(Layout l, const std::string& live) {
    Install in;
    in.layout = l;
    in.live = live;
    const std::string parent = parentOf(live), name = baseOf(live);
    switch (l) {
        case Layout::Windows:
            in.staging = live + ".staging";
            in.previous = live + ".old";
            break;
        case Layout::Mac: {
            std::string stem = name;
            if (stem.size() > 4 && stem.compare(stem.size() - 4, 4, ".app") == 0) stem.resize(stem.size() - 4);
            in.staging = parent + "/." + stem + "-update";
            in.previous = parent + "/." + stem + "-previous.app";
            break;
        }
        case Layout::LinuxUser:
            in.staging = live + ".staging";
            in.previous = live + ".previous";
            break;
        default: break;
    }
    return in;
}

Install detectInstall(const std::string& exeDir) {
    Install none;
    if (exeDir.empty()) {
        none.why = "THE GAME CAN'T TELL WHERE IT IS INSTALLED";
        return none;
    }
#ifdef __APPLE__
    const std::string app = parentOf(parentOf(exeDir));
    const std::string marker = parentOf(exeDir) + "/Resources/" + kMarker;
#else
    const std::string app = exeDir;
    const std::string marker = exeDir + "/" + kMarker;
#endif
    const std::string text = readSmall(marker);
    std::istringstream ls(text);
    std::string key, name;
    ls >> key >> name;
    const Layout l = key == "layout" ? layoutFromName(name) : Layout::None;
    if (l == Layout::None) {
        none.why = "THIS IS A DEVELOPMENT BUILD: IT DOESN'T UPDATE ITSELF";
        return none;
    }
    Install in = installAt(l, app);
    if (l == Layout::Deb) return in;
    if (l == Layout::Mac) {
        if (app.find("/AppTranslocation/") != std::string::npos) {
            in.why = "MACOS IS RUNNING A QUARANTINED COPY OF THE GAME: MOVE CRUMPLEZONE.APP TO APPLICATIONS (OR ANY FOLDER) AND OPEN IT FROM THERE";
            return in;
        }
        if (app.compare(0, 9, "/Volumes/") == 0 && !writableDir(parentOf(app))) {
            in.why = "THE GAME IS RUNNING FROM THE DISK IMAGE: DRAG IT TO APPLICATIONS FIRST";
            return in;
        }
    }
    if (!writableDir(parentOf(app))) in.why = "THIS ACCOUNT CAN'T WRITE TO " + parentOf(app) + ", SO THE GAME CAN'T REPLACE ITSELF";
    return in;
}

// ---- the decision ----------------------------------------------------------------------------------------------------
Decision decide(const Manifest& m, const std::string& current, const Install& inst, const std::string& manifestUrl, int hostNet, int ourNet) {
    Decision d;
    const bool newer = compareVersions(m.version, current) > 0;
    const bool forHost = hostNet > 0 && hostNet > ourNet;
    if (!newer) {
        d.verdict = Verdict::UpToDate;
        d.message = forHost ? "YOUR FRIEND'S GAME IS NEWER THAN THE LATEST RELEASE ON THE UPDATE SERVER (" + m.version + "): THEY MAY BE ON A TEST BUILD"
                            : "YOU HAVE THE LATEST VERSION (" + current + ")";
        return d;
    }
    const std::string lead = (forHost ? "YOUR FRIEND'S GAME IS NEWER. " : "") + std::string("UPDATE ") + m.version;
    const std::string where = !m.notes.empty() ? m.notes : "THE GAME'S RELEASES PAGE";
    if (inst.layout == Layout::None) {
        d.verdict = Verdict::Manual;
        d.message = lead + " IS OUT. " + inst.why;
        return d;
    }
    if (inst.layout == Layout::Deb) {
        d.verdict = Verdict::Manual;
        d.file = m.file("deb");
        const std::string url = d.file ? fileUrl(manifestUrl, m, *d.file) : "";
        // (Never a swap here: /opt/crumplezone and /usr/games belong to the package manager and need sudo. The player picks one of two.)
        // (The card shows text in capitals, so no file names to type: COPY LINK copies the .deb's address.)
        d.message = lead + " AVAILABLE. THIS COPY WAS INSTALLED FROM THE .DEB, WHICH CAN'T UPDATE ITSELF. EITHER GET THE NEW .DEB (COPY LINK: " +
                    (url.empty() ? where : url) + ") AND OPEN IT WITH YOUR SOFTWARE INSTALLER, OR SWITCH TO THE PER-USER INSTALL, WHICH UPDATES " +
                    "ITSELF: GET THE LINUX .ZIP FROM " + where + ", UNZIP IT AND RUN INSTALL.SH (NO SUDO NEEDED)";
        return d;
    }
    const FileEntry* f = m.file(layoutName(inst.layout));
    if (!f || fileUrl(manifestUrl, m, *f).empty()) {
        d.verdict = Verdict::NotForThisOS;
        const char* os = inst.layout == Layout::Windows ? "WINDOWS" : inst.layout == Layout::Mac ? "MAC" : "LINUX";
        d.message = lead + " IS OUT, BUT NOT FOR " + os + " YET";
        return d;
    }
    if (!inst.why.empty()) {
        d.verdict = Verdict::Manual;
        d.message = lead + " AVAILABLE, BUT " + inst.why + ". OR DOWNLOAD IT FROM " + where;
        return d;
    }
    d.verdict = Verdict::SelfUpdate;
    d.file = f;
    char mb[32];
    std::snprintf(mb, sizeof mb, f->size < 10e6 ? "%.1f MB" : "%.0f MB", f->size / 1e6);
    d.message = lead + " AVAILABLE (" + mb + ")";
    return d;
}

// ---- zip -------------------------------------------------------------------------------------------------------------
namespace {
uint32_t crc32(const uint8_t* p, size_t n) {
    static uint32_t table[256];
    static bool init = false;
    if (!init) {
        for (uint32_t i = 0; i < 256; i++) {
            uint32_t c = i;
            for (int k = 0; k < 8; k++) c = c & 1 ? 0xEDB88320u ^ (c >> 1) : c >> 1;
            table[i] = c;
        }
        init = true;
    }
    uint32_t c = 0xFFFFFFFFu;
    for (size_t i = 0; i < n; i++) c = table[(c ^ p[i]) & 0xFF] ^ (c >> 8);
    return c ^ 0xFFFFFFFFu;
}
uint16_t le16(const uint8_t* p) { return (uint16_t)(p[0] | p[1] << 8); }
uint32_t le32(const uint8_t* p) { return (uint32_t)p[0] | (uint32_t)p[1] << 8 | (uint32_t)p[2] << 16 | (uint32_t)p[3] << 24; }

// "a/b/c" with no empty, "." or ".." parts, no backslash or drive letter. Directories end in '/' (removed by the caller).
bool safeRelPath(const std::string& p) {
    if (p.empty() || p[0] == '/' || p.find('\\') != std::string::npos || p.find(':') != std::string::npos) return false;
    size_t i = 0;
    while (i <= p.size()) {
        size_t j = p.find('/', i);
        if (j == std::string::npos) j = p.size();
        const std::string part = p.substr(i, j - i);
        if (part.empty() || part == "." || part == "..") return false;
        i = j + 1;
    }
    return true;
}
// A symlink at `entry` pointing to `target` stays inside the unpacked tree.
[[maybe_unused]] bool safeLink(const std::string& entry, const std::string& target) {
    if (target.empty() || target[0] == '/' || target.find('\\') != std::string::npos) return false;
    int depth = 0;
    for (char c : entry) depth += c == '/';  // (the folders above the link)
    size_t i = 0;
    while (i <= target.size()) {
        size_t j = target.find('/', i);
        if (j == std::string::npos) j = target.size();
        const std::string part = target.substr(i, j - i);
        if (part == "..") {
            if (--depth < 0) return false;
        } else if (!part.empty() && part != ".")
            depth++;
        i = j + 1;
    }
    return true;
}
}  // namespace

bool unzip(const std::string& zipPath, const std::string& dir, std::string& why) {
    std::ifstream f(zipPath, std::ios::binary);
    if (!f) {
        why = "CAN'T OPEN THE DOWNLOADED FILE";
        return false;
    }
    const std::string data((std::istreambuf_iterator<char>(f)), std::istreambuf_iterator<char>());
    const uint8_t* z = (const uint8_t*)data.data();
    const size_t n = data.size();
    // The end-of-central-directory record: the last "PK\5\6" within the final 64 KB + 22 bytes.
    size_t eocd = std::string::npos;
    for (size_t i = n >= 22 ? n - 22 : 0; n >= 22; i--) {
        if (le32(z + i) == 0x06054b50) {
            eocd = i;
            break;
        }
        if (i == 0 || n - i > 65557) break;
    }
    if (eocd == std::string::npos) {
        why = "THE DOWNLOADED FILE IS NOT A ZIP";
        return false;
    }
    const uint16_t count = le16(z + eocd + 10);
    const uint32_t cdSize = le32(z + eocd + 12), cdOff = le32(z + eocd + 16);
    if (count == 0xFFFF || cdOff == 0xFFFFFFFFu || (uint64_t)cdOff + cdSize > eocd) {
        why = "THE ZIP IS TOO BIG OR DAMAGED (ZIP64 IS NOT SUPPORTED)";
        return false;
    }
    std::error_code ec;
    if (fs::exists(dir, ec)) {
        why = "THE UNPACK FOLDER IS ALREADY THERE";
        return false;
    }
    fs::create_directories(dir, ec);
    if (ec) {
        why = "CAN'T MAKE THE FOLDER " + dir;
        return false;
    }
    size_t p = cdOff;
    for (int e = 0; e < count; e++) {
        if (p + 46 > eocd || le32(z + p) != 0x02014b50) {
            why = "THE ZIP'S DIRECTORY IS DAMAGED";
            return false;
        }
        const uint16_t madeBy = le16(z + p + 4), flags = le16(z + p + 8), method = le16(z + p + 10);
        const uint32_t crc = le32(z + p + 16), csize = le32(z + p + 20), usize = le32(z + p + 24);
        const uint16_t nameLen = le16(z + p + 28), extraLen = le16(z + p + 30), commentLen = le16(z + p + 32);
        const uint32_t attr = le32(z + p + 38), local = le32(z + p + 42);
        if (p + 46 + nameLen > eocd) {
            why = "THE ZIP'S DIRECTORY IS DAMAGED";
            return false;
        }
        std::string name((const char*)z + p + 46, nameLen);
        p += 46 + (size_t)nameLen + extraLen + commentLen;
        if (flags & 1) {
            why = "THE ZIP IS ENCRYPTED";
            return false;
        }
        const bool isDir = !name.empty() && name.back() == '/';
        if (isDir) name.pop_back();
        // (The Mac's resource-fork shadows: not part of the app.)
        const std::string base = name.substr(name.find_last_of('/') == std::string::npos ? 0 : name.find_last_of('/') + 1);
        if (name.compare(0, 9, "__MACOSX/") == 0 || name == "__MACOSX" || base.compare(0, 2, "._") == 0) continue;
        if (!safeRelPath(name)) {
            why = "THE ZIP HAS AN UNSAFE PATH: " + name.substr(0, 60);
            return false;
        }
        const std::string out = dir + "/" + name;
        const uint32_t mode = (madeBy >> 8) == 3 ? attr >> 16 : 0;  // (made on unix: the mode is in the high half)
        if (isDir || (mode & 0170000) == 0040000) {
            fs::create_directories(out, ec);
            continue;
        }
        if (local + 30ull > n || le32(z + local) != 0x04034b50) {
            why = "THE ZIP IS DAMAGED (" + name.substr(0, 40) + ")";
            return false;
        }
        const size_t start = (size_t)local + 30 + le16(z + local + 26) + le16(z + local + 28);
        if (start + (uint64_t)csize > n) {
            why = "THE ZIP IS CUT SHORT";
            return false;
        }
        std::string body(usize, '\0');
        if (method == 0) {
            if (csize != usize) {
                why = "THE ZIP IS DAMAGED (" + name.substr(0, 40) + ")";
                return false;
            }
            if (usize) std::memcpy(&body[0], z + start, usize);
        } else if (method == 8) {
            if (csize > 0x7fffffffu || usize > 0x7fffffffu) {
                why = "A FILE IN THE ZIP IS TOO BIG";
                return false;
            }
            const int got = usize ? stbi_zlib_decode_noheader_buffer(&body[0], (int)usize, (const char*)z + start, (int)csize) : 0;
            if (got != (int)usize) {
                why = "THE ZIP IS DAMAGED (" + name.substr(0, 40) + ")";
                return false;
            }
        } else {
            why = "THE ZIP USES A COMPRESSION THIS GAME CAN'T READ";
            return false;
        }
        if (crc32((const uint8_t*)body.data(), body.size()) != crc) {
            why = "THE ZIP IS DAMAGED (BAD CHECKSUM IN " + name.substr(0, 40) + ")";
            return false;
        }
        fs::create_directories(fs::path(out).parent_path(), ec);
        if ((mode & 0170000) == 0120000) {
#ifndef _WIN32
            if (!safeLink(name, body)) {
                why = "THE ZIP HAS A LINK POINTING OUT OF THE GAME: " + name.substr(0, 60);
                return false;
            }
            fs::create_symlink(body, out, ec);
            if (ec) {
                why = "CAN'T MAKE THE LINK " + name.substr(0, 60);
                return false;
            }
#endif
            continue;
        }
        {
            std::ofstream o(out, std::ios::binary | std::ios::trunc);
            o.write(body.data(), (std::streamsize)body.size());
            o.close();
            if (!o) {
                why = "CAN'T WRITE " + name.substr(0, 60) + " (IS THE DISK FULL?)";
                return false;
            }
        }
#ifndef _WIN32
        if (mode) fs::permissions(out, (mode & 0111) ? fs::perms(0755) : fs::perms(0644), fs::perm_options::replace, ec);
#endif
    }
    return true;
}

// ---- swapping --------------------------------------------------------------------------------------------------------
bool removeTree(const std::string& path) {
    std::error_code ec;
    fs::remove_all(path, ec);
    return !fs::exists(fs::symlink_status(path, ec));
}

bool swapIn(const std::string& live, const std::string& fresh, const std::string& previous, const std::vector<std::string>& keep, std::string& why) {
    std::error_code ec;
    if (!fs::is_directory(fresh, ec)) {
        why = "THE NEW VERSION ISN'T THERE (" + fresh + ")";
        return false;
    }
    if (fs::exists(fs::symlink_status(previous, ec)) && !removeTree(previous)) {
        why = "CAN'T REMOVE THE OLDER BACKUP " + previous;
        return false;
    }
    const bool hadLive = fs::exists(fs::symlink_status(live, ec));
    if (hadLive) {
        fs::rename(live, previous, ec);
        if (ec) {
            why = "CAN'T MOVE THE GAME'S FOLDER ASIDE (IS SOMETHING STILL USING IT?): " + ec.message();
            return false;
        }
    }
    fs::rename(fresh, live, ec);
    if (ec) {
        why = "CAN'T MOVE THE NEW VERSION INTO PLACE: " + ec.message();
        std::error_code ec2;
        if (hadLive) fs::rename(previous, live, ec2);
        return false;
    }
    for (const std::string& k : keep) {
        const std::string from = previous + "/" + k, to = live + "/" + k;
        if (fs::exists(from, ec) && !fs::exists(to, ec)) fs::copy_file(from, to, ec);
    }
    return true;
}

bool rollBack(const std::string& live, const std::string& previous, std::string& why) {
    std::error_code ec;
    if (!fs::is_directory(previous, ec)) {
        why = "THERE IS NO PREVIOUS VERSION TO GO BACK TO";
        return false;
    }
    const std::string aside = live + ".rollback";
    if (fs::exists(fs::symlink_status(aside, ec)) && !removeTree(aside)) {
        why = "CAN'T CLEAR " + aside;
        return false;
    }
    const bool hadLive = fs::exists(fs::symlink_status(live, ec));
    if (hadLive) {
        fs::rename(live, aside, ec);
        if (ec) {
            why = "CAN'T MOVE THE GAME'S FOLDER ASIDE: " + ec.message();
            return false;
        }
    }
    fs::rename(previous, live, ec);
    if (ec) {
        why = "CAN'T PUT THE PREVIOUS VERSION BACK: " + ec.message();
        std::error_code ec2;
        if (hadLive) fs::rename(aside, live, ec2);
        return false;
    }
    if (hadLive) fs::rename(aside, previous, ec);  // (the newer one becomes the backup)
    return true;
}

// ---- after a swap ----------------------------------------------------------------------------------------------------
std::string writeAfterSwap(const AfterSwap& s) {
    std::ostringstream o;
    if (!s.from.empty()) o << "from " << s.from << "\n";
    if (!s.to.empty()) o << "to " << s.to << "\n";
    o << "starts " << s.starts << "\n";
    if (!s.failed.empty()) {
        std::string f = s.failed;
        for (char& c : f)
            if (c == '\n' || c == '\r') c = ' ';
        o << "failed " << f << "\n";
    }
    if (s.rolledBack) o << "rolledback 1\n";
    if (!s.skip.empty()) o << "skip " << s.skip << "\n";
    return o.str();
}

AfterSwap readAfterSwap(const std::string& text) {
    AfterSwap s;
    std::istringstream in(text);
    std::string line;
    while (std::getline(in, line)) {
        if (!line.empty() && line.back() == '\r') line.pop_back();
        const size_t sp = line.find(' ');
        const std::string k = line.substr(0, sp), v = sp == std::string::npos ? "" : line.substr(sp + 1);
        if (k == "from") s.from = v;
        else if (k == "to") s.to = v;
        else if (k == "starts") s.starts = std::atoi(v.c_str());
        else if (k == "failed") s.failed = v;
        else if (k == "rolledback") s.rolledBack = v == "1";
        else if (k == "skip") s.skip = v;
    }
    return s;
}

StartAction onStart(const AfterSwap& s, const std::string& current) {
    if (!s.failed.empty()) return StartAction::ReportFailure;
    if (s.rolledBack) return StartAction::ReportRollBack;
    if (s.to.empty() || s.to != current) return StartAction::None;
    return s.starts >= 2 ? StartAction::RollBack : StartAction::Count;
}

}  // namespace update

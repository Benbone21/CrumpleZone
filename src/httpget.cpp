#include "httpget.h"

#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <thread>
#include <vector>

#ifdef _WIN32
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <winhttp.h>
#else
#include <dlfcn.h>
#include <fcntl.h>
#include <signal.h>
#include <spawn.h>
#include <sys/stat.h>
#include <sys/wait.h>
#include <unistd.h>
extern char** environ;
#endif

namespace fs = std::filesystem;

namespace http {
namespace {

[[maybe_unused]] const char* kAgent = "CrumpleZone-updater/1";

// A new, empty temporary file of our own (made exclusively, so nothing planted in a shared /tmp is followed); "" on failure.
std::string tempName(const char* tag) {
    std::error_code ec;
    fs::path dir = fs::temp_directory_path(ec);
    if (ec) dir = ".";
#ifdef _WIN32
    static std::atomic<int> counter{0};
    for (int i = 0; i < 100; i++) {
        char name[96];
        std::snprintf(name, sizeof name, "cz-%s-%lu-%d-%lu.tmp", tag, (unsigned long)GetCurrentProcessId(), counter++, (unsigned long)GetTickCount());
        const std::string path = (dir / name).string();
        HANDLE h = CreateFileA(path.c_str(), GENERIC_WRITE, 0, nullptr, CREATE_NEW, FILE_ATTRIBUTE_TEMPORARY, nullptr);
        if (h != INVALID_HANDLE_VALUE) {
            CloseHandle(h);
            return path;
        }
    }
    return "";
#else
    std::string templ = (dir / (std::string("cz-") + tag + "-XXXXXX")).string();
    const int fd = mkstemp(&templ[0]);
    if (fd < 0) return "";
    close(fd);
    return templ;
#endif
}

// Words for curl's error numbers (libcurl's CURLcode and the curl program's exit status are the same numbers).
[[maybe_unused]] std::string curlWords(int code, long httpStatus) {
    switch (code) {
        case 6: return "CAN'T FIND THE UPDATE SERVER (NO INTERNET, OR ITS NAME IS WRONG)";
        case 5: return "CAN'T FIND THE PROXY";
        case 7: return "CAN'T CONNECT TO THE UPDATE SERVER (NO INTERNET, OR THE SERVER IS DOWN)";
        case 22: {
            if (httpStatus == 404) return "THE UPDATE SERVER DOESN'T HAVE THAT FILE (404)";
            char b[80];
            if (httpStatus > 0) std::snprintf(b, sizeof b, "THE UPDATE SERVER REFUSED (HTTP %ld)", httpStatus);
            else std::snprintf(b, sizeof b, "THE UPDATE SERVER REFUSED (AN HTTP ERROR)");
            return b;
        }
        case 23: return "CAN'T WRITE THE DOWNLOAD (IS THE DISK FULL?)";
        case 28: return "THE UPDATE SERVER STOPPED ANSWERING (TIMED OUT)";
        case 35: return "THE SECURE CONNECTION FAILED";
        case 42: return "STOPPED";
        case 47: return "THE UPDATE SERVER REDIRECTS TOO MANY TIMES";
        case 51:
        case 60: return "THE UPDATE SERVER'S CERTIFICATE IS NOT VALID (OR THIS COMPUTER'S CLOCK IS WRONG)";
        case 18: return "THE DOWNLOAD WAS CUT SHORT";
        case 56: return "THE CONNECTION DROPPED IN THE MIDDLE";
        case 1: return "THE UPDATE ADDRESS USES A PROTOCOL THAT ISN'T ALLOWED";
        default: {
            char b[64];
            std::snprintf(b, sizeof b, "THE DOWNLOAD FAILED (CURL ERROR %d)", code);
            return b;
        }
    }
}

// Plain http only to this machine (the tests' server); everything else must be https.
bool loopbackHttp(const std::string& url) {
    for (const char* p : {"http://127.0.0.1:", "http://127.0.0.1/", "http://localhost:", "http://localhost/", "http://[::1]:", "http://[::1]/"})
        if (url.compare(0, std::strlen(p), p) == 0) return true;
    return false;
}
bool allowedScheme(const std::string& url) { return url.compare(0, 8, "https://") == 0 || loopbackHttp(url); }

#ifndef _WIN32
// ---- libcurl, loaded at run time ---------------------------------------------------------------------------------------
struct Curl {
    void* lib = nullptr;
    void* (*easy_init)() = nullptr;
    int (*easy_setopt)(void*, int, ...) = nullptr;
    int (*easy_perform)(void*) = nullptr;
    void (*easy_cleanup)(void*) = nullptr;
    int (*easy_getinfo)(void*, int, ...) = nullptr;
    int (*global_init)(long) = nullptr;
    bool ok = false;
};
// (The option numbers from curl.h: type base + number; they never change.)
enum {
    O_WRITEDATA = 10001, O_URL = 10002, O_TIMEOUT = 13, O_LOW_SPEED_LIMIT = 19, O_LOW_SPEED_TIME = 20, O_USERAGENT = 10018,
    O_NOPROGRESS = 43, O_FAILONERROR = 45, O_FOLLOWLOCATION = 52, O_MAXREDIRS = 68, O_CONNECTTIMEOUT = 78, O_NOSIGNAL = 99,
    O_PROTOCOLS = 181, O_REDIR_PROTOCOLS = 182, O_WRITEFUNCTION = 20011, O_XFERINFODATA = 10057, O_XFERINFOFUNCTION = 20219,
    I_RESPONSE_CODE = 0x200000 + 2, P_HTTP = 1, P_HTTPS = 2
};

Curl& curl() {
    static Curl c;
    static bool tried = false;
    if (tried) return c;
    tried = true;
#ifdef __APPLE__
    const char* names[] = {"/usr/lib/libcurl.4.dylib", "libcurl.4.dylib", "libcurl.dylib"};
#else
    const char* names[] = {"libcurl.so.4", "libcurl-gnutls.so.4", "libcurl.so.3"};
#endif
    for (const char* n : names)
        if ((c.lib = dlopen(n, RTLD_NOW | RTLD_LOCAL))) break;
    if (!c.lib) return c;
    c.easy_init = (void* (*)())dlsym(c.lib, "curl_easy_init");
    c.easy_setopt = (int (*)(void*, int, ...))dlsym(c.lib, "curl_easy_setopt");
    c.easy_perform = (int (*)(void*))dlsym(c.lib, "curl_easy_perform");
    c.easy_cleanup = (void (*)(void*))dlsym(c.lib, "curl_easy_cleanup");
    c.easy_getinfo = (int (*)(void*, int, ...))dlsym(c.lib, "curl_easy_getinfo");
    c.global_init = (int (*)(long))dlsym(c.lib, "curl_global_init");
    c.ok = c.easy_init && c.easy_setopt && c.easy_perform && c.easy_cleanup && c.easy_getinfo && c.global_init && c.global_init(3) == 0;
    return c;
}

struct Sink {
    std::FILE* f;
    Progress* p;
    bool writeFailed = false;
};
size_t onData(char* ptr, size_t size, size_t n, void* user) {
    Sink* s = (Sink*)user;
    if (s->p && s->p->cancel) return 0;
    const size_t k = size * n;
    if (std::fwrite(ptr, 1, k, s->f) != k) {
        s->writeFailed = true;
        return 0;
    }
    if (s->p) s->p->got += k;
    return k;
}
int onProgress(void* user, int64_t dltotal, int64_t, int64_t, int64_t) {
    Sink* s = (Sink*)user;
    if (s->p && dltotal > 0) s->p->total = (uint64_t)dltotal;
    return s->p && s->p->cancel ? 1 : 0;
}

bool libcurlGet(const std::string& url, std::FILE* f, Progress* p, std::string& why) {
    Curl& c = curl();
    void* h = c.easy_init();
    if (!h) {
        why = "THE DOWNLOAD COULDN'T START (LIBCURL)";
        return false;
    }
    Sink sink{f, p};
    const long protos = loopbackHttp(url) ? (P_HTTP | P_HTTPS) : P_HTTPS;
    c.easy_setopt(h, O_URL, url.c_str());
    c.easy_setopt(h, O_USERAGENT, kAgent);
    c.easy_setopt(h, O_FOLLOWLOCATION, 1L);
    c.easy_setopt(h, O_MAXREDIRS, 5L);
    c.easy_setopt(h, O_PROTOCOLS, protos);
    c.easy_setopt(h, O_REDIR_PROTOCOLS, protos);
    c.easy_setopt(h, O_FAILONERROR, 1L);
    c.easy_setopt(h, O_NOSIGNAL, 1L);
    c.easy_setopt(h, O_CONNECTTIMEOUT, 20L);
    c.easy_setopt(h, O_LOW_SPEED_LIMIT, 1L);  // (less than a byte a second for a minute: give up)
    c.easy_setopt(h, O_LOW_SPEED_TIME, 60L);
    c.easy_setopt(h, O_WRITEFUNCTION, &onData);
    c.easy_setopt(h, O_WRITEDATA, (void*)&sink);
    c.easy_setopt(h, O_XFERINFOFUNCTION, &onProgress);
    c.easy_setopt(h, O_XFERINFODATA, (void*)&sink);
    c.easy_setopt(h, O_NOPROGRESS, 0L);
    const int rc = c.easy_perform(h);
    long status = 0;
    c.easy_getinfo(h, I_RESPONSE_CODE, &status);
    c.easy_cleanup(h);
    if (rc == 0) return true;
    why = sink.writeFailed ? curlWords(23, 0) : (p && p->cancel) ? "STOPPED" : curlWords(rc, status);
    return false;
}

// ---- the curl program ------------------------------------------------------------------------------------------------
std::string curlTool() {
#ifdef __APPLE__
    if (access("/usr/bin/curl", X_OK) == 0) return "/usr/bin/curl";
#endif
    const char* path = std::getenv("PATH");
    std::string ps = path && *path ? path : "/usr/local/bin:/usr/bin:/bin";
    size_t i = 0;
    while (i <= ps.size()) {
        size_t j = ps.find(':', i);
        if (j == std::string::npos) j = ps.size();
        const std::string cand = (j > i ? ps.substr(i, j - i) : ".") + "/curl";
        if (access(cand.c_str(), X_OK) == 0) return cand;
        i = j + 1;
    }
    return "";
}

bool curlToolGet(const std::string& url, const std::string& path, Progress* p, std::string& why) {
    const std::string tool = curlTool();
    if (tool.empty()) {
        why = "NO WAY TO DOWNLOAD: INSTALL CURL (SUDO APT INSTALL CURL) OR LIBCURL";
        return false;
    }
    const std::string errPath = tempName("curlerr");
    if (errPath.empty()) {
        why = "CAN'T MAKE A TEMPORARY FILE";
        return false;
    }
    const std::string proto = loopbackHttp(url) ? "=http,https" : "=https";
    std::vector<std::string> args = {tool, "--fail", "--silent", "--show-error", "--location", "--max-redirs", "5", "--proto", proto,
                                     "--proto-redir", proto, "--connect-timeout", "20", "--speed-limit", "1", "--speed-time", "60",
                                     "--user-agent", kAgent, "--output", path, "--", url};
    std::vector<char*> argv;
    for (std::string& a : args) argv.push_back(&a[0]);
    argv.push_back(nullptr);
    posix_spawn_file_actions_t fa;
    posix_spawn_file_actions_init(&fa);
    posix_spawn_file_actions_addopen(&fa, 0, "/dev/null", O_RDONLY, 0);
    posix_spawn_file_actions_addopen(&fa, 1, "/dev/null", O_WRONLY, 0);
    posix_spawn_file_actions_addopen(&fa, 2, errPath.c_str(), O_WRONLY | O_CREAT | O_TRUNC, 0600);
    pid_t pid = 0;
    const int rc = posix_spawn(&pid, tool.c_str(), &fa, nullptr, argv.data(), environ);
    posix_spawn_file_actions_destroy(&fa);
    if (rc != 0) {
        std::remove(errPath.c_str());
        why = "CAN'T RUN CURL";
        return false;
    }
    int status = 0;
    for (;;) {
        const pid_t w = waitpid(pid, &status, WNOHANG);
        if (w == pid) break;
        if (w < 0) {
            status = -1;
            break;
        }
        if (p) {
            struct stat st;
            if (stat(path.c_str(), &st) == 0) p->got = (uint64_t)st.st_size;
            if (p->cancel) kill(pid, SIGTERM);
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(50));
    }
    std::string err;
    {
        std::ifstream e(errPath);
        std::getline(e, err);
    }
    std::remove(errPath.c_str());
    if (WIFEXITED(status) && WEXITSTATUS(status) == 0) {
        struct stat st;
        if (p && stat(path.c_str(), &st) == 0) p->got = (uint64_t)st.st_size;
        return true;
    }
    if (p && p->cancel) {
        why = "STOPPED";
        return false;
    }
    long httpStatus = 0;
    const size_t at = err.find("error: ");
    if (at != std::string::npos) httpStatus = std::atol(err.c_str() + at + 7);
    why = WIFEXITED(status) ? curlWords(WEXITSTATUS(status), httpStatus) : "CURL STOPPED UNEXPECTEDLY";
    return false;
}
#else
// ---- WinHTTP ---------------------------------------------------------------------------------------------------------
std::wstring widen(const std::string& s) {
    const int n = MultiByteToWideChar(CP_UTF8, 0, s.c_str(), -1, nullptr, 0);
    if (n <= 0) return L"";
    std::wstring w(n, L'\0');
    MultiByteToWideChar(CP_UTF8, 0, s.c_str(), -1, &w[0], n);
    w.resize(n - 1);
    return w;
}

std::string winWords(DWORD e, DWORD status) {
    if (status) {
        if (status == 404) return "THE UPDATE SERVER DOESN'T HAVE THAT FILE (404)";
        char b[64];
        std::snprintf(b, sizeof b, "THE UPDATE SERVER REFUSED (HTTP %lu)", (unsigned long)status);
        return b;
    }
    switch (e) {
        case ERROR_WINHTTP_NAME_NOT_RESOLVED: return "CAN'T FIND THE UPDATE SERVER (NO INTERNET, OR ITS NAME IS WRONG)";
        case ERROR_WINHTTP_CANNOT_CONNECT: return "CAN'T CONNECT TO THE UPDATE SERVER (NO INTERNET, OR THE SERVER IS DOWN)";
        case ERROR_WINHTTP_TIMEOUT: return "THE UPDATE SERVER STOPPED ANSWERING (TIMED OUT)";
        case ERROR_WINHTTP_SECURE_FAILURE:
        case ERROR_WINHTTP_SECURE_INVALID_CA:
        case ERROR_WINHTTP_SECURE_CERT_DATE_INVALID:
        case ERROR_WINHTTP_SECURE_CERT_CN_INVALID: return "THE UPDATE SERVER'S CERTIFICATE IS NOT VALID (OR THIS COMPUTER'S CLOCK IS WRONG)";
        case ERROR_WINHTTP_CONNECTION_ERROR: return "THE CONNECTION DROPPED IN THE MIDDLE";
        case ERROR_WINHTTP_REDIRECT_FAILED: return "THE UPDATE SERVER REDIRECTED SOMEWHERE NOT ALLOWED";
        default: {
            char b[64];
            std::snprintf(b, sizeof b, "THE DOWNLOAD FAILED (WINDOWS ERROR %lu)", (unsigned long)e);
            return b;
        }
    }
}

bool winhttpGet(const std::string& url, std::FILE* f, Progress* p, std::string& why) {
    const std::wstring w = widen(url);
    URL_COMPONENTS uc;
    std::memset(&uc, 0, sizeof uc);
    uc.dwStructSize = sizeof uc;
    static thread_local wchar_t host[256], path[4096], extra[8192];
    uc.lpszHostName = host, uc.dwHostNameLength = 256;
    uc.lpszUrlPath = path, uc.dwUrlPathLength = 4096;
    uc.lpszExtraInfo = extra, uc.dwExtraInfoLength = 8192;
    if (!WinHttpCrackUrl(w.c_str(), 0, 0, &uc)) {
        why = "THE UPDATE ADDRESS IS NOT VALID";
        return false;
    }
    const bool https = uc.nScheme == INTERNET_SCHEME_HTTPS;
    if (!https && !loopbackHttp(url)) {
        why = "THE UPDATE ADDRESS USES A PROTOCOL THAT ISN'T ALLOWED";
        return false;
    }
    std::wstring object = std::wstring(path, uc.dwUrlPathLength) + std::wstring(extra, uc.dwExtraInfoLength);
    if (object.empty()) object = L"/";
    HINTERNET s = WinHttpOpen(L"CrumpleZone-updater/1", WINHTTP_ACCESS_TYPE_DEFAULT_PROXY, WINHTTP_NO_PROXY_NAME, WINHTTP_NO_PROXY_BYPASS, 0);
    if (!s) {
        why = winWords(GetLastError(), 0);
        return false;
    }
    WinHttpSetTimeouts(s, 20000, 20000, 60000, 60000);
    HINTERNET c = WinHttpConnect(s, std::wstring(host, uc.dwHostNameLength).c_str(), uc.nPort, 0);
    HINTERNET r = c ? WinHttpOpenRequest(c, L"GET", object.c_str(), nullptr, WINHTTP_NO_REFERER, WINHTTP_DEFAULT_ACCEPT_TYPES, https ? WINHTTP_FLAG_SECURE : 0) : nullptr;
    if (r) {
        // Redirects (GitHub: .../releases/latest/download/x -> .../releases/download/vX/x -> a signed objects.githubusercontent.com
        // URL): followed by WinHTTP itself, at most 5, never from https down to http (the starting URL is https except the tests'
        // loopback server).
        DWORD policy = WINHTTP_OPTION_REDIRECT_POLICY_DISALLOW_HTTPS_TO_HTTP, most = 5;
        WinHttpSetOption(r, WINHTTP_OPTION_REDIRECT_POLICY, &policy, sizeof policy);
        WinHttpSetOption(r, WINHTTP_OPTION_MAX_HTTP_AUTOMATIC_REDIRECTS, &most, sizeof most);
    }
    bool ok = false;
    DWORD status = 0;
    DWORD err = 0;
    if (r && WinHttpSendRequest(r, WINHTTP_NO_ADDITIONAL_HEADERS, 0, WINHTTP_NO_REQUEST_DATA, 0, 0, 0) && WinHttpReceiveResponse(r, nullptr)) {
        DWORD sz = sizeof status;
        WinHttpQueryHeaders(r, WINHTTP_QUERY_STATUS_CODE | WINHTTP_QUERY_FLAG_NUMBER, WINHTTP_HEADER_NAME_BY_INDEX, &status, &sz, WINHTTP_NO_HEADER_INDEX);
        if (status == 200) {
            DWORD len = 0;
            sz = sizeof len;
            const bool knownLength =
                WinHttpQueryHeaders(r, WINHTTP_QUERY_CONTENT_LENGTH | WINHTTP_QUERY_FLAG_NUMBER, WINHTTP_HEADER_NAME_BY_INDEX, &len, &sz, WINHTTP_NO_HEADER_INDEX);
            if (p && knownLength) p->total = len;
            static thread_local char buf[1 << 16];
            uint64_t total = 0;
            ok = true;
            for (;;) {
                if (p && p->cancel) {
                    why = "STOPPED";
                    ok = false;
                    break;
                }
                DWORD got = 0;
                if (!WinHttpReadData(r, buf, sizeof buf, &got)) {
                    err = GetLastError();
                    why = winWords(err, 0);
                    ok = false;
                    break;
                }
                if (got == 0) {
                    if (knownLength && total != len) {  // (the connection closed early)
                        why = "THE DOWNLOAD WAS CUT SHORT";
                        ok = false;
                    }
                    break;
                }
                total += got;
                if (std::fwrite(buf, 1, got, f) != got) {
                    why = "CAN'T WRITE THE DOWNLOAD (IS THE DISK FULL?)";
                    ok = false;
                    break;
                }
                if (p) p->got += got;
            }
        } else {
            why = winWords(0, status);
        }
    } else {
        err = GetLastError();
        why = winWords(err, 0);
    }
    if (r) WinHttpCloseHandle(r);
    if (c) WinHttpCloseHandle(c);
    WinHttpCloseHandle(s);
    return ok;
}
#endif

}  // namespace

bool available(Backend b) {
    switch (b) {
#ifdef _WIN32
        case Backend::WinHttp: return true;
        case Backend::LibCurl:
        case Backend::CurlTool: return false;
#else
        case Backend::WinHttp: return false;
        case Backend::LibCurl: return curl().ok;
        case Backend::CurlTool: return !curlTool().empty();
#endif
        case Backend::Auto: return available(autoBackend());
    }
    return false;
}

Backend autoBackend() {
#ifdef _WIN32
    return Backend::WinHttp;
#else
    return curl().ok ? Backend::LibCurl : Backend::CurlTool;
#endif
}

const char* backendName(Backend b) {
    switch (b) {
        case Backend::WinHttp: return "WinHTTP";
        case Backend::LibCurl: return "libcurl";
        case Backend::CurlTool: return "curl";
        default: return backendName(autoBackend());
    }
}

bool download(const std::string& url, const std::string& path, Progress* progress, std::string& why, Backend b) {
    if (b == Backend::Auto) b = autoBackend();
    if (!allowedScheme(url)) {
        why = "THE UPDATE ADDRESS USES A PROTOCOL THAT ISN'T ALLOWED (IT MUST BE HTTPS)";
        return false;
    }
    if (!available(b)) {
        why = "NO WAY TO DOWNLOAD: INSTALL CURL (SUDO APT INSTALL CURL) OR LIBCURL";
        return false;
    }
    if (progress) progress->got = 0, progress->total = 0;
    bool ok = false;
#ifndef _WIN32
    if (b == Backend::CurlTool) {
        ok = curlToolGet(url, path, progress, why);
    } else
#endif
    {
        std::FILE* f = std::fopen(path.c_str(), "wb");
        if (!f) {
            why = "CAN'T WRITE THE DOWNLOAD TO " + path;
            return false;
        }
#ifdef _WIN32
        ok = winhttpGet(url, f, progress, why);
#else
        ok = libcurlGet(url, f, progress, why);
#endif
        if (std::fclose(f) != 0 && ok) {
            ok = false;
            why = "CAN'T WRITE THE DOWNLOAD (IS THE DISK FULL?)";
        }
    }
    if (!ok) std::remove(path.c_str());
    return ok;
}

bool fetch(const std::string& url, size_t maxBytes, std::string& body, std::string& why, Backend b) {
    body.clear();
    const std::string tmp = tempName("fetch");
    if (tmp.empty()) {
        why = "CAN'T MAKE A TEMPORARY FILE";
        return false;
    }
    if (!download(url, tmp, nullptr, why, b)) return false;
    std::ifstream f(tmp, std::ios::binary);
    std::string s((std::istreambuf_iterator<char>(f)), std::istreambuf_iterator<char>());
    f.close();
    std::remove(tmp.c_str());
    if (s.size() > maxBytes) {
        why = "THE UPDATE SERVER SENT TOO MUCH";
        return false;
    }
    body.swap(s);
    return true;
}

}  // namespace http

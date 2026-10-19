#include "platform.h"

#include <algorithm>
#include <cstdio>
#include <fstream>
#include <sstream>

#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#elif defined(__APPLE__)
#include <mach-o/dyld.h>
#include <climits>
#include <cstdlib>
#include <cstdint>
#else
#include <unistd.h>
#endif

#ifdef _WIN32
#include <io.h>
#endif

std::string exePath() {
#ifdef _WIN32
    char buf[4096];
    const DWORD n = GetModuleFileNameA(nullptr, buf, sizeof buf - 1);
    if (n == 0 || n >= sizeof buf - 1) return "";
    buf[n] = 0;
    return buf;
#elif defined(__APPLE__)
    char buf[4096];
    uint32_t size = sizeof buf;
    if (_NSGetExecutablePath(buf, &size) != 0) return "";
    char real[PATH_MAX];
    return realpath(buf, real) ? real : buf;  // (resolve the symlinks, as /proc/self/exe does)
#else
    char buf[4096];
    const ssize_t n = readlink("/proc/self/exe", buf, sizeof buf - 1);
    if (n <= 0) return "";
    buf[n] = 0;
    return buf;
#endif
}

std::string exeDir() {
    const std::string s = exePath();
    const size_t cut = s.find_last_of("/\\");
    return cut == std::string::npos ? "" : s.substr(0, cut);
}

long gAtomicFailAfter = -1;

namespace {

bool replaceFile(const std::string& from, const std::string& to) {
#ifdef _WIN32
    return MoveFileExA(from.c_str(), to.c_str(), MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH) != 0;
#else
    return std::rename(from.c_str(), to.c_str()) == 0;
#endif
}

// PATH.tmp written in full, flushed to the disk; false (temp removed) if any of it failed.
bool writeTemp(const std::string& tmp, const std::string& data) {
    FILE* f = std::fopen(tmp.c_str(), "wb");
    if (!f) return false;
    bool ok = true;
    const long failAt = gAtomicFailAfter;
    if (failAt >= 0) {
        gAtomicFailAfter = -1;
        std::fwrite(data.data(), 1, std::min<size_t>(data.size(), (size_t)failAt), f);
        ok = false;  // (the simulated crash: the temp is left half-written, as a power cut would leave it)
        std::fclose(f);
        return ok;
    }
    ok = std::fwrite(data.data(), 1, data.size(), f) == data.size();
    ok = std::fflush(f) == 0 && ok;
#ifdef _WIN32
    ok = _commit(_fileno(f)) == 0 && ok;
#else
    ok = fsync(fileno(f)) == 0 && ok;
#endif
    ok = std::fclose(f) == 0 && ok;
    if (!ok) std::remove(tmp.c_str());
    return ok;
}

}  // namespace

bool writeFileAtomic(const std::string& path, const std::string& data, std::string* error, bool keepBak, bool (*valid)(const std::string&)) {
    auto fail = [&](const std::string& why) {
        if (error) *error = why;
        return false;
    };
    const std::string tmp = path + ".tmp";
    if (keepBak) {  // (the old file, if it is a good one, becomes the backup before anything of the new one is in place)
        std::ifstream old(path, std::ios::binary);
        if (old) {
            std::stringstream ss;
            ss << old.rdbuf();
            const std::string was = ss.str();
            if (!was.empty() && (!valid || valid(was))) {
                const std::string btmp = path + ".bak.tmp";
                if (!writeTemp(btmp, was) || !replaceFile(btmp, path + ".bak")) {
                    std::remove(btmp.c_str());
                    return fail("CAN'T WRITE " + path + ".bak");
                }
            }
        }
    }
    if (!writeTemp(tmp, data)) return fail("CAN'T WRITE " + tmp);
    if (!replaceFile(tmp, path)) {
        std::remove(tmp.c_str());
        return fail("CAN'T REPLACE " + path);
    }
#ifndef _WIN32
    {  // (the rename itself, to the disk too)
        const size_t slash = path.find_last_of('/');
        const std::string dir = slash == std::string::npos ? "." : path.substr(0, slash == 0 ? 1 : slash);
        FILE* d = std::fopen(dir.c_str(), "rb");
        if (d) fsync(fileno(d)), std::fclose(d);
    }
#endif
    return true;
}

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
#include <unistd.h>  // (fsync for the atomic saves)
#else
#include <unistd.h>
#endif

#ifdef _WIN32
#include <io.h>
#else
#include <dirent.h>
#endif
#include <sys/stat.h>

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

std::vector<std::string> listDir(const std::string& dir) {
    std::vector<std::string> out;
#ifdef _WIN32
    WIN32_FIND_DATAA fd;
    HANDLE h = FindFirstFileA((dir + "/*").c_str(), &fd);
    if (h == INVALID_HANDLE_VALUE) return out;
    do {
        const std::string n = fd.cFileName;
        if (n != "." && n != "..") out.push_back(n);
    } while (FindNextFileA(h, &fd));
    FindClose(h);
#else
    DIR* d = opendir(dir.c_str());
    if (!d) return out;
    while (dirent* e = readdir(d)) {
        const std::string n = e->d_name;
        if (n != "." && n != "..") out.push_back(n);
    }
    closedir(d);
#endif
    return out;
}

bool isDir(const std::string& path) {
#ifdef _WIN32
    const DWORD a = GetFileAttributesA(path.c_str());
    return a != INVALID_FILE_ATTRIBUTES && (a & FILE_ATTRIBUTE_DIRECTORY) && !(a & FILE_ATTRIBUTE_REPARSE_POINT);
#else
    struct stat st;
    return lstat(path.c_str(), &st) == 0 && S_ISDIR(st.st_mode);
#endif
}

long long fileMTime(const std::string& path) {
    struct stat st;
    return stat(path.c_str(), &st) == 0 ? (long long)st.st_mtime : 0;
}

bool removeTree(const std::string& dir) {
    if (dir.empty() || dir == "/" || dir == ".") return false;
    if (isDir(dir)) {
        for (const std::string& n : listDir(dir)) removeTree(dir + "/" + n);
#ifdef _WIN32
        return RemoveDirectoryA(dir.c_str()) != 0;
#else
        return rmdir(dir.c_str()) == 0;
#endif
    }
#ifdef _WIN32
    SetFileAttributesA(dir.c_str(), FILE_ATTRIBUTE_NORMAL);
    return DeleteFileA(dir.c_str()) != 0 || RemoveDirectoryA(dir.c_str()) != 0;  // (a folder symlink is removed as a folder)
#else
    return std::remove(dir.c_str()) == 0;  // (a symlink is unlinked, never followed)
#endif
}

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

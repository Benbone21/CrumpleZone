#include "platform.h"

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

// The few things that differ between the Linux, Windows and macOS builds.
#pragma once
#include <cstdlib>
#include <string>
#include <vector>

#ifdef _WIN32
#include <direct.h>
#else
#include <sys/stat.h>
#endif

// Where settings and the garage live: %APPDATA%\crumplezone on Windows,
// $XDG_CONFIG_HOME (or ~/.config)/crumplezone elsewhere.
inline std::string configDir() {
#ifdef _WIN32
    const char* app = std::getenv("APPDATA");
    return std::string(app && *app ? app : ".") + "/crumplezone";
#else
    const char* xdg = std::getenv("XDG_CONFIG_HOME");
    const char* home = std::getenv("HOME");
    const std::string base = xdg && *xdg ? xdg : home && *home ? std::string(home) + "/.config" : ".";
    return base + "/crumplezone";
#endif
}

// Sets (value) or removes (nullptr) an environment variable; setenv/unsetenv are POSIX only.
inline void setEnvVar(const char* name, const char* value) {
#ifdef _WIN32
    _putenv_s(name, value ? value : "");  // (an empty value removes it)
#else
    if (value) setenv(name, value, 1);
    else unsetenv(name);
#endif
}

// Makes a folder (its parent must exist); fine if it already does.
inline void makeDir(const std::string& path) {
#ifdef _WIN32
    _mkdir(path.c_str());
#else
    mkdir(path.c_str(), 0755);
#endif
}

// The names (not paths) inside a folder, without "." and ".."; empty if it can't be read. (platform.cpp)
std::vector<std::string> listDir(const std::string& dir);
// Whether PATH is a folder (a symlink to one doesn't count), and a file's last-modified time in seconds (0 if it isn't there).
bool isDir(const std::string& path);
long long fileMTime(const std::string& path);
// Deletes a folder and everything in it (symlinks are removed, not followed); false if anything is left. (platform.cpp)
bool removeTree(const std::string& dir);

// The running executable's full path (symlinks resolved) and its folder; "" if unknown.
// (platform.cpp: /proc/self/exe, GetModuleFileName, _NSGetExecutablePath.)
std::string exePath();
std::string exeDir();

// Writes a file so a crash or power loss never leaves it half-written: the text goes to PATH.tmp (same folder), is flushed and
// fsync'd, then renamed over PATH (Windows: MoveFileEx REPLACE_EXISTING). With keepBak, the file it replaces is first copied to
// PATH.bak (one generation) if it is there and `valid` (when given) accepts it, so a bad old file never displaces a good backup.
// A failure leaves the old file untouched. (platform.cpp)
bool writeFileAtomic(const std::string& path, const std::string& data, std::string* error = nullptr, bool keepBak = false,
                     bool (*valid)(const std::string&) = nullptr);
// Tests only: if >= 0, the next atomic write stops (as a failed write would) after this many bytes of the temp file, then goes back to -1.
extern long gAtomicFailAfter;

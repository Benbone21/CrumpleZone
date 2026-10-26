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

// Hands memory the program has freed back to the system (glibc keeps it in its arenas, one per thread that allocated: a map
// built on a worker leaves hundreds of MB counted as ours after it is gone). A no-op where the allocator returns it itself.
void releaseFreedMemory();

// The running executable's full path (symlinks resolved) and its folder; "" if unknown.
// (platform.cpp: /proc/self/exe, GetModuleFileName, _NSGetExecutablePath.)
std::string exePath();
std::string exeDir();

// Finds an installed folder like "assets/audio/sfx": the first of these bases that has it wins -- the working folder (running from the
// repo), the exe's folder, exeDir()/../Resources (inside a macOS .app) and exeDir()/.. and ../.. (a build/ or tools/ binary). The .deb,
// the .app and the Windows shortcuts don't start the game in its own folder, so a cwd-relative "assets/..." misses there. Returns the
// folder (base + "/" + rel), or "" if none has it. The second form takes explicit bases (for the tests). (platform.cpp)
std::string findAssetDir(const std::string& rel);
std::string findAssetDir(const std::string& rel, const std::vector<std::string>& bases);

// Writes a file so a crash or power loss never leaves it half-written: the text goes to PATH.tmp (same folder), is flushed and
// fsync'd, then renamed over PATH (Windows: MoveFileEx REPLACE_EXISTING). With keepBak, the file it replaces is first copied to
// PATH.bak (one generation) if it is there and `valid` (when given) accepts it, so a bad old file never displaces a good backup.
// A failure leaves the old file untouched. (platform.cpp)
bool writeFileAtomic(const std::string& path, const std::string& data, std::string* error = nullptr, bool keepBak = false,
                     bool (*valid)(const std::string&) = nullptr);
// Tests only: if >= 0, the next atomic write stops (as a failed write would) after this many bytes of the temp file, then goes back to -1.
extern long gAtomicFailAfter;

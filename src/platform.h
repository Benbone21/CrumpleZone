// The few things that differ between the Linux, Windows and macOS builds.
#pragma once
#include <cstdlib>
#include <string>

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

// The running executable's full path (symlinks resolved) and its folder; "" if unknown.
// (platform.cpp: /proc/self/exe, GetModuleFileName, _NSGetExecutablePath.)
std::string exePath();
std::string exeDir();

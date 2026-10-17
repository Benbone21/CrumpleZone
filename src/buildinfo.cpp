#include "buildinfo.h"

#include <cctype>

#include "platform.h"

#ifndef CZ_GIT
#define CZ_GIT "unknown"
#endif

std::string buildDescription() {
    std::string exe = exePath();
    const size_t slash = exe.find_last_of("/\\");
    if (slash != std::string::npos) exe = exe.substr(slash + 1);
    const bool dev = exe.find("-dev") != std::string::npos;
    std::string s = std::string(CZ_GIT) + "  " + (dev ? "DEV BUILD" : "RELEASE") + "  BUILT " + __DATE__ + " " + __TIME__;
    for (char& c : s) c = (char)std::toupper((unsigned char)c);
    return s;
}

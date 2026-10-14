#include "gpu.h"
#include <string>

namespace {
std::string lower(const char* text) {
    std::string s = text ? text : "";
    for (char& c : s) if (c >= 'A' && c <= 'Z') c += 'a' - 'A';
    return s;
}
}

GpuVendor detectGpuVendor(const char* vendor, const char* renderer) {
    const std::string v = lower(vendor), r = lower(renderer);
    auto has = [&](const char* name) { return v.find(name) != std::string::npos || r.find(name) != std::string::npos; };
    // Software renderers can mention a CPU vendor without using that vendor's GPU.
    if (r.find("llvmpipe") != std::string::npos || r.find("softpipe") != std::string::npos)
        return GpuVendor::Mesa;
    if (has("nvidia") || has("geforce")) return GpuVendor::NVIDIA;
    if (has("amd") || has("radeon") || v.find("ati technologies") != std::string::npos) return GpuVendor::AMD;
    if (has("intel")) return GpuVendor::Intel;
    if (has("apple")) return GpuVendor::Apple;
    if (has("mesa")) return GpuVendor::Mesa;
    return GpuVendor::Unknown;
}

const char* gpuVendorName(GpuVendor vendor) {
    switch (vendor) {
        case GpuVendor::NVIDIA: return "NVIDIA";
        case GpuVendor::AMD: return "AMD";
        case GpuVendor::Apple: return "Apple";
        case GpuVendor::Intel: return "Intel";
        case GpuVendor::Mesa: return "Mesa";
        default: return "Unknown";
    }
}

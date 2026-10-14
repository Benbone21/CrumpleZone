// GPU names are diagnostics; rendering choices follow capabilities and user settings.
#pragma once

enum class GpuVendor { Unknown, NVIDIA, AMD, Apple, Intel, Mesa };

GpuVendor detectGpuVendor(const char* vendor, const char* renderer);
const char* gpuVendorName(GpuVendor vendor);

// Compressed textures, GL side: uploads texenc.h's encoded levels (BC1/BC3/BC5, or RGBA8 where S3TC
// is missing or CZ_NOTEXCOMP is set), sets filtering and anisotropy, and keeps the VRAM registry.
#pragma once
#include <string>
#include <vector>

#include "gl.h"
#include "texenc.h"

namespace texcomp {

// S3TC and its sRGB variants are exposed (needs a context; the first call asks, so make it on the
// main thread before encoding on others). CZ_NOTEXCOMP=1 says no, for comparisons.
bool s3tcAvailable();
// Encodes `rgbaLevels` (a full chain) for this machine: compressed where it can be, else RGBA8.
texenc::Encoded make(texenc::Kind kind, bool srgb, const std::vector<std::vector<uint8_t>>& rgbaLevels, int w, int h);

// Uploads one 2D texture / a layer array (all layers the same size and kind); returns the id. Mips,
// trilinear filtering, repeat wrap and anisotropy are set. `name` is the registry's key.
GLuint upload2D(const texenc::Encoded& e, const std::string& name, bool repeat = true);
GLuint uploadArray(const std::vector<texenc::Encoded>& layers, const std::string& name);
void release(GLuint tex, const std::string& name);

// The anisotropy ceiling, scaled by quality (1..3: 4x, 8x, 16x, clamped to the hardware); applies
// to every texture uploaded so far and the ones after.
void setQuality(int q);
float anisotropy();

// Notes a texture made elsewhere (render targets, the font) in the registry.
void note(const std::string& name, const char* format, int w, int h, int mips, size_t bytes, bool runtime);

}  // namespace texcomp

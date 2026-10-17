#include "texcomp.h"

#include <algorithm>
#include <cstdlib>
#include <cstring>

using namespace gl;

namespace texcomp {
namespace {
struct Held {
    GLuint tex;
    GLenum target;
};
std::vector<Held>& held() {
    static std::vector<Held> h;
    return h;
}
float aniso = 8;  // the ceiling asked for
bool hasExt(const char* want) {
    GLint count = 0;
    GetIntegerv(GL_NUM_EXTENSIONS, &count);
    for (GLint i = 0; i < count; i++) {
        const char* name = (const char*)GetStringi(GL_EXTENSIONS, (GLuint)i);
        if (name && std::strcmp(name, want) == 0) return true;
    }
    return false;
}
void applyAniso(GLenum target) {
    static const float hw = maxTextureAnisotropy();
    if (hw > 1) TexParameterf(target, GL_TEXTURE_MAX_ANISOTROPY, std::min(aniso, hw));
}
const char* formatName(const texenc::Encoded& e) {
    if (!e.compressed) return e.srgb ? "SRGB8_A8" : "RGBA8";
    switch (e.kind) {
        case texenc::Kind::Color: return e.srgb ? "BC1 sRGB" : "BC1";
        case texenc::Kind::Data: return "BC1";
        case texenc::Kind::ColorAlpha: return e.srgb ? "BC3 sRGB" : "BC3";
        case texenc::Kind::Normal2: return "BC5";
        case texenc::Kind::Packed4: return "BC3 packed";
        default: return "?";
    }
}
GLenum internalFormat(const texenc::Encoded& e) {
    if (!e.compressed) return e.srgb ? GL_SRGB8_ALPHA8 : GL_RGBA8;
    switch (e.kind) {
        case texenc::Kind::Color: return e.srgb ? GL_COMPRESSED_SRGB_S3TC_DXT1 : GL_COMPRESSED_RGB_S3TC_DXT1;
        case texenc::Kind::Data: return GL_COMPRESSED_RGB_S3TC_DXT1;
        case texenc::Kind::ColorAlpha: return e.srgb ? GL_COMPRESSED_SRGB_ALPHA_S3TC_DXT5 : GL_COMPRESSED_RGBA_S3TC_DXT5;
        case texenc::Kind::Normal2: return GL_COMPRESSED_RG_RGTC2;
        default: return GL_COMPRESSED_RGBA_S3TC_DXT5;  // Packed4
    }
}
void setup(GLenum target, const texenc::Encoded& e, bool repeat) {
    TexParameteri(target, GL_TEXTURE_MIN_FILTER, GL_LINEAR_MIPMAP_LINEAR);
    TexParameteri(target, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    const GLint wrap = repeat ? GL_REPEAT : GL_CLAMP_TO_EDGE;
    TexParameteri(target, GL_TEXTURE_WRAP_S, wrap);
    TexParameteri(target, GL_TEXTURE_WRAP_T, wrap);
    TexParameteri(target, GL_TEXTURE_MAX_LEVEL, (GLint)e.levels.size() - 1);
    if (e.swizzle) {  // Packed4 was stored (r,y,a,x): sampling gives back (x,y,r,a)
        TexParameteri(target, GL_TEXTURE_SWIZZLE_R, GL_ALPHA);
        TexParameteri(target, GL_TEXTURE_SWIZZLE_G, GL_GREEN);
        TexParameteri(target, GL_TEXTURE_SWIZZLE_B, GL_RED);
        TexParameteri(target, GL_TEXTURE_SWIZZLE_A, GL_BLUE);
    }
    applyAniso(target);
}
}  // namespace

bool s3tcAvailable() {
    static const bool ok = !std::getenv("CZ_NOTEXCOMP") && hasExt("GL_EXT_texture_compression_s3tc") &&
                           (hasExt("GL_EXT_texture_sRGB") || hasExt("GL_EXT_texture_compression_s3tc_srgb"));
    return ok;
}

texenc::Encoded make(texenc::Kind kind, bool srgb, const std::vector<std::vector<uint8_t>>& rgbaLevels, int w, int h) {
    static const bool off = std::getenv("CZ_NOTEXCOMP") != nullptr;
    return texenc::encode(off ? texenc::Kind::Raw : kind, srgb, rgbaLevels, w, h, s3tcAvailable());
}

void setQuality(int q) {
    aniso = q <= 1 ? 4.0f : q == 2 ? 8.0f : 16.0f;
    for (const Held& h : held()) {
        BindTexture(h.target, h.tex);
        applyAniso(h.target);
    }
}
float anisotropy() { return std::min(aniso, maxTextureAnisotropy()); }  // (the hardware limit's query is cached in applyAniso)

GLuint upload2D(const texenc::Encoded& e, const std::string& name, bool repeat) {
    GLuint t = 0;
    GenTextures(1, &t);
    BindTexture(GL_TEXTURE_2D, t);
    PixelStorei(GL_UNPACK_ALIGNMENT, 4);
    const GLenum fmt = internalFormat(e);
    int w = e.w, h = e.h;
    for (size_t l = 0; l < e.levels.size(); l++) {
        if (e.compressed) CompressedTexImage2D(GL_TEXTURE_2D, (GLint)l, fmt, w, h, 0, (GLsizei)e.levels[l].size(), e.levels[l].data());
        else TexImage2D(GL_TEXTURE_2D, (GLint)l, fmt, w, h, 0, GL_RGBA, GL_UNSIGNED_BYTE, e.levels[l].data());
        w = std::max(1, w / 2), h = std::max(1, h / 2);
    }
    setup(GL_TEXTURE_2D, e, repeat);
    held().push_back({t, GL_TEXTURE_2D});
    note(name, formatName(e), e.w, e.h, (int)e.levels.size(), e.bytes(), false);
    return t;
}

GLuint uploadArray(const std::vector<texenc::Encoded>& layers, const std::string& name) {
    const texenc::Encoded& e = layers[0];
    GLuint t = 0;
    GenTextures(1, &t);
    BindTexture(GL_TEXTURE_2D_ARRAY, t);
    PixelStorei(GL_UNPACK_ALIGNMENT, 4);
    const GLenum fmt = internalFormat(e);
    int w = e.w, h = e.h;
    std::vector<uint8_t> level;
    size_t bytes = 0;
    for (size_t l = 0; l < e.levels.size(); l++) {  // (a level's layers sit one after another)
        level.clear();
        for (const texenc::Encoded& L : layers) level.insert(level.end(), L.levels[l].begin(), L.levels[l].end());
        bytes += level.size();
        if (e.compressed) CompressedTexImage3D(GL_TEXTURE_2D_ARRAY, (GLint)l, fmt, w, h, (GLsizei)layers.size(), 0, (GLsizei)level.size(), level.data());
        else TexImage3D(GL_TEXTURE_2D_ARRAY, (GLint)l, fmt, w, h, (GLsizei)layers.size(), 0, GL_RGBA, GL_UNSIGNED_BYTE, level.data());
        w = std::max(1, w / 2), h = std::max(1, h / 2);
    }
    setup(GL_TEXTURE_2D_ARRAY, e, true);
    held().push_back({t, GL_TEXTURE_2D_ARRAY});
    note(name, formatName(e), e.w, e.h, (int)e.levels.size(), bytes, false);
    texenc::TexInfo ti = texenc::textureList().at(name);
    ti.layers = (int)layers.size();
    texenc::noteTexture(ti);
    return t;
}

void release(GLuint tex, const std::string& name) {
    if (tex) DeleteTextures(1, &tex);
    auto& h = held();
    h.erase(std::remove_if(h.begin(), h.end(), [&](const Held& x) { return x.tex == tex; }), h.end());
    texenc::forgetTexture(name);
}

void note(const std::string& name, const char* format, int w, int h, int mips, size_t bytes, bool runtime) {
    texenc::TexInfo t;
    t.name = name, t.format = format, t.w = w, t.h = h, t.mips = mips, t.bytes = bytes, t.runtime = runtime;
    texenc::noteTexture(t);
}

}  // namespace texcomp

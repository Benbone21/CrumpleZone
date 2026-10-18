// Texture compression, CPU side (no GL): BC1/BC3/BC4/BC5 block encoders (and decoders, for tests),
// mip chains, and the registry of every texture the game holds (format, size, mips, bytes) behind
// the VRAM report. The GL half is texcomp.h.
#pragma once
#include <cstddef>
#include <cstdint>
#include <map>
#include <string>
#include <vector>

namespace texenc {

// Bump when any encoder's output changes: the on-disk cache of encoded textures (meshcache.h) is keyed by it.
inline constexpr int kEncoderVersion = 2;  // (2: built with -ffp-contract=off)

// Block encoders. `rgba` is w*h*4 bytes; any size (edges are padded by clamping). Output is the
// blocks, row by row: BC1 and BC4 8 bytes a block, BC3 and BC5 16.
void encodeBC1(const uint8_t* rgba, int w, int h, std::vector<uint8_t>& out);               // RGB (alpha ignored)
void encodeBC3(const uint8_t* rgba, int w, int h, std::vector<uint8_t>& out);               // RGB as BC1, then A as BC4
void encodeBC4(const uint8_t* rgba, int w, int h, int channel, std::vector<uint8_t>& out);  // one channel
void encodeBC5(const uint8_t* rgba, int w, int h, int chA, int chB, std::vector<uint8_t>& out);
// What a GPU makes of the blocks, back to RGBA (BC4: the channel in R, G=B=0, A=255; BC5: R and G).
void decodeBC1(const uint8_t* blocks, int w, int h, std::vector<uint8_t>& rgba);
void decodeBC3(const uint8_t* blocks, int w, int h, std::vector<uint8_t>& rgba);
void decodeBC4(const uint8_t* blocks, int w, int h, std::vector<uint8_t>& rgba);
void decodeBC5(const uint8_t* blocks, int w, int h, std::vector<uint8_t>& rgba);
inline size_t blockBytes(int w, int h, int perBlock) { return (size_t)((w + 3) / 4) * ((h + 3) / 4) * perBlock; }

// A full mip chain (level 0 first) of an RGBA image, box filtered. sRGB: colour averaged in linear
// light (alpha always linear). Every level down to 1x1.
int mipCount(int w, int h);
std::vector<std::vector<uint8_t>> mipChain(const uint8_t* rgba, int w, int h, bool srgb);

// What a texture holds (for the encoder's choice and the fall-back).
enum class Kind {
    Color,       // opaque colour (sRGB or not): BC1
    ColorAlpha,  // colour with a cut-out or height in A: BC3
    Data,        // opaque RGB that is not colour (roughness, occlusion): BC1, linear
    Normal2,     // a normal in R,G (B recomputed by the shader): BC5
    Packed4,     // four independent channels (normal xy, roughness, occlusion): BC3, channels swizzled
    Raw,         // exact: never compressed
};

// One texture's levels, ready for the GL half to upload.
struct Encoded {
    int w = 0, h = 0;
    Kind kind = Kind::Raw;
    bool srgb = false, compressed = false, swizzle = false;  // swizzle: the Packed4 undo (see texcomp.cpp)
    std::vector<std::vector<uint8_t>> levels;                // RGBA8 when !compressed, else blocks
    size_t bytes() const {
        size_t n = 0;
        for (const auto& l : levels) n += l.size();
        return n;
    }
};
// Compression allowed: false uses RGBA8 for everything (the extension is missing, or CZ_NOTEXCOMP).
Encoded encode(Kind kind, bool srgb, const std::vector<std::vector<uint8_t>>& levels, int w, int h, bool allowS3tc);

// The registry: every texture the game holds, by name (a name registered again replaces it).
struct TexInfo {
    std::string name, format;
    int w = 0, h = 0, layers = 1, mips = 1;
    size_t bytes = 0;
    bool runtime = false;  // made at run time (render targets, bakes) rather than loaded from a file
};
void noteTexture(const TexInfo& t);
void forgetTexture(const std::string& name);
const std::map<std::string, TexInfo>& textureList();
size_t totalTextureBytes();
// Textures with mips below the full chain which asked for filtering (mips == 0 marks a texture with
// none on purpose: data and render targets), by name.
std::vector<std::string> textureReport(std::string* table = nullptr);

}  // namespace texenc

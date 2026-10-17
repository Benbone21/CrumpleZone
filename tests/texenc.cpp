// Texture compression (texenc.h), no GL: the block encoders round-trip within an error bound on
// photo-like content, flat areas and edges, the fall-back leaves RGBA8 alone, and every mip chain
// goes all the way to 1x1 (the registry flags a texture that stops short).
#include <cmath>
#include <cstdint>
#include <vector>

#include "../src/texenc.h"
#include "harness.h"

namespace {
using namespace texenc;

// Something like a scanned surface: low-frequency colour, grain, a hard edge, a clear flat patch.
std::vector<uint8_t> sample(int w, int h, uint32_t seed = 5) {
    std::vector<uint8_t> px((size_t)w * h * 4);
    uint32_t s = seed;
    auto rnd = [&] { return (s = s * 1664525u + 1013904223u) >> 24; };
    for (int y = 0; y < h; y++)
        for (int x = 0; x < w; x++) {
            uint8_t* p = &px[((size_t)y * w + x) * 4];
            const float u = (float)x / w, v = (float)y / h;
            float r = 120 + 70 * std::sin(u * 9 + v * 3), g = 100 + 60 * std::sin(u * 5 - v * 7 + 1), b = 70 + 40 * std::sin(v * 11);
            const float grain = (float)((int)rnd() - 128) * 0.12f;
            if (x > w / 2 && y < h / 3) r = 230, g = 200, b = 40;  // an edge and a flat patch
            p[0] = (uint8_t)std::fmax(0, std::fmin(255, r + grain)), p[1] = (uint8_t)std::fmax(0, std::fmin(255, g + grain)), p[2] = (uint8_t)std::fmax(0, std::fmin(255, b + grain));
            p[3] = (uint8_t)(128 + 120 * std::sin(u * 6) * std::cos(v * 4));
        }
    return px;
}
double psnr(const std::vector<uint8_t>& a, const std::vector<uint8_t>& b, int ch) {
    double se = 0;
    long n = 0;
    for (size_t i = 0; i < a.size(); i += 4)
        for (int k = 0; k < 4; k++)
            if (ch & (1 << k)) se += (double)(a[i + k] - b[i + k]) * (a[i + k] - b[i + k]), n++;
    return 10 * std::log10(255.0 * 255.0 / std::fmax(1e-9, se / n));
}
}  // namespace

SCENARIO(texencRoundTrip, "texenc/round-trip", "BC1, BC3, BC4 and BC5 reproduce photo-like content within a PSNR bound, and flat blocks stay close") {
    for (int size : {64, 37}) {  // (37: edge blocks are padded)
        const std::vector<uint8_t> img = sample(size, size);
        std::vector<uint8_t> blocks, back;
        encodeBC1(img.data(), size, size, blocks);
        ctx.check("BC1 size is 8 bytes a block", blocks.size() == blockBytes(size, size, 8));
        decodeBC1(blocks.data(), size, size, back);
        const double p1 = psnr(img, back, 7);
        ctx.metric("bc1 rgb psnr " + std::to_string(size), p1, "dB", 28, 80);
        encodeBC3(img.data(), size, size, blocks);
        decodeBC3(blocks.data(), size, size, back);
        ctx.check("BC3 colour above 28 dB", psnr(img, back, 7) > 28);
        ctx.check("BC3 alpha above 40 dB", psnr(img, back, 8) > 40);
        encodeBC4(img.data(), size, size, 3, blocks);
        decodeBC4(blocks.data(), size, size, back);
        std::vector<uint8_t> a = img;
        for (size_t i = 0; i < a.size(); i += 4) a[i] = a[i + 3], back[i + 3] = a[i + 3];
        ctx.check("BC4 one channel above 40 dB", psnr(a, back, 1) > 40);
        encodeBC5(img.data(), size, size, 0, 1, blocks);
        decodeBC5(blocks.data(), size, size, back);
        ctx.check("BC5 two channels above 40 dB", psnr(img, back, 3) > 40);
    }
    std::vector<uint8_t> flat(16 * 16 * 4, 255), blocks, back;
    for (size_t i = 0; i < flat.size(); i += 4) flat[i] = 201, flat[i + 1] = 77, flat[i + 2] = 130;
    encodeBC1(flat.data(), 16, 16, blocks);
    decodeBC1(blocks.data(), 16, 16, back);
    int worst = 0;
    for (size_t i = 0; i < flat.size(); i += 4)
        for (int k = 0; k < 3; k++) worst = std::max(worst, std::abs(flat[i + k] - back[i + k]));
    ctx.check("a flat block is off by at most 8 levels (565 steps)", worst <= 8);
}

SCENARIO(texencFallback, "texenc/fallback", "without the S3TC extension colour stays RGBA8; Raw never compresses; normals use core BC5") {
    const std::vector<uint8_t> img = sample(32, 32);
    const auto chain = mipChain(img.data(), 32, 32, true);
    const Encoded off = encode(Kind::Color, true, chain, 32, 32, false);
    ctx.check("colour falls back to RGBA8", !off.compressed && off.levels.size() == chain.size() && off.levels[0] == chain[0]);
    ctx.check("packed4 falls back, unswizzled", !encode(Kind::Packed4, false, chain, 32, 32, false).swizzle);
    ctx.check("raw stays raw even when allowed", !encode(Kind::Raw, false, chain, 32, 32, true).compressed);
    const Encoded on = encode(Kind::Color, true, chain, 32, 32, true);
    ctx.check("colour compresses to about an eighth of RGBA8", on.compressed && on.bytes() * 6 <= off.bytes());
    ctx.check("BC5 normals need no extension", encode(Kind::Normal2, false, chain, 32, 32, false).compressed);
    ctx.check("BC1 is 8 times smaller on a block-aligned chain", on.levels[0].size() * 8 == chain[0].size());
}

SCENARIO(texencMips, "texenc/mips", "a mip chain runs to 1x1; sRGB averaging is done in linear light; the registry flags a short chain") {
    for (auto [w, h] : {std::pair<int, int>{1024, 1024}, {256, 64}, {37, 5}, {1, 1}}) {
        const std::vector<uint8_t> img((size_t)w * h * 4, 90);
        const auto c = mipChain(img.data(), w, h, false);
        ctx.check("full chain " + std::to_string(w) + "x" + std::to_string(h), (int)c.size() == mipCount(w, h) && c.back().size() == 4);
    }
    std::vector<uint8_t> bw(2 * 2 * 4, 0);
    for (int i : {0, 1, 2, 3, 4, 5, 6}) bw[i] = 255;  // the top row white, the bottom black
    bw[3] = bw[7] = bw[11] = bw[15] = 255;
    const auto lin = mipChain(bw.data(), 2, 2, false), srgb = mipChain(bw.data(), 2, 2, true);
    ctx.check("linear average of black and white is mid grey", lin[1][0] >= 127 && lin[1][0] <= 129);
    ctx.check("sRGB average is brighter (average light, then encode)", srgb[1][0] > 180 && srgb[1][0] < 200);
    for (const auto& t : std::vector<std::string>{"t_short", "t_full", "t_none"}) forgetTexture(t);
    noteTexture({"t_short", "BC1", 256, 256, 1, 4, 100, false});
    noteTexture({"t_full", "BC1", 256, 256, 1, mipCount(256, 256), 100, false});
    noteTexture({"t_none", "RGBA8", 256, 256, 1, 0, 100, true});
    const auto bad = textureReport();
    ctx.check("only the short chain is flagged", bad.size() == 1 && bad[0] == "t_short");
    for (const auto& t : std::vector<std::string>{"t_short", "t_full", "t_none"}) forgetTexture(t);
}

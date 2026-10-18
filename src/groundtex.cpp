#include "groundtex.h"
#include "meshcache.h"
#include "platform.h"
#include "texcomp.h"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <thread>
#include <vector>

#ifdef _WIN32
#include <windows.h>
#else
#include <unistd.h>
#endif

// stb_image, as static functions (so another file may include its own copy).
#define STB_IMAGE_STATIC
#define STB_IMAGE_IMPLEMENTATION
#define STBI_ONLY_JPEG
#define STBI_ONLY_PNG
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wunused-function"
#include "third_party/stb_image.h"
#pragma GCC diagnostic pop

using namespace gl;

namespace {
// Poly Haven asset ids, in the shader's layer order. (Their names are not always what they show:
// forrest_ground_01 and rocky_terrain_02 are two grasses, gravel_ground_01 a pale dusty ground.)
const char* const kGroundLayers[] = {
    "brown_mud_03",         // 0 wet mud
    "brown_mud_dry",        // 1 dry dirt, trails
    "brown_mud_leaves_01",  // 2 mud with sprouts
    "forest_leaves_04",     // 3 leaf and needle litter
    "forrest_ground_01",    // 4 sparse grass and twigs
    "gravel_ground_01",     // 5 pale dusty ground (sand, graded)
    "rocky_terrain_02",     // 6 rich dark grass
    "rocky_trail",          // 7 stones and gravel
    "sandy_gravel_02",      // 8 orange hardpan
    "snow_02",              // 9 snow
};
const int kLayerCount = (int)(sizeof(kGroundLayers) / sizeof(kGroundLayers[0]));

bool fileExists(const std::string& p) {
    FILE* f = std::fopen(p.c_str(), "rb");
    if (!f) return false;
    std::fclose(f);
    return true;
}


struct Decoded {
    std::vector<uint8_t> albedo, normal;  // RGBA, size x size
    texenc::Encoded encAlbedo, encNormal;  // the same, as mip chains in their upload format
    int size = 0;
    bool ok = false;
    std::string err;
};

uint8_t* loadJpg(const std::string& path, int channels, int& w, int& h) {
    int c = 0;
    return stbi_load(path.c_str(), &w, &h, &c, channels);
}

// Halves an RGBA image (box filter).
void halve(std::vector<uint8_t>& img, int& size) {
    const int n = size / 2;
    std::vector<uint8_t> out((size_t)n * n * 4);
    for (int y = 0; y < n; y++)
        for (int x = 0; x < n; x++)
            for (int c = 0; c < 4; c++) {
                const int s = img[((size_t)(2 * y) * size + 2 * x) * 4 + c] + img[((size_t)(2 * y) * size + 2 * x + 1) * 4 + c] +
                              img[((size_t)(2 * y + 1) * size + 2 * x) * 4 + c] + img[((size_t)(2 * y + 1) * size + 2 * x + 1) * 4 + c];
                out[((size_t)y * n + x) * 4 + c] = (uint8_t)((s + 2) / 4);
            }
    img.swap(out);
    size = n;
}

void decodeLayer(const std::string& dir, const char* id, int maxSize, int expect, Decoded& out) {
    const std::string base = dir + "/" + id + "_";
    int w[4] = {}, h[4] = {};
    uint8_t* diff = loadJpg(base + "diff_1k.jpg", 3, w[0], h[0]);
    uint8_t* nor = loadJpg(base + "nor_gl_1k.jpg", 3, w[1], h[1]);
    uint8_t* arm = loadJpg(base + "arm_1k.jpg", 3, w[2], h[2]);
    uint8_t* disp = loadJpg(base + "disp_1k.jpg", 1, w[3], h[3]);
    const int size = w[0];
    auto fail = [&](const std::string& why) {
        out.err = std::string(id) + ": " + why;
        stbi_image_free(diff), stbi_image_free(nor), stbi_image_free(arm), stbi_image_free(disp);
    };
    if (!diff || !nor || !arm || !disp) return fail("missing or unreadable file");
    for (int k = 0; k < 4; k++)
        if (w[k] != size || h[k] != size || size != expect) return fail("unexpected image size");
    const size_t n = (size_t)size * size;
    // Height: stretched so the 2nd..98th percentile spans 0..1 (each set's own range differs a lot).
    int hist[256] = {};
    for (size_t i = 0; i < n; i++) hist[disp[i]]++;
    int lo = 0, hi = 255, acc = 0;
    for (int v = 0; v < 256; v++) {
        acc += hist[v];
        if (acc >= (int)(n * 0.02)) { lo = v; break; }
    }
    acc = 0;
    for (int v = 255; v >= 0; v--) {
        acc += hist[v];
        if (acc >= (int)(n * 0.02)) { hi = v; break; }
    }
    const float span = (float)std::max(hi - lo, 8);
    out.albedo.resize(n * 4);
    out.normal.resize(n * 4);
    for (size_t i = 0; i < n; i++) {
        out.albedo[i * 4] = diff[i * 3], out.albedo[i * 4 + 1] = diff[i * 3 + 1], out.albedo[i * 4 + 2] = diff[i * 3 + 2];
        out.albedo[i * 4 + 3] = (uint8_t)std::lround(std::min(std::max(((float)disp[i] - (float)lo) / span, 0.0f), 1.0f) * 255.0f);
        out.normal[i * 4] = nor[i * 3], out.normal[i * 4 + 1] = nor[i * 3 + 1];
        out.normal[i * 4 + 2] = arm[i * 3 + 1];  // roughness
        out.normal[i * 4 + 3] = arm[i * 3];      // ambient occlusion
    }
    stbi_image_free(diff), stbi_image_free(nor), stbi_image_free(arm), stbi_image_free(disp);
    int sz = size;
    while (sz > maxSize) {
        int s2 = sz;
        halve(out.albedo, sz);
        halve(out.normal, s2);
    }
    out.size = sz;
    out.ok = true;
}

// Mips and (where the machine takes it) block compression, on the layer's own thread. The albedo
// array is colour with the height in A (BC3); the normal array packs normal xy, roughness and
// occlusion (BC3, swizzled back on sampling).
void encodeLayer(Decoded& d) {
    d.encAlbedo = texcomp::make(texenc::Kind::ColorAlpha, true, texenc::mipChain(d.albedo.data(), d.size, d.size, true), d.size, d.size);
    d.encNormal = texcomp::make(texenc::Kind::Packed4, false, texenc::mipChain(d.normal.data(), d.size, d.size, false), d.size, d.size);
    std::vector<uint8_t>().swap(d.albedo);
    std::vector<uint8_t>().swap(d.normal);
}
}  // namespace

std::string findGroundDir() {
    std::vector<std::string> roots = {"."};
    const std::string exe = exeDir();
    if (!exe.empty()) roots.insert(roots.end(), {exe, exe + "/../Resources", exe + "/..", exe + "/../.."});  // (../Resources: inside a macOS .app)
    for (const std::string& r : roots) {
        const std::string d = r + "/assets/ground";
        if (fileExists(d + "/" + kGroundLayers[0] + "_diff_1k.jpg")) return d;
    }
    return "";
}

void freeGroundTextures(GroundTextures& gt) {
    if (gt.albedo) texcomp::release(gt.albedo, "ground albedo");
    if (gt.normal) texcomp::release(gt.normal, "ground normal");
    gt = GroundTextures();
}

bool loadGroundTextures(GroundTextures& gt, int maxSize, std::string& err) {
    maxSize = maxSize >= 1024 ? 1024 : 512;
    if (gt.ok() && gt.size == maxSize) return true;
    const std::string dir = findGroundDir();
    if (dir.empty()) {
        err = "assets/ground not found";
        return false;
    }
    // Decode every layer on its own thread (JPEG decoding is most of the start-up cost), then mip
    // and compress it there too.
    const auto t0 = std::chrono::steady_clock::now();
    texcomp::s3tcAvailable();  // (asks GL: here, not on the threads)
    std::vector<Decoded> dec(kLayerCount);
    std::vector<std::thread> pool;
    for (int l = 0; l < kLayerCount; l++)
        pool.emplace_back([&, l] {
            meshcache::Hasher key;
            key.u64((uint64_t)texenc::kEncoderVersion), key.u64((uint64_t)maxSize), key.u64(texcomp::s3tcAvailable()), key.str(kGroundLayers[l]);
            for (const char* part : {"diff", "nor_gl", "arm", "disp"})
                key.u64(meshcache::fileStamp(dir + "/" + kGroundLayers[l] + "_" + part + "_1k.jpg"));
            std::vector<uint8_t> blob;
            if (meshcache::getBlob(key.h, blob)) {  // (kept on disk: the albedo's blocks, then the normal's)
                uint64_t n;
                std::memcpy(&n, blob.data(), blob.size() >= 8 ? 8 : 0);
                if (blob.size() >= 8 && n <= blob.size() - 8 &&
                    meshcache::unpackEncoded(std::vector<uint8_t>(blob.begin() + 8, blob.begin() + 8 + n), dec[l].encAlbedo) &&
                    meshcache::unpackEncoded(std::vector<uint8_t>(blob.begin() + 8 + n, blob.end()), dec[l].encNormal)) {
                    dec[l].size = dec[l].encAlbedo.w, dec[l].ok = true;
                    return;
                }
            }
            decodeLayer(dir, kGroundLayers[l], maxSize, 1024, dec[l]);
            if (dec[l].ok) {
                encodeLayer(dec[l]);
                const std::vector<uint8_t> a = meshcache::packEncoded(dec[l].encAlbedo), b = meshcache::packEncoded(dec[l].encNormal);
                std::vector<uint8_t> both;
                const uint64_t n = a.size();
                both.insert(both.end(), (const uint8_t*)&n, (const uint8_t*)&n + 8);
                both.insert(both.end(), a.begin(), a.end()), both.insert(both.end(), b.begin(), b.end());
                meshcache::putBlob(key.h, both);
            }
        });
    for (std::thread& t : pool) t.join();
    for (const Decoded& d : dec)
        if (!d.ok) {
            err = d.err;
            return false;
        }
    const int size = maxSize;
    freeGroundTextures(gt);
    std::vector<texenc::Encoded> ea, en;
    for (Decoded& d : dec) ea.push_back(std::move(d.encAlbedo)), en.push_back(std::move(d.encNormal));
    GLuint tex[2];
    tex[0] = texcomp::uploadArray(ea, "ground albedo");
    tex[1] = texcomp::uploadArray(en, "ground normal");
    BindTexture(GL_TEXTURE_2D_ARRAY, 0);
    if (std::getenv("CZ_TEXSTATS"))
        std::fprintf(stderr, "texstats: ground textures %d, %.0f ms (decode, mips, encode, upload)\n", size,
                     std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - t0).count());
    gt.albedo = tex[0], gt.normal = tex[1], gt.size = size;
    return true;
}

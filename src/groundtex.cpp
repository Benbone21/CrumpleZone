#include "groundtex.h"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstdio>
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

std::string exeDir() {
    char buf[4096];
#ifdef _WIN32
    const DWORD n = GetModuleFileNameA(nullptr, buf, sizeof buf - 1);
    if (n == 0 || n >= sizeof buf - 1) return "";
    buf[n] = 0;
#else
    const ssize_t n = readlink("/proc/self/exe", buf, sizeof buf - 1);
    if (n <= 0) return "";
    buf[n] = 0;
#endif
    std::string s = buf;
    const size_t cut = s.find_last_of("/\\");
    return cut == std::string::npos ? "" : s.substr(0, cut);
}

struct Decoded {
    std::vector<uint8_t> albedo, normal;  // RGBA, size x size
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
    out.ok = true;
}
}  // namespace

std::string findGroundDir() {
    std::vector<std::string> roots = {"."};
    const std::string exe = exeDir();
    if (!exe.empty()) roots.insert(roots.end(), {exe, exe + "/..", exe + "/../.."});
    for (const std::string& r : roots) {
        const std::string d = r + "/assets/ground";
        if (fileExists(d + "/" + kGroundLayers[0] + "_diff_1k.jpg")) return d;
    }
    return "";
}

void freeGroundTextures(GroundTextures& gt) {
    if (gt.albedo) DeleteTextures(1, &gt.albedo);
    if (gt.normal) DeleteTextures(1, &gt.normal);
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
    // Decode every layer on its own thread (JPEG decoding is most of the start-up cost).
    std::vector<Decoded> dec(kLayerCount);
    std::vector<std::thread> pool;
    for (int l = 0; l < kLayerCount; l++) pool.emplace_back([&, l] { decodeLayer(dir, kGroundLayers[l], maxSize, 1024, dec[l]); });
    for (std::thread& t : pool) t.join();
    for (const Decoded& d : dec)
        if (!d.ok) {
            err = d.err;
            return false;
        }
    const int size = maxSize;
    freeGroundTextures(gt);
    GLuint tex[2];
    GenTextures(2, tex);
    float aniso = 1;
    GetFloatv(GL_MAX_TEXTURE_MAX_ANISOTROPY, &aniso);
    PixelStorei(GL_UNPACK_ALIGNMENT, 4);
    for (int k = 0; k < 2; k++) {
        BindTexture(GL_TEXTURE_2D_ARRAY, tex[k]);
        TexImage3D(GL_TEXTURE_2D_ARRAY, 0, k == 0 ? GL_SRGB8_ALPHA8 : GL_RGBA8, size, size, kLayerCount, 0, GL_RGBA, GL_UNSIGNED_BYTE, nullptr);
        for (int l = 0; l < kLayerCount; l++)
            TexSubImage3D(GL_TEXTURE_2D_ARRAY, 0, 0, 0, l, size, size, 1, GL_RGBA, GL_UNSIGNED_BYTE,
                          (k == 0 ? dec[l].albedo : dec[l].normal).data());
        GenerateMipmap(GL_TEXTURE_2D_ARRAY);
        TexParameteri(GL_TEXTURE_2D_ARRAY, GL_TEXTURE_MIN_FILTER, GL_LINEAR_MIPMAP_LINEAR);
        TexParameteri(GL_TEXTURE_2D_ARRAY, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
        TexParameteri(GL_TEXTURE_2D_ARRAY, GL_TEXTURE_WRAP_S, GL_REPEAT);
        TexParameteri(GL_TEXTURE_2D_ARRAY, GL_TEXTURE_WRAP_T, GL_REPEAT);
        if (aniso > 1) TexParameterf(GL_TEXTURE_2D_ARRAY, GL_TEXTURE_MAX_ANISOTROPY, std::min(aniso, 16.0f));
    }
    BindTexture(GL_TEXTURE_2D_ARRAY, 0);
    gt.albedo = tex[0], gt.normal = tex[1], gt.size = size;
    return true;
}

// The real-terrain importer's tool (src/realterrain.h, docs/real-terrain.md).
//
//   build/realterrain info FILE                    what a DEM is: format, size, tiles, compression, where (no pixels read)
//   build/realterrain sample FILE LAT LON [...]    the elevation at points (degrees; a GeoTIFF or .hgt in degrees)
//   build/realterrain make SIDECAR|DIR [--out DIR] [--preview FILE.ppm] [--no-world] [--scans]
//        prepares the map, prints its plan (lakes, rivers, places, roads and trails), builds the world headless (its land hash,
//        what's on it, how long) and draws it from above; --out writes the map's folder for maps/: the cropped DEM (dem.tif,
//        float32, LZW) and its terrain.txt, so the game needn't carry the whole source tile.
//   build/realterrain synth cone|valley|mesa DIR   writes a synthetic DEM and its terrain.txt (for trying the importer)
#include <chrono>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <functional>
#include <fstream>
#include <sstream>
#include <string>
#include <vector>

#include "../src/loadprof.h"
#include "../src/props.h"
#include "../src/realterrain.h"
#include "../src/world.h"
#include "../src/worldcache.h"

namespace rt = realterrain;

namespace {
int usage() {
    std::fprintf(stderr,
                 "usage: realterrain info FILE\n"
                 "       realterrain sample FILE LAT LON [LAT LON ...]\n"
                 "       realterrain make SIDECAR|DIR [--out DIR] [--preview FILE.ppm] [--no-world] [--scans]\n"
                 "       realterrain synth cone|valley|mesa DIR\n");
    return 2;
}

int info(const std::string& path) {
    std::string err;
    const std::string e = path.size() > 4 ? path.substr(path.size() - 4) : "";
    if (e == ".tif" || e == "tiff") {
        rt::TiffInfo in;
        if (!rt::tiffInfo(path, in, &err)) return std::fprintf(stderr, "%s\n", err.c_str()), 1;
        std::printf("GeoTIFF%s, %s-endian, %d x %d, %d-bit %s, %d sample(s)\n", in.bigTiff ? " (BigTIFF)" : "", in.bigEndian ? "big" : "little", in.w, in.h, in.bits,
                    in.sampleFormat == 3 ? "float" : in.sampleFormat == 2 ? "int" : "uint", in.samples);
        std::printf("  %s %d x %d, compression %d (%s), predictor %d\n", in.tiled ? "tiles" : "strips of rows", in.tiled ? in.tileW : in.w,
                    in.tiled ? in.tileH : in.rowsPerStrip, in.compression,
                    in.compression == 1 ? "none" : in.compression == 5 ? "LZW" : in.compression == 8 || in.compression == 32946 ? "DEFLATE" : "UNSUPPORTED", in.predictor);
        std::printf("  model type %d, EPSG %d, %s\n", in.modelType, in.epsg, in.pixelIsPoint ? "pixel is point" : "pixel is area");
        if (in.geo == rt::Dem::GEO_DEGREES)
            std::printf("  degrees: west %.6f north %.6f, pixel %.9f x %.9f deg (%.2f x %.2f m at the middle)\n", in.x0, in.y0, in.dx, in.dy,
                        in.dx * rt::metresPerDegLon(in.y0 - in.h * in.dy / 2), in.dy * rt::metresPerDegLat(in.y0 - in.h * in.dy / 2));
        else if (in.geo == rt::Dem::GEO_METRES) std::printf("  metres: x0 %.2f y0 %.2f, pixel %.3f x %.3f m\n", in.x0, in.y0, in.dx, in.dy);
        else std::printf("  no geo tags (give metres_per_pixel)\n");
        if (in.hasNoData) std::printf("  no-data value %g\n", in.noData);
        return 0;
    }
    rt::Dem d;
    if (!rt::loadDem(path, d, &err)) return std::fprintf(stderr, "%s\n", err.c_str()), 1;
    float lo = 1e30f, hi = -1e30f;
    for (float v : d.v) lo = std::min(lo, v), hi = std::max(hi, v);
    std::printf("%s, %d x %d, values %.2f .. %.2f\n", d.format.c_str(), d.w, d.h, lo, hi);
    if (d.geo == rt::Dem::GEO_DEGREES) std::printf("  degrees: west %.6f north %.6f, pixel %.9f deg\n", d.x0, d.y0, d.dx);
    return 0;
}

int sample(const std::string& path, int argc, char** argv) {
    rt::Dem head;
    std::string err;
    if (!rt::demHeader(path, head, &err)) return std::fprintf(stderr, "%s\n", err.c_str()), 1;
    if (head.geo != rt::Dem::GEO_DEGREES) return std::fprintf(stderr, "not in degrees\n"), 1;
    for (int i = 0; i + 1 < argc; i += 2) {
        const double lat = std::atof(argv[i]), lon = std::atof(argv[i + 1]);
        const double px = (lon - head.x0) / head.dx - 0.5, py = (head.y0 - lat) / head.dy - 0.5;
        const int x0 = (int)std::floor(px), y0 = (int)std::floor(py);
        rt::Dem d;
        if (!rt::loadDem(path, d, &err, rt::Window{x0, y0, x0 + 2, y0 + 2}) || d.w < 2 || d.h < 2) {
            std::printf("%.5f %.5f: off the file (%s)\n", lat, lon, err.c_str());
            continue;
        }
        const double tx = px - x0, ty = py - y0;
        const double v = (d.at(0, 0) * (1 - tx) + d.at(1, 0) * tx) * (1 - ty) + (d.at(0, 1) * (1 - tx) + d.at(1, 1) * tx) * ty;
        std::printf("%.5f %.5f: %.2f m  (pixel %d, %d: %.2f)\n", lat, lon, v, x0, y0, d.at(0, 0));
    }
    return 0;
}

// The map from above: the ground's colour, shaded from the north-west, the water, the roads, the trees and the places.
void preview(const World& w, const rt::Terrain& t, const std::string& path) {
    const int n = w.gridSize(), step = std::max(1, (n + 1023) / 1024), W = (n + step - 1) / step;
    std::vector<unsigned char> img((size_t)W * W * 3);
    const double half = w.halfSize();
    for (int py = 0; py < W; py++)
        for (int px = 0; px < W; px++) {
            const double x = -half + px * step, z = half - py * step;  // (north up)
            const double hx = w.height(x + step, z) - w.height(x - step, z), hz = w.height(x, z + step) - w.height(x, z - step);
            V3 nn(-hx, 2.0 * step, -hz);
            nn = norm(nn);
            const double shade = clampd(0.45 + 0.75 * dot(nn, norm(V3(-0.6, 0.8, 0.5))), 0.15, 1.15);
            const int id = w.cellIndex(x, z);
            double r = 0.36, g = 0.5, b = 0.26;
            switch (w.cellPaint(id)) {
                case PAINT_ROCK: r = 0.52, g = 0.5, b = 0.47; break;
                case PAINT_REDROCK: r = 0.72, g = 0.42, b = 0.28; break;
                case PAINT_DESERT: r = 0.74, g = 0.56, b = 0.4; break;
                case PAINT_SAND: r = 0.86, g = 0.72, b = 0.5; break;
                case PAINT_SCREE: r = 0.6, g = 0.58, b = 0.54; break;
                case PAINT_SNOW: r = 0.95, g = 0.96, b = 0.98; break;
                case PAINT_MUD: r = 0.35, g = 0.28, b = 0.2; break;
                case PAINT_GRAVEL: r = 0.66, g = 0.62, b = 0.55; break;
                case PAINT_TRAIL: r = 0.8, g = 0.68, b = 0.48; break;
                default: break;
            }
            r *= shade, g *= shade, b *= shade;
            const double wl = w.waterLevel(x, z);
            if (wl > -1e8) {
                const double depth = clampd((wl - w.height(x, z)) / 2.0, 0, 1);
                r = 0.15 + 0.1 * (1 - depth), g = 0.35 + 0.15 * (1 - depth), b = 0.6 + 0.1 * (1 - depth);
            }
            if (w.roadAt(x, z).near && (w.cellPaint(id) == PAINT_TRAIL || w.cellPaint(id) == PAINT_GRAVEL)) r = 0.95, g = 0.88, b = 0.6;
            unsigned char* p = &img[((size_t)py * W + px) * 3];
            p[0] = (unsigned char)clampd(r * 255, 0, 255), p[1] = (unsigned char)clampd(g * 255, 0, 255), p[2] = (unsigned char)clampd(b * 255, 0, 255);
        }
    auto dot2 = [&](double x, double z, int rad, unsigned char R, unsigned char G, unsigned char B) {
        const int cx = (int)((x + half) / step), cy = (int)((half - z) / step);
        for (int a = -rad; a <= rad; a++)
            for (int b2 = -rad; b2 <= rad; b2++) {
                const int qx = cx + a, qy = cy + b2;
                if (qx < 0 || qy < 0 || qx >= W || qy >= W) continue;
                unsigned char* p = &img[((size_t)qy * W + qx) * 3];
                p[0] = R, p[1] = G, p[2] = B;
            }
    };
    for (const Tree& tr : w.trees) dot2(tr.base.x, tr.base.z, 0, 30, 70, 30);
    for (const MapPlace& pl : t.plan().places) dot2(pl.p.x, pl.p.z, 3, pl.kind == PK_RANGER_STATION ? 255 : 220, 40, pl.kind == PK_LOOKOUT || pl.kind == PK_SUMMIT ? 200 : 40);
    std::ofstream f(path, std::ios::binary);
    f << "P6\n" << W << " " << W << "\n255\n";
    f.write((const char*)img.data(), (std::streamsize)img.size());
}

int make(int argc, char** argv) {
    std::string path, out, prev;
    bool world = true, scans = false;
    for (int i = 0; i < argc; i++) {
        const std::string a = argv[i];
        if (a == "--out" && i + 1 < argc) out = argv[++i];
        else if (a == "--preview" && i + 1 < argc) prev = argv[++i];
        else if (a == "--no-world") world = false;
        else if (a == "--scans") scans = true;
        else path = a;
    }
    if (path.empty()) return usage();
    rt::Spec s;
    std::string err;
    const auto t0 = std::chrono::steady_clock::now();
    if (!rt::loadSpec(path, s, &err)) return std::fprintf(stderr, "%s\n", err.c_str()), 1;
    auto t = rt::prepare(s, &err);
    if (!t) return std::fprintf(stderr, "%s\n", err.c_str()), 1;
    float lo = 1e30f, hi = -1e30f;
    for (float v : t->dem.v) lo = std::min(lo, v), hi = std::max(hi, v);
    std::printf("%s: %s (%s), hash %016llx\n", s.name.c_str(), s.sourcePath.c_str(), t->dem.format.c_str(), (unsigned long long)s.hash);
    std::printf("  map %.0f x %.0f m; DEM crop %d x %d px at %.2f x %.2f m; elevation %.1f .. %.1f m (map y 0 = %.1f m)\n", 2 * t->half, 2 * t->half, t->dem.w, t->dem.h,
                t->mppX, t->mppY, t->base + lo / s.exaggeration, t->base + hi / s.exaggeration, t->base);
    const auto& p = t->plan();
    int wet = 0, dry = 0;
    for (const auto& r : p.rivers) (r.wet ? wet : dry)++;
    std::printf("  plan: %d x %d cells of %.0f m; %d lakes, %d river stretches, %d dry washes, %d roads/trails\n", p.n, p.n, p.cs, (int)p.lakes.size(), wet, dry,
                (int)p.trails.size());
    for (size_t k = 0; k < p.places.size(); k++)
        std::printf("    %-16s %-28s (%6.0f, %6.0f) %.0f m\n", placeKindName(p.places[k].kind), p.places[k].name.c_str(), p.places[k].p.x, p.places[k].p.z,
                    t->realElevation(p.padY[k]));
    for (const auto& tr : p.trails) {
        double L = 0;
        for (size_t k = 1; k < tr.pts.size(); k++) L += std::hypot(tr.pts[k].x - tr.pts[k - 1].x, tr.pts[k].z - tr.pts[k - 1].z);
        std::printf("    %s %.1f km (<= %.0f%%) %s -> %s\n", tr.road ? "road " : "trail", L / 1000, tr.maxGrade * 100,
                    tr.from >= 0 ? p.places[tr.from].name.c_str() : "(the network)", tr.to >= 0 ? p.places[tr.to].name.c_str() : "(the network)");
    }
    std::printf("  prepared in %.2f s\n", std::chrono::duration<double>(std::chrono::steady_clock::now() - t0).count());
    if (!out.empty()) {
        namespace fs = std::filesystem;
        std::error_code ec;
        fs::create_directories(out, ec);
        rt::Dem raw;
        if (!rt::loadDem(s.sourcePath, raw, &err, t->window)) return std::fprintf(stderr, "%s\n", err.c_str()), 1;
        rt::TiffOptions o;
        o.compression = 5, o.predictor = 3, o.tiled = true, o.tile = 256;
        if (!rt::writeGeoTiff(out + "/dem.tif", raw, o)) return std::fprintf(stderr, "can't write %s/dem.tif\n", out.c_str()), 1;
        std::istringstream in(s.text);
        std::string line, text;
        bool named = false;
        while (std::getline(in, line)) {
            std::istringstream ls(line);
            std::string key;
            ls >> key;
            for (char& c : key) c = (char)std::tolower((unsigned char)c);
            if (key == "source" || key == "file" || key == "dem") line = "source dem.tif";
            if (key == "crop" || key == "crop_px") line = "# " + line + "  (cropped already)";
            named |= key == "name";
            text += line + "\n";
        }
        if (!named) text = "name " + s.name + "\n" + text;
        std::ofstream(out + "/terrain.txt") << text;
        std::printf("  wrote %s/dem.tif (%d x %d) and %s/terrain.txt\n", out.c_str(), raw.w, raw.h, out.c_str());
    }
    if (!world) return 0;
    rt::ScopedTerrain scope(t);
    props::ScopedRockScans none(scans ? props::activeRockScans() : nullptr);
    worldcache::bypass = true;
    const auto t1 = std::chrono::steady_clock::now();
    World w(MAP_REAL);
    std::printf("  world: %d x %d cells, %zu trees, %zu rocks, %zu bushes, %zu rivers, %zu stations; built in %.2f s; land hash %016llx\n", w.gridSize(), w.gridSize(),
                w.trees.size(), w.rocks.size(), w.bushes.size(), w.rivers.size(), w.stations.size(),
                std::chrono::duration<double>(std::chrono::steady_clock::now() - t1).count(), (unsigned long long)w.landHash());
    std::printf("  start (%.0f, %.0f)\n", w.spawn.x, w.spawn.z);
    loadprof::report("real");
    if (!prev.empty()) {
        preview(w, *t, prev);
        std::printf("  preview: %s\n", prev.c_str());
    }
    return 0;
}

// Synthetic DEMs, as metres-projected float32 GeoTIFFs, 10 m pixels.
int synth(const std::string& kind, const std::string& dir) {
    namespace fs = std::filesystem;
    std::error_code ec;
    fs::create_directories(dir, ec);
    const int px = 10;
    int side = 0;
    std::string sidecar;
    std::function<double(double, double)> f;
    if (kind == "cone") {
        side = 151;
        f = [](double x, double z) {
            const double r = std::sqrt(x * x + z * z);
            return 1000 + (r < 450 ? 300 * (1 - (r / 450) * (r / 450)) : 0) + 0.002 * x;
        };
        sidecar = "name CONE\nsize 1200\nsnow_line 1260\nclimate alpine\n";
    } else if (kind == "valley") {
        side = 331;
        f = [](double x, double z) {
            const double zc = 260 * std::sin(x / 420), d = std::fabs(z - zc);
            const double cross = d < 110 ? 0.01 * d : std::min(140.0, 1.1 + (d - 110) * 0.2);
            const double hills = 25 * std::sin(x / 230 + 1) * std::cos(z / 270) * std::min(1.0, d / 400);
            return 600 + 0.02 * (x + 1600) + cross + hills;
        };
        sidecar = "name GREEN VALLEY\nsize 3000\nriver_area 0.8\nwash_area 0.3\nsnow_line 805\n";
    } else if (kind == "mesa") {
        side = 331;
        f = [](double x, double z) {
            // Canyon country: a river in a canyon, benches, mesas with cliffs, slickrock domes.
            const double zc = 300 * std::sin(x / 500 + 0.5), d = std::fabs(z - zc);
            const double canyon = d < 60 ? 0 : d < 120 ? (d - 60) * 1.4 : 84 + std::min(40.0, (d - 120) * 0.08);
            const double mesa = 60 / (1 + std::exp(-(0.5 - (std::hypot(x - 700, z + 800) / 450)) * 18)) + 50 / (1 + std::exp(-(0.5 - (std::hypot(x + 900, z - 900) / 380)) * 18));
            const double domes = 14 * std::max(0.0, std::sin(x / 70) * std::cos(z / 60)) * (d > 160 ? 1 : 0);
            return 1200 + 0.01 * x + canyon + mesa + domes;
        };
        sidecar = "name SLICKROCK\nsize 3000\nclimate desert\nledges 1.2 0.8\nriver_area 0.3\nwash_area 0.3\n";
    } else
        return usage();
    rt::Dem d;
    d.w = d.h = side;
    d.v.resize((size_t)side * side);
    d.geo = rt::Dem::GEO_METRES, d.dx = d.dy = px, d.x0 = 600000 - 0.5 * side * px, d.y0 = 4250000 + 0.5 * side * px;
    for (int r = 0; r < side; r++)
        for (int c = 0; c < side; c++) d.v[(size_t)r * side + c] = (float)f((c - 0.5 * (side - 1)) * px, (0.5 * (side - 1) - r) * px);
    rt::TiffOptions o;
    o.compression = 5, o.predictor = 3, o.tiled = true, o.tile = 128;
    if (!rt::writeGeoTiff(dir + "/dem.tif", d, o)) return std::fprintf(stderr, "can't write %s\n", dir.c_str()), 1;
    std::ofstream(dir + "/terrain.txt") << "# A synthetic DEM (build/realterrain synth " << kind << ")\nsource dem.tif\n" << sidecar;
    std::printf("wrote %s/dem.tif (%d x %d at %d m) and %s/terrain.txt\n", dir.c_str(), side, side, px, dir.c_str());
    return 0;
}
}  // namespace

int main(int argc, char** argv) {
    if (argc < 3) return usage();
    const std::string cmd = argv[1];
    if (cmd == "info") return info(argv[2]);
    if (cmd == "sample") return sample(argv[2], argc - 3, argv + 3);
    if (cmd == "make") return make(argc - 2, argv + 2);
    if (cmd == "synth" && argc >= 4) return synth(argv[2], argv[3]);
    return usage();
}

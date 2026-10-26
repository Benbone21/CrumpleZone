// Real-world terrain (src/realterrain.h, docs/real-terrain.md): the DEM formats read back exactly what was written (.hgt, 16-bit
// PNG, GeoTIFF stripped and tiled, uncompressed, LZW and DEFLATE, predictors 1-3, either byte order), and synthetic DEMs written
// here become maps: a cone mountain (its height, rock on the steep, snow over the line, a summit on top), a valley (a river down
// its floor, outposts on flat ground, roads and trails within their grade), a hollow and a shore (a lake, the sea), and the same
// file always builds the same land (and another file other land). A guest needs the host's file (the welcome carries its hash).
#include <unistd.h>

#include <atomic>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <string>
#include <thread>

#include "../src/net.h"
#include "../src/places.h"
#include "../src/props.h"
#include "../src/realterrain.h"
#include "../src/worldcache.h"
#include "harness.h"

namespace rt = realterrain;
namespace fs = std::filesystem;

namespace {
// A folder of its own for each test (left behind: under the system's temp folder).
std::string freshDir(const std::string& tag) {
    static std::atomic<int> counter{0};
    const auto now = std::chrono::steady_clock::now().time_since_epoch().count();
    const fs::path d = fs::temp_directory_path() / ("cz-realterrain-" + tag + "-" + std::to_string((long long)getpid()) + "-" + std::to_string(counter++) + "-" +
                                                     std::to_string((long long)(now % 1000000)));
    std::error_code ec;
    fs::create_directories(d, ec);
    return d.string();
}
void writeText(const std::string& path, const std::string& text) {
    std::ofstream f(path);
    f << text;
}
// A DEM of `w` x `h` pixels `px` m apart, its middle at the origin (x east, z north), from f(x, z) (elevation, m).
template <class F>
rt::Dem synthDem(int w, int h, double px, F f) {
    rt::Dem d;
    d.w = w, d.h = h;
    d.v.resize((size_t)w * h);
    d.geo = rt::Dem::GEO_METRES, d.dx = d.dy = px;
    d.x0 = 600000 - 0.5 * w * px, d.y0 = 4250000 + 0.5 * h * px;
    for (int r = 0; r < h; r++)
        for (int c = 0; c < w; c++) {
            const double x = (c - 0.5 * (w - 1)) * px, z = (0.5 * (h - 1) - r) * px;
            d.v[(size_t)r * w + c] = (float)f(x, z);
        }
    return d;
}
// Writes the DEM (as a tiled LZW GeoTIFF, floating-point predictor: the way USGS 3DEP ships them) and the sidecar; prepared.
std::shared_ptr<const rt::Terrain> makeTerrain(const std::string& dir, const rt::Dem& dem, const std::string& sidecar, std::string* why = nullptr) {
    rt::TiffOptions o;
    o.compression = 5, o.predictor = 3, o.tiled = true, o.tile = 64;
    rt::writeGeoTiff(dir + "/dem.tif", dem, o);
    writeText(dir + "/terrain.txt", "source dem.tif\n" + sidecar);
    rt::Spec s;
    std::string err;
    if (!rt::loadSpec(dir, s, &err)) {
        if (why) *why = err;
        return nullptr;
    }
    auto t = rt::prepare(s, &err);
    if (why) *why = err;
    return t;
}
std::unique_ptr<World> buildReal(std::shared_ptr<const rt::Terrain> t) {
    rt::ScopedTerrain scope(t);
    props::ScopedRockScans none(nullptr);
    const bool was = worldcache::bypass;
    worldcache::bypass = true;
    auto w = std::make_unique<World>(MAP_REAL);
    worldcache::bypass = was;
    return w;
}
double coneAt(double x, double z) {
    const double r = std::sqrt(x * x + z * z);
    return 1000 + (r < 450 ? 300 * (1 - (r / 450) * (r / 450)) : 0) + 0.002 * x;
}
double valleyZ(double x) { return 150 * std::sin(x / 260); }
double valleyAt(double x, double z) {
    const double d = std::fabs(z - valleyZ(x));
    const double cross = d < 70 ? 0.01 * d : std::min(80.0, 0.7 + (d - 70) * 0.22);
    return 1200 + 0.025 * (x + 900) + cross;
}
}  // namespace

SCENARIO(realFormats, "realterrain/formats", "SRTM .hgt, 16-bit PNG and GeoTIFF (every compression, predictor, layout and byte order) read back what was written") {
    const std::string dir = freshDir("formats");
    // .hgt: the pattern back, its corner from its name.
    {
        const int side = 121;
        std::vector<int16_t> v((size_t)side * side);
        for (int y = 0; y < side; y++)
            for (int x = 0; x < side; x++) v[(size_t)y * side + x] = (int16_t)(1000 + 3 * x + 2 * y);
        v[(size_t)60 * side + 60] = -32768;  // a void: filled from round it
        const std::string p = dir + "/N38W110.hgt";
        rt::writeHgt(p, side, v);
        rt::Dem d;
        std::string err;
        const bool ok = rt::loadDem(p, d, &err);
        ctx.check(".hgt loads", ok, err);
        ctx.check(".hgt: 121 x 121, the pattern", ok && d.w == side && d.h == side && d.at(5, 7) == 1000 + 15 + 14 && d.at(120, 0) == 1360, std::to_string(d.at(5, 7)));
        ctx.check(".hgt: the void filled from its neighbours", ok && std::fabs(d.at(60, 60) - (1000 + 180 + 120)) < 0.01, std::to_string(d.at(60, 60)));
        ctx.check(".hgt: its corner from the name (N38W110: 38-39 N, 109-110 W)",
                  d.geo == rt::Dem::GEO_DEGREES && std::fabs(d.x0 - (-110 - 0.5 / 120)) < 1e-12 && std::fabs(d.y0 - (39 + 0.5 / 120)) < 1e-12);
        // As a map: centred on a lat/lon, metres per pixel from the latitude.
        writeText(dir + "/hgt.terrain.txt", "name HGT TEST\nsource N38W110.hgt\ncentre 38.5 -109.5\nsize 3000\nsmooth 0\ndetail 0\n");
        rt::Spec s;
        const bool sok = rt::loadSpec(dir + "/hgt.terrain.txt", s, &err);
        auto t = sok ? rt::prepare(s, &err) : nullptr;
        ctx.check(".hgt as a map (centre + size)", t != nullptr, err);
        if (t) {
            ctx.check("...3000 m across, pixels ~926 m north-south and ~725 m east-west at 38.5 N",
                      t->half == 1500 && std::fabs(t->mppY - 926.0) < 3 && std::fabs(t->mppX - 725.0) < 4,
                      std::to_string(t->mppX) + " " + std::to_string(t->mppY));
            const double e = t->demAt(t->mppX, 0) - t->demAt(0, 0), s2 = t->demAt(0, -t->mppY) - t->demAt(0, 0);
            ctx.check("...east one pixel is +3 m, south one pixel +2 m (the file's pattern)", std::fabs(e - 3) < 0.01 && std::fabs(s2 - 2) < 0.01,
                      std::to_string(e) + " " + std::to_string(s2));
        }
    }
    // 16-bit PNG.
    {
        const int w = 64, h = 48;
        std::vector<uint16_t> v((size_t)w * h);
        for (int y = 0; y < h; y++)
            for (int x = 0; x < w; x++) v[(size_t)y * w + x] = (uint16_t)(100 * x + 7 * y + 300);
        rt::writePng16(dir + "/height.png", w, h, v);
        rt::Dem d;
        std::string err;
        const bool ok = rt::loadDem(dir + "/height.png", d, &err);
        bool same = ok && d.w == w && d.h == h;
        for (int y = 0; same && y < h; y++)
            for (int x = 0; x < w; x++) same &= d.at(x, y) == v[(size_t)y * w + x];
        ctx.check("16-bit PNG reads back every value", same, err);
        writeText(dir + "/png.terrain.txt", "source height.png\nmetres_per_pixel 5\nvertical_scale 0.1\nsmooth 0\ndetail 0\nsize 200\n");
        rt::Spec s;
        auto t = rt::loadSpec(dir + "/png.terrain.txt", s, &err) ? rt::prepare(s, &err) : nullptr;
        ctx.check("16-bit PNG as a map (metres per pixel and vertical scale from the sidecar)", t && t->mppX == 5, err);
        if (t) ctx.check("...one pixel east is 100 x 0.1 = 10 m up", std::fabs(t->demAt(5, 0) - t->demAt(0, 0) - 10) < 1e-6, std::to_string(t->demAt(5, 0) - t->demAt(0, 0)));
    }
    // GeoTIFF, every way it's written.
    {
        rt::Dem d;
        d.w = 203, d.h = 91;
        d.v.resize((size_t)d.w * d.h);
        for (int y = 0; y < d.h; y++)
            for (int x = 0; x < d.w; x++) d.v[(size_t)y * d.w + x] = (float)(1234.5 + 37 * std::sin(x * 0.37) * std::cos(y * 0.21) + (x * 7919 + y * 104729) % 97 * 0.125);
        d.geo = rt::Dem::GEO_DEGREES, d.dx = d.dy = 1.0 / 10800, d.x0 = -110.0, d.y0 = 39.0;
        int tried = 0, good = 0;
        std::string bad;
        for (int comp : {1, 5, 8})
            for (int pred : {1, 2, 3})
                for (int tiled = 0; tiled < 2; tiled++)
                    for (int be = 0; be < 2; be++)
                        for (int i16 = 0; i16 < 2; i16++) {
                            if (i16 && pred == 3) continue;
                            rt::TiffOptions o;
                            o.compression = comp, o.predictor = pred, o.tiled = tiled, o.tile = 64, o.bigEndian = be, o.int16 = i16;
                            char name[96];
                            std::snprintf(name, sizeof name, "%s/t_c%d_p%d_t%d_b%d_i%d.tif", dir.c_str(), comp, pred, tiled, be, i16);
                            rt::writeGeoTiff(name, d, o);
                            tried++;
                            rt::Dem a, b;
                            std::string err;
                            const rt::Window win{17, 9, 150, 80};
                            bool ok = rt::loadGeoTiff(name, a, &err) && rt::loadGeoTiff(name, b, &err, win);
                            ok = ok && a.w == d.w && a.h == d.h && b.w == 133 && b.h == 71;
                            for (int y = 0; ok && y < d.h; y++)
                                for (int x = 0; x < d.w; x++) {
                                    const float want = i16 ? (float)(int16_t)std::lround(d.v[(size_t)y * d.w + x]) : d.v[(size_t)y * d.w + x];
                                    ok &= a.at(x, y) == want;
                                    if (x >= win.x0 && x < win.x1 && y >= win.y0 && y < win.y1) ok &= b.at(x - win.x0, y - win.y0) == want;
                                }
                            ok = ok && a.geo == rt::Dem::GEO_DEGREES && a.x0 == -110.0 && a.y0 == 39.0 && a.dx == d.dx;
                            ok = ok && std::fabs(b.x0 - (-110.0 + 17 * d.dx)) < 1e-12 && std::fabs(b.y0 - (39.0 - 9 * d.dy)) < 1e-12;
                            if (ok) good++;
                            else bad += std::string(" ") + name + (err.empty() ? "" : " (" + err + ")");
                        }
        ctx.check("GeoTIFF: all " + std::to_string(tried) + " ways read back exactly, whole and cropped", good == tried, bad);
        rt::TiffInfo in;
        rt::TiffOptions o;
        o.compression = 5, o.predictor = 3, o.tiled = true, o.tile = 128;
        rt::writeGeoTiff(dir + "/info.tif", d, o);
        ctx.check("GeoTIFF: its header (tiled, LZW, predictor 3, float32, geographic)",
                  rt::tiffInfo(dir + "/info.tif", in) && in.tiled && in.tileW == 128 && in.compression == 5 && in.predictor == 3 && in.bits == 32 && in.sampleFormat == 3 &&
                      in.geo == rt::Dem::GEO_DEGREES && in.modelType == 2);
    }
    // The one cosine the crop needs, by series: as std::cos to the last digits.
    double worst = 0;
    for (double a = -720; a <= 720; a += 0.37) worst = std::max(worst, std::fabs(rt::cosDeg(a) - std::cos(a * M_PI / 180)));
    ctx.check("cosDeg agrees with cos", worst < 1e-13, std::to_string(worst));
    ctx.check("a degree of latitude ~111 km, of longitude at 38.5 N ~87.2 km",
              std::fabs(rt::metresPerDegLat(38.5) - 111000) < 200 && std::fabs(rt::metresPerDegLon(38.5) - 87240) < 150, std::to_string(rt::metresPerDegLon(38.5)));
}

SCENARIO(realSidecar, "realterrain/sidecar", "The sidecar's keys, and what's wrong with a bad one") {
    rt::Spec s;
    std::string err;
    const bool ok = rt::parseSpec("# Moab\nname moab\nsource USGS_13_n39w110.tif\ncentre 38.57 -109.55  # town\nsize 6000\nclimate desert\nexaggeration 1.2\n"
                                  "ledges 1.5 0.7\nsnow_line 3500\nriver_area 50\nseed 7\n",
                                  "maps/moab", s, &err);
    ctx.check("a good sidecar parses", ok, err);
    ctx.check("...its values", s.name == "MOAB" && s.sourcePath == "maps/moab/USGS_13_n39w110.tif" && s.hasCentre && s.centreLat == 38.57 && s.size == 6000 &&
                                   s.climate == rt::CLIMATE_DESERT && s.exaggeration == 1.2 && s.ledgeStep == 1.5 && s.ledgeStrength == 0.7 && s.riverArea == 50 && s.seed == 7);
    ctx.check("an unknown key is an error", !rt::parseSpec("source a.tif\nwibble 3\n", ".", s, &err) && err.find("wibble") != std::string::npos, err);
    ctx.check("no source is an error", !rt::parseSpec("name x\n", ".", s, &err), err);
    ctx.check("a missing file is an error", !rt::loadSpec(freshDir("missing"), s, &err), err);
}

SCENARIO(realCone, "realterrain/cone", "A cone mountain from a GeoTIFF: its height, rock on the steep, snow over the line, grass below, a summit on top") {
    const std::string dir = freshDir("cone");
    std::string why;
    auto t = makeTerrain(dir, synthDem(151, 151, 10, coneAt), "name CONE\nsize 1200\nsnow_line 1270\n", &why);
    ctx.check("the cone is prepared", t != nullptr, why);
    if (!t) return;
    ctx.check("...1200 m across, its base the lowest point", t->half == 600 && std::fabs(t->base - (1000 - 0.002 * 750)) < 1.0, std::to_string(t->base));
    auto w = buildReal(t);
    ctx.check("World(MAP_REAL) is the cone", w->map == MAP_REAL && w->gridSize() == 1201);
    const double top = w->height(0, 0), foot = w->height(-460, 300);
    ctx.metric("the cone stands 300 m over its foot", top - foot, "m", 297, 303.5);
    double worstFit = 0;
    for (double r = 0; r <= 420; r += 35)
        for (int k = 0; k < 4; k++) {
            const double x = (k == 0 ? r : k == 1 ? -r : 0), z = (k == 2 ? r : k == 3 ? -r : 0);
            if (w->roadAt(x, z).near || w->waterLevel(x, z) > -1e8) continue;
            worstFit = std::max(worstFit, std::fabs(w->height(x, z) - (coneAt(x, z) - t->base)));
        }
    ctx.metric("the land follows the DEM (bicubic, smoothed, fine detail)", worstFit, "m", 0, 1.6);
    int rock = 0, scree = 0, snow = 0, grass = 0, n = 0;
    for (int k = 0; k < 16; k++) {
        const double a = k * M_PI / 8;
        auto at = [&](double r) { return w->surfaceAt(r * std::cos(a), r * std::sin(a)); };
        rock += at(385) == SURF_ROCK, scree += at(235) == SURF_SCREE, snow += at(45) == SURF_SNOW;
        n++;
    }
    for (double x = -440; x <= 440; x += 40) grass += w->surfaceAt(x, -520 + 0 * x) == SURF_GRASS, n += 0;
    ctx.check("rock on the steep flank (slope ~1.1)", rock >= 12, std::to_string(rock) + "/16");
    ctx.check("scree on the middle flank (slope ~0.7)", scree >= 11, std::to_string(scree) + "/16");
    ctx.check("snow on the top, over the snow line", snow >= 12, std::to_string(snow) + "/16");
    ctx.check("grass on the flat round its foot", grass >= 15, std::to_string(grass) + "/23");
    const MapPlace* summit = nullptr;
    for (const MapPlace& p : t->plan().places)
        if ((p.kind == PK_SUMMIT || p.kind == PK_LOOKOUT) && std::hypot(p.p.x, p.p.z) < 30) summit = &p;
    ctx.check("a summit (or lookout) on the top", summit != nullptr, summit ? summit->name : "");
    {
        rt::ScopedTerrain scope(t);
        ctx.check("mapPlaces(MAP_REAL) is the plan's", &mapPlaces(MAP_REAL) == &t->plan().places && !discoverSpots(MAP_REAL).empty());
        ctx.check("the map is named after the sidecar", std::string(mapName(MAP_REAL)) == "REAL: CONE", mapName(MAP_REAL));
    }
    int trees = 0;
    for (const Tree& tr : w->trees) trees += std::hypot(tr.base.x, tr.base.z) < 380;
    ctx.check("trees grow, but none on the rock or the snow", !w->trees.empty() && trees < (int)w->trees.size() / 3, std::to_string(trees) + " of " + std::to_string(w->trees.size()));
    ctx.check("bushes and rocks scattered", !w->bushes.empty() && !w->rocks.empty(), std::to_string(w->bushes.size()) + " " + std::to_string(w->rocks.size()));
}

SCENARIO(realValley, "realterrain/valley", "A valley: a river down its floor along the drainage, outposts on flat ground, roads and trails within their grade") {
    const std::string dir = freshDir("valley");
    std::string why;
    auto t = makeTerrain(dir, synthDem(181, 181, 10, valleyAt), "name VALLEY\nsize 1600\nriver_area 0.4\nwash_area 0.05\noutposts 3\n", &why);
    ctx.check("the valley is prepared", t != nullptr, why);
    if (!t) return;
    const rt::Terrain::Plan& plan = t->plan();
    auto w = buildReal(t);
    int wet = 0, n = 0, west = 0;
    for (double x = -500; x <= 500; x += 50) {
        const double z = valleyZ(x);
        double best = -1e9;
        V3 flow;
        for (double dz = -12; dz <= 12; dz += 2)
            if (w->waterLevel(x, z + dz) > best) best = w->waterLevel(x, z + dz), flow = w->waterFlow(x, z + dz);
        wet += best > -1e8, west += best > -1e8 && flow.x < 0, n++;
    }
    ctx.check("a river runs down the valley floor", wet >= n * 8 / 10, std::to_string(wet) + "/" + std::to_string(n));
    ctx.check("...flowing west, down the valley", west >= wet * 8 / 10, std::to_string(west) + "/" + std::to_string(wet));
    ctx.check("...recorded as World::rivers", !w->rivers.empty() && w->rivers[0].spots.size() > 50);
    ctx.check("the benches up the sides stay dry", w->waterLevel(0, valleyZ(0) + 300) < -1e8 && w->waterLevel(-300, valleyZ(-300) - 300) < -1e8);
    int outposts = 0, flatOK = 0;
    std::string detail;
    for (size_t k = 0; k < plan.places.size(); k++) {
        const MapPlace& p = plan.places[k];
        if (p.pad <= 0) continue;
        outposts++;
        double lo = 1e9, hi = -1e9, wlo = 1e9, whi = -1e9;
        for (double dx = -p.pad; dx <= p.pad; dx += 2)
            for (double dz = -p.pad; dz <= p.pad; dz += 2) {
                if (dx * dx + dz * dz > p.pad * p.pad) continue;
                lo = std::min(lo, t->demAt(p.p.x + dx, p.p.z + dz)), hi = std::max(hi, t->demAt(p.p.x + dx, p.p.z + dz));
                if (dx * dx + dz * dz < (p.pad - 2) * (p.pad - 2)) wlo = std::min(wlo, w->height(p.p.x + dx, p.p.z + dz)), whi = std::max(whi, w->height(p.p.x + dx, p.p.z + dz));
            }
        const bool ok = hi - lo < 4.5 && whi - wlo < 0.35 && w->waterLevel(p.p.x, p.p.z) < -1e8;
        flatOK += ok;
        char b[160];
        std::snprintf(b, sizeof b, " %s: land %.1f m, pad %.2f m;", p.name.c_str(), hi - lo, whi - wlo);
        detail += b;
    }
    ctx.check("outposts placed", outposts >= 2, std::to_string(outposts));
    ctx.check("...each on flat dry ground, its pad level", flatOK == outposts, detail);
    ctx.check("the first is the ranger station, with a repair station; the start beside it",
              !plan.places.empty() && plan.places[0].kind == PK_RANGER_STATION && !w->stations.empty() && std::hypot(w->spawn.x - plan.places[0].p.x, w->spawn.z - plan.places[0].p.z) < 40);
    int roads = 0, steep = 0, samples = 0;
    double worst = 0;
    for (const rt::PlanTrail& tr : plan.trails) {
        roads += tr.road;
        const std::vector<V3> line = rt::smoothLine(tr.pts, 1.0);
        for (size_t k = 0; k + 4 < line.size(); k++) {
            bool nearWater = false;
            for (double o = -15; o <= 15 && !nearWater; o += 3) nearWater |= w->waterLevel(line[k].x + o, line[k].z) > -1e8 || w->waterLevel(line[k].x, line[k].z + o) > -1e8;
            if (nearWater) continue;
            const double run = std::hypot(line[k + 4].x - line[k].x, line[k + 4].z - line[k].z);
            const double g = std::fabs(w->height(line[k + 4].x, line[k + 4].z) - w->height(line[k].x, line[k].z)) / run;
            samples++;
            worst = std::max(worst, g - tr.maxGrade);
            if (g > tr.maxGrade + 0.03 && std::getenv("CZ_REAL_DEBUG")) std::printf("trail %d road %d k %d/%d at %.0f,%.0f grade %.3f\n", (int)(&tr - &plan.trails[0]), (int)tr.road, (int)k, (int)line.size(), line[k].x, line[k].z, g);
            steep += g > tr.maxGrade + 0.03;
            if (!w->roadAt(line[k].x, line[k].z).near) steep++;
        }
    }
    ctx.check("roads join the outposts", roads >= 1 && !plan.trails.empty(), std::to_string(roads) + " roads, " + std::to_string(plan.trails.size()) + " in all");
    ctx.check("...every road and trail within its grade (12%, trails 30%), on its tread", steep == 0 && samples > 100,
              std::to_string(steep) + " of " + std::to_string(samples) + " over, worst +" + std::to_string(worst));
}

SCENARIO(realWater, "realterrain/water", "A closed hollow holds a lake at its spill level; land under sea level is sea") {
    {
        const std::string dir = freshDir("lake");
        auto pit = [](double x, double z) {
            const double d = std::hypot(x - 150, z + 100);
            return 1000 + 0.01 * x - 6 * std::max(0.0, 1 - (d / 150) * (d / 150));
        };
        std::string why;
        auto t = makeTerrain(dir, synthDem(121, 121, 10, pit), "size 1000\n", &why);
        ctx.check("the hollow is prepared", t != nullptr, why);
        if (!t) return;
        auto w = buildReal(t);
        const double wl = w->waterLevel(150, -100), h = w->height(150, -100);
        ctx.check("a lake in the hollow", wl > -1e8 && wl - h > 0.3, std::to_string(wl) + " over " + std::to_string(h));
        ctx.check("...at about its spill level (the lip on its downhill side)", !t->plan().lakes.empty() && std::fabs(wl - t->plan().lakes[0].level) < 1e-3 &&
                                                                                  wl > (pit(0, -100) - t->base) - 1.0 && wl < (pit(300, -100) - t->base) + 0.5,
                  std::to_string(wl));
        ctx.check("...and dry ground away from it", w->waterLevel(-300, 300) < -1e8 && w->waterLevel(150, 250) < -1e8);
    }
    {
        const std::string dir = freshDir("sea");
        std::string why;
        auto t = makeTerrain(dir, synthDem(121, 121, 10, [](double x, double) { return 5 + 0.02 * x; }), "size 1000\nsea_level 0\n", &why);
        ctx.check("the shore is prepared", t != nullptr, why);
        if (!t) return;
        auto w = buildReal(t);
        const double sea = t->mapHeight(0);
        ctx.check("the sea below sea level", std::fabs(w->waterLevel(-340, 0) - sea) < 1e-4 && w->height(-340, 0) < sea - 0.1, std::to_string(w->waterLevel(-340, 0)));
        ctx.check("...land above it dry", w->waterLevel(200, 0) < -1e8);
    }
}

SCENARIO(realDeterminism, "realterrain/determinism", "The same file builds the same land (land hash), through the world cache too; another file other land") {
    const std::string dir = freshDir("det");
    std::string why;
    auto t = makeTerrain(dir, synthDem(151, 151, 10, coneAt), "size 1200\nledges 1.5\n", &why);
    ctx.check("prepared", t != nullptr, why);
    if (!t) return;
    const uint64_t a = buildReal(t)->landHash();
    // (Read again from the file: a new Terrain, a new plan.)
    rt::Spec s;
    rt::loadSpec(dir, s);
    auto t2 = rt::prepare(s);
    auto w2 = buildReal(t2);
    const uint64_t b = w2->landHash();
    ctx.check("the same file: the same land hash", a == b && s.hash == t->spec.hash, std::to_string(a) + " " + std::to_string(b));
    World back(MAP_PROVING_GROUND);
    ctx.check("...and the same through the world cache's encoding", worldcache::decode(worldcache::encode(*w2), back) && back.landHash() == b && back.map == MAP_REAL);
    {
        rt::ScopedTerrain scope(t);
        ctx.check("the cache key is the file's", worldcache::keyFor(MAP_REAL) != 0 || !worldcache::cacheable(MAP_REAL));
    }
    const std::string dir2 = freshDir("det2");
    auto t3 = makeTerrain(dir2, synthDem(151, 151, 10, [](double x, double z) { return coneAt(x, z) + (x > 100 && z > 100 ? 2.0 : 0.0); }), "size 1200\nledges 1.5\n", &why);
    ctx.check("another file: another hash and other land", t3 && t3->spec.hash != t->spec.hash && buildReal(t3)->landHash() != a);
}

SCENARIO(realNet, "realterrain/net", "A guest with the host's real-terrain file joins on it; one without is told which file it needs") {
    const std::string dir = freshDir("net");
    std::string why;
    auto t = makeTerrain(dir, synthDem(81, 81, 10, coneAt), "name NETCONE\nsize 600\n", &why);
    ctx.check("prepared", t != nullptr, why);
    if (!t) return;
    // The guest finds terrains in maps/: a folder of them, holding a copy of the host's.
    const std::string maps = freshDir("netmaps");
    std::error_code ec;
    fs::create_directories(maps + "/netcone", ec);
    fs::copy_file(dir + "/dem.tif", maps + "/netcone/dem.tif", ec);
    fs::copy_file(dir + "/terrain.txt", maps + "/netcone/terrain.txt", ec);
    rt::ScopedTerrain host(t);
    const int port = 31500 + (int)(getpid() % 300);
    for (int attempt = 0; attempt < 2; attempt++) {
        const bool has = attempt == 0;
        NetSession h;
        if (!ctx.check(std::string("host opens a session on the real terrain") + (has ? "" : " (again)"), h.host(port + attempt, "HOSTY", MAP_REAL), h.error)) return;
        h.adopt(ctx.sim);
        NetSession g;
        bool joined = false, done = false;
        uint64_t guestHash = 0;
        std::thread joiner([&] {
            rt::ScopedTerrain guest(nullptr, has ? std::vector<std::string>{maps} : std::vector<std::string>{});
            joined = g.join("127.0.0.1:" + std::to_string(port + attempt), "BUDDY", 3.0);
            guestHash = rt::activeHash();
            done = true;
        });
        for (int k = 0; k < 400 && !done; k++) {
            h.receive(ctx.sim, ctx.t);
            usleep(5000);
        }
        joiner.join();
        if (has) ctx.check("a guest with the file joins, on the host's terrain", joined && guestHash == t->spec.hash, g.error);
        else ctx.check("a guest without it is turned away, told which", !joined && g.error.find("NETCONE") != std::string::npos, g.error);
        g.close();
        h.close();
    }
}

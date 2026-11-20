// The plant generator (plantgen.h): every species file loads and grows, the same mesh from the same seed, each far copy within its
// budget and lighter than the one before, trees with wood where their collider stands, a palette's species all there, and the
// classic maps drawn with them (--proc-plants) keep their colliders.
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <fstream>
#include <map>
#include <set>
#include <sstream>

#include "../src/forest.h"
#include "../src/modelkit.h"
#include "../src/plantgen.h"
#include "../src/plants.h"
#include "../src/procworld.h"
#include "../src/vegetation.h"
#include "../src/world.h"
#include "harness.h"

namespace {

uint64_t meshHash(const mk::Mesh& m) {
    uint64_t h = 1469598103934665603ull;
    auto eat = [&](const void* p, size_t n) {
        for (size_t i = 0; i < n; i++) h = (h ^ ((const unsigned char*)p)[i]) * 1099511628211ull;
    };
    for (const mk::MVert& v : m.v) eat(v.p, 12), eat(v.n, 12), eat(v.c, 12), eat(&v.mat, 1);
    return h;
}
mk::Mesh grow(const plantgen::Species& s, uint32_t seed, int lod, bool snow = false, double dry = 0) {
    plantgen::Options o;
    o.seed = seed, o.lod = lod, o.snow = snow, o.dry = dry;
    mk::Mesh m;
    plantgen::generate(s, o, m);
    return m;
}
bool sound(const mk::Mesh& m, std::string& why) {
    if (m.v.size() % 3) return why = "not whole triangles", false;
    for (const mk::MVert& v : m.v) {
        for (int k = 0; k < 3; k++)
            if (!std::isfinite(v.p[k]) || !std::isfinite(v.n[k]) || !std::isfinite(v.c[k])) return why = "not finite", false;
        if (std::fabs(std::sqrt(v.n[0] * v.n[0] + v.n[1] * v.n[1] + v.n[2] * v.n[2]) - 1) > 0.01) return why = "normal not unit length", false;
        for (int k = 0; k < 3; k++)
            if (v.c[k] < 0 || v.c[k] > 1.5) return why = "colour out of range", false;
    }
    return true;
}
// The drawn stem's radius where a level plane at y cuts the wood: the cuts nearest the axis (a limb or a stub crossing the plane
// further out isn't the stem), measured from their own middle (a stem leans).
double woodRadius(const mk::Mesh& m, double y) {
    std::vector<std::pair<double, double>> cut;
    for (size_t i = 0; i + 2 < m.v.size(); i += 3) {
        if (m.v[i].mat != mk::WOOD) continue;
        for (int e = 0; e < 3; e++) {
            const float* a = m.v[i + e].p;
            const float* b = m.v[i + (e + 1) % 3].p;
            if ((a[1] - y) * (b[1] - y) > 0 || a[1] == b[1]) continue;
            const double f = (y - a[1]) / (b[1] - a[1]);
            cut.push_back({a[0] + (b[0] - a[0]) * f, a[2] + (b[2] - a[2]) * f});
        }
    }
    if (cut.empty()) return 0;
    double cx = 0, cz = 0, r = 0;
    for (int pass = 0; pass < 3; pass++) {
        std::vector<double> d;
        for (const auto& q : cut) d.push_back(std::hypot(q.first - cx, q.second - cz));
        std::sort(d.begin(), d.end());
        r = d[d.size() / 4];
        double sx = 0, sz = 0;
        int n = 0;
        for (const auto& q : cut)
            if (std::hypot(q.first - cx, q.second - cz) < 1.6 * r + 0.02) sx += q.first, sz += q.second, n++;
        if (n) cx = sx / n, cz = sz / n;
    }
    return r;
}
// A species of the test's own (no file): what the determinism check pins.
const char* const kTestTree = "name TEST TREE\nclass tree\nid 900\nform decurrent\ncount 4 3 3\ncards 4\ndead 0.2\n";
}  // namespace


SCENARIO(procPlantsSpecies, "procplants/species", "Every species file in assets/plants loads: known keys only, a class and an id of its own, and the classic model it replaces exists") {
    ctx.check("the species folder is found", !plantgen::dir().empty(), plantgen::dir());
    for (const std::string& e : plantgen::loadErrors()) ctx.check("loads: " + e, false);
    ctx.metric("species", (double)plantgen::all().size(), "", 19, 400);
    // The first set: what the maps grew before, each with a species now.
    for (const char* want : {"fir", "spruce", "ponderosa", "pinyon", "juniper", "cottonwood", "aspen", "oak", "tamarisk", "snag"}) {
        const plantgen::Species* s = plantgen::find(want);
        ctx.check(std::string(want) + ": a tree species", s && s->tree);
    }
    for (const char* want : {"sagebrush", "blackbrush", "mormon_tea", "rabbitbrush", "saltbush", "yucca", "prickly_pear", "bunchgrass", "coyote_willow"}) {
        const plantgen::Species* s = plantgen::find(want);
        ctx.check(std::string(want) + ": a shrub species", s && !s->tree);
    }
    std::set<std::string> classic;
    for (int k = 0; k < 4; k++) {
        static const char* const names[4] = {"pine", "broadleaf", "snag", "dryland"};
        const mk::ModelDef* d = mk::findModel(forest::modelId(k));
        for (int v = 0; d && v < (int)d->variants.size(); v++) classic.insert(std::string(names[k]) + " " + std::to_string(v));
    }
    for (int k = 0; k < BUSH_KIND_COUNT; k++) classic.insert(std::string("bush ") + plantKindName(k));
    for (const plantgen::Species& s : plantgen::all()) {
        ctx.check(s.key + ": found by its id", (s.tree ? plantgen::treeById(s.id) : plantgen::shrubById(s.id)) == &s);
        ctx.check(s.key + ": a size range", s.v("size", 0) > 0 && s.v("size", 1) >= s.v("size", 0));
        for (const std::string& r : s.replaces) {
            ctx.check(s.key + ": replaces a classic model that exists (" + r + ")", classic.count(r) > 0);
            ctx.check(s.key + ": a tree for a tree, a shrub for a bush", (r.rfind("bush ", 0) == 0) == !s.tree);
            ctx.check(s.key + ": the one that replaces " + r, plantgen::replacing(r) == &s);
        }
    }
    // The model kit shows them all: a design each.
    const mk::ModelDef* td = mk::findModel("tree_gen");
    const mk::ModelDef* sd = mk::findModel("shrub_gen");
    size_t trees = 0;
    for (const plantgen::Species& s : plantgen::all()) trees += s.tree;
    ctx.check("the model kit has a design for each", td && sd && td->variants.size() == trees && sd->variants.size() == plantgen::all().size() - trees);
    // A bad file says why.
    plantgen::Species bad;
    std::string err;
    ctx.check("an unknown key is refused", !plantgen::parse("x", "form shrub\nid 1\nlefa 1 2 3\n", bad, &err) && err.find("lefa") != std::string::npos, err);
    ctx.check("a missing form is refused", !plantgen::parse("x", "id 1\n", bad, &err), err);
    for (const plantgen::KeyInfo& k : plantgen::keys()) ctx.check(std::string("key ") + k.key + " is documented", k.what != nullptr);
}

SCENARIO(procPlantsDeterminism, "procplants/determinism", "The same species and seed give the same mesh, bit for bit, every time and on every build; another seed is another plant; the far copies are the same plant") {
    for (const plantgen::Species& s : plantgen::all()) {
        bool same = true, differs = true;
        for (int lod = 0; lod < 4; lod++) {
            const uint64_t a = meshHash(grow(s, 3, lod)), b = meshHash(grow(s, 3, lod)), c = meshHash(grow(s, 4, lod));
            same &= a == b, differs &= a != c;
        }
        ctx.check(s.key + ": the same seed, the same mesh", same);
        ctx.check(s.key + ": another seed, another plant", differs);
        // (the same individual further off: about as tall and as wide)
        const mk::Mesh full = grow(s, 3, 0), far = grow(s, 3, 2);
        const double h0 = full.hi.y, h2 = far.hi.y, w0 = std::max(full.hi.x - full.lo.x, full.hi.z - full.lo.z), w2 = std::max(far.hi.x - far.lo.x, far.hi.z - far.lo.z);
        ctx.check(s.key + ": its far copy is the same size", std::fabs(h2 - h0) < 0.25 * h0 + 0.1 && std::fabs(w2 - w0) < 0.4 * w0 + 0.1,
                  std::to_string(h0) + " x " + std::to_string(w0) + " against " + std::to_string(h2) + " x " + std::to_string(w2));
    }
    // A species of the test's own, pinned: the mesh must not change with the compiler, the C library or the machine (detmath.h, no
    // FMA). A deliberate change to the generator changes these: update them and say so.
    plantgen::Species t;
    std::string err;
    ctx.check("the test species parses", plantgen::parse("test_tree", kTestTree, t, &err), err);
    const uint64_t h0 = meshHash(grow(t, 1, 0)), h1 = meshHash(grow(t, 7, 2)), h2 = meshHash(grow(t, 2, 0, true));
    std::printf("  test tree hashes: %016llx %016llx %016llx\n", (unsigned long long)h0, (unsigned long long)h1, (unsigned long long)h2);
    ctx.check("pinned: seed 1, full", h0 == 0xa2444ba117a8d029ull);
    ctx.check("pinned: seed 7, far", h1 == 0x3e0e8128427c5054ull);
    ctx.check("pinned: seed 2, snowy", h2 == 0x66b055588ca04b54ull);
}

SCENARIO(procPlantsBudgets, "procplants/budgets", "Every species at every seed the game uses: sound triangles, standing on the ground at the model's size, each far copy within its budget and lighter than the last") {
    // Triangles: a tree's full model is drawn within 30 m, then to 75 m, to 200 m and beyond (forest.h lodReach); a shrub's within
    // 11 m, to 30 m and beyond (kBushLodAt).
    static const size_t treeBudget[4] = {6500, 2800, 480, 150}, shrubBudget[3] = {1100, 450, 150};
    size_t worst[4] = {};
    for (const plantgen::Species& s : plantgen::all()) {
        const int lods = s.tree ? 4 : 3;
        size_t most[4] = {};
        for (uint32_t seed = 1; seed <= 6; seed++) {
            size_t prev = 0;
            for (int lod = 0; lod < lods; lod++) {
                const mk::Mesh m = grow(s, seed, lod);
                const std::string tag = s.key + " seed " + std::to_string(seed) + " lod " + std::to_string(lod) + ": ";
                std::string why;
                if (!sound(m, why)) ctx.check(tag + "sound", false, why);
                if (m.triangles() == 0) ctx.check(tag + "grows something", false);
                if (lod > 0 && m.triangles() >= prev) ctx.check(tag + "lighter than the copy before", false, std::to_string(m.triangles()) + " after " + std::to_string(prev));
                prev = m.triangles(), most[lod] = std::max(most[lod], m.triangles());
                if (lod) continue;
                if (m.lo.y < -0.6 || m.lo.y > 0.25) ctx.check(tag + "stands on the ground", false, std::to_string(m.lo.y));
                const double wide = std::max({std::fabs(m.lo.x), std::fabs(m.hi.x), std::fabs(m.lo.z), std::fabs(m.hi.z)});
                if (s.tree && (m.hi.y < 8.5 || m.hi.y > 10.6)) ctx.check(tag + "10 tall", false, std::to_string(m.hi.y));
                if (!s.tree && (wide < 0.45 || wide > 1.5)) ctx.check(tag + "0.8 in half-width", false, std::to_string(wide));
            }
        }
        std::printf("  %-14s %s  %5zu %5zu %5zu %5zu tris\n", s.key.c_str(), s.tree ? "tree " : "shrub", most[0], most[1], most[2], most[3]);
        for (int lod = 0; lod < lods; lod++) {
            const size_t budget = s.tree ? treeBudget[lod] : shrubBudget[lod];
            ctx.check(s.key + " lod " + std::to_string(lod) + ": within budget", most[lod] <= budget, std::to_string(most[lod]) + " > " + std::to_string(budget));
            if (s.tree) worst[lod] = std::max(worst[lod], most[lod]);
        }
        // Snow and drought change it, and add no triangles.
        if (s.tree && s.v("green_stems") > 1) {}  // (a cactus column: nothing level for snow to lie on)
        else if (s.tree) ctx.check(s.key + ": snow lies on it", meshHash(grow(s, 1, 0, true)) != meshHash(grow(s, 1, 0)) && grow(s, 1, 0, true).triangles() == grow(s, 1, 0).triangles());
        else if (s.v("cards") > 0 || s.v("green_stems") > 0 || !s.woody()) ctx.check(s.key + ": dry is another colour", meshHash(grow(s, 1, 0, false, 1)) != meshHash(grow(s, 1, 0)));  // (bare wood has nothing to dry)
    }
    ctx.metric("the heaviest tree, full", (double)worst[0], "tris", 500, 6500);
    ctx.metric("the heaviest tree, farthest", (double)worst[3], "tris", 6, 150);
}

SCENARIO(procPlantsTrunks, "procplants/trunks", "A generated tree has a trunk to hit: wood at its foot, a Pole as thick as the drawn stem 1 m up; a snag has no foliage; a shrub has no collider") {
    World w(MAP_PROVING_GROUND);
    const size_t poles0 = w.poles.size();
    for (const plantgen::Species& s : plantgen::all()) {
        if (!s.tree) continue;
        for (int snow = 0; snow < 2; snow++) {
            const mk::Mesh m = grow(s, 1, 0, snow != 0);
            int woodFoot = 0, leafy = 0;
            for (const mk::MVert& q : m.v) leafy += q.mat == mk::FOLIAGE || q.mat == mk::SNOWLEAF || q.mat == mk::LEAFCUT, woodFoot += q.p[1] < 0.5 && q.mat == mk::WOOD;
            ctx.check(s.key + (snow ? " snowy" : "") + ": the trunk is wood at the foot", woodFoot > 0);
            if (s.v("green_stems") > 1) continue;  // (a cactus: all stem)
            if (s.v("cards") <= 0) ctx.check(s.key + (snow ? " snowy" : "") + ": bare", leafy == 0, std::to_string(leafy));
            else ctx.check(s.key + (snow ? " snowy" : "") + ": has foliage to fade", leafy > 0);
        }
        // Planted at the middle of its size range: its Pole against the drawn stem 1 m above the ground.
        const double h = 0.5 * (s.v("size", 0) + s.v("size", 1));
        Tree t;
        t.base = V3(400 + 12.0 * s.id, w.height(400 + 12.0 * s.id, 300) - 0.2, 300), t.height = h, t.radius = h * 0.3, t.tint = 1, t.kind = forest::kGenKind, t.design = s.id;
        w.plant(t);
        const Pole& p = w.poles.back();
        ctx.check(s.key + ": a Pole where it stands", std::hypot(p.x - t.base.x, p.z - t.base.z) < 0.01 && p.r >= 0.06 && p.r <= 0.9, std::to_string(p.r));
        if ((int)s.v("stems", 1) > 1) continue;  // (a thicket's stems: the Pole is its first one's)
        const double k = h / plantgen::kTreeHeight, y = 1.2 / k;
        const mk::Mesh m = grow(s, 1, 0);
        const double drawn = woodRadius(m, y) * k;
        if (y < 3.0 && y < 7.0 * s.v("trunk_len") && drawn > 0) ctx.metric(s.key + ": pole over the drawn stem", p.r / drawn, "", 0.7, 1.4);  // (below where it forks)
    }
    ctx.check("a pole for each", w.poles.size() > poles0);
}

SCENARIO(procPlantsPalette, "procplants/palette", "A palette with `plants procedural` names the generator's species: Moab's all resolve, its trees are kind 4 with their species' id, and the forest draws each with every far copy") {
    std::string path = plantgen::dir() + "/../maps/vegetation/colorado_plateau_desert.txt";
    std::ifstream f(path);
    ctx.check("Moab's palette is found", (bool)f, path);
    if (!f) return;
    std::stringstream ss;
    ss << f.rdbuf();
    veg::Palette pal;
    std::string err;
    ctx.check("it parses", veg::parse(ss.str(), pal, &err), err);
    ctx.check("it asks for the generated plants", pal.procedural);
    World w(MAP_PROVING_GROUND);
    w.trees.clear(), w.bushes.clear();
    std::set<int> treeIds, shrubIds;
    int classicTrees = 0, classicShrubs = 0, n = 0;
    for (const veg::ZoneRule& z : pal.zone) {
        for (const veg::TreeSpec& t : z.trees) {
            if (t.kind != forest::kGenKind) {
                classicTrees++;
                continue;
            }
            const plantgen::Species* s = plantgen::treeById(t.design);
            ctx.check(t.species + ": its species is there", s && s->key == t.species);
            if (!s || !treeIds.insert(t.design).second) continue;
            Tree tr;
            tr.base = V3(300 + 10.0 * n++, 0, 300), tr.height = t.hMin, tr.radius = 1, tr.tint = 1, tr.kind = t.kind, tr.design = t.design;
            w.plant(tr);
        }
        for (const veg::ShrubSpec& s : z.shrubs) {
            if (s.kind < plantgen::kShrubKindBase) {
                classicShrubs++;
                continue;
            }
            const plantgen::Species* g = plantgen::shrubById(s.kind - plantgen::kShrubKindBase);
            ctx.check(s.species + ": its species is there", g && g->key == s.species);
            ctx.check(s.species + ": sized by its species", s.rMin > 0.05 && s.rMax >= s.rMin && s.rMax < 4);
            if (!g || !shrubIds.insert(s.kind).second) continue;
            w.bushes.push_back({V3(300 + 3.0 * shrubIds.size(), 0, 320), 0.5 * (s.rMin + s.rMax), 1.0f, s.kind});
        }
    }
    ctx.metric("generated tree species in the palette", (double)treeIds.size(), "", 5, 40);
    ctx.metric("generated shrub species in the palette", (double)shrubIds.size(), "", 8, 80);
    ctx.check("no classic tree left in Moab's palette", classicTrees == 0, std::to_string(classicTrees));
    std::printf("  %d shrub lines are classic kinds (reeds, tumbleweed, the low juniper)\n", classicShrubs);
    forest::Set trees, bushes;
    forest::build(w, false, false, trees);
    forest::buildBushes(w, bushes);
    ctx.check("an instance per tree", trees.inst.size() == treeIds.size(), std::to_string(trees.inst.size()));
    for (const forest::Shape& s : trees.shapes) {
        bool all = true;
        for (int l = 0; l < forest::kLods; l++) all &= !s.lod[l].v.empty() && (l == 0 || s.lod[l].v.size() < s.lod[l - 1].v.size());
        ctx.check("tree look " + std::to_string(s.look.design) + ": every far copy, each lighter", all);
        ctx.check("tree look " + std::to_string(s.look.design) + ": 10 tall", s.top > 8.5 && s.top < 10.6, std::to_string(s.top));
    }
    ctx.check("an instance per shrub", bushes.inst.size() == shrubIds.size(), std::to_string(bushes.inst.size()));
    for (const forest::Shape& s : bushes.shapes) ctx.check("shrub look " + std::to_string(s.look.kind) + ": three copies", !s.lod[0].v.empty() && !s.lod[1].v.empty() && !s.lod[2].v.empty() && s.lod[3].v.empty());
    // Without the key the same names are the classic models.
    veg::Palette old;
    ctx.check("a palette without the key parses", veg::parse("zone upland 10 10\n tree juniper 1 2 4\n shrub sagebrush 1\n", old, &err), err);
    ctx.check("...and grows the classic models", !old.procedural && old.zone[veg::Z_UPLAND].trees[0].kind == 3 && old.zone[veg::Z_UPLAND].shrubs[0].kind == BUSH_SAGE);
}

namespace {
// The tree species a biome's palette stands in for the classic trees (its `stand` lines for the tree kinds and `dead`): their ids.
std::set<int> standTrees(int zone) {
    std::set<int> ids;
    std::ifstream f(plantgen::dir() + "/../maps/vegetation/" + plantZoneName(zone) + ".txt");
    std::stringstream ss;
    ss << f.rdbuf();
    veg::Palette p;
    if (!f || !veg::parse(ss.str(), p)) return ids;
    for (const char* key : {"pine", "broadleaf", "snag", "dryland", "dead"})
        if (p.stand.count(key))
            for (const veg::Palette::Stand& st : p.stand[key])
                if (const plantgen::Species* sp = plantgen::find(st.species); sp && sp->tree) ids.insert(sp->id);
    return ids;
}
World wilds(int kind) {
    const int was = World::wildsKind;
    World::wildsKind = kind;
    World w(MAP_WILDS);
    World::wildsKind = was;
    return w;
}
// Where a biome's living trees stand thickest on a map: the 128 m square with the most trees of its palette's species (not its dead
// ones) on ground marked for it, as text.
std::string densest(const World& w, int zone) {
    std::set<int> ids = standTrees(zone);
    for (const char* dead : {"snag", "snag_broken", "snag_burnt", "deadfall"})
        if (const plantgen::Species* sp = plantgen::find(dead)) ids.erase(sp->id);
    std::map<std::pair<int, int>, int> cells;
    int total = 0;
    for (const Tree& t : w.trees)
        if (ids.count(t.species) && w.plantZone(t.base.x, t.base.z) == zone) cells[{(int)std::floor(t.base.x / 128), (int)std::floor(t.base.z / 128)}]++, total++;
    if (!total) return "no living trees of its palette on ground marked for it";
    auto best = cells.begin();
    for (auto it = cells.begin(); it != cells.end(); ++it)
        if (it->second > best->second) best = it;
    char b[160];
    std::snprintf(b, sizeof b, "%d living trees of its palette on its ground, most (%d) round %d,%d", total, best->second, best->first.first * 128 + 64, best->first.second * 128 + 64);
    return b;
}
const Pole* poleOf(const World& w, const Tree& t) {
    for (const Pole& p : w.poles)
        if (std::hypot(p.x - t.base.x, p.z - t.base.z) < 0.01) return &p;
    return nullptr;
}
}  // namespace

SCENARIO(procPlantsSwitch, "procplants/switch", "Classic maps: every tree is planted as a species of its biome (the world is the same whatever the look); the default draws those species and the generated bushes, --classic-plants the classic models on the same trees and Poles") {
    World w(MAP_BACKCOUNTRY);
    size_t settled = 0;
    for (const Tree& t : w.trees) settled += t.kind == forest::kGenKind || (t.species >= 0 && plantgen::treeById(t.species));
    ctx.check("every tree has its species", settled == w.trees.size(), std::to_string(w.trees.size() - settled) + " without");
    forest::Set off, on, bOff, bOn;
    {
        plantgen::EverywhereHere sw(false);
        forest::build(w, false, false, off);
        forest::buildBushes(w, bOff);
    }
    size_t gen = 0;
    for (const forest::Shape& s : off.shapes) gen += s.look.design >= forest::kGenDesign;
    for (const forest::Shape& s : bOff.shapes) gen += s.look.kind >= plantgen::kShrubKindBase;
    ctx.check("--classic-plants: only the classic models", gen == 0, std::to_string(gen));
    {
        plantgen::EverywhereHere sw(true);
        forest::build(w, false, false, on);
        forest::buildBushes(w, bOn);
    }
    ctx.check("the same trees and bushes either way", on.inst.size() == off.inst.size() && bOn.inst.size() == bOff.inst.size());
    size_t classic = 0, genBush = 0, wrong = 0;
    for (const forest::Shape& s : on.shapes) classic += s.look.design < forest::kGenDesign;
    for (const forest::Shape& s : bOn.shapes) genBush += s.look.kind >= plantgen::kShrubKindBase;
    ctx.check("the default: every tree look is a generated species", classic == 0, std::to_string(classic) + " classic");
    ctx.check("the default: the bushes are generated (a kind no palette or species covers stays classic)", genBush > 0, std::to_string(genBush) + " of " + std::to_string(bOn.shapes.size()));
    // Each tree is drawn as the species the world says, at the world's height, either look.
    double worstTall = 0, worstClassic = 0;
    for (size_t i = 0; i < on.inst.size(); i++) {
        const forest::Inst& a = on.inst[i];
        const forest::Shape& s = on.shapes[a.shape];
        const Tree& t = w.trees[a.tree];
        wrong += t.kind != forest::kGenKind && s.look.design != forest::kGenDesign + t.species;
        worstTall = std::max(worstTall, std::fabs(s.top * a.scale - t.height) / t.height);
        const forest::Inst& c = off.inst[i];
        worstClassic = std::max(worstClassic, std::fabs(c.scale * forest::modelHeight(t.kind) - t.height) / t.height);
    }
    ctx.check("the default draws each tree as its species", wrong == 0, std::to_string(wrong));
    ctx.metric("generated trees: drawn height off the tree's own", worstTall, "", 0, 0.12);
    ctx.metric("classic look: the model's scale off the tree's own height", worstClassic, "", 0, 0.001);
}

SCENARIO(procPlantsStandIns, "procplants/stand-ins", "A classic tree's Pole is the stem of the species it grows as, short ones too: as thick as the drawn stem, no taller than the drawn tree, on every kind of wilds and on the Backcountry") {
    plantgen::EverywhereHere sw(true);
    std::map<int, int> seen;  // species -> trees checked
    std::set<int> measured, measuredFoot;
    size_t tall = 0, trees = 0, noPole = 0, offPole = 0;
    double worstTop = 0;
    auto look = [&](const World& w, const char* what) {
        forest::Set set;
        forest::build(w, false, false, set);
        for (const forest::Inst& in : set.inst) {
            const Tree& t = w.trees[in.tree];
            if (t.kind == forest::kGenKind || t.species < 0) continue;
            const plantgen::Species* sp = plantgen::treeById(t.species);
            if (!sp) continue;
            trees++;
            if (seen[t.species]++ >= 3) continue;  // (a few of each species a map)
            const forest::Shape& s = set.shapes[in.shape];
            const Pole* pole = poleOf(w, t);
            if (!pole) {
                noPole++;
                continue;
            }
            // The Pole against the tree as drawn: it ends inside the drawn tree, 0.6 of the way up; it is the radius the species' own
            // stem has 1.2 m above its foot at this height (what World::plant asks for).
            const double drawnTop = s.top * in.scale, poleTop = pole->y1 - t.base.y;
            tall += poleTop > drawnTop + 0.05;
            worstTop = std::max(worstTop, std::fabs(poleTop - 0.6 * t.height));
            offPole += std::fabs(pole->r - forest::trunkRadius(forest::kGenKind, t.species, t.height)) > 1e-9;
            // ...and against the wood drawn: where the stem is one stem there (below its fork, not a thicket).
            if ((int)sp->v("stems", 1) > 1) continue;
            const double k = in.scale, y = 1.2 / k;
            char name[120];
            if (y > 5.0 * sp->v("trunk_len") || y > 6.0) {
                // (A low tree that has forked by 1 m up, a mesquite: its Pole is its main stem's there, which is a limb by then. Against
                // the trunk at its foot, 0.3 m above the ground: the Pole is never wider than the trunk drawn, nor a wire inside it.)
                const double foot = woodRadius(s.lod[0], 0.5 / k) * k;
                if (foot <= 0 || !measuredFoot.insert(t.species).second) continue;
                std::snprintf(name, sizeof name, "%s: %s, %.1f m tall (kind %d), forks low: pole over the trunk drawn at its foot", what, sp->key.c_str(), t.height, t.kind);
                ctx.metric(name, pole->r / foot, "", 0.3, 1.1);
                continue;
            }
            const double drawn = woodRadius(s.lod[0], y) * k;
            if (drawn <= 0 || !measured.insert(t.species).second) continue;
            std::snprintf(name, sizeof name, "%s: %s, %.1f m tall (kind %d): pole over the drawn stem", what, sp->key.c_str(), t.height, t.kind);
            ctx.metric(name, pole->r / drawn, "", 0.6, 1.6);
        }
    };
    static const char* const names[WILDS_COUNT] = {"meadow", "desert", "forest", "canyon", "wetland", "upland"};
    for (int kind = 0; kind < WILDS_COUNT; kind++) look(wilds(kind), names[kind]);
    look(World(MAP_BACKCOUNTRY), "backcountry");
    ctx.check("every stand-in has its Pole", noPole == 0, std::to_string(noPole));
    ctx.check("no Pole stands over its drawn tree", tall == 0, std::to_string(tall));
    ctx.check("every Pole is its species' stem's radius at the tree's height", offPole == 0, std::to_string(offPole));
    ctx.metric("a Pole's height off 0.6 of its tree's", worstTop, "m", 0, 0.001);
    for (const char* shorty : {"mesquite", "palo_verde", "catclaw", "saguaro", "krummholz"}) {
        const plantgen::Species* sp = plantgen::find(shorty);
        ctx.check(std::string(shorty) + ": planted somewhere, with its own Pole", sp && seen.count(sp->id) > 0);
    }
    ctx.metric("species planted on these maps", (double)seen.size(), "", 12, 60);
    ctx.metric("species whose drawn stem was measured against the Pole", (double)measured.size(), "", 8, 60);
    // A short species' tree is short in the world (not a tall tree drawn short): its height is the planted one's times stand_scale.
    {
        World w = wilds(WILDS_DESERT);
        const plantgen::Species* mq = plantgen::find("mesquite");
        double hi = 0;
        int n = 0;
        for (const Tree& t : w.trees)
            if (mq && t.species == mq->id) hi = std::max(hi, t.height), n++;
        // (the desert wilds plant trees 3.5 to 6.5 m: a mesquite stands 0.55 of that)
        ctx.check("the desert's mesquites are short trees", n > 0 && hi <= 6.5 * mq->v("stand_scale") + 0.01, std::to_string(hi));
        // The look switch changes nothing of the world.
        plantgen::EverywhereHere classicLook(false);
        World w2 = wilds(WILDS_DESERT);
        bool same = w2.trees.size() == w.trees.size() && w2.poles.size() == w.poles.size() && w2.landHash() == w.landHash();
        for (size_t i = 0; same && i < w.trees.size(); i++) same = w.trees[i].species == w2.trees[i].species && w.trees[i].height == w2.trees[i].height;
        for (size_t i = 0; same && i < w.poles.size(); i++) same = w.poles[i].r == w2.poles[i].r && w.poles[i].y1 == w2.poles[i].y1;
        ctx.check("--classic-plants builds the same world: the same species, heights and Poles", same);
    }
}

SCENARIO(procPlantsAreas, "procplants/biome-areas", "The plant biomes are areas the maps' builders mark, not a noise: each kind of wilds grows its own palette, the Backcountry's districts and the big map's regions theirs, the Frontier's desert and the Mountain's belts") {
    static const int kWildsZone[WILDS_COUNT] = {PZ_TEMPERATE, PZ_HOT_DESERT, PZ_MONTANE, PZ_HIGH_DESERT, PZ_RIPARIAN, PZ_SUBALPINE};
    static const char* const names[WILDS_COUNT] = {"meadow", "desert", "forest", "canyon", "wetland", "upland"};
    // (Of a map's trees, the share whose species is in `zone`'s palette: a tree by water is riparian, one on snow subalpine, and a
    // palette's dead trees are in several, so never quite all or none.)
    auto share = [&](const World& w, int zone, double x0, double z0, double x1, double z1, int* count = nullptr) {
        const std::set<int> ids = standTrees(zone);
        int n = 0, in = 0;
        for (const Tree& t : w.trees)
            if (t.base.x >= x0 && t.base.x <= x1 && t.base.z >= z0 && t.base.z <= z1 && t.kind != forest::kGenKind) n++, in += ids.count(t.species) > 0;
        if (count) *count = n;
        return n ? (double)in / n : 0.0;
    };
    for (int kind = 0; kind < WILDS_COUNT; kind++) {
        const World w = wilds(kind);
        int zoned = 0, all = 0, n = 0;
        for (double x = -560; x <= 560; x += 40)
            for (double z = -560; z <= 560; z += 40) all++, zoned += w.plantZone(x, z) == kWildsZone[kind];
        ctx.check(std::string(names[kind]) + " wilds: marked " + plantZoneName(kWildsZone[kind]) + " all over", zoned == all);
        const double s = share(w, kWildsZone[kind], -1e9, -1e9, 1e9, 1e9, &n);
        ctx.metric(std::string(names[kind]) + " wilds: trees of its palette's species", s, "", 0.97, 1.0);
        ctx.check(std::string(names[kind]) + " wilds: has trees", n > 20, std::to_string(n));
    }
    {
        // The desert wilds grow desert trees and no forest ones; the canyon's are the plateau's.
        const World d = wilds(WILDS_DESERT), c = wilds(WILDS_CANYON);
        std::set<std::string> desert, canyon;
        for (const Tree& t : d.trees)
            if (const plantgen::Species* sp = plantgen::treeById(t.species)) desert.insert(sp->key);
        for (const Tree& t : c.trees)
            if (const plantgen::Species* sp = plantgen::treeById(t.species)) canyon.insert(sp->key);
        ctx.check("a desert level grows mesquite, palo verde or catclaw", desert.count("mesquite") + desert.count("palo_verde") + desert.count("catclaw") > 0);
        ctx.check("...and no fir, oak or ponderosa", desert.count("fir") + desert.count("oak") + desert.count("ponderosa") == 0);
        ctx.check("a canyon level grows juniper and pinyon", canyon.count("juniper") && canyon.count("pinyon"));
        ctx.check("...and no saguaro, fir or oak", canyon.count("saguaro") + canyon.count("fir") + canyon.count("oak") == 0);
    }
    {
        // The Backcountry (the big map): its districts and the regions' country.
        const World w(MAP_BACKCOUNTRY);
        ctx.check("the big map is marked all over", !w.plantZones.empty() && std::count(w.plantZones.begin(), w.plantZones.end(), (uint8_t)PZ_NONE) == 0);
        struct Spot {
            const char* what;
            double x, z;
            int zone;
        };
        const Spot spots[] = {
            {"Elk Valley's woods", -54, -250, PZ_TEMPERATE},
            {"the dune bowl", 388, -225, PZ_HOT_DESERT},
            {"Timber Basin (north-west: prairie by the old noise)", -1842, 2460, PZ_MONTANE},
            {"Timber Basin", -2000, 2000, PZ_MONTANE},
            {"Ranger Flats (west)", -2000, 0, PZ_PRAIRIE},
            {"Badlands Wash (south-east)", 2000, -2000, PZ_HIGH_DESERT},
            {"the Lakeshore (south-west)", -1500, -2400, PZ_TEMPERATE},
            {"the Frontier's badlands (its grown country)", 1500, -4400, PZ_HIGH_DESERT},
            {"the Frontier's own desert (its old map's west)", 1500 - 450, -3496 - 150, PZ_HOT_DESERT},
            {"the Open Country's canyons (south)", -4000, -2400, PZ_HIGH_DESERT},
            {"the Open Country's meadows", -4300, 0, PZ_PRAIRIE},
            {"the Open Country's wooded hills (north)", -4000, 2200, PZ_TEMPERATE},
        };
        for (const Spot& sp : spots) ctx.check(std::string(sp.what) + ": marked " + plantZoneName(sp.zone), w.plantZone(sp.x, sp.z) == sp.zone, plantZoneName(w.plantZone(sp.x, sp.z)));
        // What grows there is that palette's (a square round each spot; water, snow and the borders' mixing leave a few of others).
        struct Area {
            const char* what;
            double x, z, half;
            int zone;
            double least;
        };
        const Area areas[] = {
            {"Timber Basin", -2000, 2000, 300, PZ_MONTANE, 0.85},
            {"Ranger Flats", -2000, 0, 300, PZ_PRAIRIE, 0.7},
            {"the Frontier's badlands", 1500, -4400, 400, PZ_HIGH_DESERT, 0.85},
            {"the Open Country's canyons", -4000, -2400, 400, PZ_HIGH_DESERT, 0.85},
            {"the Open Country's meadows", -4300, 0, 400, PZ_PRAIRIE, 0.7},
        };
        for (const Area& a : areas) {
            int n = 0;
            const double s = share(w, a.zone, a.x - a.half, a.z - a.half, a.x + a.half, a.z + a.half, &n);
            ctx.metric(std::string(a.what) + ": trees of " + plantZoneName(a.zone) + " (" + std::to_string(n) + " trees)", s, "", a.least, 1.0);
            ctx.check(std::string(a.what) + ": trees stand there", n >= 5, std::to_string(n));
        }
        // No prairie in the Backcountry's own forest districts (it was a noise's guess there).
        {
            int n = 0;
            const double prairie = share(w, PZ_PRAIRIE, -2600, 1200, 2600, 2600, &n) - share(w, PZ_MONTANE, -2600, 1200, 2600, 2600);
            ctx.check("the northern forests are forest, not prairie", n > 1000 && prairie < 0, std::to_string(n));
        }
        // How much of the map each biome is, and where most of its trees stand (a 128 m square: somewhere to go and look).
        size_t count[PZ_COUNT] = {};
        for (uint8_t z : w.plantZones) count[z]++;
        for (int z = 1; z < PZ_COUNT; z++) ctx.note(std::string(plantZoneName(z)) + ": " + std::to_string((int)std::lround(1000.0 * count[z] / w.plantZones.size()) / 10.0).substr(0, 4) + "% of the big map; " + densest(w, z));
    }
    for (int map : {MAP_FRONTIER, MAP_MOUNTAIN}) {
        const World w(map);
        for (int z = 1; z < PZ_COUNT; z++) ctx.note(std::string(mapName(map)) + " (the old map, --map " + std::to_string(map) + "), " + plantZoneName(z) + ": " + densest(w, z));
    }
    {
        const World w = wilds(WILDS_DESERT);
        ctx.note(std::string("a desert level's wilds, hot_desert: ") + densest(w, PZ_HOT_DESERT));
    }
    {
        const World f(MAP_FRONTIER), m(MAP_MOUNTAIN);
        ctx.check("the Frontier's west is hot desert", f.plantZone(-450, 0) == PZ_HOT_DESERT && f.plantZone(-400, -300) == PZ_HOT_DESERT);
        ctx.check("the Frontier's bayou is riparian, its range mountain forest", f.plantZone(318, -415) == PZ_RIPARIAN && (f.plantZone(0, 400) == PZ_MONTANE || f.plantZone(0, 400) == PZ_SUBALPINE));
        ctx.check("the Frontier's south meadows are broadleaf woods", f.plantZone(0, -300) == PZ_TEMPERATE);
        size_t count[PZ_COUNT] = {};
        for (uint8_t z : m.plantZones) count[z]++;
        ctx.check("the Mountain: valley woods, mountain forest and the treeline's belt", count[PZ_TEMPERATE] > 0 && count[PZ_MONTANE] > 0 && count[PZ_SUBALPINE] > 0 && count[PZ_NONE] == 0);
    }
}

SCENARIO(procPlantsProcWorld, "procplants/procedural-world", "The procedural world's plant biomes are its own climate's: mesa country grows the plateau's high desert, the dunes hot desert, the plains prairie; and a tree or a bush is the same species with the same trunk whichever window of the world builds it") {
    const uint64_t seed = 1;
    const proc::ProcWorld& G = proc::ProcWorld::forSeed(seed);
    int64_t sx, sz;
    double yaw;
    G.start(sx, sz, yaw);
    struct Want {
        int biome, zone;
        const char* what;
        double gx = 0, gz = 0;
        bool found = false;
    };
    Want wants[3] = {{proc::PB_BADLANDS, PZ_HIGH_DESERT, "mesa country"}, {proc::PB_DUNES, PZ_HOT_DESERT, "the dunes"}, {proc::PB_MEADOW, PZ_PRAIRIE, "the plains"}};
    // (The nearest place out from the start where the climate is clearly each one's.)
    for (double r = 500; r <= 7000; r += 250)
        for (int k = 0; k < 36; k++) {
            const double gx = (double)sx + r * std::cos(k * 0.1745329), gz = (double)sz + r * std::sin(k * 0.1745329);
            if (wants[0].found && wants[1].found && wants[2].found) break;
            const proc::Macro m = G.macroAt(gx, gz, false);
            for (Want& w : wants)
                if (!w.found && m.w[w.biome] > 0.92 && (w.biome != proc::PB_MEADOW || m.wet < 0.54)) w.found = true, w.gx = gx, w.gz = gz;
        }
    auto window = [&](double gx, double gz) {
        World::procSeed = seed, World::procOriginSet = true, World::procOriginX = (int64_t)gx, World::procOriginZ = (int64_t)gz;
        World ww(MAP_PROCEDURAL);
        World::procOriginSet = false;
        return ww;
    };
    auto shrubHash = [](const World& w, const Bush& b) { return (uint32_t)(1 + (int)(forest::plantHash(w, b.p.x, b.p.z) & 0x3FFFFFFF)) * 2654435761u ^ 0x27d4eb2fu; };  // (forest::buildBushes')
    for (const Want& want : wants) {
        ctx.check(std::string(want.what) + ": found within 7 km of seed 1's start", want.found);
        if (!want.found) continue;
        const World w = window(want.gx, want.gz);
        const double lx = want.gx - (double)w.proc.ox, lz = want.gz - (double)w.proc.oz;
        char b[200];
        std::snprintf(b, sizeof b, "%s: seed %llu, global %.0f,%.0f (window origin %lld,%lld, local %.0f,%.0f)", want.what, (unsigned long long)seed, want.gx, want.gz, (long long)w.proc.ox,
                      (long long)w.proc.oz, lx, lz);
        ctx.note(b);
        ctx.check(std::string(want.what) + ": marked " + plantZoneName(want.zone), w.plantZone(lx, lz) == want.zone, plantZoneName(w.plantZone(lx, lz)));
        ctx.check(std::string(want.what) + ": the window is marked all over", !w.plantZones.empty() && std::count(w.plantZones.begin(), w.plantZones.end(), (uint8_t)PZ_NONE) == 0);
        // What stands on ground marked so, within 600 m: that palette's trees (but for those by water or on snow, and the borders' mix).
        const std::set<int> ids = standTrees(want.zone);
        int n = 0, in = 0, shrubs = 0, shrubsIn = 0;
        for (const Tree& t : w.trees)
            if (std::hypot(t.base.x - lx, t.base.z - lz) < 600 && w.plantZone(t.base.x, t.base.z) == want.zone) n++, in += ids.count(t.species) > 0;
        for (const Bush& bu : w.bushes)
            if (std::hypot(bu.p.x - lx, bu.p.z - lz) < 600 && w.plantZone(bu.p.x, bu.p.z) == want.zone) shrubs++, shrubsIn += forest::plantBiomeAt(w, bu.p.x, bu.p.z, shrubHash(w, bu)) == want.zone;
        std::snprintf(b, sizeof b, "%s: %d trees on its ground within 600 m, %d of its palette's species; %d bushes, %d in its biome", want.what, n, in, shrubs, shrubsIn);
        ctx.note(b);
        if (n >= 10) ctx.metric(std::string(want.what) + ": trees of " + plantZoneName(want.zone), (double)in / n, "", 0.8, 1.0);
        if (shrubs >= 10) ctx.metric(std::string(want.what) + ": bushes drawn from " + plantZoneName(want.zone), (double)shrubsIn / shrubs, "", 0.8, 1.0);
        ctx.check(std::string(want.what) + ": something grows there", n + shrubs >= 10, std::to_string(n + shrubs));
        if (want.biome != proc::PB_BADLANDS) continue;
        // Another window over the same land, a slide on: every tree and bush both hold is the same there.
        const World w2 = window(want.gx + 512, want.gz + 512);
        auto key = [](const World& ww, double x, double z) {
            return std::make_pair((long long)std::llround((x + (double)ww.proc.ox) * 100), (long long)std::llround((z + (double)ww.proc.oz) * 100));
        };
        std::map<std::pair<long long, long long>, const Tree*> first;
        std::map<std::pair<long long, long long>, int> firstBush;
        for (const Tree& t : w.trees) first[key(w, t.base.x, t.base.z)] = &t;
        for (const Bush& bu : w.bushes) firstBush[key(w, bu.p.x, bu.p.z)] = forest::plantBiomeAt(w, bu.p.x, bu.p.z, shrubHash(w, bu));
        int both = 0, differ = 0, poleDiffer = 0, bothBush = 0, bushDiffer = 0;
        std::map<std::pair<long long, long long>, double> poleR;
        for (const Pole& p : w.poles) poleR[key(w, p.x, p.z)] = p.r;
        std::map<std::pair<long long, long long>, double> poleR2;
        for (const Pole& p : w2.poles) poleR2[key(w2, p.x, p.z)] = p.r;
        for (const Tree& t : w2.trees) {
            const auto k = key(w2, t.base.x, t.base.z);
            auto it = first.find(k);
            if (it == first.end()) continue;
            both++;
            differ += it->second->species != t.species || it->second->height != t.height || it->second->kind != t.kind || it->second->design != t.design;
            poleDiffer += !poleR.count(k) || !poleR2.count(k) || poleR[k] != poleR2[k];
        }
        for (const Bush& bu : w2.bushes) {
            auto it = firstBush.find(key(w2, bu.p.x, bu.p.z));
            if (it == firstBush.end()) continue;
            bothBush++, bushDiffer += it->second != forest::plantBiomeAt(w2, bu.p.x, bu.p.z, shrubHash(w2, bu));
        }
        ctx.check("two windows 512 m apart share thousands of trees and bushes", both > 1000 && bothBush > 1000, std::to_string(both) + " trees, " + std::to_string(bothBush) + " bushes");
        ctx.check("...each tree the same species and height in both", differ == 0, std::to_string(differ) + " of " + std::to_string(both));
        ctx.check("...with the same Pole", poleDiffer == 0, std::to_string(poleDiffer));
        ctx.check("...each bush in the same biome in both", bushDiffer == 0, std::to_string(bushDiffer) + " of " + std::to_string(bothBush));
    }
}

SCENARIO(procPlantsBiomes, "procplants/biomes", "Each biome has its palette file: it parses, names only species there are, says what every classic tree kind and bush is drawn as there, with dead ones; the default draws them and --classic-plants none") {
    static const char* const files[] = {"hot_desert", "colorado_plateau_desert", "dry_prairie", "temperate_forest", "riparian_wetland", "montane_forest", "subalpine"};
    std::set<std::string> used;
    for (const char* name : files) {
        std::ifstream f(plantgen::dir() + "/../maps/vegetation/" + name + ".txt");
        ctx.check(std::string(name) + ": the file is there", (bool)f);
        if (!f) continue;
        std::stringstream ss;
        ss << f.rdbuf();
        veg::Palette p;
        std::string err;
        ctx.check(std::string(name) + ": parses", veg::parse(ss.str(), p, &err), err);
        ctx.check(std::string(name) + ": generated species", p.procedural);
        for (const char* key : {"pine", "broadleaf", "snag", "bush", "dead", "deadbush"}) ctx.check(std::string(name) + ": a stand list for " + key, p.stand.count(key) && !p.stand[key].empty());
        ctx.check(std::string(name) + ": a share of it dead, a share dry", p.dead > 0 && p.dead < 0.5 && p.dry >= 0 && p.dry <= 1);
        for (const auto& kv : p.stand)
            for (const veg::Palette::Stand& s : kv.second) used.insert(s.species);
        size_t zoned = 0;
        for (const veg::ZoneRule& z : p.zone) zoned += z.trees.size() + z.shrubs.size();
        ctx.check(std::string(name) + ": zones for a real map", zoned >= 8, std::to_string(zoned));
    }
    ctx.metric("species the biomes draw", (double)used.size(), "", 30, 400);
    for (const char* want : {"creosote", "mesquite", "palo_verde", "catclaw", "ocotillo", "cholla", "barrel_cactus", "dry_grass", "dead_shrub", "tumbleweed", "snag", "maple", "fern", "tall_grass", "stump", "cattail", "manzanita", "deadfall", "krummholz", "alpine_mat"})
        ctx.check(std::string(want) + " grows in some biome", used.count(want) > 0);
    // The species' text is hashed (a map's hash takes it in where its palette grows them).
    ctx.check("the species files have a hash", plantgen::textHash() != 0 && plantgen::textHash() == plantgen::textHash());
    // A desert tree stands shorter than the classic tree it is planted as (its Pole is its own stem's: procplants/stand-ins).
    const plantgen::Species* mq = plantgen::find("mesquite");
    ctx.check("a desert tree stands short", mq && mq->v("stand_scale") < 0.8);
}

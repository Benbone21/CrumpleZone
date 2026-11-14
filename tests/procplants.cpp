// The plant generator (plantgen.h): every species file loads and grows, the same mesh from the same seed, each far copy within its
// budget and lighter than the one before, trees with wood where their collider stands, a palette's species all there, and the
// classic maps drawn with them (--proc-plants) keep their colliders.
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <fstream>
#include <set>
#include <sstream>

#include "../src/forest.h"
#include "../src/modelkit.h"
#include "../src/plantgen.h"
#include "../src/plants.h"
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
    ctx.check("pinned: seed 7, far", h1 == 0xb1827e7f3f7f5452ull);
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

SCENARIO(procPlantsSwitch, "procplants/switch", "Classic maps: off (the default) nothing is generated; with --proc-plants every tree a species replaces is drawn generated at the tree's own height on the classic trunk's collider, and the replaced bushes too") {
    World w(MAP_BACKCOUNTRY);
    forest::Set off, on, bOff, bOn;
    {
        plantgen::EverywhereHere sw(false);
        forest::build(w, false, false, off);
        forest::buildBushes(w, bOff);
    }
    size_t gen = 0;
    for (const forest::Shape& s : off.shapes) gen += s.look.design >= forest::kGenDesign;
    for (const forest::Shape& s : bOff.shapes) gen += s.look.kind >= plantgen::kShrubKindBase;
    ctx.check("off: only the classic models", gen == 0, std::to_string(gen));
    {
        plantgen::EverywhereHere sw(true);
        forest::build(w, false, false, on);
        forest::buildBushes(w, bOn);
    }
    ctx.check("the same trees and bushes either way", on.inst.size() == off.inst.size() && bOn.inst.size() == bOff.inst.size());
    size_t classic = 0, genBush = 0;
    for (const forest::Shape& s : on.shapes) classic += s.look.design < forest::kGenDesign;
    for (const forest::Shape& s : bOn.shapes) genBush += s.look.kind >= plantgen::kShrubKindBase;
    ctx.check("on: every tree look is a generated species", classic == 0, std::to_string(classic) + " classic");
    ctx.check("on: the bushes are generated (a kind no palette or species covers stays classic)", genBush > 0, std::to_string(genBush) + " of " + std::to_string(bOn.shapes.size()));
    // Each tree as tall as it was, its drawn trunk about its (unchanged) Pole.
    std::set<std::pair<int, int>> seen;
    double worstTall = 0;
    for (size_t i = 0; i < on.inst.size(); i++) {
        const forest::Inst& a = on.inst[i];
        const forest::Shape& s = on.shapes[a.shape];
        const Tree& t = w.trees[a.tree];
        const plantgen::Species* sp = plantgen::treeById(s.look.design - forest::kGenDesign);
        const double stand = sp ? sp->v("stand_scale") : 1;  // (a desert or treeline tree stands short of the classic one)
        worstTall = std::max(worstTall, std::fabs(s.top * a.scale - t.height * stand) / (t.height * stand));
        if (!seen.insert({s.look.kind, s.look.design}).second) continue;
        // (a single trunk that forks above where the Pole is measured: a low desert tree's limbs are not a cylinder to compare)
        // (nor a short stand-in: its believable trunk is thinner than the tall classic tree's Pole, which it hides in its crown)
        if (!sp || (int)sp->v("stems", 1) > 1 || stand < 0.7 || 1.2 / a.scale > 5.0 * sp->v("trunk_len")) continue;
        const Pole* pole = nullptr;
        for (const Pole& p : w.poles)
            if (std::hypot(p.x - t.base.x, p.z - t.base.z) < 0.01) pole = &p;
        if (!pole) continue;
        const double drawn = woodRadius(s.lod[0], 1.2 / a.scale) * a.scale;
        char name[96];
        std::snprintf(name, sizeof name, "kind %d as species %d, %.0f m tall: pole over the drawn trunk", s.look.kind, s.look.design - forest::kGenDesign, t.height);
        if (drawn > 0) ctx.metric(name, pole->r / drawn, "", 0.6, 1.6);
    }
    ctx.metric("generated trees: height off the tree's own", worstTall, "", 0, 0.12);
    ctx.metric("species drawn for the classic trees", (double)seen.size(), "", 5, 40);
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
    // A desert stand-in is drawn shorter than the classic tree it stands for; its Pole is the classic tree's.
    const plantgen::Species* mq = plantgen::find("mesquite");
    ctx.check("a desert tree stands short", mq && mq->v("stand_scale") < 0.8);
}

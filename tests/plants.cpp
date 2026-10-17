// Plants (plants.h): every kind has its design in the bush model, each biome grows only its own kinds, a bush's kind is saved
// with the level (and an older file's bushes load as the leafy shrub), the editor can take a map's bush as an object of its kind,
// and no plant is anything a truck can hit.
#include <cmath>
#include <cstdio>
#include <set>

#include "../src/editcore.h"
#include "../src/level.h"
#include "../src/modelkit.h"
#include "harness.h"

namespace {
Level roundTrip(const Level& l) {
    Level back;
    parseLevelText(saveLevelText(l), back);
    return back;
}
}  // namespace

SCENARIO(plantsKinds, "plants/kinds", "Every plant kind is a design of the bush model that builds and fits its size, and grows somewhere; each habitat's mix only picks kinds that grow there") {
    const mk::ModelDef* d = mk::findModel("bush");
    ctx.check("the bush model is there with a design per kind", d && (int)d->variants.size() == BUSH_KIND_COUNT, d ? std::to_string(d->variants.size()) : "none");
    for (int k = 0; k < BUSH_KIND_COUNT; k++) {
        const PlantInfo& pi = plantInfo(k);
        ctx.check(std::string(pi.name) + ": a sane size range", pi.rMin > 0.2 && pi.rMin < pi.rDefault && pi.rDefault < pi.rMax && pi.rMax < 2 && pi.ry > 0 && pi.rx > 0);
        bool somewhere = false;
        for (int h = 0; h < HAB_COUNT; h++) somewhere |= plantGrowsIn(h, k);
        ctx.check(std::string(pi.name) + ": grows somewhere", somewhere);
        if (!d || k >= (int)d->variants.size()) continue;
        // The design stands about as wide as its radius says (built 0.8 m in radius), and its height fits the pick ellipsoid.
        mk::Mesh m;
        mk::build(*d, k, mk::defaults(d->variants[k]), m);
        m.finish();
        const double wide = std::max(m.hi.x - m.lo.x, m.hi.z - m.lo.z) / 2, high = m.hi.y;
        ctx.check(std::string(pi.name) + ": wide as its radius, its pick ellipsoid about as tall", wide > 0.25 && wide < 1.3 && high < pi.cy * 0.8 * 2.4 + 0.6 && high > pi.cy * 0.8 * 0.5, std::to_string(wide) + " x " + std::to_string(high));
    }
    for (int h = 0; h < HAB_COUNT; h++)
        for (int edge = 0; edge < 2; edge++) {
            std::set<int> seen;
            bool only = true;
            for (int i = 0; i < 2000; i++) {
                const int k = plantKindFor(h, edge != 0, (i + 0.5) / 2000.0);
                seen.insert(k);
                only &= plantGrowsIn(h, k);
            }
            ctx.check("habitat " + std::to_string(h) + (edge ? " edge" : " open") + ": only its own kinds, and a mix of them", only && seen.size() >= 4, std::to_string(seen.size()) + " kinds");
        }
    ctx.check("deserts have cacti and no ferns, marshes reeds and no cacti", plantGrowsIn(HAB_DESERT, BUSH_CACTUS) && !plantGrowsIn(HAB_DESERT, BUSH_FERN) && plantGrowsIn(HAB_WETLAND, BUSH_REEDS) && !plantGrowsIn(HAB_WETLAND, BUSH_CACTUS));
}

SCENARIO(plantsScatter, "plants/scatter", "Each kind of country scatters only its own plants (the wilds, the map builders), more than one kind, all of them soft") {
    for (int kind = 0; kind < WILDS_COUNT; kind++) {
        World::wildsKind = kind;
        World w(MAP_WILDS);
        World::wildsKind = WILDS_MEADOW;
        std::set<int> seen;
        bool only = true;
        for (const Bush& b : w.bushes) seen.insert(b.kind), only &= plantGrowsIn(kind, b.kind) && b.r > 0.25 && b.r <= plantInfo(b.kind).rMax + 1e-9;
        ctx.check(std::string(wildsKindName(kind)) + ": " + std::to_string(w.bushes.size()) + " bushes, only its own kinds", !w.bushes.empty() && only);
        ctx.check(std::string(wildsKindName(kind)) + ": a mix of at least four kinds", seen.size() >= 4, std::to_string(seen.size()));
        if (kind == WILDS_DESERT) ctx.check("the desert has cacti and sage", seen.count(BUSH_CACTUS) && seen.count(BUSH_SAGE) && !seen.count(BUSH_FERN));
        if (kind == WILDS_WETLAND) ctx.check("the marsh has reeds", seen.count(BUSH_REEDS) > 0);
        if (kind == WILDS_FOREST) ctx.check("the forest has ferns and no cacti", seen.count(BUSH_FERN) && !seen.count(BUSH_CACTUS));
    }
    // The map builders: every kind they grow is a real one, and the mix is there.
    for (int map : {(int)MAP_PROVING_GROUND, (int)MAP_MOUNTAIN, (int)MAP_FREEWAY, (int)MAP_FRONTIER, (int)MAP_BACKCOUNTRY}) {
        World w(map);
        std::set<int> seen;
        bool real = true;
        for (const Bush& b : w.bushes) seen.insert(b.kind), real &= b.kind >= 0 && b.kind < BUSH_KIND_COUNT;
        ctx.check("map " + std::to_string(map) + ": " + std::to_string(w.bushes.size()) + " bushes of real kinds, several of them", real && seen.size() >= 3, std::to_string(seen.size()));
    }
    // Soft: planting adds no collider (a pole, a box, a rock).
    World w(MAP_WILDS);
    LevelObject o;
    o.kind = OBJ_BUSH, o.p = V3(30, 0, 30), o.size = V3(0.7, 0, 0), o.sub = BUSH_CACTUS, o.col[0] = 1;
    Level lv;
    lv.map = MAP_WILDS;
    lv.objects.push_back(o);
    const size_t poles = w.poles.size(), boxes = w.boxes.size(), rocks = w.rocks.size();
    applyLevel(lv, w);
    ctx.check("a cactus is scenery only", w.poles.size() == poles && w.boxes.size() == boxes && w.rocks.size() == rocks && !w.bushes.empty() && w.bushes.back().kind == BUSH_CACTUS);
}

SCENARIO(plantsLevel, "plants/level-kind", "A bush's kind is saved with the level and comes back; an older file's #bush lines (no kind) load as the leafy shrub; the editor takes a map's bush as its kind") {
    Level lv;
    lv.map = MAP_WILDS;
    for (int k = 0; k < BUSH_KIND_COUNT; k++) {
        LevelObject o;
        o.kind = OBJ_BUSH, o.p = V3(20 + 3 * k, 0, 40), o.size = V3(plantInfo(k).rDefault, 0, 0), o.col[0] = 1.1f, o.sub = k, o.lift = k == 2 ? 0.5 : 0;
        lv.objects.push_back(o);
    }
    const std::string text = saveLevelText(lv);
    const Level back = roundTrip(lv);
    bool same = back.objects.size() == lv.objects.size();
    for (size_t i = 0; same && i < back.objects.size(); i++) same = back.objects[i].kind == OBJ_BUSH && back.objects[i].sub == lv.objects[i].sub && std::fabs(back.objects[i].size.x - lv.objects[i].size.x) < 1e-3 && std::fabs(back.objects[i].lift - lv.objects[i].lift) < 1e-3;
    ctx.check("every kind round-trips through save and load", same);
    ctx.check("the leafy shrub's line is the old one (no kind on it)", text.find("#bush 20.00 40.00 0.900 1.10 0.000\n") != std::string::npos, text.substr(text.find("#bush"), 40));
    // A level made before kinds: with and without the lift.
    Level old;
    parseLevelText("CRUMPLEZONE LEVEL 1\nname OLD\nmap 5\n#bush 10 20 1.2 1.0 0.5\n#bush 11 21 0.8 0.9\n", old);
    ctx.check("older #bush lines are leafy shrubs", old.objects.size() == 2 && old.objects[0].kind == OBJ_BUSH && old.objects[0].sub == 0 && old.objects[1].sub == 0 && std::fabs(old.objects[0].lift - 0.5) < 1e-9 && std::fabs(old.objects[1].size.x - 0.8) < 1e-9);
    Level bad;
    parseLevelText("CRUMPLEZONE LEVEL 1\nname BAD\nmap 5\n#bush 10 20 1.2 1.0 0.5 99\n", bad);
    ctx.check("a kind this build hasn't got is the leafy shrub", bad.objects.size() == 1 && bad.objects[0].sub == 0);
    // Played: the world's bush has the kind.
    Level play;
    play.map = MAP_WILDS;
    LevelObject o;
    o.kind = OBJ_BUSH, o.p = V3(60, 0, 60), o.size = V3(0.8, 0, 0), o.col[0] = 1, o.sub = BUSH_YUCCA;
    play.objects.push_back(o);
    World w = levelWorld(play);
    applyLevel(play, w);
    ctx.check("the played bush is the yucca", !w.bushes.empty() && w.bushes.back().kind == BUSH_YUCCA && std::fabs(w.bushes.back().r - 0.8) < 1e-9);
    // The editor: a map's own fern, picked, is a fern object (moving it keeps its kind).
    World ew(MAP_WILDS);
    EditCore ed;
    Level el;
    el.map = MAP_WILDS;
    ed.begin(ew, el);
    ew.bushes.push_back(growBush(V3(80, ew.height(80, 80), 80), HAB_FOREST, true, 0.5, 1.0f));
    ew.bushes.back().kind = BUSH_FERN;
    EditPick pick;
    pick.kind = EditPick::BASE, pick.baseKind = 6, pick.index = (int)ew.bushes.size() - 1, pick.at = ew.bushes.back().p;
    LevelObject got;
    ctx.check("a map's fern becomes a fern object", ed.asObject(pick, got) && got.kind == OBJ_BUSH && got.sub == BUSH_FERN);
}

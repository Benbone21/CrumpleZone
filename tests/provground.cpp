// The proving ground's section park shows the player's saved edits of the section levels: save an edit of CLIFF SHELF and the next
// proving ground has it (terrain, rocks), an unedited level matches the built-in one, and deleting the edit brings the built-in back.
#include <cmath>
#include <cstdio>

#include "../src/editcore.h"
#include "../src/level.h"
#include "../src/platform.h"
#include "../src/sectionfile.h"
#include "harness.h"

namespace {

bool fileExists(const std::string& p) {
    FILE* f = std::fopen(p.c_str(), "rb");
    if (f) std::fclose(f);
    return f != nullptr;
}

struct Sig {
    double hsum = 0, hmax = 0;  // heights relative to the entry over the section's ground
    size_t rocks = 0;
};

const Level* builtinNamed(const char* name) {
    for (const Level& l : builtinLevels())
        if (l.name == name) return &l;
    return nullptr;
}

// The ground along and beside a section's trail (relative to its entry) and the rocks within its canvas.
Sig sigOf(const World& w, const Level& lv, SectionType t, double half = 6) {
    Sig s;
    const World::SectionSpot* sp = nullptr;
    for (const auto& q : w.sections)
        if (q.type == (int)t) sp = &q;
    if (!sp) return s;
    const V3 dir(std::cos(sp->heading), 0, -std::sin(sp->heading)), left(-dir.z, 0, dir.x);
    for (double u = -10; u <= 120; u += 2)
        for (double v = -half; v <= half; v += 1.5) {
            const V3 p = sp->entry + dir * u + left * v;
            const double h = w.height(p.x, p.z) - sp->entry.y;
            s.hsum += h, s.hmax = std::max(s.hmax, std::fabs(h));
        }
    for (const Rock& r : w.rocks)
        if (std::hypot(r.c.x - sp->entry.x, r.c.z - sp->entry.z) < 140) s.rocks++;
    (void)lv;
    return s;
}

std::string cliffFile() { return userLevelFolder() + "/" + levelFileName("CLIFF SHELF"); }

void clean() {
    for (const char* e : {".czl", ".czs"}) std::remove((cliffFile() + e).c_str());
}

}  // namespace

SCENARIO(provGroundEdits, "levels/proving-ground-edits", "A saved edit of a section level shows in the next proving ground (same session), an unedited level matches the built-in one, deleting the edit brings the built-in back") {
    makeDir("build/test-config-pg");
    setEnvVar("XDG_CONFIG_HOME", "build/test-config-pg");
    makeDir(configDir());
    makeDir(userLevelFolder());
    clean();
    const bool showcase = World::withShowcase;
    World::withShowcase = true;
    const Level* cliff = builtinNamed("CLIFF SHELF");
    ctx.check("the built-in level is there", cliff != nullptr);
    if (!cliff) return;

    const World a(MAP_PROVING_GROUND);
    const Sig sa = sigOf(a, *cliff, SEC_CLIFF_SHELF);
    ctx.check("no edits: the park is the generator's", !a.parkEdited && a.sections.size() == SEC_COUNT);

    // Save the level untouched: the park is built from the file, and matches the built-in one.
    {
        Level own = *cliff;
        own.builtin = false, own.file.clear();
        World w = levelWorld(own);
        EditCore ed;
        ed.begin(w, own);
        ctx.check("saves", ed.save(cliffFile() + ".czl"), ed.saveNote);
        ctx.check("with its section file", fileExists(cliffFile() + ".czs"));
    }
    const World b(MAP_PROVING_GROUND);
    const Sig sb = sigOf(b, *cliff, SEC_CLIFF_SHELF);
    ctx.check("an unedited level is picked up from its file", b.parkEdited && b.editStamp == levelFilesStamp());
    ctx.check("and matches the built-in one", std::fabs(sb.hsum - sa.hsum) < 0.03 * std::fabs(sa.hsum) + 5 && std::fabs(sb.hmax - sa.hmax) < 0.5 && std::abs((int)sb.rocks - (int)sa.rocks) <= 6,
              std::to_string(sb.hsum) + "/" + std::to_string(sa.hsum) + " " + std::to_string(sb.rocks) + "/" + std::to_string(sa.rocks));

    // Edit it: a hill on the trail and a rock; save; the next build has both.
    const uint64_t stampBefore = levelFilesStamp();
    auto saveEdit = [&] {
        Level own = *cliff;
        own.builtin = false, own.file.clear();
        World w = levelWorld(own);
        EditCore ed;
        ed.begin(w, own);
        const double ex = own.section.x + 60, ez = own.section.z;  // (60 m into the canvas, on the trail)
        ed.brush(BRUSH_RAISE, ex, ez, 14, 6.0);
        ed.brush(BRUSH_RAISE, ex + 10, ez + 30, 12, 14.0);  // (a hill beside it, to see from a distance)
        LevelObject r;
        r.kind = OBJ_ROCK, r.p = V3(ex + 20, 0, ez + 8), r.size = V3(1.5, 1, 1.2), r.top = 1, r.seed = 5, r.sub = 1;
        ed.addObject(r);
        ctx.check("saves the edit", ed.save(cliffFile() + ".czl"), ed.saveNote);
    };
    saveEdit();
    ctx.check("the save is noticed (caches: the stamp)", levelFilesStamp() > stampBefore);
    const World c(MAP_PROVING_GROUND);
    const Sig sc = sigOf(c, *cliff, SEC_CLIFF_SHELF);
    ctx.check("the edit shows: the ground moved", c.parkEdited && std::fabs(sc.hsum - sa.hsum) > 40, std::to_string(sc.hsum) + " vs " + std::to_string(sa.hsum));
    ctx.check("the edit shows: the rock is there", sc.rocks == sb.rocks + 1, std::to_string(sc.rocks) + " vs " + std::to_string(sb.rocks));
    // Other sections untouched.
    const Sig ha = sigOf(a, *cliff, SEC_TIMBER), hc = sigOf(c, *cliff, SEC_TIMBER);
    ctx.check("the other sections are as they were", std::fabs(ha.hsum - hc.hsum) < 1e-6 && ha.rocks == hc.rocks);

    // Delete the edit: the built-in comes back.
    clean();
    const World d(MAP_PROVING_GROUND);
    const Sig sd = sigOf(d, *cliff, SEC_CLIFF_SHELF);
    ctx.check("deleting the edit brings the built-in back", !d.parkEdited && std::fabs(sd.hsum - sa.hsum) < 1e-6 && sd.rocks == sa.rocks);

    if (std::getenv("CZ_PG_KEEP")) {
        for (const auto& q : c.sections) std::printf("section %s entry %.1f %.1f heading %.2f\n", q.name.c_str(), q.entry.x, q.entry.z, q.heading);
        saveEdit();
    }  // (for a look at it: the edit is left in the scratch config folder)
    World::withShowcase = showcase;
    setEnvVar("XDG_CONFIG_HOME", nullptr);
}

// ---- The model yard: every design of every model, in rows by model, spaced by bounds ----------------------------------------------

#include "../src/modelkit.h"

SCENARIO(provGroundYard, "levels/model-yard", "The proving ground's model yard shows every design of every model, one row per model with its designs side by side, no two overlapping, all on the pad") {
    const bool showcase = World::withShowcase;
    World::withShowcase = true;
    const World w(MAP_PROVING_GROUND);
    World::withShowcase = showcase;
    size_t want = 0;
    for (const mk::ModelDef& d : mk::catalog())
        if (d.id != "fp_hand" && d.id != "lug_nut") want += std::max<size_t>(1, d.variants.size());
    size_t yard = 0;
    struct R {
        double x0, x1, z0, z1;
        std::string id;
        int variant;
    };
    std::vector<R> rs;
    for (const Placed& pl : w.models) {
        if (pl.variant < 0 || pl.yaw != -M_PI / 2 || pl.p.x > -20) continue;  // (the yard's: designs named, facing north, west of the spawn)
        const mk::ModelDef* d = mk::findModel(pl.id);
        if (!d || d->variants.empty()) continue;
        mk::Mesh m;
        mk::P pp = mk::pickedParams(*d, pl.variant);
        for (const auto& kv : pl.set) pp.v[kv.first] = kv.second;
        mk::build(*d, pl.variant, pp, m);
        rs.push_back({pl.p.x - m.hi.z, pl.p.x - m.lo.z, pl.p.z + m.lo.x, pl.p.z + m.hi.x, pl.id, pl.variant});
        yard++;
    }
    std::string missing;
    for (const mk::ModelDef& d : mk::catalog())
        for (int v = 0; v < (int)d.variants.size(); v++) {
            bool in = d.id == "fp_hand" || d.id == "lug_nut";
            for (const R& r : rs) in |= r.id == d.id && r.variant == v;
            if (!in) missing += d.id + "/" + std::to_string(v) + " ";
        }
    ctx.check("every design of every model is there", missing.empty(), missing);
    ctx.check("(count)", yard >= want - 8, std::to_string(yard) + " of " + std::to_string(want));
    int overlaps = 0;
    double minX = 1e9, minZ = 1e9;
    for (size_t i = 0; i < rs.size(); i++) {
        minX = std::min(minX, rs[i].x0), minZ = std::min(minZ, rs[i].z0);
        for (size_t j = i + 1; j < rs.size(); j++)
            if (rs[i].x0 < rs[j].x1 - 0.01 && rs[j].x0 < rs[i].x1 - 0.01 && rs[i].z0 < rs[j].z1 - 0.01 && rs[j].z0 < rs[i].z1 - 0.01) overlaps++;
    }
    ctx.check("no two overlap", overlaps == 0, std::to_string(overlaps));
    ctx.check("all on the pad", minX > -440 && minZ > -440, std::to_string(minX) + " " + std::to_string(minZ));
    ctx.metric("designs shown", (double)yard, "", 1, 5000);
    ctx.metric("yard west edge", minX, "m", -500, 0);
    ctx.metric("yard south edge", minZ, "m", -500, 0);
}

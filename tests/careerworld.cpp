// Career worlds: difficulty start cash, which vehicles can start a career, folder names.
#include <cstdio>
#include <fstream>
#include <sstream>

#include "../src/careerworld.h"
#include "../src/net.h"
#include "../src/platform.h"
#include "../src/vehicles.h"
#include "harness.h"

SCENARIO(careerWorldSettings, "career/world-settings", "Start cash by difficulty; the Jesko can't be a starter; world slugs are folder-safe, unique and short") {
    ctx.check("light starts with 2000", startCash(DIFF_LIGHT) == 2000);
    ctx.check("medium starts with 1000", startCash(DIFF_MEDIUM) == 1000);
    ctx.check("hardcore starts with 500", startCash(DIFF_HARDCORE) == 500);
    ctx.check("difficulty name", std::string(difficultyName(DIFF_HARDCORE)) == "HARDCORE");
    ctx.check("crew mode name", std::string(modeName(MODE_CREW)) == "SHARED CREW");
    ctx.check("personal mode name", std::string(modeName(MODE_PERSONAL)) == "PERSONAL");

    int jesko = -1;
    for (int v = 0; v < (int)vehicleCatalog().size(); v++)
        if (std::string(vehicleCatalog()[v].name) == "KOENIGSEGG JESKO") jesko = v;
    ctx.check("the catalog has the Jesko", jesko >= 0);
    ctx.check("the Jesko is not a starter", !startableVehicle(jesko));
    bool allOthers = true;
    for (int v : listedVehicles())
        if (v != jesko && !startableVehicle(v)) allOthers = false;
    ctx.check("every other listed vehicle is a starter", allOthers);

    ctx.check("plain name", worldSlug("My World", {}) == "my-world");
    ctx.check("taken gets -2", worldSlug("my world!", {"my-world"}) == "my-world-2");
    ctx.check("taken twice gets -3", worldSlug("my world!", {"my-world", "my-world-2"}) == "my-world-3");
    ctx.check("empty name", worldSlug("", {}) == "world");
    ctx.check("long name is cut", worldSlug(std::string(50, 'a'), {}).size() <= 32);
    ctx.check("long taken name stays short", worldSlug(std::string(50, 'a'), {std::string(32, 'a')}).size() <= 32);
}

namespace {

std::string freshDir(const std::string& name) {
    makeDir("build");
    makeDir("build/test-career");
    const std::string dir = "build/test-career/" + name;
    removeTree(dir);
    makeDir(dir);
    return dir;
}
std::string slurp(const std::string& path) {
    std::ifstream f(path, std::ios::binary);
    std::stringstream ss;
    ss << f.rdbuf();
    return ss.str();
}
void spit(const std::string& path, const std::string& text) {
    std::ofstream f(path, std::ios::binary);
    f << text;
}
bool same(const WorldSave& a, const WorldSave& b) {
    if (a.name != b.name || a.slug != b.slug || a.map != b.map || a.diff != b.diff || a.mode != b.mode || a.build != b.build ||
        a.opened != b.opened || a.playedSeconds != b.playedSeconds || a.damaged != b.damaged || a.left.size() != b.left.size())
        return false;
    for (size_t i = 0; i < a.left.size(); i++)
        if (a.left[i].owner != b.left[i].owner || a.left[i].vehicle != b.left[i].vehicle || a.left[i].condition != b.left[i].condition) return false;
    return true;
}

}  // namespace

SCENARIO(careerWorldSave, "career/world-save", "A world with left vehicles and opened caches saves, loads back equal and is listed") {
    ScopedCareerDir scope(freshDir("world-save"));
    WorldSave w = newWorld("My World", 2, DIFF_HARDCORE, MODE_CREW, {});
    w.left = {{"Ann", 3, "dent 3 0.5\nline two with \\ slash"}, {"Bo B", 1, ""}};
    w.opened = {"3:WALL TOP", "1:CLIFF"};
    w.playedSeconds = 1234.5;
    ctx.check("build is this release", w.build == net::kRelease);
    ctx.check("saves", saveWorld(w));
    WorldSave r;
    ctx.check("loads", loadWorld(w.slug, r));
    ctx.check("equal", same(w, r));
    const auto all = listWorlds();
    ctx.check("listed", all.size() == 1 && all[0].slug == w.slug && same(all[0], w));
}

SCENARIO(careerWorldDamaged, "career/world-damaged", "A garbage world.txt with no .bak is a damaged entry; with a good .bak it loads the .bak") {
    const std::string dir = freshDir("world-damaged");
    ScopedCareerDir scope(dir);
    WorldSave w = newWorld("Rough", 1, DIFF_LIGHT, MODE_PERSONAL, {});
    w.opened = {"a"};
    ctx.check("first save", saveWorld(w));
    w.opened = {"a", "b"};
    ctx.check("second save", saveWorld(w));
    const std::string path = dir + "/worlds/rough/world.txt";
    ctx.check("the .bak is the first", slurp(path + ".bak").find("cache b") == std::string::npos && !slurp(path + ".bak").empty());
    spit(path, "x#!\x01z");
    WorldSave r;
    ctx.check("loads the .bak", loadWorld("rough", r) && !r.damaged && r.opened.size() == 1 && r.name == "Rough");
    ctx.check("a save over garbage keeps the good .bak", saveWorld(w) && slurp(path + ".bak").find("name Rough") != std::string::npos);
    spit(path, "x#!\x01z");
    std::remove((path + ".bak").c_str());
    WorldSave d;
    ctx.check("both bad: false", !loadWorld("rough", d));
    ctx.check("and damaged", d.damaged && d.slug == "rough");
    const auto all = listWorlds();
    ctx.check("still listed, damaged", all.size() == 1 && all[0].damaged && all[0].slug == "rough");
    ctx.check("a missing slug is damaged too", !loadWorld("nope", d) && d.damaged);
    ctx.check("a path-like slug is refused", !loadWorld("../x", d) && !saveWorld([] { WorldSave s; s.slug = "../x"; return s; }()));
}

SCENARIO(careerWorldSlugs, "career/world-slugs", "Two worlds whose names slug the same get different folders; both are saved and listed") {
    ScopedCareerDir scope(freshDir("world-slugs"));
    WorldSave a = newWorld("My World", 0, DIFF_MEDIUM, MODE_PERSONAL, listWorlds());
    ctx.check("a saves", saveWorld(a));
    WorldSave b = newWorld("my world!", 0, DIFF_MEDIUM, MODE_PERSONAL, listWorlds());
    ctx.check("b saves", saveWorld(b));
    ctx.check("different slugs", a.slug != b.slug, a.slug + " " + b.slug);
    ctx.check("both listed", listWorlds().size() == 2);
}

SCENARIO(careerWorldDelete, "career/world-delete", "deleteWorld removes the folder and its listing") {
    const std::string dir = freshDir("world-delete");
    ScopedCareerDir scope(dir);
    WorldSave a = newWorld("One", 0, DIFF_MEDIUM, MODE_PERSONAL, {});
    WorldSave b = newWorld("Two", 0, DIFF_MEDIUM, MODE_PERSONAL, {});
    saveWorld(a), saveWorld(b), saveWorld(a);  // (a twice: it has a .bak to remove as well)
    ctx.check("two listed", listWorlds().size() == 2);
    ctx.check("deletes", deleteWorld("one"));
    ctx.check("the folder is gone", !isDir(dir + "/worlds/one"));
    const auto all = listWorlds();
    ctx.check("only two is left", all.size() == 1 && all[0].slug == "two");
    ctx.check("deleting it again is false", !deleteWorld("one"));
}

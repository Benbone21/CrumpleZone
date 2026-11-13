// The generated plants (plantgen.h) in the model kit: "tree_gen" and "shrub_gen", one design per species file in assets/plants,
// so the model lab, the proving ground's yard and build/modelsheet show them. The game itself draws them through forest.cpp.
#include "../modelkit.h"
#include "../plantgen.h"

using namespace mk;

namespace {

std::vector<ModelDef> make() {
    ModelDef trees{"tree_gen", "GENERATED TREE", "NATURE AND PEOPLE", "Trees grown by rules from a species file (assets/plants): a seed is an individual; scaled per tree", 16, {}};
    ModelDef shrubs{"shrub_gen", "GENERATED SHRUB", "NATURE AND PEOPLE", "Shrubs, grasses, yuccas and cacti grown by rules from a species file (assets/plants)", 2.0, {}};
    trees.cache = shrubs.cache = false;
    for (const plantgen::Species& s : plantgen::all()) {
        const plantgen::Species* sp = &s;  // (the library lives as long as the program)
        Variant v;
        v.name = s.title;
        for (char& c : v.name)
            if (c == ':' || c == ',') c = ' ';
        v.blurb = s.blurb;
        v.params = {seedParam(1), choice("lod", "DETAIL", 0, {"FULL", "NEAR", "FAR", "FARTHEST"})};
        if (s.tree) v.params.push_back(choice("snow", "SNOW", 0, {"CLEAR", "SNOWY"})), v.params.push_back(choice("season", "SEASON", 0, {"SUMMER", "LATE SUMMER", "AUTUMN"}));
        else v.params.push_back(choice("dry", "DRY", 0, {"GREEN", "DRY"}));
        v.build = [sp](B& b, const P& p) {
            plantgen::Options o;
            o.seed = (uint32_t)std::max(1, p.i("seed")), o.lod = p.i("lod"), o.snow = p.i("snow") != 0, o.dry = p.i("dry"), o.season = p.i("season");
            plantgen::generate(*sp, o, b.out());
            for (MVert& q : b.out().v)
                if (q.mat == LEAFCUT) q.mat = FOLIAGE;  // (the kit's renderers don't cut leaf cards: plain foliage here)
        };
        (s.tree ? trees : shrubs).variants.push_back(std::move(v));
    }
    std::vector<ModelDef> out;
    if (!trees.variants.empty()) out.push_back(std::move(trees));
    if (!shrubs.variants.empty()) out.push_back(std::move(shrubs));
    return out;
}

LateReg reg(make);

}  // namespace

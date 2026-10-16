// The code-made models (modelkit.h, src/models/): every design builds, sound and within its
// budget, standing on the ground; the settings the game sets per placed copy change it; the
// model lab's picks save and load.
#include <cmath>
#include <cstdio>
#include <map>

#include "../src/modelkit.h"
#include "harness.h"

namespace {

// What a model may cost (triangles): things placed by the thousand are cheap.
size_t budgetFor(const std::string& id) {
    static const std::map<std::string, size_t> b = {
        {"tree_pine", 220}, {"tree_broadleaf", 260}, {"tree_snag", 120}, {"bush", 80}, {"stump", 80},
        {"marker_post", 100}, {"cairn", 250}, {"trail_sign", 400}, {"person", 2500},
        {"repair_shed", 12000}, {"fire_tower", 12000}, {"sign_gantry", 12000}, {"race_gate", 12000},
        {"cabin", 6500}, {"chain_bundle", 2600}, {"crate_engine_v8", 2600}, {"wreck_pickup", 10000}, {"ore_cart", 5000}, {"mine_portal", 6000}, {"jersey_barrier", 6000}, {"guardrail", 6000},
    };
    auto it = b.find(id);
    return it == b.end() ? 2500 : it->second;
}

bool sound(const mk::Mesh& m, std::string& why) {
    for (const mk::MVert& v : m.v) {
        for (int k = 0; k < 3; k++)
            if (!std::isfinite(v.p[k]) || !std::isfinite(v.n[k]) || !std::isfinite(v.c[k])) return why = "not finite", false;
        const double nl = std::sqrt(v.n[0] * v.n[0] + v.n[1] * v.n[1] + v.n[2] * v.n[2]);
        if (std::fabs(nl - 1) > 0.01) return why = "normal not unit length", false;
        for (int k = 0; k < 3; k++)
            if (v.c[k] < 0 || v.c[k] > 1.5) return why = "colour out of range", false;
    }
    if (m.v.size() % 3) return why = "not whole triangles", false;
    return true;
}

double checksum(const mk::Mesh& m) {
    double s = 0;
    for (size_t i = 0; i < m.v.size(); i++) s += m.v[i].p[0] * 1.3 + m.v[i].p[1] * 2.1 + m.v[i].p[2] * 0.7 + m.v[i].c[0];
    return s + m.v.size();
}

}  // namespace

SCENARIO(modelsBuild, "models/build", "Every design of every model builds: sound triangles, within its budget, standing on the ground, about the size it says") {
    ctx.check("there are models", mk::catalog().size() > 0, std::to_string(mk::catalog().size()));
    int designs = 0;
    for (const mk::ModelDef& d : mk::catalog()) {
        ctx.check(d.id + ": has designs", !d.variants.empty());
        for (int v = 0; v < (int)d.variants.size(); v++) {
            designs++;
            mk::Mesh m;
            mk::build(d, v, mk::P(), m);
            const std::string tag = d.id + " " + std::string(1, (char)('A' + v)) + " (" + d.variants[v].name + "): ";
            std::string why;
            const V3 size = m.hi - m.lo;
            std::printf("  %-16s %c %-22s %6zu tris  %5.2f x %5.2f x %5.2f m\n", d.id.c_str(), 'A' + v, d.variants[v].name.c_str(), m.triangles(), size.x, size.z, size.y);
            ctx.check(tag + "builds something", m.triangles() > 0);
            ctx.check(tag + "sound", sound(m, why), why);
            ctx.check(tag + "within budget", m.triangles() <= budgetFor(d.id), std::to_string(m.triangles()) + " > " + std::to_string(budgetFor(d.id)));
            const bool hangs = d.id == "tree_saver" || d.id == "chain_sling" || d.id == "fp_hand";  // (round a trunk or boulder at belt height; in your hand)
            ctx.check(tag + "stands on the ground", hangs || (m.lo.y > -0.6 && m.lo.y < 0.25), std::to_string(m.lo.y));
            const double big = std::max({size.x, size.y, size.z});
            ctx.check(tag + "about the size it says", big > d.size * 0.25 && big < d.size * 4 + 1, std::to_string(big) + " m against " + std::to_string(d.size));
            bool namesOk = d.variants[v].name.find_first_of(":,") == std::string::npos;
            for (const mk::Param& q : d.variants[v].params) namesOk &= q.lo <= q.def && q.def <= q.hi;
            ctx.check(tag + "names and defaults fit", namesOk);
        }
    }
    std::printf("%zu models, %d designs\n", mk::catalog().size(), designs);
}

SCENARIO(modelsInstance, "models/instance", "The settings the game sets per copy change every design: a sign's rating, a crate opened, a jack raised, a walker's stride, a gate's width, a barrier's length, snow on a tree") {
    struct Want { const char* id; const char* key; double a, b; };
    const Want wants[] = {
        {"trail_sign", "rating", 0, 3}, {"supply_crate", "open", 0, 1}, {"jack", "lift", 0, 1}, {"person", "swing", -1, 1},
        {"race_gate", "width", 8, 30}, {"race_gate", "finish", 0, 1}, {"jersey_barrier", "length", 4, 20}, {"guardrail", "length", 8, 20},
        {"sign_gantry", "span", 30, 38}, {"tree_pine", "snow", 0, 1}, {"tree_broadleaf", "snow", 0, 1}, {"tree_snag", "snow", 0, 1},
        {"bush", "dry", 0, 1}, {"tree_pine", "seed", 1, 2},
    };
    for (const Want& w : wants) {
        const mk::ModelDef* d = mk::findModel(w.id);
        if (!d) {
            std::printf("  (no %s yet)\n", w.id);
            continue;
        }
        for (int v = 0; v < (int)d->variants.size(); v++) {
            mk::P pa, pb;
            pa.v[w.key] = w.a, pb.v[w.key] = w.b;
            mk::Mesh a, b;
            mk::build(*d, v, pa, a), mk::build(*d, v, pb, b);
            ctx.check(std::string(w.id) + " " + (char)('A' + v) + ": " + w.key + " changes it", std::fabs(checksum(a) - checksum(b)) > 1e-6);
        }
    }
    // The person: at swing +1 the left leg (+Z) is forward.
    if (const mk::ModelDef* d = mk::findModel("person"))
        for (int v = 0; v < (int)d->variants.size(); v++) {
            mk::P p;
            p.v["swing"] = 1;
            mk::Mesh m;
            mk::build(*d, v, p, m);
            double left = 0, right = 0;
            int nl = 0, nr = 0;
            for (const mk::MVert& q : m.v)
                if (q.p[1] < 0.25) (q.p[2] > 0 ? (left += q.p[0], nl++) : (right += q.p[0], nr++));
            ctx.check(std::string("person ") + (char)('A' + v) + ": swing +1 puts the left foot forward", nl && nr && left / nl > right / nr + 0.1,
                      std::to_string(nl ? left / nl : 0) + " vs " + std::to_string(nr ? right / nr : 0));
        }
}

SCENARIO(modelsPicks, "models/picks", "The model lab's picks save and load: the design, its status and note, its tweaks; the game builds the picked design") {
    const mk::ModelDef* d = nullptr;
    for (const mk::ModelDef& m : mk::catalog())
        if (m.variants.size() >= 2 && !m.variants[1].params.empty()) d = &m;
    ctx.check("a model with two designs and settings", d != nullptr);
    if (!d) return;
    const mk::Param& q = d->variants[1].params[0];
    const double tweak = q.names.empty() ? (q.lo + q.hi) / 2 + (q.hi - q.lo) * 0.1 : q.hi;
    auto saved = mk::choices();
    mk::choices().clear();
    mk::Choice& c = mk::choices()[d->id];
    c.pick = 1, c.status = mk::REDO, c.note = "MAKE IT TALLER", c.tweaks[1][q.key] = tweak;
    const std::string path = "build/test-model-picks.txt";
    ctx.check("saves", mk::saveChoices(path));
    mk::choices().clear();
    ctx.check("loads", mk::loadChoices(path));
    const mk::Choice& back = mk::choices()[d->id];
    ctx.check("the same picks", back.pick == 1 && back.status == mk::REDO && back.note == "MAKE IT TALLER" && back.tweaks.count(1) &&
                                    std::fabs(back.tweaks.at(1).at(q.key) - tweak) < 1e-6);
    ctx.check("the game builds the picked design, tweaked", mk::pickedVariant(*d) == 1 && std::fabs(mk::pickedParams(*d, 1)(q.key) - tweak) < 1e-6);
    std::remove(path.c_str());
    mk::choices() = saved;
    mk::forgetMeshes();
}

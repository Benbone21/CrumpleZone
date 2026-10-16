// Model kit: the game's code-made models (props, buildings, gear, people), each with a few
// design variants and some tweakable parameters, and the picks made in the model lab
// (lab.cpp, `crumplezone --lab`) that decide which variant the game uses and how.
//
// A model is built in its own space, in metres: the origin on the ground under the middle
// of its footprint, +X its front (the side you'd walk up to), +Y up, +Z its left. (That's
// the game's own frame for a thing turned by `yaw`: front (cos, 0, -sin), left (sin, 0, cos).)
//
// Models are registered from src/models/*.cpp:
//
//     static mk::Reg jerryCan({"jerry_can", "JERRY CAN", "GEAR", "Spare fuel, 20 L", 0.7, {
//         {"NATO STEEL", "Pressed steel, three handles", {mk::paintParam(0)},
//          [](mk::B& b, const mk::P& p) { b.color(mk::paint(p.i("paint"))).mat(mk::PAINT); b.box(...); }},
//         ...
//     }});
//
// No OpenGL here: tools/modelsheet.cpp draws them on the CPU, the renderer turns them into
// its own vertices (scenery.cpp appendModel).
#pragma once
#include <cstdint>
#include <functional>
#include <map>
#include <string>
#include <utility>
#include <vector>

#include "mathx.h"

namespace mk {

// The lit shader's materials (render.h ShaderMat).
enum Mat : uint8_t {
    MATTE = 0,      // plain diffuse: cloth, plastic, rubber, paper
    CONCRETE = 3,   // cast concrete (shows form lines)
    PAINT = 4,      // painted metal: clear-coat shine
    GLASS = 5,
    EMISSIVE = 6,   // lamps, reflectors: glows
    METAL = 8,      // bare/galvanised steel: tinted reflections
    CHROME = 11,
    ROCK = 12,      // stone: grain, veins, lichen on the tops
    FOLIAGE = 13,   // leaves, needles, grass: soft light, glows with the sun behind
    WOOD = 14,      // weathered grain
};

struct MVert {
    float p[3], n[3], c[3];
    uint8_t mat;
};

// Triangles: every three vertices.
struct Mesh {
    std::vector<MVert> v;
    V3 lo{1e9, 1e9, 1e9}, hi{-1e9, -1e9, -1e9};  // bounds (after finish())
    void finish();
    size_t triangles() const { return v.size() / 3; }
};

struct V2 {
    double x = 0, y = 0;
    V2() {}
    V2(double x_, double y_) : x(x_), y(y_) {}
};

// A tweakable number (or, with `names`, a choice: 0..names-1).
struct Param {
    std::string key, label;
    double def = 0, lo = 0, hi = 1, step = 0.01;
    std::vector<std::string> names;
};
inline Param num(const std::string& key, const std::string& label, double def, double lo, double hi, double step = 0.01) {
    return Param{key, label, def, lo, hi, step, {}};
}
inline Param choice(const std::string& key, const std::string& label, int def, std::vector<std::string> names) {
    return Param{key, label, (double)def, 0, (double)names.size() - 1, 1, std::move(names)};
}
// Shared colour choices for painted things.
const std::vector<std::string>& paintNames();  // RED, OLIVE, SAND, BLACK, ORANGE, BLUE, WHITE, GREEN, YELLOW, GREY
V3 paint(int i);
inline Param paintParam(int def = 0, const std::string& key = "paint", const std::string& label = "PAINT") {
    return choice(key, label, def, paintNames());
}
// A random seed, for things that should come out different each time (rocks, trees).
inline Param seedParam(int def = 1) { return Param{"seed", "SEED", (double)def, 1, 999, 1, {}}; }

// The values a build reads.
struct P {
    std::map<std::string, double> v;
    double operator()(const std::string& key) const;  // 0 if not set
    int i(const std::string& key) const { return (int)std::lround((*this)(key)); }
    bool has(const std::string& key) const { return v.count(key) > 0; }
};

class B;
using BuildFn = std::function<void(B&, const P&)>;

struct Variant {
    std::string name, blurb;
    std::vector<Param> params;
    BuildFn build;
};

struct ModelDef {
    std::string id, title, category, use;  // use: what it is / where the game uses it
    double size = 1;                        // about how big it is (m): how far back the camera sits
    std::vector<Variant> variants;
};

// Registers a model (from a static in src/models/*.cpp).
struct Reg {
    explicit Reg(ModelDef def);
};
const std::vector<ModelDef>& catalog();
const ModelDef* findModel(const std::string& id);

// The builder: primitives in the current local frame, in the current colour and material.
class B {
public:
    explicit B(Mesh& out) : out_(out) {}

    // Style.
    B& color(const V3& c) { col_ = c; return *this; }
    B& color(double r, double g, double b) { col_ = V3(r, g, b); return *this; }
    B& mat(Mat m) { mat_ = m; return *this; }
    B& style(const V3& c, Mat m) { col_ = c; mat_ = m; return *this; }
    V3 curColor() const { return col_; }
    Mat curMat() const { return mat_; }

    // The local frame (a stack). Angles in degrees: yaw about up, pitch about left (nose
    // up), roll about front.
    void push();
    void pop();
    B& move(const V3& d);
    B& move(double x, double y, double z) { return move(V3(x, y, z)); }
    B& yaw(double deg);
    B& pitch(double deg);
    B& roll(double deg);
    B& rotate(const V3& axis, double deg);
    B& scale(const V3& s);
    B& scale(double s) { return scale(V3(s, s, s)); }
    B& mirrorZ();  // left <-> right (for building one side, then the other)
    // Builds `f` in a pushed frame: b.at({x, y, z}, yaw, [&]{ ... }).
    void at(const V3& p, double yawDeg, const std::function<void()>& f);
    void at(const V3& p, const std::function<void()>& f) { at(p, 0, f); }
    // `f` built twice: as is, and mirrored left to right.
    void both(const std::function<void()>& f);
    // The current frame applied to a local point / direction.
    V3 world(const V3& p) const;

    // Primitives. Sizes are full sizes (not half).
    void tri(const V3& a, const V3& b, const V3& c);                       // flat
    void quad(const V3& a, const V3& b, const V3& c, const V3& d);         // flat (a b c d round the edge)
    void triN(const V3& a, const V3& b, const V3& c, const V3& na, const V3& nb, const V3& nc);
    void box(const V3& center, const V3& size, double bevel = 0);       // axis-aligned in the frame; bevel: chamfered edges
    // From `a` to `b` (a straight beam/plank of the given cross-section): `up` picks its roll.
    void beam(const V3& a, const V3& b, double w, double h, const V3& up = V3(0, 1, 0), double bevel = 0);
    void cyl(const V3& a, const V3& b, double r, int segs = 16, bool caps = true);
    void cone(const V3& a, const V3& b, double ra, double rb, int segs = 16, bool caps = true);
    void sphere(const V3& c, const V3& radii, int detail = 2);            // detail 0..3 (icosphere)
    // A lumpy blob: rocks (facet ~0.7), bushes and crowns (facet ~0.1). `cutBelow`: leave out
    // triangles wholly below this local y (buried).
    void blob(const V3& c, const V3& radii, double bump, uint32_t seed, int detail = 2, double facet = 0.3, double cutBelow = -1e9);
    // Turned on a lathe about the local up axis through `base`: profile points (radius, height),
    // bottom to top. A radius of 0 closes the end. smooth: rounded (else faceted).
    void lathe(const V3& base, const std::vector<V2>& profile, int segs = 20, bool smooth = true);
    // A flat shape (the polygon, in the local front/up plane: x along front, y up; any simple
    // polygon, either winding) given thickness along left (centred on z0).
    void extrude(const std::vector<V2>& poly, double thickness, double z0 = 0);
    // The same, but the shape lies in the ground plane (x front, y = left) and is extruded up.
    void extrudeUp(const std::vector<V2>& poly, double height, double y0 = 0);
    // A ring round local up (axis Y) at c: radius R, tube radius r; arcDeg < 360 leaves a gap.
    void torus(const V3& c, double R, double r, int segs = 24, int sides = 8, double arcDeg = 360);
    // A round tube along a path (ropes, pipes, handles, rails, wire).
    void tube(const std::vector<V3>& path, double r, int sides = 8, bool closed = false, bool caps = true);
    // Block letters (the HUD's 5x7 font: capitals, digits, some punctuation), a flat face standing
    // `depth` proud of the plane x = center.x, read left to right from the front; `px` is
    // one font pixel (the letters are 7 px tall), centred on (center.y, center.z). (Turn the
    // frame first to letter some other face.)
    void letters(const std::string& s, const V3& center, double px, double depth = 0.004);

    Mesh& out() { return out_; }

private:
    struct Frame {
        V3 o, x{1, 0, 0}, y{0, 1, 0}, z{0, 0, 1};
    };
    Mesh& out_;
    Frame f_;
    std::vector<Frame> stack_;
    V3 col_{0.6, 0.6, 0.6};
    Mat mat_ = MATTE;
    void emit(const V3& p, const V3& n);  // local point and normal
    V3 tnormal(const V3& n) const;
};

// Helpers for model code.
V3 mix(const V3& a, const V3& b, double t);
double hash01(uint32_t seed, uint32_t i);  // repeatable 0..1
std::vector<V2> roundedRect(double w, double h, double r, int segs = 4);  // centred
std::vector<V2> circle(double r, int segs, double cx = 0, double cy = 0);

// Builds variant `variant` of `def` with params `p` (missing ones take their defaults).
void build(const ModelDef& def, int variant, const P& p, Mesh& out);
P defaults(const Variant& v);

// ---- Picks from the model lab -------------------------------------------------------
enum Status { NEW = 0, LIKED = 1, REDO = 2 };
struct Choice {
    int pick = 0;          // the variant the game uses
    int status = NEW;
    std::string note;      // what to change (for a redo)
    std::map<int, std::map<std::string, double>> tweaks;  // per variant: params moved off their defaults
};
std::map<std::string, Choice>& choices();
// assets/models/choices.txt (found like assets/props); "" if there's no assets folder.
std::string choicesPath();
bool loadChoices(const std::string& path);
bool saveChoices(const std::string& path);
// The variant picked for `id` and its params (picks loaded on first use).
int pickedVariant(const ModelDef& def);
P pickedParams(const ModelDef& def, int variant);
// The picked mesh for a model, cached ("" or unknown id: an empty mesh). `over`: params to
// set on top (per placed copy: a sign's rating, a crate's state); built fresh, not cached.
const Mesh& modelMesh(const std::string& id);
Mesh modelMesh(const std::string& id, const std::vector<std::pair<std::string, double>>& over);
void forgetMeshes();  // after picks change
uint64_t picksVersion();  // goes up whenever the picks (or the meshes built from them) change

}  // namespace mk

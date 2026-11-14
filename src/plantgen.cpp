#include "plantgen.h"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <mutex>
#include <sstream>

#include "detmath.h"
#include "platform.h"

namespace plantgen {
namespace {

const double kPi = 3.14159265358979323846;
const V3 UP(0, 1, 0);
const V3 SNOW(0.86, 0.88, 0.92);

// ---- The keys ------------------------------------------------------------------------------------------------------------
// Lengths are fractions of the plant's height (a woody plant is grown 1 tall, then scaled), angles degrees, colours r g b.
// A value per branching order (0 the stems, 1 the limbs off them, ...) where it says "per order": past the last given, the last.
const std::vector<KeyInfo>& keyTable() {
    static const std::vector<KeyInfo> k = {
        {"name", "", "what it is called (the model yard's label)"},
        {"about", "", "one line on how it looks"},
        {"class", "shrub", "tree (a trunk to hit: World::trees) or shrub (World::bushes)"},
        {"id", "-1", "a number of its own among its class, never changed once maps use it"},
        {"form", "decurrent", "excurrent | decurrent | multistem | shrub | wand (presets of the keys below) or bunchgrass | rosette | pads"},
        {"replaces", "", "the classic model it stands in for with --proc-plants: pine N, broadleaf N, snag N, dryland N, bush KIND"},
        {"size", "3 6", "how big it grows (m): a tree's height, a shrub's half-width (a palette may say otherwise)"},
        // The skeleton.
        {"stems", "1 1", "how many stems rise from the foot (least, most)"},
        {"stem_spread", "0", "how far the stems lean out from upright"},
        {"stem_base", "0", "the radius of the ring the stems rise from"},
        {"trunk_len", "1", "the main stem's length before the plant is fitted to its height"},
        {"trunk_radius", "0.035", "the main stem's radius at the ground"},
        {"flare", "1.35", "how much wider the stem's foot is"},
        {"pipe", "2.4", "the pipe model's exponent: a stem's radius^pipe is the sum of its branches' (2 thin limbs .. 3 stout)"},
        {"taper", "0.004", "radius gained per unit of length toward the foot, on top of the pipe model"},
        {"twig", "0.004", "the thinnest radius drawn"},
        {"orders", "3", "branching orders: 1 a bare stem, 2 limbs, 3 limbs and twigs, 4 ..."},
        {"segs", "8 5 3 2", "bends along a branch, per order"},
        {"gnarl", "0.03 0.10 0.14 0.18", "how crooked, per order (0 straight)"},
        {"up", "0 0.15 0.05 0", "how it turns up (+) or droops (-) along its length, per order"},
        {"start", "0.15 0.25 0.25 0.3", "where along a branch its children begin, per order of the parent"},
        {"count", "24 5 3", "children on a branch, per order of the parent"},
        {"angle", "70 50 45", "a child's angle off its parent, per order of the parent"},
        {"angle_top", "-1", "the limbs' angle at the top of the stem (-1: the same as at the bottom)"},
        {"angle_var", "10", "how much the angles vary"},
        {"length", "0.3 0.45 0.5", "a child's length: limbs x the crown's shape; further orders x the parent's length"},
        {"falloff", "0.5", "how much shorter the children get toward a branch's end"},
        {"crown", "round", "the limbs' lengths up the stem: conical | spire | round | columnar | spreading | flat"},
        {"planar", "0 0.7 0.7", "children kept to the sides of a branch (a flat bough), per order of the parent"},
        {"dead", "0", "the share of limbs that are dead: bare and grey"},
        {"dead_low", "0", "the bottom share of the crown where the limbs are dead stubs"},
        {"dead_top", "0", "the chance the leader is a dead spike"},
        {"broken", "0", "how far branches are snapped short (a snag: 0.6)"},
        {"width", "0", "the crown's radius over the height it is fitted to (0: as it grows)"},
        {"sides", "8", "the most sides a stem is drawn with"},
        {"green_stems", "0", "1: the branches are the foliage (leafless green stems: Mormon tea); 2: the stems too (a cactus)"},
        {"lean", "0", "a single stem leans at least this share of stem_spread (a toppling dead tree)"},
        {"stand_scale", "1", "standing in for a classic map's tree, it is drawn this share of that tree's height (a desert tree is short)"},
        // Foliage.
        {"leaf_order", "2", "branches of this order and above carry foliage"},
        {"leaf_from", "0.3", "along the outer part of a branch, from this far along it"},
        {"leaf_step", "0.05", "between one clump and the next along a branch (0: at the tip only)"},
        {"leader", "0", "the top share of the main stem that carries foliage too (a conifer's spire)"},
        {"cards", "4", "leaf cards in a clump (0: no foliage)"},
        {"card", "0.06 0.035", "a card's length and width"},
        {"scatter", "0.02", "how far the cards stand from the clump's middle"},
        {"align", "0.3", "0 turned every way .. 1 along the branch"},
        {"flat", "0", "1: lying level (a fir's bough)"},
        {"droop", "0", "the cards hang (a willow)"},
        {"puff", "0 0.8", "a solid tuft at each clump under its cards: its radius, and how tall it is for its width (0: cards only)"},
        {"cut", "1", "1: the cards' pixels are cut to a leafy outline by the tree shader (0: drawn whole)"},
        {"ao", "0.45", "how much darker the foliage is deep in the crown"},
        {"far", "1", "how much bigger the far copies' cards are drawn (they are fewer)"},
        // Colours.
        {"bark", "0.30 0.25 0.20", "bark at the foot"},
        {"bark2", "0.42 0.36 0.29", "bark on the limbs"},
        {"deadwood", "0.55 0.52 0.47", "dead limbs"},
        {"leaf", "0.16 0.24 0.10", "foliage, its two shades"},
        {"leaf2", "0.24 0.32 0.13", ""},
        {"leaf_tip", "0.15", "how much lighter a card's tip is"},
        {"leaf_dry", "0.42 0.36 0.20", "in drought (a shrub's dry copies)"},
        {"autumn", "0.72 0.56 0.12", "in autumn (season 2; half way in late summer)"},
        {"flower", "0 0 0", "flower heads' colour"},
        {"flower_share", "0", "the share of the upper cards in flower"},
        // Grass, rosettes, pads.
        {"blades", "50", "bunchgrass: blades; rosette: leaves"},
        {"blade", "1.0 0.035", "a blade's length and width"},
        {"spread", "35", "how far the blades lean out"},
        {"stalks", "0 0", "flower or seed stalks (least, most)"},
        {"stalk_len", "1.4", "a stalk's height"},
        {"skirt", "0.25", "rosette: the share of leaves that are dead and hang"},
        {"pads", "14 24", "pads in a clump (least, most)"},
        {"pad", "0.24 0.17 0.035", "a pad's length, width and thickness"},
    };
    return k;
}

// What `form` sets before the file's own lines.
const char* presetFor(const std::string& form) {
    if (form == "excurrent")
        return "orders 3\ncount 30 5 3\nangle 85 55 45\nangle_top 50\nlength 0.30 0.36 0.4\ncrown conical\nstart 0.12 0.2 0.3\ngnarl 0.012 0.05 0.1\n"
               "up 0 0.05 0\nleaf_order 1\nleaf_from 0.25\nleader 0.1\nsegs 9 3 2\ntrunk_radius 0.022\npipe 2.6\nplanar 0 0.9 0.9\n";
    if (form == "decurrent")
        return "orders 4\ntrunk_len 0.55\ncount 4 3 3\nangle 45 45 45\nlength 0.55 0.6 0.55\ncrown spreading\nstart 0.55 0.35 0.35 0.3\ngnarl 0.05 0.12 0.16 0.2\n"
               "up 0 0.35 0.15 0.05\nleaf_order 3\nleaf_from 0.2\nsegs 6 5 3 2\ntrunk_radius 0.04\nplanar 0 0.2 0.3\n";
    if (form == "multistem")
        return "orders 3\nstems 3 5\nstem_spread 28\nstem_base 0.02\ntrunk_len 0.8\ncount 5 3\nangle 45 40\nlength 0.4 0.5\ncrown round\nstart 0.3 0.3\n"
               "gnarl 0.1 0.15 0.2\nup 0.2 0.15 0\nleaf_order 2\nsegs 6 4 2\ntrunk_radius 0.03\n";
    if (form == "shrub")
        return "orders 3\nstems 5 8\nstem_spread 50\nstem_base 0.04\ntrunk_len 0.75\ncount 3 3\nangle 40 40\nlength 0.45 0.5\ncrown round\nstart 0.3 0.3\n"
               "gnarl 0.12 0.18 0.2\nup 0.25 0.2 0\nleaf_order 1\nleaf_from 0.4\nsegs 3 2 1\ntrunk_radius 0.03\nsides 5\ntwig 0.008\ntaper 0.006\nwidth 0.6\n";
    if (form == "wand")
        return "orders 1\nstems 12 18\nstem_spread 14\nstem_base 0.08\ntrunk_len 1\ngnarl 0.05\nup 0.1\nleaf_order 0\nleaf_from 0.3\nleaf_step 0.06\nsegs 4\n"
               "trunk_radius 0.012\nsides 4\ntwig 0.005\ntaper 0.004\nalign 0.7\nwidth 0.35\nflare 1\n";
    return "";
}

bool isWordKey(const std::string& k) { return k == "name" || k == "about" || k == "class" || k == "form" || k == "replaces" || k == "crown"; }

const KeyInfo* keyInfo(const std::string& k) {
    for (const KeyInfo& i : keyTable())
        if (k == i.key) return &i;
    return nullptr;
}

std::vector<double> numbers(const std::string& s) {
    std::vector<double> v;
    std::istringstream in(s);
    for (std::string w; in >> w;) v.push_back(std::atof(w.c_str()));
    return v;
}

const std::map<std::string, std::vector<double>>& defaults() {
    static const std::map<std::string, std::vector<double>> d = [] {
        std::map<std::string, std::vector<double>> m;
        for (const KeyInfo& i : keyTable())
            if (!isWordKey(i.key)) m[i.key] = numbers(i.def);
        return m;
    }();
    return d;
}

std::string lower(std::string s) {
    for (char& c : s) c = (char)std::tolower((unsigned char)c);
    return s;
}

// ---- Random numbers: integers only, so the same everywhere -----------------------------------------------------------------
uint32_t mix32(uint32_t z) {
    z ^= z >> 16, z *= 0x85EBCA6Bu, z ^= z >> 13, z *= 0xC2B2AE35u, z ^= z >> 16;
    return z;
}
struct Rng {
    uint32_t s;
    explicit Rng(uint32_t seed) : s(mix32(seed * 0x9E3779B9u + 0x7F4A7C15u)) {}
    uint32_t next() { return mix32(s += 0x9E3779B9u); }
    double u() { return (next() >> 8) * (1.0 / 16777216.0); }
    double sym() { return 2 * u() - 1; }
    double range(double a, double b) { return a + (b - a) * u(); }
    Rng fork(uint32_t k) const { return Rng(s ^ mix32(k * 0x632BE5ABu + 0x1B873593u)); }
    V3 unit() {  // (a direction, evenly over the sphere)
        const double y = sym(), a = u() * 2 * kPi, r = std::sqrt(std::max(0.0, 1 - y * y));
        return V3(r * dm::cos(a), y, r * dm::sin(a));
    }
};

double rad(double deg) { return deg * (kPi / 180); }
double clamp01(double x) { return x < 0 ? 0 : x > 1 ? 1 : x; }
double lerp(double a, double b, double t) { return a + (b - a) * t; }
V3 vmix(const V3& a, const V3& b, double t) { return a + (b - a) * t; }
V3 perp(const V3& t) { return norm(cross(t, std::fabs(t.y) < 0.9 ? UP : V3(1, 0, 0))); }

// ---- Mesh writing ----------------------------------------------------------------------------------------------------------
struct Out {
    mk::Mesh& m;
    void vert(const V3& p, const V3& n, const V3& c, mk::Mat mat) {
        mk::MVert q;
        V3 nn = norm(n);
        if (len(nn) < 0.5) nn = UP;
        q.p[0] = (float)p.x, q.p[1] = (float)p.y, q.p[2] = (float)p.z;
        q.n[0] = (float)nn.x, q.n[1] = (float)nn.y, q.n[2] = (float)nn.z;
        q.c[0] = (float)std::min(1.4, std::max(0.0, c.x)), q.c[1] = (float)std::min(1.4, std::max(0.0, c.y)), q.c[2] = (float)std::min(1.4, std::max(0.0, c.z));
        q.mat = mat;
        m.v.push_back(q);
    }
    void tri(const V3& a, const V3& b, const V3& c, const V3& na, const V3& nb, const V3& nc, const V3& ca, const V3& cb, const V3& cc, mk::Mat mat) {
        if (len(cross(b - a, c - a)) < 1e-12) return;
        vert(a, na, ca, mat), vert(b, nb, cb, mat), vert(c, nc, cc, mat);
    }
};

// ---- The woody skeleton ----------------------------------------------------------------------------------------------------
struct Br {
    int parent = -1, pnode = 0, order = 0, limb = -1;
    bool dead = false, snapped = false;
    std::vector<V3> p;
    std::vector<double> r;
    double length = 0;
};
struct Site {
    V3 p, d;
    int br;
    double along;  // 0 the branch's foot .. 1 its tip
};

double crownShape(const std::string& c, double t) {  // t: 0 the crown's foot .. 1 the top; the limbs' length there
    if (c == "conical") return 0.12 + 0.88 * (1 - t) * (0.55 + 0.45 * std::min(1.0, t * 7 + 0.3));
    if (c == "spire") return 0.1 + 0.9 * (1 - t) * (1 - t) * 0.6 + 0.9 * (1 - t) * 0.4 * std::min(1.0, t * 5 + 0.4);
    if (c == "columnar") return 0.55 + 0.45 * dm::sin(kPi * (0.1 + 0.8 * t));
    if (c == "spreading") return 0.6 + 0.4 * t;
    if (c == "flat") return 0.45 + 0.55 * std::sqrt(clamp01(t));
    return 0.35 + 0.65 * dm::sin(kPi * (0.12 + 0.8 * t));  // round
}

struct Woody {
    const Species& sp;
    std::vector<Br> br;
    int orders;
    int limbs = 0;

    explicit Woody(const Species& s) : sp(s), orders(std::max(1, (int)s.v("orders"))) {}

    void grow(int parent, int pnode, const V3& o, V3 d, double length, int order, bool dead, int limb, Rng r) {
        const int me = (int)br.size();
        br.emplace_back();
        {
            Br& b = br.back();
            b.parent = parent, b.pnode = pnode, b.order = order, b.dead = dead, b.limb = limb;
        }
        const double broken = sp.v("broken");
        bool snapped = false;
        if (broken > 0 && (order > 0 || r.u() < broken)) length *= 1 - broken * r.u() * (order == 0 ? 0.5 : 1), snapped = true;
        if (dead && order > 0) length *= 0.55 + 0.45 * r.u(), snapped = snapped || r.u() < 0.5;
        const int segs = std::max(1, (int)sp.v("segs", order));
        const double floorY = 0.06 * sp.v("trunk_len");
        const double step = length / segs, gn = sp.v("gnarl", order), upk = sp.v("up", order);
        std::vector<V3> pts;
        V3 p = o;
        pts.push_back(p);
        for (int i = 0; i < segs; i++) {
            d = norm(d + r.unit() * gn + V3(0, upk / segs, 0));
            if (order == 0 && d.y < 0.25) d = norm(V3(d.x, 0.25, d.z));  // (a stem never turns back down)
            if (order > 0 && p.y + d.y * step < floorY) d = norm(V3(d.x, 0.15 + 0.5 * std::fabs(d.y), d.z));  // (nor a limb into the ground)
            p = p + d * step;
            pts.push_back(p);
        }
        br[me].p = pts, br[me].length = length, br[me].snapped = snapped;
        if (order + 1 >= orders) return;
        int n = (int)std::lround(sp.v("count", order) * (order == 0 ? 0.9 + 0.2 * r.u() : 0.7 + 0.6 * r.u()));
        if (order > 0) n = std::max(1, n);
        if (dead) n = std::min(n, 1 + (int)(r.u() * 2));
        const double st = sp.v("start", order), a0 = sp.v("angle", order), aTop = sp.v("angle_top"), aVar = sp.v("angle_var");
        const double planar = sp.v("planar", order), deadShare = sp.v("dead"), deadLow = sp.v("dead_low"), fall = sp.v("falloff");
        const std::string crown = sp.w("crown");
        double phi = r.u() * 2 * kPi;
        for (int k = 0; k < n; k++) {
            const double t = std::min(0.97, st + (1 - st) * (k + 0.5 + 0.7 * (r.u() - 0.5)) / n), tt = (t - st) / std::max(1e-6, 1 - st);
            phi += 2.39996323 + 0.5 * r.sym();
            const double f = t * segs;
            const int i = std::min(segs - 1, (int)f);
            const V3 P = pts[i] + (pts[i + 1] - pts[i]) * (f - i), T = norm(pts[i + 1] - pts[i]);
            const V3 U = perp(T), W = cross(T, U);
            V3 radial = U * dm::cos(phi) + W * dm::sin(phi);
            if (planar > 0 && std::fabs(T.y) < 0.95) {
                const V3 side = norm(cross(T, UP)) * ((k & 1) ? 1.0 : -1.0);
                radial = norm(vmix(radial, side, planar));
            }
            double a = order == 0 && aTop >= 0 ? lerp(a0, aTop, tt) : a0;
            a = rad(a + aVar * r.sym());
            const V3 cd = norm(T * dm::cos(a) + radial * dm::sin(a));
            double L;
            if (order == 0) L = sp.v("length", 0) * crownShape(crown, tt) * (0.8 + 0.4 * r.u());
            else L = sp.v("length", order) * length * (1 - fall * t) * (0.8 + 0.4 * r.u());
            bool dd = dead;
            if (order == 0) dd = r.u() < deadShare || (tt < deadLow && r.u() < 0.85);
            grow(me, i, P, cd, L, order + 1, dd, order == 0 ? limbs++ : limb, r.fork(1000 + (uint32_t)k));
        }
    }

    void growAll(uint32_t seed) {
        Rng r(seed * 7919u + 13u);
        const int sMin = std::max(1, (int)sp.v("stems", 0)), sMax = std::max(sMin, (int)sp.v("stems", 1));
        const int stems = sMin + (int)(r.u() * (sMax - sMin + 0.999));
        const double spread = rad(sp.v("stem_spread")), ring = sp.v("stem_base"), tl = sp.v("trunk_len");
        const double a0 = r.u() * 2 * kPi;
        for (int s = 0; s < stems; s++) {
            const double az = a0 + 2 * kPi * s / stems + (stems > 1 ? 0.5 * r.sym() : 0);
            const double lean = stems > 1 ? spread * (0.45 + 0.55 * r.u()) : spread * (sp.v("lean") + (1 - sp.v("lean")) * r.u());
            const V3 out(dm::cos(az), 0, dm::sin(az));
            const V3 d = norm(UP * dm::cos(lean) + out * dm::sin(lean));
            const V3 o = out * (stems > 1 ? ring * (0.6 + 0.4 * r.u()) : 0.0) - UP * 0.02;
            const bool deadTop = sp.v("dead_top") > 0 && s == 0 && r.u() < sp.v("dead_top");
            grow(-1, 0, o, d, tl * (s == 0 ? 1.0 : 0.72 + 0.28 * r.u()), 0, false, -1, r.fork(77 + (uint32_t)s));
            if (deadTop) {  // the leader died back: a bare spike out of the top
                const int stem = stemIndex(s);
                const V3 top = br[stem].p.back(), T = norm(top - br[stem].p[br[stem].p.size() - 2]);
                Rng rr = r.fork(555);
                const size_t at = br.size();
                grow(stem, (int)br[stem].p.size() - 2, top, norm(T + UP * 0.6 + rr.unit() * 0.25), 0.16 + 0.1 * rr.u(), orders - 1, true, -1, rr);
                br[at].dead = true;
            }
        }
    }
    int stemIndex(int s) const {
        int k = 0;
        for (size_t i = 0; i < br.size(); i++)
            if (br[i].order == 0 && br[i].parent < 0 && k++ == s) return (int)i;
        return 0;
    }

    // Fitted to a height of 1 (and its crown to `width`), then the thicknesses by the pipe model.
    void fit(double breastH) {
        double top = 0.05, wide = 0.01;
        const double margin = sp.v("cards") > 0 ? sp.v("card", 0) * 0.8 + sp.v("scatter") : 0;
        for (const Br& b : br)
            for (const V3& q : b.p) top = std::max(top, q.y + (b.snapped ? sp.v("trunk_radius") * 1.1 : b.dead ? 0 : margin)), wide = std::max(wide, std::sqrt(q.x * q.x + q.z * q.z) + margin);
        const double k = 1 / top;
        double kx = k;
        if (sp.v("width") > 0) kx = k * std::min(1.7, std::max(0.6, sp.v("width") / (wide * k)));
        for (Br& b : br) {
            double L = 0;
            for (size_t i = 0; i < b.p.size(); i++) {
                b.p[i] = V3(b.p[i].x * kx, b.p[i].y * k, b.p[i].z * kx);
                if (i) L += len(b.p[i] - b.p[i - 1]);
            }
            b.length = L;
        }
        const double pw = std::max(1.5, sp.v("pipe")), taper = sp.v("taper"), twig = sp.v("twig");
        std::vector<std::vector<double>> add(br.size());
        for (size_t i = 0; i < br.size(); i++) add[i].assign(br[i].p.size(), 0.0);
        for (int i = (int)br.size() - 1; i >= 0; i--) {
            Br& b = br[i];
            const int n = (int)b.p.size();
            b.r.assign(n, 1.0);
            double a = b.snapped ? 2.5 : 1.0;
            b.r[n - 1] = dm::pow(a, 1 / pw);
            for (int j = n - 2; j >= 0; j--) a += add[i][j], b.r[j] = dm::pow(a, 1 / pw);
            if (b.parent >= 0) add[b.parent][std::min(b.pnode, (int)add[b.parent].size() - 1)] += dm::pow(b.r[0], pw);
        }
        double base = 1e-9;
        for (const Br& b : br)
            if (b.parent < 0) base = std::max(base, b.r[0]);
        const double s = sp.v("trunk_radius") / base;
        for (Br& b : br) {
            const int n = (int)b.p.size();
            double fromTip = 0;
            for (int j = n - 1; j >= 0; j--) {
                if (j < n - 1) fromTip += len(b.p[j + 1] - b.p[j]);
                b.r[j] = std::max(twig, b.r[j] * s + taper * fromTip);
            }
        }
        // (A branch no thicker than the stem it leaves.)
        for (Br& b : br) {
            if (b.parent < 0) continue;
            const Br& pa = br[b.parent];
            const double lim = 0.85 * pa.r[std::min(b.pnode, (int)pa.r.size() - 1)];
            if (b.r[0] > lim && lim > 0) {
                const double f = lim / b.r[0];
                for (double& q : b.r) q = std::max(twig * 0.8, q * f);
            }
        }
        const double flare = sp.v("flare");
        for (Br& b : br)
            if (b.parent < 0) b.r[0] *= flare;
        if (breastH > 0) {  // a classic tree's trunk: its collider stays what it was
            const double have = radiusAt(0.1 / std::clamp(sp.v("stand_scale"), 0.2, 1.0));  // (1.2 m up the classic tree it stands for)
            const double f = std::min(2.5, std::max(0.4, breastH / std::max(1e-6, have))), fs = std::sqrt(f);
            for (Br& b : br)
                for (double& q : b.r) q *= b.order == 0 ? f : fs;
        }
    }
    // The first stem's radius at height y (plant heights).
    double radiusAt(double y) const {
        const Br& b = br[0];
        for (size_t i = 0; i + 1 < b.p.size(); i++)
            if (b.p[i + 1].y >= y) {
                const double t = clamp01((y - b.p[i].y) / std::max(1e-9, b.p[i + 1].y - b.p[i].y));
                return lerp(b.r[i], b.r[i + 1], t);
            }
        return b.r.back();
    }
};

V3 along(const Br& b, double s, V3* tangent) {  // the point `s` along it (a length), and the way it runs there
    double at = 0;
    for (size_t i = 0; i + 1 < b.p.size(); i++) {
        const double l = len(b.p[i + 1] - b.p[i]);
        if (s <= at + l || i + 2 == b.p.size()) {
            if (tangent) *tangent = norm(b.p[i + 1] - b.p[i]);
            return b.p[i] + (b.p[i + 1] - b.p[i]) * clamp01((s - at) / std::max(1e-9, l));
        }
        at += l;
    }
    if (tangent) *tangent = UP;
    return b.p.back();
}

struct Colours {
    V3 bark, bark2, dead, leaf, leaf2, flower;
    double tip, flowerShare;
};
Colours coloursOf(const Species& sp, const Options& o) {
    Colours c;
    c.bark = sp.col("bark"), c.bark2 = sp.col("bark2"), c.dead = sp.col("deadwood");
    c.leaf = sp.col("leaf"), c.leaf2 = sp.col("leaf2"), c.flower = sp.col("flower");
    c.tip = sp.v("leaf_tip"), c.flowerShare = sp.v("flower_share");
    const double fall = o.season == 2 ? 0.85 : o.season == 1 ? 0.35 : 0;
    if (fall > 0) c.leaf = vmix(c.leaf, sp.col("autumn") * 0.8, fall), c.leaf2 = vmix(c.leaf2, sp.col("autumn"), fall);
    if (o.dry > 0) {
        const V3 d = sp.col("leaf_dry");
        c.leaf = vmix(c.leaf, d * 0.8, 0.85 * o.dry), c.leaf2 = vmix(c.leaf2, d, 0.85 * o.dry), c.flowerShare *= 1 - o.dry;
    }
    return c;
}

void tube(Out& out, const Br& b, int sides, int stride, const V3& c0, const V3& c1, mk::Mat mat, uint32_t seed, bool point) {
    std::vector<int> ring;
    const int n = (int)b.p.size();
    for (int i = 0; i < n - 1; i += stride) ring.push_back(i);
    ring.push_back(n - 1);
    const int m = (int)ring.size();
    std::vector<V3> T(m), U(m);
    for (int i = 0; i < m; i++) {
        const V3 t = b.p[ring[std::min(i + 1, m - 1)]] - b.p[ring[std::max(i - 1, 0)]];
        T[i] = len(t) > 1e-12 ? norm(t) : UP;
    }
    U[0] = perp(T[0]);
    for (int i = 1; i < m; i++) {
        const V3 u = U[i - 1] - T[i] * dot(U[i - 1], T[i]);
        U[i] = len(u) > 1e-9 ? norm(u) : perp(T[i]);
    }
    Rng r(seed);
    std::vector<double> strip(sides);
    for (double& s : strip) s = 0.86 + 0.28 * r.u();  // (bark in strips: each side a shade)
    auto P = [&](int i, int j, V3& nn) {
        const V3 w = cross(T[i], U[i]);
        const double a = 2 * kPi * j / sides;
        nn = U[i] * dm::cos(a) + w * dm::sin(a);
        return b.p[ring[i]] + nn * b.r[ring[i]];
    };
    auto C = [&](int i, int j) { return vmix(c0, c1, m > 1 ? (double)i / (m - 1) : 0.0) * strip[j % sides]; };
    for (int i = 0; i + 1 < m; i++) {
        const bool apex = point && i + 2 == m;
        for (int j = 0; j < sides; j++) {
            const int k = (j + 1) % sides;
            V3 n00, n01, n10, n11;
            const V3 p00 = P(i, j, n00), p01 = P(i, k, n01);
            if (apex) {
                const V3 tipP = b.p[ring[m - 1]];
                out.tri(p00, p01, tipP, n00, n01, norm(n00 + n01 + T[m - 1]), C(i, j), C(i, k), C(i + 1, j), mat);
                continue;
            }
            const V3 p10 = P(i + 1, j, n10), p11 = P(i + 1, k, n11);
            out.tri(p00, p01, p11, n00, n01, n11, C(i, j), C(i, k), C(i + 1, k), mat);
            out.tri(p00, p11, p10, n00, n11, n10, C(i, j), C(i + 1, k), C(i + 1, j), mat);
        }
    }
    if (!point) {  // a snapped end: a splintered cap, paler wood
        const int i = m - 1;
        const V3 ctr = b.p[ring[i]] + T[i] * (b.r[ring[i]] * (0.3 + 0.9 * r.u()));
        const V3 pale = vmix(c1, V3(0.62, 0.56, 0.46), 0.5);
        for (int j = 0; j < sides; j++) {
            V3 n0, n1;
            const V3 p0 = P(i, j, n0), p1 = P(i, (j + 1) % sides, n1);
            out.tri(p0, p1, ctr, norm(n0 + T[i]), norm(n1 + T[i]), T[i], C(i, j), C(i, j + 1), pale, mat);
        }
    }
}

// A leaf card: a kite from `c` along `a` (length L), `w` wide across `s`. Its corners' normals lean from the clump's middle and
// from the crown's, so a clump shades round and the crown's far side is dark: a volume, not a sheet.
struct Crown {
    V3 c, r;
};
void card(Out& out, const V3& c, const V3& a, const V3& s, double L, double w, const V3& clump, const Crown& cr, const V3& col, const V3& tipCol, mk::Mat mat, Rng& r) {
    const V3 v[4] = {c - a * (L * 0.38), c + s * (w * 0.5) + a * (L * (0.02 + 0.12 * r.sym())), c + a * (L * 0.62), c - s * (w * 0.5) + a * (L * (0.02 + 0.12 * r.sym()))};
    V3 face = norm(cross(a, s));
    V3 n[4];
    for (int k = 0; k < 4; k++) {
        const V3 fromCrown = norm(V3((v[k].x - cr.c.x) / cr.r.x, (v[k].y - cr.c.y) / cr.r.y, (v[k].z - cr.c.z) / cr.r.z));
        const V3 fromClump = norm(v[k] - clump);
        const V3 f = dot(face, fromCrown) < 0 ? -face : face;
        n[k] = norm(fromCrown * 0.55 + fromClump * 0.3 + f * 0.25 + UP * 0.12);
    }
    const V3 cm = vmix(col, tipCol, 0.5);
    out.tri(v[0], v[1], v[2], n[0], n[1], n[2], col, cm, tipCol, mat);
    out.tri(v[0], v[2], v[3], n[0], n[2], n[3], col, tipCol, cm, mat);
}

// A puff: a small lumpy solid (a five-sided double pyramid, every corner pushed in or out so no two have the same outline),
// shaded round and from the crown, lighter on top.
void puff(Out& out, const V3& c, const V3& axis, double r, double tall, const Crown& cr, const V3& col, const V3& top, Rng& r_) {
    const V3 a = norm(UP * 0.5 + axis * 0.5 + r_.unit() * 0.9), u = perp(a), w = cross(a, u);
    const double turn = r_.u() * 2 * kPi;
    V3 v[7], n[7], k[7];
    v[0] = a * (r * (0.75 + 0.5 * r_.u())), v[1] = a * (-r * (0.6 + 0.4 * r_.u()));
    for (int i = 0; i < 5; i++) {
        const double ang = turn + 2 * kPi * (i + 0.5 * r_.sym()) / 5, rr = r * (0.7 + 0.65 * r_.u());
        v[2 + i] = (u * dm::cos(ang) + w * dm::sin(ang)) * rr + a * (r * 0.45 * r_.sym());
    }
    for (int i = 0; i < 7; i++) {
        V3 d = v[i];
        d.y *= tall;
        v[i] = c + d;
        const V3 fromCrown = norm(V3((v[i].x - cr.c.x) / cr.r.x, (v[i].y - cr.c.y) / cr.r.y, (v[i].z - cr.c.z) / cr.r.z));
        n[i] = norm(norm(d) * 0.5 + fromCrown * 0.5 + UP * 0.1);
        k[i] = vmix(col * 0.86, top, clamp01(0.5 + 0.5 * d.y / std::max(1e-9, r * tall)));
    }
    for (int i = 0; i < 5; i++) {
        const int p = 2 + i, q = 2 + (i + 1) % 5;
        out.tri(v[0], v[p], v[q], n[0], n[p], n[q], k[0], k[p], k[q], mk::FOLIAGE);
        out.tri(v[1], v[q], v[p], n[1], n[q], n[p], k[1], k[q], k[p], mk::FOLIAGE);
    }
}

const double kKeepPuff[4] = {1.0, 0.5, 0.2, 0.075};
const double kGrowPuff[4] = {1.0, 1.35, 2.0, 2.9};
const double kKeep[4] = {1.0, 0.36, 0.12, 0.04};
const double kGrow[4] = {1.0, 1.55, 2.5, 4.0};

void woody(const Species& sp, const Options& o, Out& out, double scale) {
    Woody g(sp);
    g.growAll(o.seed);
    g.fit(o.breast > 0 ? o.breast / scale : 0);
    const Colours col = coloursOf(sp, o);
    const int lod = std::min(3, std::max(0, o.lod)), orders = g.orders;
    const bool leafless = sp.v("cards") <= 0, green = sp.v("green_stems") > 0;
    const int maxSides = std::max(3, (int)sp.v("sides"));
    const double twig = sp.v("twig"), R0 = sp.v("trunk_radius");
    const size_t wood0 = out.m.v.size();
    // Stems and branches.
    for (size_t i = 0; i < g.br.size(); i++) {
        const Br& b = g.br[i];
        const bool bare = leafless || b.dead || green;  // (nothing hides it)
        const bool terminal = b.order == orders - 1;
        const bool stem = b.parent < 0;
        if (lod == 1) {
            if (terminal && orders > 2 && (!bare || (i % 2))) continue;
            if (!sp.tree && !bare && b.order >= 1) continue;
        } else if (lod == 2) {
            if (b.order >= 2) continue;
            // (a tree keeps its thick limbs: a trunk that stopped at the fork stood as a spike under a floating crown)
            const bool thick = sp.tree && orders > 3 && b.r[0] > R0 * 0.3;
            if (b.order == 1 && !thick && (!bare || (!sp.tree && (i % 2)) || (b.dead && !leafless && (i % 2)))) continue;
            if (!sp.tree && stem && i > 0 && (i % 2) && !leafless && !green) continue;
        } else if (lod == 3) {
            if (b.order >= 2) continue;
            if (b.order == 1 && !(leafless && sp.tree && !(i % 3)) && !(sp.tree && orders > 3 && b.r[0] > R0 * 0.45)) continue;
            if (stem && i > 0 && (i % 3)) continue;
        }
        const double rr = b.r[0] / std::max(1e-9, R0);
        int sides = rr > 0.6 ? maxSides : rr > 0.3 ? maxSides - 2 : rr > 0.14 ? maxSides - 3 : 3;
        sides = std::max(3, std::min(maxSides, sides) - (lod >= 2 ? 3 : lod));
        if (terminal && orders > 1) sides = 3;
        const int n = (int)b.p.size();
        const int stride = lod == 0 ? 1 : lod == 1 ? (n > 4 ? 2 : 1) : lod == 2 && sp.tree ? std::max(1, (n - 1) / 2) : std::max(1, n - 1);
        V3 c0 = vmix(col.bark, col.bark2, clamp01(b.order * 0.5)), c1 = vmix(col.bark, col.bark2, clamp01(b.order * 0.5 + 0.5));
        mk::Mat mat = mk::WOOD;
        if (b.dead) c0 = vmix(c0, col.dead, 0.7), c1 = col.dead * 1.08;
        else if (green && sp.v("green_stems") > 1) c0 = col.leaf, c1 = col.leaf2;  // (a cactus: green, but solid: it doesn't dissolve for the camera as leaves do)
        else if (green && b.order >= 1) c0 = col.leaf, c1 = col.leaf2, mat = mk::FOLIAGE;
        else if (green) c1 = vmix(c1, col.leaf, 0.6);
        tube(out, b, sides, stride, c0, c1, mat, o.seed * 31u + (uint32_t)i, !b.snapped || b.r.back() <= twig * 1.01);
    }
    const size_t woodEnd = out.m.v.size();
    // Foliage: clumps along the leafy branches.
    const int nCards = (int)sp.v("cards");
    if (nCards > 0) {
        std::vector<Site> sites;
        const int leafOrder = std::min(orders - 1, (int)sp.v("leaf_order"));
        const double from = sp.v("leaf_from"), step = sp.v("leaf_step"), leader = sp.v("leader");
        for (size_t i = 0; i < g.br.size(); i++) {
            const Br& b = g.br[i];
            if (b.dead) continue;
            double f0;
            if (b.order >= leafOrder) f0 = from;
            else if (b.order == 0 && leader > 0) f0 = 1 - leader;
            else continue;
            const double L = b.length;
            int n = step > 0 ? (int)(L * (1 - f0) / step) : 0;
            for (int k = 0; k <= n; k++) {
                Site s;
                const double at = L - k * step;
                s.p = along(b, at, &s.d), s.br = (int)i, s.along = L > 0 ? at / L : 1;
                sites.push_back(s);
            }
        }
        if (!sites.empty()) {
            Crown cr;
            V3 lo(1e9, 1e9, 1e9), hi(-1e9, -1e9, -1e9);
            for (const Site& s : sites)
                lo = V3(std::min(lo.x, s.p.x), std::min(lo.y, s.p.y), std::min(lo.z, s.p.z)), hi = V3(std::max(hi.x, s.p.x), std::max(hi.y, s.p.y), std::max(hi.z, s.p.z));
            const double cl = sp.v("card", 0), cw = sp.v("card", 1), scat = sp.v("scatter"), align = sp.v("align"), flat = sp.v("flat"), droop = sp.v("droop");
            cr.c = (lo + hi) * 0.5;
            cr.c.y = lerp(lo.y, hi.y, 0.4);
            cr.r = V3(std::max(0.03, (hi.x - lo.x) * 0.5 + cl), std::max(0.03, (hi.y - lo.y) * 0.6 + cl), std::max(0.03, (hi.z - lo.z) * 0.5 + cl));
            const double puffR = sp.v("puff", 0);
            const bool cutCards = sp.v("cut") > 0;
            const double ao = sp.v("ao"), grow = 1 + (kGrow[lod] - 1) * sp.v("far");
            Rng r(o.seed * 2654435761u + 99u);
            for (size_t si = 0; si < sites.size(); si++) {
                const Site& s = sites[si];
                Rng rs = r.fork((uint32_t)si);
                // (how deep in the crown: 0 its middle .. 1 its skin)
                const V3 rel((s.p.x - cr.c.x) / cr.r.x, (s.p.y - cr.c.y) / cr.r.y, (s.p.z - cr.c.z) / cr.r.z);
                const double depth = clamp01(len(rel) * 1.15), shade = (1 - ao) + ao * depth * depth, upper = clamp01(0.5 + 0.6 * rel.y);
                if (puffR > 0) {
                    Rng rp = rs.fork(7);
                    const double keep = rp.u(), tone = rp.u(), sz = 0.8 + 0.4 * rp.u();
                    if (keep < kKeepPuff[lod]) {
                        const double pr = puffR * sz * (1 + (kGrowPuff[lod] - 1) * sp.v("far"));
                        V3 pc = s.p;
                        pc.y = std::max(pc.y, pr * 0.6);
                        const V3 base = vmix(col.leaf, col.leaf2, tone) * shade * (0.9 + 0.2 * upper);
                        puff(out, pc, s.d, pr, sp.v("puff", 1), cr, base, base * (1 + col.tip), rp);
                    }
                }
                for (int c = 0; c < nCards; c++) {
                    Rng rc = rs.fork(31 + (uint32_t)c);
                    const double keep = rc.u();
                    const V3 R1 = rc.unit(), R2 = rc.unit(), off = rc.unit() * (scat * rc.u());
                    const double size = (0.75 + 0.5 * rc.u()) * grow, tone = rc.u(), fl = rc.u();
                    if (keep >= kKeep[lod]) continue;
                    V3 a = norm(s.d * align + R1 * (1 - align) + UP * 0.05);
                    if (flat > 0) a = norm(V3(a.x, a.y * (1 - flat), a.z));
                    if (droop != 0) a = norm(a - UP * droop);
                    V3 sd = cross(a, R2);
                    if (flat > 0) sd = vmix(norm(sd), norm(cross(a, UP)), flat);
                    sd = len(sd) > 1e-6 ? norm(sd) : perp(a);
                    V3 c0 = vmix(col.leaf, col.leaf2, tone) * shade * (0.9 + 0.2 * upper);
                    mk::Mat mat = cutCards ? mk::LEAFCUT : mk::FOLIAGE;
                    if (col.flowerShare > 0 && fl < col.flowerShare * (0.4 + 1.2 * upper) && depth > 0.5) c0 = col.flower * (0.85 + 0.3 * tone);
                    const V3 c1 = c0 * (1 + col.tip);
                    V3 cc = s.p + off;
                    cc.y = std::max(cc.y, cl * size * 0.3);
                    card(out, cc, a, sd, cl * size, cw * size, s.p, cr, c0, c1, mat, rc);
                }
            }
        }
    }
    // Snow lying on what faces up.
    if (o.snow) {
        auto& v = out.m.v;
        Rng r(o.seed + 4242u);
        for (size_t i = wood0; i + 2 < v.size(); i += 3) {
            const bool leaf = i >= woodEnd || v[i].mat == mk::FOLIAGE || v[i].mat == mk::LEAFCUT;
            const V3 p0(v[i].p[0], v[i].p[1], v[i].p[2]), p1(v[i + 1].p[0], v[i + 1].p[1], v[i + 1].p[2]), p2(v[i + 2].p[0], v[i + 2].p[1], v[i + 2].p[2]);
            const V3 gn = norm(cross(p1 - p0, p2 - p0));
            const double upn = leaf ? std::fabs(gn.y) * 0.6 + 0.4 * (v[i].n[1] + v[i + 1].n[1] + v[i + 2].n[1]) / 3 : (v[i].n[1] + v[i + 1].n[1] + v[i + 2].n[1]) / 3;
            const double jit = 0.3 * (r.u() - 0.5), sh = 0.9 + 0.1 * r.u();
            if (upn < (leaf ? 0.5 : 0.62) + jit) continue;
            for (int k = 0; k < 3; k++) {
                v[i + k].c[0] = (float)(SNOW.x * sh), v[i + k].c[1] = (float)(SNOW.y * sh), v[i + k].c[2] = (float)(SNOW.z * sh);
                v[i + k].mat = leaf ? mk::SNOWLEAF : mk::MATTE;
            }
        }
    }
}

// ---- Bunchgrass: a tuft of arching blades, some seed stalks ----------------------------------------------------------------
void grass(const Species& sp, const Options& o, Out& out) {
    const Colours col = coloursOf(sp, o);
    const int lod = std::min(3, std::max(0, o.lod));
    Rng r(o.seed * 7919u + 5u);
    const int n = std::max(3, (int)sp.v("blades"));
    const double bl = sp.v("blade", 0), bw = sp.v("blade", 1), spread = rad(sp.v("spread")), ring = sp.v("stem_base");
    const double grow = lod == 0 ? 1 : lod == 1 ? 1.5 : lod == 2 ? 2.4 : 3.4;
    const int sMin = (int)sp.v("stalks", 0), sMax = std::max(sMin, (int)sp.v("stalks", 1));
    const int stalks = sMin + (int)(r.u() * (sMax - sMin + 0.999));
    for (int k = 0; k < n + stalks; k++) {
        Rng rb = r.fork((uint32_t)k);
        const bool stalk = k >= n;
        const double keep = rb.u(), az = rb.u() * 2 * kPi, lean = spread * (stalk ? 0.35 : 0.25 + 0.95 * rb.u()), L = (stalk ? sp.v("stalk_len") : bl) * (0.6 + 0.4 * rb.u());
        const double tone = rb.u(), bend = 0.25 + 0.5 * rb.u(), ro = ring * std::sqrt(rb.u()), ra = rb.u() * 2 * kPi;
        if (keep >= kKeep[lod] && k >= 2 && !(stalk && lod < 2 && keep < 0.7)) continue;  // (the first two always: a far copy is never nothing)
        const V3 outw(dm::cos(az), 0, dm::sin(az)), side(-outw.z, 0, outw.x);
        const V3 d0 = norm(UP * dm::cos(lean) + outw * dm::sin(lean)), d1 = norm(d0 + outw * bend - UP * (stalk ? 0.0 : 0.25 * bend));
        const V3 p0 = V3(dm::cos(ra), 0, dm::sin(ra)) * ro - UP * 0.02, p1 = p0 + d0 * (L * 0.55), p2 = p1 + d1 * (L * 0.45);
        const double w = (stalk ? bw * 0.45 : bw) * grow;
        const V3 n0 = norm(outw * 0.5 + UP * 0.8), cb = vmix(col.leaf, col.leaf2, 0.3 * tone) * 0.8, cm = vmix(col.leaf, col.leaf2, 0.5 + 0.5 * tone), ct = stalk && len(col.flower) > 0 ? col.flower : col.leaf2 * (1 + col.tip);
        if (lod >= 2) {
            out.tri(p0 - side * w, p0 + side * w, p2, n0, n0, n0, cb, cb, ct, mk::FOLIAGE);
            continue;
        }
        out.tri(p0 - side * (w * 0.5), p0 + side * (w * 0.5), p1 + side * (w * 0.4), n0, n0, n0, cb, cb, cm, mk::FOLIAGE);
        out.tri(p0 - side * (w * 0.5), p1 + side * (w * 0.4), p1 - side * (w * 0.4), n0, n0, n0, cb, cm, cm, mk::FOLIAGE);
        out.tri(p1 - side * (w * 0.4), p1 + side * (w * 0.4), p2, n0, n0, norm(n0 + UP), cm, cm, ct, mk::FOLIAGE);
        if (stalk && lod == 0) {  // a seed head: a little open spray
            for (int q = 0; q < 3; q++) {
                const V3 e = rb.unit();
                out.tri(p2 - d1 * (L * 0.07), p2 + e * (L * 0.035), p2 + d1 * (L * 0.03) + cross(e, d1) * (L * 0.025), n0, n0, n0, ct * 0.9, ct, ct, mk::FOLIAGE);
            }
        }
    }
}

// ---- A rosette (yucca): stiff sword leaves every way from a crown, a skirt of dead ones, a flower stalk on some --------------
void rosette(const Species& sp, const Options& o, Out& out) {
    const Colours col = coloursOf(sp, o);
    const int lod = std::min(3, std::max(0, o.lod));
    Rng r(o.seed * 7919u + 9u);
    const int n = std::max(6, (int)sp.v("blades"));
    const double bl = sp.v("blade", 0), bw = sp.v("blade", 1), trunk = sp.v("trunk_len") < 0.99 ? sp.v("trunk_len") : 0.0, skirt = sp.v("skirt");
    const double grow = lod == 0 ? 1 : lod == 1 ? 1.4 : lod == 2 ? 2.1 : 3.0;
    const V3 head(0, trunk + bl * 0.08, 0), dry = sp.col("leaf_dry");
    if (trunk > 0.03 && lod < 3) {
        Br b;
        b.p = {V3(0, -0.02, 0), V3(0, trunk * 0.5, 0), V3(0, trunk, 0)};
        const double tr = sp.v("trunk_radius");
        b.r = {tr * 1.2, tr, tr * 0.9};
        tube(out, b, lod ? 4 : 6, 1, col.bark, col.bark2, mk::WOOD, o.seed, false);
    }
    for (int k = 0; k < n; k++) {
        Rng rb = r.fork((uint32_t)k);
        const double keep = rb.u(), az = k * 2.39996323 + 0.4 * rb.sym(), f = (k + 0.5) / n;  // f: 0 the newest, upright .. 1 the oldest, lowest
        const double L = bl * (0.7 + 0.3 * rb.u()) * (0.75 + 0.25 * dm::sin(kPi * std::min(1.0, f * 1.4))), tone = rb.u();
        if (keep >= kKeep[lod]) continue;
        const bool deadLeaf = f > 1 - skirt;
        const double elev = deadLeaf ? rad(-25 - 50 * rb.u()) : rad(86 - 92 * f + 8 * rb.sym());  // (from the horizontal)
        const V3 outw(dm::cos(az), 0, dm::sin(az)), side(-outw.z, 0, outw.x);
        const V3 d = norm(outw * dm::cos(elev) + UP * dm::sin(elev)), nrm = norm(cross(side, d));
        const V3 p0 = head + outw * (bw * 0.6) - UP * (deadLeaf ? bl * 0.06 : 0.0), p1 = p0 + d * (L * 0.3), p2 = p0 + d * L;
        const double w = bw * grow * (deadLeaf ? 0.8 : 1.0);
        const V3 cb = (deadLeaf ? dry * 0.85 : vmix(col.leaf, col.leaf2, tone) * 0.75), ct = deadLeaf ? dry : vmix(col.leaf, col.leaf2, tone) * (1 + col.tip);
        const V3 nn = norm(nrm + UP * 0.4 + outw * 0.3);
        out.tri(p0 - side * (w * 0.3), p0 + side * (w * 0.3), p1 + side * (w * 0.5), nn, nn, nn, cb, cb, ct * 0.9, mk::FOLIAGE);
        out.tri(p0 - side * (w * 0.3), p1 + side * (w * 0.5), p1 - side * (w * 0.5), nn, nn, nn, cb, ct * 0.9, ct * 0.9, mk::FOLIAGE);
        out.tri(p1 - side * (w * 0.5), p1 + side * (w * 0.5), p2, nn, nn, nn, ct * 0.9, ct * 0.9, ct, mk::FOLIAGE);
    }
    const int sMin = (int)sp.v("stalks", 0), sMax = std::max(sMin, (int)sp.v("stalks", 1));
    const int stalks = sMin + (int)(r.u() * (sMax - sMin + 0.999));
    for (int s = 0; s < stalks && lod < 3; s++) {
        Rng rb = r.fork(900 + (uint32_t)s);
        const double H = sp.v("stalk_len") * (0.8 + 0.3 * rb.u());
        const V3 lean = rb.unit() * 0.08;
        Br b;
        b.p = {head, head + (UP + lean) * (H * 0.5), head + (UP + lean * 1.6) * H};
        b.r = {bw * 0.22, bw * 0.16, bw * 0.08};
        tube(out, b, 3, 1, col.leaf * 0.9, vmix(col.leaf2, dry, 0.5), mk::WOOD, o.seed + 3, true);
        if (len(col.flower) <= 0 || o.dry > 0.5) continue;
        const int bells = lod == 0 ? 14 : lod == 1 ? 7 : 3;
        for (int q = 0; q < bells; q++) {  // the flowers: cream bells down the top half
            const double t = 0.5 + 0.5 * (q + 0.5) / bells, a = q * 2.39996323, sz = bl * 0.11 * (lod >= 2 ? 2.0 : 1.0);
            const V3 c = head + (UP + lean * 1.3) * (H * t), e(dm::cos(a), 0, dm::sin(a)), sd(-e.z, 0, e.x);
            const V3 fc = col.flower * (0.9 + 0.2 * rb.u());
            out.tri(c + e * (sz * 0.3), c + e * (sz * 1.2) + sd * (sz * 0.5) - UP * sz, c + e * (sz * 1.2) - sd * (sz * 0.5) - UP * sz, e, norm(e + sd), norm(e - sd), fc, fc * 0.9, fc * 0.9, mk::MATTE);
        }
    }
}

// ---- Cactus pads (prickly pear): flat oval pads growing off one another, a low sprawling clump -----------------------------
void padMesh(Out& out, const V3& base, const V3& a, const V3& s, double L, double w, double th, const V3& c0, const V3& c1, int lod) {
    const V3 f = norm(cross(a, s)), ctr = base + a * (L * 0.5);
    if (lod >= 2) {
        const V3 v0 = base, v1 = ctr + s * (w * 0.55), v2 = base + a * L, v3 = ctr - s * (w * 0.55);
        out.tri(v0, v1, v2, f, norm(f + s), norm(f + a), c0, c1, c1, mk::FOLIAGE);
        out.tri(v0, v2, v3, f, norm(f + a), norm(f - s), c0, c1, c1, mk::FOLIAGE);
        return;
    }
    const int rim = lod == 0 ? 8 : 6;
    std::vector<V3> e(rim), en(rim);
    for (int k = 0; k < rim; k++) {
        const double ang = 2 * kPi * k / rim, ca = dm::cos(ang), sa = dm::sin(ang);
        // (an egg: narrow at the joint, widest past the middle)
        const double wide = w * 0.5 * (1 + 0.25 * ca);
        e[k] = ctr + a * (L * 0.5 * ca) + s * (wide * sa);
        en[k] = norm(a * ca + s * sa);
    }
    for (int side = 0; side < 2; side++) {
        const V3 fn = side ? -f : f, mid = ctr + fn * (th * 0.5);
        for (int k = 0; k < rim; k++) {
            const int j = (k + 1) % rim;
            const V3 ca_ = vmix(c0, c1, 0.5 + 0.5 * dot(norm(e[k] - base), a)), cb_ = vmix(c0, c1, 0.5 + 0.5 * dot(norm(e[j] - base), a));
            out.tri(mid, e[k], e[j], fn, norm(en[k] + fn * 0.5), norm(en[j] + fn * 0.5), vmix(c0, c1, 0.6), ca_, cb_, mk::FOLIAGE);
        }
    }
}
void pads(const Species& sp, const Options& o, Out& out) {
    const Colours col = coloursOf(sp, o);
    const int lod = std::min(3, std::max(0, o.lod));
    Rng r(o.seed * 7919u + 21u);
    const int nMin = std::max(2, (int)sp.v("pads", 0)), nMax = std::max(nMin, (int)sp.v("pads", 1));
    const int n = nMin + (int)(r.u() * (nMax - nMin + 0.999));
    const double L0 = sp.v("pad", 0), W0 = sp.v("pad", 1), th = sp.v("pad", 2), reach = sp.v("width") > 0 ? sp.v("width") : 0.8;
    struct Pad {
        V3 base, a, s;
        double L, w;
        int level;
    };
    std::vector<Pad> ps;
    const int roots = std::max(2, n / 3);
    for (int k = 0; k < n; k++) {
        Rng rp = r.fork((uint32_t)k);
        Pad p;
        const double L = L0 * (0.75 + 0.4 * rp.u()), w = W0 * (0.8 + 0.35 * rp.u());
        if (k < roots) {  // off the ground, round the clump, leaning out
            const double az = 2 * kPi * k / roots + 0.6 * rp.sym(), ro = reach * (0.1 + 0.42 * std::sqrt(rp.u())), lean = rad(15 + 45 * rp.u());
            const V3 outw(dm::cos(az), 0, dm::sin(az));
            p.base = outw * ro - UP * (L * 0.12);
            p.a = norm(UP * dm::cos(lean) + outw * dm::sin(lean));
            p.level = 0;
        } else {  // off the top edge of an earlier pad
            const Pad& pa = ps[(size_t)(rp.u() * ps.size()) % ps.size()];
            const double t = 0.7 + 0.3 * rp.u(), sideways = rp.sym();
            p.base = pa.base + pa.a * (pa.L * t) + pa.s * (pa.w * 0.35 * sideways);
            p.a = norm(pa.a + pa.s * (0.9 * sideways) + UP * 0.35 + rp.unit() * 0.45);
            if (p.a.y < 0.1) p.a = norm(V3(p.a.x, 0.1, p.a.z));
            p.level = pa.level + 1;
        }
        p.s = norm(cross(p.a, rp.unit()));
        p.L = L * (p.level ? 0.92 : 1.0), p.w = w;
        ps.push_back(p);
    }
    for (size_t k = 0; k < ps.size(); k++) {
        Rng rp = r.fork(500 + (uint32_t)k);
        const double keep = rp.u(), tone = rp.u();
        if (lod == 3 && keep > 0.45) continue;
        const Pad& p = ps[k];
        const double gr = lod == 3 ? 1.5 : 1.0;
        const V3 c0 = vmix(col.leaf, col.leaf2, tone) * 0.82, c1 = vmix(col.leaf, col.leaf2, tone) * (1 + col.tip);
        padMesh(out, p.base, p.a, p.s, p.L * gr, p.w * gr, th, c0, c1, lod);
        // Fruit along the top edge of some (red-purple tunas).
        if (lod == 0 && len(col.flower) > 0 && rp.u() < col.flowerShare) {
            const int nf = 1 + (int)(rp.u() * 2.5);
            for (int q = 0; q < nf; q++) {
                const double sgn = rp.sym() * 0.7;
                const V3 c = p.base + p.a * (p.L * (0.98 - 0.12 * std::fabs(sgn))) + p.s * (p.w * 0.45 * sgn), up2 = norm(p.a + p.s * sgn * 0.6), f = norm(cross(p.a, p.s));
                const double fs = L0 * 0.16;
                out.tri(c - p.s * (fs * 0.5), c + p.s * (fs * 0.5), c + up2 * (fs * 1.6), -p.s, p.s, up2, col.flower * 0.8, col.flower * 0.8, col.flower, mk::MATTE);
                out.tri(c - f * (fs * 0.5), c + f * (fs * 0.5), c + up2 * (fs * 1.6), -f, f, up2, col.flower * 0.8, col.flower * 0.8, col.flower, mk::MATTE);
            }
        }
    }
}

std::string plantsDir() {
    if (const char* env = std::getenv("CZ_PLANTS_DIR")) return env;
    std::vector<std::string> bases = {"."};
    if (const std::string exe = exeDir(); !exe.empty()) bases.push_back(exe), bases.push_back(exe + "/../Resources");
    const char* rel[] = {"/assets", "/../assets", "/../../assets", "/../../../assets"};
    for (const std::string& b : bases)
        for (const char* r : rel)
            if (std::ifstream(b + r + "/plants/index.txt")) return b + r + "/plants";
    return "";
}

struct Library {
    std::string dir;
    std::vector<Species> all;
    std::vector<std::string> errors;
};
const Library& library() {
    static const Library lib = [] {
        Library l;
        l.dir = plantsDir();
        if (l.dir.empty()) return l;
        // index.txt lists the species (a name a line): no directory listing needed, and the order is the file's.
        std::ifstream idx(l.dir + "/index.txt");
        for (std::string line; std::getline(idx, line);) {
            const size_t h = line.find('#');
            if (h != std::string::npos) line.resize(h);
            std::istringstream ls(line);
            std::string name;
            if (!(ls >> name)) continue;
            std::ifstream f(l.dir + "/" + name + ".txt");
            if (!f) {
                l.errors.push_back(name + ": no such file");
                continue;
            }
            std::stringstream ss;
            ss << f.rdbuf();
            Species s;
            std::string err;
            if (!parse(name, ss.str(), s, &err)) {
                l.errors.push_back(name + ": " + err);
                continue;
            }
            for (const Species& o : l.all)
                if (o.tree == s.tree && o.id == s.id) err = "id " + std::to_string(s.id) + " is " + o.key + "'s";
            if (s.id < 0) err = "no id";
            if (!err.empty()) {
                l.errors.push_back(name + ": " + err);
                continue;
            }
            l.all.push_back(std::move(s));
        }
        for (const std::string& e : l.errors) std::fprintf(stderr, "plants: %s\n", e.c_str());
        return l;
    }();
    return lib;
}

}  // namespace

const std::vector<KeyInfo>& keys() { return keyTable(); }

double Species::v(const char* k, size_t i) const {
    auto it = num.find(k);
    const std::vector<double>* a = it != num.end() ? &it->second : nullptr;
    if (!a || a->empty()) {
        auto d = defaults().find(k);
        if (d == defaults().end() || d->second.empty()) return 0;
        a = &d->second;
    }
    return (*a)[std::min(i, a->size() - 1)];
}
V3 Species::col(const char* k) const { return V3(v(k, 0), v(k, 1), v(k, 2)); }
std::string Species::w(const char* k) const {
    auto it = word.find(k);
    if (it != word.end()) return it->second;
    const KeyInfo* i = keyInfo(k);
    return i ? i->def : "";
}

bool parse(const std::string& key, const std::string& text, Species& out, std::string* err) {
    out = Species();
    out.key = key;
    auto fail = [&](const std::string& why) {
        if (err) *err = why;
        return false;
    };
    auto apply = [&](const std::string& src, bool own) -> bool {
        std::istringstream in(src);
        std::string line;
        int ln = 0;
        while (std::getline(in, line)) {
            ln++;
            const size_t h = line.find('#');
            if (h != std::string::npos) line.resize(h);
            std::istringstream ls(line);
            std::string k;
            if (!(ls >> k)) continue;
            k = lower(k);
            std::string rest;
            std::getline(ls, rest);
            const size_t a = rest.find_first_not_of(" \t\r"), b = rest.find_last_not_of(" \t\r");
            rest = a == std::string::npos ? "" : rest.substr(a, b - a + 1);
            const std::string at = "line " + std::to_string(ln) + ": ";
            if (!keyInfo(k)) return fail(at + "unknown key " + k);
            if (rest.empty()) return fail(at + k + " needs a value");
            if (k == "name") out.title = rest;
            else if (k == "about") out.blurb = rest;
            else if (k == "class") {
                if (lower(rest) != "tree" && lower(rest) != "shrub") return fail(at + "class is tree or shrub");
                out.tree = lower(rest) == "tree";
            } else if (k == "replaces") out.replaces.push_back(lower(rest));
            else if (k == "form") {
                if (own) continue;  // (read first: below)
            } else if (k == "crown") out.word[k] = lower(rest);
            else {
                for (char c : rest)
                    if (!(std::isdigit((unsigned char)c) || c == '.' || c == '-' || c == '+' || c == ' ' || c == '\t' || c == 'e')) return fail(at + k + " takes numbers");
                out.num[k] = numbers(rest);
                if (out.num[k].empty()) return fail(at + k + " needs a value");
            }
        }
        return true;
    };
    // The form first (its preset under the file's own lines).
    {
        std::istringstream in(text);
        for (std::string line; std::getline(in, line);) {
            const size_t h = line.find('#');
            if (h != std::string::npos) line.resize(h);
            std::istringstream ls(line);
            std::string k, v;
            if (ls >> k >> v && lower(k) == "form") out.form = lower(v);
        }
    }
    static const char* const forms[] = {"excurrent", "decurrent", "multistem", "shrub", "wand", "bunchgrass", "rosette", "pads"};
    bool known = false;
    for (const char* f : forms) known = known || out.form == f;
    if (!known) return fail(out.form.empty() ? "no form" : "unknown form " + out.form);
    if (!apply(presetFor(out.form), false)) return false;
    if (!apply(text, true)) return false;
    if (out.num.count("id")) out.id = (int)out.num["id"][0];
    if (out.title.empty()) out.title = key;
    for (char& c : out.title) c = (char)std::toupper((unsigned char)c);
    return true;
}

void generate(const Species& s, const Options& o, mk::Mesh& m) {
    const size_t from = m.v.size();
    Out out{m};
    if (s.woody()) woody(s, o, out, s.tree ? kTreeHeight : 1.0);
    else if (s.form == "bunchgrass") grass(s, o, out);
    else if (s.form == "rosette") rosette(s, o, out);
    else pads(s, o, out);
    // To the model's size: a tree 10 tall; a shrub 0.8 in half-width (its `width` is its half-width over its height).
    double k = kTreeHeight;
    if (!s.tree) {
        const double w = s.v("width") > 0 ? s.v("width") : 0.5;
        k = kShrubRadius / w;
    }
    if (s.tree) {  // (exactly the model's height: the skeleton's fit allows for foliage it can only guess at)
        double top = 0;
        for (size_t i = from; i < m.v.size(); i++) top = std::max(top, (double)m.v[i].p[1]);
        if (top > 0.75 && top < 1.3) k /= top;
    }
    for (size_t i = from; i < m.v.size(); i++)
        for (int c = 0; c < 3; c++) m.v[i].p[c] = (float)(m.v[i].p[c] * k);
    m.finish();
}

double trunkRadius(const Species& s, double y) {
    if (!s.woody()) return 0.1;
    Woody g(s);
    g.growAll(1);
    g.fit(0);
    return g.radiusAt(y / kTreeHeight) * kTreeHeight;
}

const std::vector<Species>& all() { return library().all; }
const std::string& dir() { return library().dir; }
const std::vector<std::string>& loadErrors() { return library().errors; }
const Species* find(const std::string& key) {
    for (const Species& s : all())
        if (s.key == key) return &s;
    return nullptr;
}
const Species* treeById(int id) {
    for (const Species& s : all())
        if (s.tree && s.id == id) return &s;
    return nullptr;
}
const Species* shrubById(int id) {
    for (const Species& s : all())
        if (!s.tree && s.id == id) return &s;
    return nullptr;
}
const Species* replacing(const std::string& what) {
    for (const Species& s : all())
        for (const std::string& r : s.replaces)
            if (r == what) return &s;
    return nullptr;
}

uint64_t textHash() {
    static const uint64_t h = [] {
        uint64_t v = 1469598103934665603ull;
        for (const Species& s : all()) {
            std::ifstream f(dir() + "/" + s.key + ".txt");
            std::stringstream ss;
            ss << f.rdbuf();
            for (char c : s.key + ss.str()) v = (v ^ (uint8_t)c) * 1099511628211ull;
        }
        return v;
    }();
    return h;
}

namespace {
bool& everywhereSwitch() {
    static bool on = [] {  // (on unless asked for the classic look)
        const char* e = std::getenv("CZ_CLASSIC_PLANTS");
        return !(e && *e && *e != '0');
    }();
    return on;
}
thread_local int everywhereHere = -1;  // -1: the switch; 0, 1: this thread's own
}  // namespace
bool everywhere() { return everywhereHere >= 0 ? everywhereHere != 0 : everywhereSwitch(); }
void setEverywhere(bool on) { everywhereSwitch() = on; }
EverywhereHere::EverywhereHere(bool on) : was(everywhereHere) { everywhereHere = on ? 1 : 0; }
EverywhereHere::~EverywhereHere() { everywhereHere = was; }

}  // namespace plantgen

// A vehicle's condition as one line of text (carsave.h).
//
// The bulky parts are little-endian binary in base64 (no spaces, no '='-padding trouble: a
// value runs to the next space):
//   dents   u32 count, then that many i16 (net::carDents: mm, x y z a node)
//   beams   u32 beam count, then per beam that isn't factory (bent or broken): varint gap
//           from the last one listed, u8 flags (1 broken, 2 bent), bent: f64 L0 - Lorig
//   frames  the body's chassis frame: u32 members, then those whose rest place moved:
//           varint gap, 3 x f32 (m, from its factory rest); then each ladder rail and
//           knuckle frame: u32 members, 3 x f32 rest a member
//   pins    u32 pin count, then the broken ones as varint gaps (terminated by the count)
//   tris    u32 tri count, then the torn or shattered: varint gap, u8 (1 torn, 2 shattered)
//   panels  u32 panel count, then per panel: u8 opened by hand, i8 which way it swings
// A gap list ends at the end of its blob.
#include "carsave.h"

#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <map>

#include "net.h"
#include "softbody.h"

namespace carsave {

namespace {

constexpr const char* kTag = "car1";

// ---- Bytes and base64 ----

struct Out {
    std::vector<uint8_t> b;
    template <class T> void put(T v) {
        uint8_t raw[sizeof(T)];
        std::memcpy(raw, &v, sizeof(T));
        b.insert(b.end(), raw, raw + sizeof(T));  // (every platform the game builds for is little-endian)
    }
    void var(uint32_t v) {
        while (v >= 0x80) b.push_back((uint8_t)(v | 0x80)), v >>= 7;
        b.push_back((uint8_t)v);
    }
};

struct In {
    const std::vector<uint8_t>& b;
    size_t at = 0;
    bool ok = true;
    explicit In(const std::vector<uint8_t>& b) : b(b) {}
    bool done() const { return at >= b.size(); }
    template <class T> T get() {
        T v{};
        if (at + sizeof(T) > b.size()) {
            ok = false;
            return v;
        }
        std::memcpy(&v, &b[at], sizeof(T));
        at += sizeof(T);
        return v;
    }
    uint32_t var() {
        uint32_t v = 0;
        for (int sh = 0; sh < 35; sh += 7) {
            if (at >= b.size()) break;
            const uint8_t x = b[at++];
            v |= (uint32_t)(x & 0x7f) << sh;
            if (!(x & 0x80)) return v;
        }
        ok = false;
        return 0;
    }
};

const char* kB64 = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";

std::string toB64(const std::vector<uint8_t>& b) {
    std::string s;
    s.reserve((b.size() + 2) / 3 * 4);
    for (size_t i = 0; i < b.size(); i += 3) {
        const uint32_t v = (uint32_t)b[i] << 16 | (i + 1 < b.size() ? (uint32_t)b[i + 1] << 8 : 0) | (i + 2 < b.size() ? b[i + 2] : 0);
        s += kB64[v >> 18 & 63];
        s += kB64[v >> 12 & 63];
        s += i + 1 < b.size() ? kB64[v >> 6 & 63] : '=';
        s += i + 2 < b.size() ? kB64[v & 63] : '=';
    }
    return s;
}

bool fromB64(const std::string& s, std::vector<uint8_t>& out) {
    out.clear();
    if (s.size() % 4) return false;
    out.reserve(s.size() / 4 * 3);
    for (size_t i = 0; i < s.size(); i += 4) {
        int v[4], pad = 0;
        for (int k = 0; k < 4; k++) {
            const char ch = s[i + k];
            if (ch == '=' && i + 4 == s.size() && k >= 2) {
                v[k] = 0, pad++;
                continue;
            }
            if (pad) return false;  // (data after padding)
            const char* p = ch ? std::strchr(kB64, ch) : nullptr;
            if (!p) return false;
            v[k] = (int)(p - kB64);
        }
        const uint32_t x = (uint32_t)v[0] << 18 | (uint32_t)v[1] << 12 | (uint32_t)v[2] << 6 | (uint32_t)v[3];
        out.push_back((uint8_t)(x >> 16));
        if (pad < 2) out.push_back((uint8_t)(x >> 8));
        if (pad < 1) out.push_back((uint8_t)x);
    }
    return true;
}

// ---- Numbers as text ----

std::string num(double v) {
    char buf[40];
    std::snprintf(buf, sizeof buf, "%.17g", v);
    return buf;
}

bool toNum(const std::string& s, double& v) {
    if (s.empty()) return false;
    char* end = nullptr;
    v = std::strtod(s.c_str(), &end);
    return end == s.c_str() + s.size() && std::isfinite(v);
}

bool toInt(const std::string& s, int& v) {
    double d;
    if (!toNum(s, d) || d != std::floor(d) || std::fabs(d) > 1e9) return false;
    v = (int)d;
    return true;
}

// "a,b,c" -> numbers; false unless exactly `n` good ones.
bool nums(const std::string& s, double* v, size_t n, char sep = ',') {
    size_t from = 0;
    for (size_t k = 0; k < n; k++) {
        const size_t to = s.find(sep, from);
        if ((to == std::string::npos) != (k + 1 == n)) return false;
        if (!toNum(s.substr(from, to == std::string::npos ? std::string::npos : to - from), v[k])) return false;
        from = to + 1;
    }
    return true;
}

std::vector<std::string> split(const std::string& s, char sep) {
    std::vector<std::string> v;
    size_t from = 0;
    for (;;) {
        const size_t to = s.find(sep, from);
        v.push_back(s.substr(from, to == std::string::npos ? std::string::npos : to - from));
        if (to == std::string::npos) return v;
        from = to + 1;
    }
}

// The text's key=value fields; false unless it's "car1 ... end".
bool fields(const std::string& text, std::map<std::string, std::string>& kv) {
    kv.clear();
    const std::vector<std::string> words = split(text, ' ');
    if (words.size() < 2 || words.front() != kTag || words.back() != "end") return false;
    for (size_t i = 1; i + 1 < words.size(); i++) {
        const size_t eq = words[i].find('=');
        if (eq == std::string::npos || eq == 0) return false;
        kv[words[i].substr(0, eq)] = words[i].substr(eq + 1);
    }
    return true;
}

// Which catalog vehicle and mods a car is (net.cpp's specId does the same for the wire).
bool identify(const Car& c, int& vehicle, ModSet& mods) {
    mods = ModSet();
    if (!c.spec) return false;
    if (c.spec->model >= 0) {
        vehicle = c.spec->model;
        for (int k = 0; k < MOD_SLOTS; k++) mods.pick[k] = c.spec->mods[k];
        return true;
    }
    const auto& cat = vehicleCatalog();
    for (size_t k = 0; k < cat.size(); k++)
        if (&cat[k] == c.spec) return vehicle = (int)k, true;
    return false;
}

enum WheelBits { W_POPPED = 1, W_DETACHED = 2, W_SHAFT = 4, W_OFF = 8, W_DEBEADED = 16, W_CHAINED = 32 };

// ---- The frames: the body's chassis (against its factory rest), the ladder rails and knuckles (whole) ----

template <class C, class F> auto wholeFrames(C& c) -> std::vector<F*> {
    std::vector<F*> v = {&c.railFrame[0], &c.railFrame[1]};
    for (auto& k : c.knuckles) v.push_back(&k.frame);
    return v;
}
std::vector<ChassisFrame*> wholeFrames(Car& c) { return wholeFrames<Car, ChassisFrame>(c); }
std::vector<const ChassisFrame*> wholeFrames(const Car& c) { return wholeFrames<const Car, const ChassisFrame>(c); }

std::vector<uint8_t> framesBlob(const Car& c) {
    Out o;
    const ChassisFrame& ch = c.chassis;
    const bool rest0 = c.chassisRest0.size() == ch.rest.size();
    o.put<uint32_t>((uint32_t)ch.rest.size());
    o.put<uint32_t>(0);  // (moved members: filled in below)
    const size_t countAt = o.b.size() - 4;
    uint32_t moved = 0, last = 0;
    for (size_t k = 0; k < ch.rest.size(); k++) {
        const V3 d = rest0 ? ch.rest[k] - c.chassisRest0[k] : ch.rest[k];
        if (d.x == 0 && d.y == 0 && d.z == 0) continue;
        o.var((uint32_t)k - last), last = (uint32_t)k;
        o.put<float>((float)d.x), o.put<float>((float)d.y), o.put<float>((float)d.z);
        moved++;
    }
    std::memcpy(&o.b[countAt], &moved, 4);
    for (const ChassisFrame* cf : wholeFrames(c)) {
        o.put<uint32_t>((uint32_t)cf->rest.size());
        for (const V3& p : cf->rest) o.put<float>((float)p.x), o.put<float>((float)p.y), o.put<float>((float)p.z);
    }
    return o.b;
}

bool applyFrames(Car& c, const std::vector<uint8_t>& b) {
    In in(b);
    ChassisFrame& ch = c.chassis;
    if (in.get<uint32_t>() != ch.rest.size() || c.chassisRest0.size() != ch.rest.size()) return false;
    const uint32_t moved = in.get<uint32_t>();
    uint32_t k = 0;
    for (uint32_t m = 0; m < moved && in.ok; m++) {
        k += in.var();
        const float x = in.get<float>(), y = in.get<float>(), z = in.get<float>();
        if (k >= ch.rest.size() || !std::isfinite(x) || !std::isfinite(y) || !std::isfinite(z)) return false;
        ch.rest[k] = c.chassisRest0[k] + V3(x, y, z);
    }
    for (ChassisFrame* cf : wholeFrames(c)) {
        if (in.get<uint32_t>() != cf->rest.size()) return false;
        for (V3& p : cf->rest) {
            const float x = in.get<float>(), y = in.get<float>(), z = in.get<float>();
            if (!std::isfinite(x) || !std::isfinite(y) || !std::isfinite(z)) return false;
            p = V3(x, y, z);
        }
    }
    return in.ok && in.done();
}

// ---- Gap lists: the indices of what isn't factory, each with a few bytes ----

std::vector<uint8_t> beamsBlob(const Car& c) {
    Out o;
    o.put<uint32_t>((uint32_t)c.beams.size());
    uint32_t last = 0;
    for (size_t i = 0; i < c.beams.size(); i++) {
        const Beam& b = c.beams[i];
        // (A tie rod's length follows the rack: its bend is the wheel's tieOffset.)
        const bool bent = b.L0 != b.Lorig && b.type != BEAM_TIEROD;
        if (!b.broken && !bent) continue;
        o.var((uint32_t)i - last), last = (uint32_t)i;
        o.put<uint8_t>((uint8_t)(b.broken | bent << 1));
        if (bent) o.put<double>(b.L0 - b.Lorig);
    }
    return o.b;
}

bool applyBeams(Car& c, const std::vector<uint8_t>& b) {
    In in(b);
    if (in.get<uint32_t>() != c.beams.size()) return false;
    uint32_t i = 0;
    bool first = true;
    while (in.ok && !in.done()) {
        const uint32_t gap = in.var();
        if (!first && gap == 0) return false;
        i += gap, first = false;
        const uint8_t f = in.get<uint8_t>();
        if (i >= c.beams.size() || f == 0 || f > 3) return false;
        Beam& bm = c.beams[i];
        bm.broken = f & 1;
        if (f & 2) {
            const double d = in.get<double>();
            if (!std::isfinite(d) || bm.Lorig + d <= 0) return false;
            bm.L0 = bm.Lorig + d;
        }
    }
    return in.ok;
}

template <class F> std::vector<uint8_t> gapList(size_t count, F flagOf) {
    Out o;
    o.put<uint32_t>((uint32_t)count);
    uint32_t last = 0;
    for (size_t i = 0; i < count; i++) {
        const uint8_t f = flagOf(i);
        if (!f) continue;
        o.var((uint32_t)i - last), last = (uint32_t)i;
        o.put<uint8_t>(f);
    }
    return o.b;
}

template <class F> bool readGapList(const std::vector<uint8_t>& b, size_t count, uint8_t maxFlag, F set) {
    In in(b);
    if (in.get<uint32_t>() != count) return false;
    uint32_t i = 0;
    bool first = true;
    while (in.ok && !in.done()) {
        const uint32_t gap = in.var();
        if (!first && gap == 0) return false;
        i += gap, first = false;
        const uint8_t f = in.get<uint8_t>();
        if (!in.ok || i >= count || f == 0 || f > maxFlag) return false;
        set(i, f);
    }
    return in.ok;
}

std::vector<uint8_t> panelsBlob(const Car& c) {
    Out o;
    o.put<uint32_t>((uint32_t)c.panels.size());
    for (const Panel& p : c.panels) {
        o.put<uint8_t>(p.hand);
        o.put<int8_t>((int8_t)(p.handSign > 0 ? 1 : p.handSign < 0 ? -1 : 0));
    }
    return o.b;
}

bool applyPanels(Car& c, const std::vector<uint8_t>& b) {
    In in(b);
    if (in.get<uint32_t>() != c.panels.size()) return false;
    for (Panel& p : c.panels) {
        const uint8_t hand = in.get<uint8_t>();
        const int8_t sign = in.get<int8_t>();
        if (hand > 1 || sign < -1 || sign > 1) return false;
        p.hand = hand;
        p.handSign = sign;
    }
    return in.ok && in.done();
}

std::string gateText(const Car::Gate& g) { return num(g.share) + ',' + std::to_string(g.dir) + ',' + num(g.jam); }

bool readGate(const std::string& s, Car::Gate& g) {
    double v[3];
    if (!nums(s, v, 3) || v[0] < 0 || v[0] > 1 || (v[1] != -1 && v[1] != 0 && v[1] != 1) || v[2] < 0) return false;
    g.share = v[0], g.dir = (int)v[1], g.jam = v[2];
    return true;
}

// The car as built, set rigidly at (com, rot) and at rest.
void place(Car& c, const V3& com, const Quat& rot) {
    V3 com0;
    Quat rot0;
    net::dentPose(c, com0, rot0);
    const Quat q = rot * Quat{rot0.w, -rot0.x, -rot0.y, -rot0.z};
    for (Node& n : c.nodes) n.p = com + q.rotate(n.p - com0), n.v = V3();
    if (!c.chassis.nodes.empty()) {
        c.chassis.rot = rot;
        for (ChassisFrame* cf : wholeFrames(c)) cf->rot = q * cf->rot, cf->rot.normalize();
    }
    for (Wheel& wh : c.wheels) wh.omega = wh.sideOmega = wh.omegaWas = wh.twist = 0, wh.stickX = wh.stickY = 0, wh.load = 0;
}

void settle(Car& c) {
    for (Node& n : c.nodes) n.v = V3(), n.f = V3();
    c.wake();
    c.updateBounds();
    if (c.grid) c.grid->stamp = -1;
    c.nodeGridStamp = -1;
}

}  // namespace

std::string capture(const Sim& sim, const Car& c) {
    (void)sim;
    int vehicle = -1;
    ModSet mods;
    if (!identify(c, vehicle, mods)) return "";
    V3 com;
    Quat rot;
    net::dentPose(c, com, rot);
    std::string s = kTag;
    s += " veh=" + std::to_string(vehicle);
    s += " mods=";
    for (int k = 0; k < MOD_SLOTS; k++) s += (k ? "," : "") + std::to_string(mods.pick[k]);
    s += " com=" + num(com.x) + ',' + num(com.y) + ',' + num(com.z);
    s += " rot=" + num(rot.w) + ',' + num(rot.x) + ',' + num(rot.y) + ',' + num(rot.z);
    s += " fuel=" + num(c.fuel) + " jerry=" + num(c.jerry);
    s += " eng=" + num(c.engineHealth) + " flood=" + num(c.floodedHealth);
    s += std::string(" hydro=") + (c.hydrolocked ? '1' : '0') + " dead=" + (c.engineDead ? '1' : '0');
    s += " psi=" + num(c.tirePsi);
    s += " bat=" + num(c.battery);
    s += " hood=" + gateText(c.hood) + " gate=" + gateText(c.gate);
    // Each wheel: flags:tie-rod bend:the wheel on it (radius, rim, width, spin inertia, mass, rim style).
    s += " wheels=";
    for (size_t k = 0; k < c.wheels.size(); k++) {
        const Wheel& wh = c.wheels[k];
        const int f = wh.popped * W_POPPED | wh.detached * W_DETACHED | wh.shaftBroken * W_SHAFT | !wh.mounted * W_OFF |
                      wh.debeaded * W_DEBEADED | wh.chained * W_CHAINED;
        s += (k ? ";" : "") + std::to_string(f) + ':' + num(wh.tieOffset) + ':' + num(wh.item.R) + ':' + num(wh.item.rimR) + ':' +
             num(wh.item.width) + ':' + num(wh.item.I) + ':' + num(wh.item.mass) + ':' + std::to_string(wh.item.style);
    }
    s += " arb=";
    for (size_t k = 0; k < c.axles.size(); k++) s += (k ? "," : "") + num(c.axles[k].arbBend);
    {
        const std::vector<int16_t> q = net::carDents(c);
        Out o;
        o.put<uint32_t>((uint32_t)q.size());
        for (int16_t v : q) o.put<int16_t>(v);
        s += " dents=" + toB64(o.b);
    }
    s += " beams=" + toB64(beamsBlob(c));
    s += " frames=" + toB64(framesBlob(c));
    s += " pins=" + toB64(gapList(c.pins.size(), [&](size_t i) { return (uint8_t)c.pins[i].broken; }));
    s += " tris=" + toB64(gapList(c.tris.size(), [&](size_t i) { return (uint8_t)(c.tris[i].torn | c.tris[i].shattered << 1); }));
    s += " panels=" + toB64(panelsBlob(c));
    s += " end";
    return s;
}

int vehicleOf(const std::string& text) {
    std::map<std::string, std::string> kv;
    int v = -1;
    if (!fields(text, kv) || !toInt(kv["veh"], v) || v < 0 || v >= (int)vehicleCatalog().size()) return -1;
    return v;
}

ModSet modsOf(const std::string& text) {
    ModSet m;
    std::map<std::string, std::string> kv;
    const int v = vehicleOf(text);
    double p[MOD_SLOTS];
    if (v < 0 || !fields(text, kv) || !nums(kv["mods"], p, MOD_SLOTS)) return m;
    for (int k = 0; k < MOD_SLOTS; k++)
        if (p[k] < 0 || p[k] >= (double)modOptions(v, k).size() || p[k] != std::floor(p[k])) return ModSet();
    for (int k = 0; k < MOD_SLOTS; k++) m.pick[k] = (int)p[k];
    return m;
}

std::string posed(const std::string& text, const V3& com, const Quat& rot) {
    std::map<std::string, std::string> kv;
    if (!fields(text, kv)) return "";
    std::string out;
    for (const std::string& w : split(text, ' ')) {
        out += out.empty() ? "" : " ";
        if (w.rfind("com=", 0) == 0) out += "com=" + num(com.x) + ',' + num(com.y) + ',' + num(com.z);
        else if (w.rfind("rot=", 0) == 0) out += "rot=" + num(rot.w) + ',' + num(rot.x) + ',' + num(rot.y) + ',' + num(rot.z);
        else out += w;
    }
    return out;
}

bool restore(Sim& sim, Car& c, const std::string& text) {
    (void)sim;
    std::map<std::string, std::string> kv;
    if (!fields(text, kv)) return false;
    // Where it was left first: a save for some other build still puts this car there.
    double pc[3], pr[4];
    if (!nums(kv["com"], pc, 3) || !nums(kv["rot"], pr, 4)) return false;
    Quat rot{pr[0], pr[1], pr[2], pr[3]};
    if (std::fabs(rot.w * rot.w + rot.x * rot.x + rot.y * rot.y + rot.z * rot.z - 1) > 1e-3) return false;
    rot.normalize();
    const V3 com(pc[0], pc[1], pc[2]);
    if (std::fabs(com.x) > 1e7 || std::fabs(com.y) > 1e7 || std::fabs(com.z) > 1e7) return false;
    place(c, com, rot);
    settle(c);

    // The same vehicle, the same build, and the same model (nodes and beams) as it was saved with.
    int vehicle = -1;
    ModSet mods;
    double pm[MOD_SLOTS];
    if (!identify(c, vehicle, mods) || vehicleOf(text) != vehicle || !nums(kv["mods"], pm, MOD_SLOTS)) return false;
    for (int k = 0; k < MOD_SLOTS; k++)
        if (pm[k] != mods.pick[k]) return false;

    // Everything read and checked before any of it goes on the car.
    std::vector<uint8_t> raw;
    if (!fromB64(kv["dents"], raw)) return false;
    std::vector<int16_t> dents;
    {
        In in(raw);
        const uint32_t n = in.get<uint32_t>();
        if (!in.ok || n != c.nodes.size() * 3) return false;
        dents.resize(n);
        for (int16_t& v : dents) v = in.get<int16_t>();
        if (!in.ok || !in.done()) return false;
    }
    std::vector<uint8_t> beams, frames, pins, tris, panels;
    if (!fromB64(kv["beams"], beams) || !fromB64(kv["frames"], frames) || !fromB64(kv["pins"], pins) || !fromB64(kv["tris"], tris) ||
        !fromB64(kv["panels"], panels))
        return false;
    double fuel, jerry, eng, flood, psi;
    int hydro, dead;
    if (!toNum(kv["fuel"], fuel) || !toNum(kv["jerry"], jerry) || !toNum(kv["eng"], eng) || !toNum(kv["flood"], flood) ||
        !toNum(kv["psi"], psi) || !toInt(kv["hydro"], hydro) || !toInt(kv["dead"], dead))
        return false;
    if (fuel < -1 || jerry < -1 || eng < 0 || eng > 1 || flood < -1 || flood > 1 || psi < 0 || psi > 200 || hydro < 0 || hydro > 1 ||
        dead < 0 || dead > 1)
        return false;
    // (A save from before batteries has no bat: a full one.)
    double bat = 1;
    if (kv.count("bat") && (!toNum(kv["bat"], bat) || bat < 0 || bat > 1)) return false;
    Car::Gate hood, gate;
    if (!readGate(kv["hood"], hood) || !readGate(kv["gate"], gate)) return false;
    struct WheelSave {
        int flags = 0, style = 0;
        double tie = 0, R = 0, rimR = 0, width = 0, I = 0, mass = 0;
    };
    std::vector<WheelSave> ws;
    if (!c.wheels.empty()) {
        for (const std::string& w : split(kv["wheels"], ';')) {
            const std::vector<std::string> p = split(w, ':');
            WheelSave s;
            if (p.size() != 8 || !toInt(p[0], s.flags) || !toNum(p[1], s.tie) || !toNum(p[2], s.R) || !toNum(p[3], s.rimR) ||
                !toNum(p[4], s.width) || !toNum(p[5], s.I) || !toNum(p[6], s.mass) || !toInt(p[7], s.style))
                return false;
            if (s.flags < 0 || s.flags > 63 || std::fabs(s.tie) > 1 || s.R <= 0 || s.R > 3 || s.rimR <= 0 || s.rimR >= s.R || s.width <= 0 ||
                s.width > 2 || s.I <= 0 || s.mass <= 0)
                return false;
            ws.push_back(s);
        }
    }
    if (ws.size() != c.wheels.size()) return false;
    std::vector<double> arb;
    if (!c.axles.empty()) {
        arb.resize(c.axles.size());
        if (!nums(kv["arb"], arb.data(), arb.size())) return false;
    }

    // Onto a scratch copy of the parts that the lists change, so a bad list leaves the car as built.
    const std::vector<Beam> beamsWas = c.beams;
    const std::vector<Pin> pinsWas = c.pins;
    const std::vector<Tri> trisWas = c.tris;
    const std::vector<Panel> panelsWas = c.panels;
    const ChassisFrame chassisWas = c.chassis, rail0 = c.railFrame[0], rail1 = c.railFrame[1];
    std::vector<ChassisFrame> knucklesWas;
    for (const Car::Knuckle& k : c.knuckles) knucklesWas.push_back(k.frame);
    const bool listsOk = applyBeams(c, beams) && applyFrames(c, frames) &&
                         readGapList(pins, c.pins.size(), 1, [&](uint32_t i, uint8_t) { c.pins[i].broken = true; }) &&
                         readGapList(tris, c.tris.size(), 3, [&](uint32_t i, uint8_t f) { c.tris[i].torn = f & 1, c.tris[i].shattered = f & 2; }) &&
                         applyPanels(c, panels);
    if (!listsOk) {
        c.beams = beamsWas, c.pins = pinsWas, c.tris = trisWas, c.panels = panelsWas;
        c.chassis = chassisWas, c.railFrame[0] = rail0, c.railFrame[1] = rail1;
        for (size_t k = 0; k < c.knuckles.size(); k++) c.knuckles[k].frame = knucklesWas[k];
        return false;
    }

    // The shape, dents and all, where it was left.
    net::applyCarDents(c, com, rot, dents);
    for (size_t k = 0; k < c.wheels.size(); k++) {
        Wheel& wh = c.wheels[k];
        const WheelSave& s = ws[k];
        wh.item.R = s.R, wh.item.rimR = s.rimR, wh.item.width = s.width, wh.item.I = s.I, wh.item.mass = s.mass, wh.item.style = s.style;
        wh.mounted = !(s.flags & W_OFF);
        wh.R = s.R, wh.rimR = s.rimR, wh.width = s.width;
        wh.I = wh.mounted ? s.I : kBareHubI;
        wh.popped = wh.mounted && (s.flags & W_POPPED);
        wh.poppedAnim = wh.popped ? 1 : 0;
        wh.debeaded = wh.mounted && (s.flags & W_DEBEADED);
        wh.detached = s.flags & W_DETACHED;
        wh.shaftBroken = s.flags & W_SHAFT;
        wh.chained = s.flags & W_CHAINED;
        wh.tieOffset = s.tie;
    }
    for (size_t k = 0; k < c.axles.size(); k++) c.axles[k].arbBend = arb[k];
    c.fuel = fuel, c.jerry = jerry;
    c.engineHealth = eng, c.floodedHealth = flood;
    c.hydrolocked = hydro, c.engineDead = dead;
    c.tirePsi = psi;
    c.battery = bat;
    c.hood = hood, c.gate = gate;
    c.jackPlaced = false;
    settle(c);
    c.updateStats();  // (the damage, the detached wheels and the panels hanging, from the beams and pins)
    c.glassBursts.clear();  // (glass already broken isn't breaking now)
    c.impactEnergy = 0;
    c.tirePops = 0;
    return true;
}

}  // namespace carsave

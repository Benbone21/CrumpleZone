#include "paint.h"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstdio>
#include <cstdlib>

const char* paintFinishName(int finish) {
    static const char* n[PF_FINISHES] = {"GLOSS", "MATTE", "METALLIC"};
    return finish >= 0 && finish < PF_FINISHES ? n[finish] : "GLOSS";
}

namespace {
uint8_t q8(double v) { return (uint8_t)std::lround(std::min(1.0, std::max(0.0, std::isfinite(v) ? v : 0.0)) * 255.0); }
}  // namespace

bool CarPaint::operator==(const CarPaint& o) const {
    if (bodySet != o.bodySet || accentSet != o.accentSet || finish != o.finish) return false;
    for (int k = 0; k < 3; k++) {
        if (bodySet && body[k] != o.body[k]) return false;
        if (accentSet && accent[k] != o.accent[k]) return false;
    }
    return true;
}
void CarPaint::bodyF(float out[3]) const {
    for (int k = 0; k < 3; k++) out[k] = body[k] / 255.0f;
}
void CarPaint::accentF(float out[3]) const {
    for (int k = 0; k < 3; k++) out[k] = accent[k] / 255.0f;
}
void CarPaint::setBody(double r, double g, double b) {
    body[0] = q8(r), body[1] = q8(g), body[2] = q8(b), bodySet = true;
}
void CarPaint::setAccent(double r, double g, double b) {
    accent[0] = q8(r), accent[1] = q8(g), accent[2] = q8(b), accentSet = true;
}

const std::vector<PaintPreset>& paintPresets() {
    static const std::vector<PaintPreset> p = {
        {"RALLY RED", {196, 30, 36}},     {"SUNSET ORANGE", {232, 110, 30}}, {"SCHOOL BUS YELLOW", {240, 190, 20}}, {"LIME", {140, 200, 40}},
        {"FOREST GREEN", {28, 92, 52}},   {"TEAL", {20, 150, 150}},          {"SKY BLUE", {90, 160, 225}},          {"COBALT", {30, 70, 190}},
        {"NAVY", {18, 28, 80}},           {"PURPLE", {110, 50, 170}},        {"HOT PINK", {232, 70, 150}},          {"DESERT TAN", {196, 168, 120}},
        {"MUD BROWN", {96, 64, 40}},      {"BONE WHITE", {236, 232, 220}},   {"GUNMETAL", {70, 76, 84}},            {"GLOSS BLACK", {18, 18, 20}},
    };
    return p;
}
int paintPresetOf(const uint8_t rgb[3]) {
    const auto& p = paintPresets();
    for (int i = 0; i < (int)p.size(); i++)
        if (p[i].rgb[0] == rgb[0] && p[i].rgb[1] == rgb[1] && p[i].rgb[2] == rgb[2]) return i;
    return -1;
}

void rgbToHsv(double r, double g, double b, double& h, double& s, double& v) {
    const double mx = std::max({r, g, b}), mn = std::min({r, g, b}), d = mx - mn;
    v = mx, s = mx > 1e-9 ? d / mx : 0;
    if (d < 1e-9) {
        h = 0;
        return;
    }
    double hh = mx == r ? std::fmod((g - b) / d, 6.0) : mx == g ? (b - r) / d + 2 : (r - g) / d + 4;
    if (hh < 0) hh += 6;
    h = hh / 6.0;
}
void hsvToRgb(double h, double s, double v, double& r, double& g, double& b) {
    h = h - std::floor(h);
    const double c = v * s, x = c * (1 - std::fabs(std::fmod(h * 6, 2.0) - 1)), m = v - c;
    const int k = std::min(5, (int)(h * 6));
    const double R[6] = {c, x, 0, 0, x, c}, G[6] = {x, c, c, x, 0, 0}, B[6] = {0, 0, x, c, c, x};
    r = R[k] + m, g = G[k] + m, b = B[k] + m;
}

std::string paintToText(const CarPaint& p) {
    char a[16] = "-", b[16] = "-";
    if (p.bodySet) std::snprintf(b, sizeof b, "%02x%02x%02x", p.body[0], p.body[1], p.body[2]);
    if (p.accentSet) std::snprintf(a, sizeof a, "%02x%02x%02x", p.accent[0], p.accent[1], p.accent[2]);
    return std::string(b) + " " + a + " " + std::to_string((int)p.finish);
}

static bool hex6(const std::string& s, uint8_t out[3]) {
    if (s.size() != 6) return false;
    for (char ch : s)
        if (!std::isxdigit((unsigned char)ch)) return false;
    for (int k = 0; k < 3; k++) out[k] = (uint8_t)std::strtol(s.substr(k * 2, 2).c_str(), nullptr, 16);
    return true;
}

bool paintFromText(const std::string& body, const std::string& accent, int finish, CarPaint& out) {
    CarPaint p;
    if (body != "-") {
        if (!hex6(body, p.body)) return false;
        p.bodySet = true;
    }
    if (accent != "-") {
        if (!hex6(accent, p.accent)) return false;
        p.accentSet = true;
    }
    p.finish = (uint8_t)(finish >= 0 && finish < PF_FINISHES ? finish : PF_GLOSS);
    out = p;
    return true;
}

void paintToWire(const CarPaint& p, uint8_t out[kPaintWire]) {
    out[0] = (uint8_t)((p.bodySet ? 1 : 0) | (p.accentSet ? 2 : 0) | (p.finish << 2));
    for (int k = 0; k < 3; k++) out[1 + k] = p.bodySet ? p.body[k] : 0, out[4 + k] = p.accentSet ? p.accent[k] : 0;
}
bool paintFromWire(const uint8_t in[kPaintWire], CarPaint& out) {
    const int finish = in[0] >> 2;
    if (finish >= PF_FINISHES) return false;
    CarPaint p;
    p.bodySet = in[0] & 1, p.accentSet = (in[0] & 2) != 0, p.finish = (uint8_t)finish;
    for (int k = 0; k < 3; k++) p.body[k] = in[1 + k], p.accent[k] = in[4 + k];
    out = p;
    return true;
}

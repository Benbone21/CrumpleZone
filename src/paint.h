// Car paint: a body colour, an accent colour (the two-tone / hardtop trim and the wheel rims) and a finish. Chosen per vehicle in the
// garage's PAINT page, saved with the garage, worn by the car in the world, and sent with the vehicle in multiplayer so everyone sees it.
// Colours are 8 bits a channel, so what is saved and what is sent is exactly what is drawn. A CarPaint with nothing set is the factory's.
#pragma once
#include <cstdint>
#include <string>
#include <vector>

enum PaintFinish : uint8_t { PF_GLOSS = 0, PF_MATTE = 1, PF_METALLIC = 2, PF_FINISHES };
const char* paintFinishName(int finish);

struct CarPaint {
    bool bodySet = false, accentSet = false;  // false: the factory's
    uint8_t body[3] = {200, 200, 200}, accent[3] = {20, 20, 22};
    uint8_t finish = PF_GLOSS;
    bool factory() const { return !bodySet && !accentSet && finish == PF_GLOSS; }
    bool operator==(const CarPaint& o) const;
    bool operator!=(const CarPaint& o) const { return !(*this == o); }
    // The colours 0..1 (only meaningful if set).
    void bodyF(float out[3]) const;
    void accentF(float out[3]) const;
    // Sets a colour from 0..1 (clamped, rounded to 8 bits) and marks it set.
    void setBody(double r, double g, double b);
    void setAccent(double r, double g, double b);
};

// The presets offered in the PAINT page (16: name and colour).
struct PaintPreset {
    const char* name;
    uint8_t rgb[3];
};
const std::vector<PaintPreset>& paintPresets();
// Index of the preset exactly this colour, else -1.
int paintPresetOf(const uint8_t rgb[3]);

// Hue, saturation, brightness (each 0..1) <-> rgb 0..1.
void rgbToHsv(double r, double g, double b, double& h, double& s, double& v);
void hsvToRgb(double h, double s, double v, double& r, double& g, double& b);

// The save-file form (body hex or '-', accent hex or '-', finish number) and back. fromText returns false on junk (`out` untouched).
std::string paintToText(const CarPaint& p);
bool paintFromText(const std::string& body, const std::string& accent, int finish, CarPaint& out);

// Over the network: appended after a vehicle's other fields, so a peer that does not know it reads the factory colour. 7 bytes:
// flags (1 body set, 2 accent set, finish << 2), then the body and accent rgb. fromWire is false for a finish we do not know.
constexpr int kPaintWire = 7;
void paintToWire(const CarPaint& p, uint8_t out[kPaintWire]);
bool paintFromWire(const uint8_t in[kPaintWire], CarPaint& out);

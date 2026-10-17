#include "plants.h"

#include <algorithm>
#include <cmath>

namespace {
const PlantInfo kInfo[BUSH_KIND_COUNT] = {
    {"BUSH", 0.5, 1.4, 0.9, 0.55, 0.65, 1.0},
    {"SAGEBRUSH", 0.4, 1.0, 0.7, 0.45, 0.55, 1.0},
    {"JUNIPER", 0.5, 1.2, 0.8, 0.6, 0.7, 0.95},
    {"FLOWER SHRUB", 0.5, 1.1, 0.8, 0.55, 0.65, 1.0},
    {"FERN", 0.5, 1.1, 0.8, 0.35, 0.45, 1.0},
    {"REEDS", 0.5, 1.0, 0.7, 1.0, 1.1, 0.75},
    {"CACTUS", 0.4, 0.9, 0.6, 1.5, 1.6, 0.55},
    {"YUCCA", 0.5, 1.0, 0.7, 0.8, 0.9, 0.9},
    {"HEATHER", 0.5, 1.2, 0.8, 0.3, 0.4, 1.0},
    {"TUMBLEWEED", 0.4, 0.8, 0.55, 0.45, 0.5, 0.8},
    {"MUSHROOMS", 0.3, 0.7, 0.5, 0.2, 0.3, 0.9},
    {"MOSSY ROCKS", 0.4, 1.0, 0.7, 0.3, 0.4, 1.0},
    {"WILDFLOWERS", 0.5, 1.2, 0.8, 0.25, 0.3, 1.0},
};

// Weights by kind, for open ground and a wood's edge, in each habitat.
//                  LEAF SAGE JUNI FLOW FERN REED CACT YUCC HEAT TUMB MUSH MOSS WILD
const int kMix[HAB_COUNT][2][BUSH_KIND_COUNT] = {
    /* meadow */ {{44, 3, 4, 14, 0, 0, 0, 0, 0, 0, 0, 3, 26}, {40, 0, 8, 12, 12, 0, 0, 0, 0, 0, 8, 8, 12}},
    /* desert */ {{3, 40, 4, 0, 0, 0, 18, 12, 0, 20, 0, 0, 3}, {3, 40, 4, 0, 0, 0, 18, 12, 0, 20, 0, 0, 3}},
    /* forest */ {{26, 0, 12, 6, 22, 0, 0, 0, 2, 0, 8, 10, 14}, {22, 0, 10, 5, 28, 0, 0, 0, 0, 0, 14, 16, 5}},
    /* canyon */ {{5, 28, 22, 0, 0, 0, 9, 14, 0, 14, 0, 0, 8}, {5, 22, 30, 0, 0, 0, 9, 14, 0, 14, 0, 0, 6}},
    /* wetland */ {{10, 0, 0, 5, 14, 46, 0, 0, 0, 0, 5, 6, 14}, {14, 0, 0, 6, 22, 24, 0, 0, 0, 0, 14, 12, 8}},
    /* upland */ {{8, 8, 18, 0, 0, 0, 0, 0, 40, 0, 0, 12, 14}, {10, 4, 24, 0, 6, 0, 0, 0, 30, 0, 4, 14, 8}},
};
}  // namespace

const PlantInfo& plantInfo(int kind) { return kInfo[plantKindOf(kind)]; }

int plantKindFor(int habitat, bool edge, double u) {
    const int* w = kMix[std::min(std::max(habitat, 0), (int)HAB_COUNT - 1)][edge ? 1 : 0];
    int sum = 0;
    for (int k = 0; k < BUSH_KIND_COUNT; k++) sum += w[k];
    double at = std::min(std::max(u, 0.0), 0.999999) * sum;
    for (int k = 0; k < BUSH_KIND_COUNT; k++) {
        if (at < w[k]) return k;
        at -= w[k];
    }
    return BUSH_LEAFY;
}

bool plantGrowsIn(int habitat, int kind) {
    if (habitat < 0 || habitat >= HAB_COUNT || kind < 0 || kind >= BUSH_KIND_COUNT) return false;
    return kMix[habitat][0][kind] > 0 || kMix[habitat][1][kind] > 0;
}

double plantRadius(int kind, double u) {
    const PlantInfo& p = plantInfo(kind);
    return p.rMin + (p.rMax - p.rMin) * std::min(std::max(u, 0.0), 1.0);
}

double plantRoll(double x, double z, int salt) {
    uint32_t h = (uint32_t)(int)std::lround(x * 8) * 73856093u ^ (uint32_t)(int)std::lround(z * 8) * 19349663u ^ (uint32_t)salt * 83492791u;
    h ^= h >> 15, h *= 0x2c1b3c6du, h ^= h >> 12, h *= 0x297a2d39u, h ^= h >> 15;
    return (h & 0xFFFFFF) / double(0x1000000);
}

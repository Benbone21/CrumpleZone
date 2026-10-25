#include "careerworld.h"

#include <algorithm>
#include <cctype>

#include "vehicles.h"

const char* difficultyName(Difficulty d) {
    switch (d) {
        case DIFF_LIGHT: return "LIGHT";
        case DIFF_MEDIUM: return "MEDIUM";
        case DIFF_HARDCORE: return "HARDCORE";
        default: return "MEDIUM";
    }
}

const char* modeName(WorldMode m) { return m == MODE_CREW ? "SHARED CREW" : "PERSONAL"; }

int startCash(Difficulty d) {
    switch (d) {
        case DIFF_LIGHT: return 2000;
        case DIFF_HARDCORE: return 500;
        default: return 1000;
    }
}

bool startableVehicle(int catalogIndex) {
    const auto& cat = vehicleCatalog();
    if (catalogIndex < 0 || catalogIndex >= (int)cat.size()) return false;
    return std::string(cat[catalogIndex].name) != "KOENIGSEGG JESKO";
}

std::string worldSlug(const std::string& name, const std::vector<std::string>& taken) {
    const size_t kMax = 32;
    std::string base;
    bool dash = false;
    for (unsigned char c : name) {
        if (std::isalnum(c)) {
            if (dash && !base.empty()) base += '-';
            dash = false;
            base += (char)std::tolower(c);
        } else {
            dash = true;
        }
    }
    if (base.size() > kMax) base.resize(kMax);
    while (!base.empty() && base.back() == '-') base.pop_back();
    if (base.empty()) base = "world";
    auto isTaken = [&](const std::string& s) { return std::find(taken.begin(), taken.end(), s) != taken.end(); };
    std::string slug = base;
    for (int n = 2; isTaken(slug); n++) {
        std::string suffix = "-" + std::to_string(n);
        slug = base.substr(0, kMax - suffix.size()) + suffix;
    }
    return slug;
}

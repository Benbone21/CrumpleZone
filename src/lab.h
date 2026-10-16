// The model lab (crumplezone --lab): every code-made model (modelkit.h) in a little outdoor
// studio, its designs side by side as the game draws them. Pick the design you like, tweak
// its settings, or flag it for a redo with a note; the picks save as you go (to
// assets/models/choices.txt) and the game draws what you picked.
#pragma once
#include <set>
#include <string>

struct SDL_Window;
class Renderer;

struct LabOptions {
    std::string select;          // start on this model (id, or part of one)
    std::string shotPrefix;      // scripted: write these frames as PPMs, then quit
    std::set<int> shotFrames;
    int variant = -1;            // scripted: focus this design (single view)
    double hours = 13.5;         // time of day
};

int runModelLab(SDL_Window* win, Renderer& renderer, const LabOptions& opt);

// Hand-made obstacle sections: a section authored in the world editor (level.h's SectionInfo), saved
// in its own frame so the future stitcher can place it anywhere, as buildSection (sections.h) does
// for the generated ones.
//
// The editor saves two files: the level (.czl, absolute cells on the proving ground: it opens and
// plays like any level) and the section (.czs, this file: heights, surfaces, water and objects
// measured from the entry, along the heading). placeSection() puts a .czs on a World at any
// SectionFrame; heights are from the entry's height, so it fits wherever the entry is put.
#pragma once
#include <string>
#include <vector>

#include "level.h"
#include "sections.h"

struct SectionFile {
    std::string name, about, author;
    SectionInfo info;   // kind, difficulty, tests, kit, generator (its canvas fields are unused: see below)
    // The canvas in the section's frame: u from u0 to u1 along the heading (the entry is u = 0), v from
    // -half to half (to the left). Ground outside blends back to its surroundings over `blend`.
    double u0 = -40, u1 = 220, half = 45, blend = 24;
    // One sample per metre: sample (i, j) is at u = u0 + i, v = -half + j.
    int nu = 0, nv = 0;
    std::vector<float> h;         // height above the entry
    std::vector<uint16_t> surf;   // surface << 8 | paint
    std::vector<float> water;     // level above the entry (-1e9 dry)
    std::vector<float> flowU, flowV;  // the water's current in the frame (m/s: along, to the left)
    // Objects, as level objects in the frame: p = (u, up from the entry's height, v), yaw from the heading.
    std::vector<LevelObject> objects;
    // The course: p = (u, 0, v), yaw from the heading. The last gate is the exit.
    std::vector<Gate> gates;
    int dropped = 0;              // extractSection: objects outside the canvas that weren't kept

    int at(int i, int j) const { return i * nv + j; }
    // Where the last gate is (the exit), in the frame.
    V3 exitLocal() const { return gates.empty() ? V3(u1 - 40, 0, 0) : gates.back().p; }
    double length() const { return exitLocal().x; }  // entry to exit along the heading
};

// The proving ground's canvas for new sections: a patch of open ground in the north-west, clear of
// the spawn pad, the loop road, the courses, the model yard and the section park.
SectionInfo defaultSectionCanvas();
// The same for the open country (MAP_WILDS), where the editor makes new sections now: clear ground in the middle of the map, all round it.
SectionInfo wildsSectionCanvas();

// A section in its frame, from a level made in the editor on `w` (the world the level is on). Needs
// lv.section.on, a start (the entry) and a gate (the exit).
bool extractSection(const World& w, const Level& lv, SectionFile& out, std::string* error = nullptr);

// Puts the section on `w` at `f` (its entry; its heading): the ground inside the canvas, its surfaces,
// water and objects, the ground round it blended in; whatever stood there is cleared first. Call
// w.buildGrid() afterwards. Where the entry and exit ended up is returned.
SectionBuilt placeSection(World& w, const SectionFile& s, const SectionFrame& f);

// The file (.czs). The editor writes one beside every section level.
std::string saveSectionText(const SectionFile& s);
bool parseSectionText(const std::string& text, SectionFile& s, std::string* error = nullptr);
bool loadSectionFile(const std::string& path, SectionFile& s, std::string* error = nullptr);
bool saveSectionFile(const SectionFile& s, const std::string& path, std::string* error = nullptr);
// "…/dune_run.czl" -> "…/dune_run.czs"
std::string sectionPathFor(const std::string& levelPath);
// Every .czs in the levels folders, in name order.
std::vector<SectionFile> listSections();

// A world point to and from a section's frame (entry, heading).
V3 sectionToLocal(const SectionFrame& f, const V3& world);
V3 sectionToWorld(const SectionFrame& f, const V3& local);

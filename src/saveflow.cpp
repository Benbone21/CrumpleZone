#include "saveflow.h"

#include <fstream>

#include "platform.h"

std::string SaveFlow::dir() const { return folder.empty() ? userLevelFolder() : folder; }

static bool fileExists(const std::string& path) {
    std::ifstream f(path);
    return (bool)f;
}

std::string SaveFlow::defaultName(const EditCore& core, const std::string& dir) {
    const std::string stem = core.level.section.on ? "MY SECTION" : mapName(core.level.map);
    for (int n = 1; n < 1000; n++) {
        const std::string name = stem + " " + std::to_string(n);
        if (!fileExists(dir + "/" + levelFileName(name) + ".czl")) return name;
    }
    return stem;
}

bool SaveFlow::needsName(const EditCore& core, const std::string& dir) {
    const Level& l = core.level;
    if (l.name.empty() || l.name == "UNTITLED" || l.builtin || l.file.empty()) {
        // (A named level that was never saved just saves, unless that would overwrite a file that is somebody's else.)
        if (!l.name.empty() && l.name != "UNTITLED" && !l.builtin && l.file.empty()) return fileExists(dir + "/" + levelFileName(l.name) + ".czl");
        return true;
    }
    return l.file.find(dir) != 0;  // (from the game's own folder: a copy goes to yours)
}

bool SaveFlow::writeNow(EditCore& core) {
    const std::string path = dir() + "/" + levelFileName(core.level.name) + ".czl";
    std::string err;
    makeDir(configDir());
    makeDir(dir());
    if (core.save(path, &err)) {
        saved = true;
        note = "SAVED " + path + (core.saveNote.empty() ? "" : ", BUT " + core.saveNote);
        return true;
    }
    note = "CAN'T SAVE: " + err;
    return false;
}

void SaveFlow::requestSave(EditCore& core, bool saveAs, After then, int thenArg) {
    saved = discarded = false;
    done = NOTHING;
    after = then, arg = thenArg;
    if (!saveAs && !needsName(core, dir())) {
        if (writeNow(core)) finish(then, thenArg);
        else stage = IDLE;
        return;
    }
    stage = NAMING;
    replaceOk = false;
    const std::string& cur = core.level.name;
    text = saveAs && !cur.empty() && cur != "UNTITLED" ? cur : defaultName(core, dir());
    fresh = true;
    note = "NAME THE LEVEL: ENTER SAVES, ESC CANCELS";
}

void SaveFlow::requestLeave(EditCore& core, After then, int thenArg) {
    saved = discarded = false;
    done = NOTHING;
    if (!core.unsaved()) {
        finish(then, thenArg);
        return;
    }
    stage = ASKING, after = then, arg = thenArg;
    note = "UNSAVED CHANGES: SAVE, DISCARD OR CANCEL";
}

void SaveFlow::type(char ch) {
    if (stage != NAMING) return;
    if (fresh) text.clear(), fresh = false;
    if (text.size() >= 28) return;
    text += ch;
    replaceOk = false;
}

void SaveFlow::backspace() {
    if (stage == NAMING && !text.empty()) text.pop_back(), replaceOk = false, fresh = false;
}

bool SaveFlow::commit(EditCore& core, const std::string& typed) {
    std::string name = typed;
    while (!name.empty() && name.back() == ' ') name.pop_back();
    while (!name.empty() && name.front() == ' ') name.erase(name.begin());
    if (name.empty()) {
        note = "TYPE A NAME FIRST";
        return false;
    }
    const std::string path = dir() + "/" + levelFileName(name) + ".czl";
    const bool same = !core.level.file.empty() && core.level.file == path;
    if (fileExists(path) && !same && !replaceOk) {
        replaceOk = true;
        note = "THAT NAME IS TAKEN: ENTER AGAIN TO REPLACE IT, OR TYPE ANOTHER";
        return false;
    }
    if (name != core.level.name) core.settings("name", [&](Level& l) { l.name = name; });
    core.level.builtin = false;
    return writeNow(core);
}

void SaveFlow::enter(EditCore& core) {
    if (stage != NAMING) return;
    if (commit(core, text)) finish(after, arg);
}

void SaveFlow::escape() {
    if (stage == IDLE) return;
    stage = IDLE, done = NOTHING;
    note = "CANCELLED";
}

void SaveFlow::choose(EditCore& core, Choice c) {
    if (stage != ASKING) return;
    if (c == CANCEL) {
        escape();
        return;
    }
    if (c == DISCARD) {
        note = "DISCARDED";
        discarded = true;
        finish(after, arg);
        return;
    }
    requestSave(core, false, after, arg);  // (asks for the name if it needs one, then goes on to what was asked)
}

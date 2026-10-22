// The editor's save prompts, without graphics (the editor draws them, tests/editobjects.cpp drives them): saving an unnamed level
// asks for a name (prefilled with one that doesn't clash with a file you have); SAVE AS asks again; leaving, opening another level
// or starting a new one with unsaved changes asks SAVE / DISCARD / CANCEL. Nothing here ever refuses silently.
#pragma once
#include <string>

#include "editcore.h"

struct SaveFlow {
    enum Stage { IDLE, NAMING, ASKING };
    // What happens once the question is answered (a save, or a discard).
    enum After { NOTHING, LEAVE, OPEN, NEWMAP };
    enum Choice { SAVE, DISCARD, CANCEL };

    Stage stage = IDLE;
    std::string text;       // the name being typed
    std::string note;       // what to tell the player (a toast), set by every step
    After after = NOTHING;
    int arg = -1;           // OPEN: which file
    bool fresh = false;     // the box holds a suggestion, shown selected: the first key typed replaces it (Backspace edits it)
    bool replaceOk = false; // the typed name is taken, and Enter again replaces it
    // Set when the step is finished: what to do now (the editor does it and clears it).
    After done = NOTHING;
    int doneArg = -1;
    bool saved = false;     // a file was written by this step (the level lists need a refresh)
    bool discarded = false; // DISCARD was chosen (the editor throws the edits and their recovery copy away, then does what was asked)

    // A name for a level that hasn't one: the map's (or the section's) name and the first number that isn't a file in `dir`.
    static std::string defaultName(const EditCore& core, const std::string& dir);
    // Does saving it need asking where it goes?
    static bool needsName(const EditCore& core, const std::string& dir);

    // Ctrl+S (saveAs: Ctrl+Shift+S).
    void requestSave(EditCore& core, bool saveAs, After then = NOTHING, int thenArg = -1);
    // Anything that would throw the work away: asks first if there is any unsaved.
    void requestLeave(EditCore& core, After then, int thenArg = -1);
    // The name prompt.
    void type(char ch);
    void backspace();
    void enter(EditCore& core);
    void escape();
    // The question.
    void choose(EditCore& core, Choice c);

    std::string fixedPath = "";  // the one file a save goes to, whatever the level is called (the main menu's camp: ~/.config/crumplezone/menu_camp.czl); "": by name, in `folder`
    std::string folder = "";  // where levels are saved ("": the user's levels folder)
    std::string dir() const;

private:
    bool commit(EditCore& core, const std::string& name);
    bool writeNow(EditCore& core);
    void finish(After a, int arg) { stage = IDLE, done = a, doneArg = arg; }
};

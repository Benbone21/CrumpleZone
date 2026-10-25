// The game's self-updater (docs/update-signing-keys.md, docs/server-updates-setup.md): on launch, and when a multiplayer host turns
// out to run a newer version, it fetches the signed manifest from the owner's server (updatecore.h), checks its signature with the
// key compiled into the game, and offers UPDATE NOW / LATER. UPDATE NOW downloads the platform's zip (with progress), checks its size
// and SHA-256, unpacks it beside the install, swaps it in (the version before is kept for rollback) and restarts:
//  - Linux (~/.local/share/crumplezone/app) and Mac (the .app): swapped while running, then the new one is started.
//  - Windows: a copy of the game in %TEMP% (crumplezone.exe --cz-swap ...) waits for the game to exit, swaps the folders, starts it.
//  - The .deb (and dev builds): only says where the new version is.
// If the new version fails to start twice in a row, the third start goes back to the previous one. Any failure leaves the current
// version running and says why in plain words. Everything network runs on a background thread; the card (updatecard.h) reads status().
#pragma once
#include <array>
#include <cstdint>
#include <string>
#include <vector>

namespace update {

enum class Phase {
    Idle,         // nothing to show
    Checking,     // asking the server (shown only for an explicit check)
    Offer,        // UPDATE x AVAILABLE: UPDATE NOW / LATER
    Info,         // a message with OK (and COPY LINK when there's a link)
    Downloading,  // progress, CANCEL
    Installing,   // verifying, unpacking, swapping
    Restarting,   // done: main.cpp restarts the game
};

struct Status {
    Phase phase = Phase::Idle;
    std::string title;    // "UPDATE 0.4.1 AVAILABLE", "UPDATE FAILED", ...
    std::string message;  // the plain-words details
    std::string link;     // a URL the player may want (the .deb, the release notes); COPY LINK copies it
    float progress = 0;   // Downloading: 0..1 (or -1 when the size is unknown)
    uint64_t got = 0, total = 0;
};

// This build's release version (the Makefile's VERSION, e.g. "0.4.0").
std::string currentVersion();

// At the start of main(), before anything else. Handles what a previous update left (counting starts, rolling back after two failed
// starts, reporting a failed Windows swap). Returns true when the game must exit at once (it started the version it rolled back to).
// `ourNet`: net::kVersion. Reads --update-url URL and --no-update-check from argv, and "url"/"check off" from <config>/update.txt.
bool init(int argc, char** argv, int ourNet);
// The launch check, in the background: only an available update (or a real problem with one) is shown; offline is a quiet notice.
void checkOnLaunch();
// The hook for the multiplayer "wrong version" screens: the host speaks protocol `hostNet` (net::kVersion on its side) and, when known,
// is on release `hostRelease`. Checks now (even if LATER was chosen) and shows the result, whatever it is. (Nothing before init(), so
// scripted and test runs never get a card.) The join-code screens call it when the backend answers FAIL(OLDER) with the host's version;
// main.cpp calls it when a direct join is refused with "version mismatch: update to X".
void checkFor(int hostNet, const std::string& hostRelease = "");

Status status();
// One-line notices for the HUD's toast (e.g. "COULDN'T CHECK FOR UPDATES: ..."); "" when none. Each is returned once.
std::string takeNotice();

void accept();    // UPDATE NOW
void later();     // LATER: not again this run, unless checkFor asks
void cancel();    // stops a download
void dismiss();   // OK on an Info card

// Restarting: main.cpp calls restart() once status().phase == Restarting. Linux: exec (doesn't return when it works). Mac: opens the
// new app. Windows: starts the swap helper. True when the caller should now quit; false (with an Info card) if it couldn't.
bool restart(int argc, char** argv);
// Call once the game has run normally for a while (main.cpp: 20 s of frames): the new version counts as good.
void markGood();

// Tests only: a config folder, an install folder (where the marker is) and the trusted keys instead of the real ones; clears the state.
void testSetup(const std::string& configDir, const std::string& exeDir, const std::vector<std::array<uint8_t, 32>>& keys);

// crumplezone.exe --cz-swap ...: the Windows helper (any platform runs it in the tests). Returns the exit code.
bool isHelperCall(int argc, char** argv);
int helperMain(int argc, char** argv);

}  // namespace update

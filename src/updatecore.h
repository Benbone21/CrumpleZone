// The updater's logic without threads, windows or network (so the tests drive all of it; updater.h runs it in the game):
// versions, the signed release manifest, the decision ("is there an update for this install, and can it update itself?"),
// the install layouts, unpacking the release zip, and swapping folders with the previous version kept for rollback.
//
// The manifest (docs/server-updates-setup.md, written and signed by CI in .github/workflows/release.yml):
//   crumplezone-update 1
//   version 0.4.1
//   net 8                                     (the multiplayer protocol it speaks: net::kVersion)
//   date 2026-10-25
//   notes https://github.com/Benbone21/crumplezone/releases/tag/v0.4.1
//   file windows v0.4.1/CrumpleZone-0.4.1-windows.zip 123456789 <sha256 hex>
//   file mac     v0.4.1/CrumpleZone-0.4.1-mac.zip ...
//   file linux   v0.4.1/CrumpleZone-0.4.1-linux.zip ...
//   file deb     v0.4.1/CrumpleZone-0.4.1-linux.deb ...      (not self-installed: the .deb's link for the message)
//   signature <128 hex: Ed25519 over every byte before this line>
// A file's URL is relative to the manifest's own URL unless it starts with https:// (or http:// for a loopback host, the tests).
// On GitHub Releases (the default, docs/server-updates-setup.md) CI writes absolute URLs,
//   file linux https://github.com/Benbone21/CrumpleZone/releases/download/v0.4.1/CrumpleZone-0.4.1-linux.zip ...
// and a relative one there resolves in that release's download folder, not under .../releases/latest/ (releaseManifestUrl).
// The URLs are inside the signed text, so a changed address is refused like any other change.
// Lines with other keys are ignored (a newer CI can add some); the signature covers them too.
#pragma once
#include <array>
#include <cstdint>
#include <string>
#include <vector>

namespace update {

// ---- versions --------------------------------------------------------------------------------------------------------
// "0.4.1", "1.10.0", "0.0.0-dev": dot-separated numbers, an optional "-suffix". A suffix sorts below the same numbers
// without one (0.4.1-rc1 < 0.4.1). <0, 0, >0. Something that isn't a version sorts below every version.
int compareVersions(const std::string& a, const std::string& b);
bool validVersion(const std::string& v);

// ---- the manifest ----------------------------------------------------------------------------------------------------
struct FileEntry {
    std::string platform;  // windows, mac, linux, deb
    std::string url;       // as written (relative or absolute)
    uint64_t size = 0;
    std::string sha256;    // lowercase hex
};

struct Manifest {
    std::string version;
    int net = 0;
    std::string date, notes;
    std::vector<FileEntry> files;
    const FileEntry* file(const std::string& platform) const;
};

using PublicKey = std::array<uint8_t, 32>;

// The text that gets signed (every line but the signature).
std::string manifestBody(const Manifest& m);
// The body plus its signature line, signed with the 32-byte private key.
std::string signManifest(const Manifest& m, const uint8_t seed[32]);
// Checks the signature against each key (any one may match: a new key can be added before the old one is retired), then
// reads it. False with a plain reason: no signature, a signature that doesn't match (tampered, or another key), malformed.
bool readManifest(const std::string& text, const std::vector<PublicKey>& keys, Manifest& out, std::string& why);
// The keys compiled into this build (updatekeys.h); empty when the owner hasn't put one in yet.
std::vector<PublicKey> builtinKeys();
// A file's full URL: relative ones against the manifest's folder. "" when it is not allowed (not https, "..", spaces...).
std::string resolveUrl(const std::string& manifestUrl, const std::string& fileUrl);
// The folder a release's relative file names belong in. GitHub's .../releases/latest/download/<name> (the default update address)
// only redirects to the newest release, so its files are looked for in .../releases/download/v<version>/ (the release the
// manifest says it is), not under /latest/. Any other address is its own folder (returned as it is).
std::string releaseManifestUrl(const std::string& manifestUrl, const std::string& version);
// resolveUrl against releaseManifestUrl: where a manifest's file really is.
std::string fileUrl(const std::string& manifestUrl, const Manifest& m, const FileEntry& f);
// https://, or http:// to 127.0.0.1 / localhost / [::1] (the tests' server).
bool allowedUrl(const std::string& url);

// ---- installs --------------------------------------------------------------------------------------------------------
enum class Layout {
    None,       // a development build (no marker file): never updates itself
    Windows,    // the installer's folder (or an unzipped one): crumplezone.exe, SDL2.dll, assets\ ...
    Mac,        // CrumpleZone.app
    LinuxUser,  // ~/.local/share/crumplezone/app (tools/../installer/linux-user-install.sh)
    Deb,        // /opt/crumplezone from the .deb: needs sudo, so the game only says where to get the new one
};
const char* layoutName(Layout l);   // the manifest's platform name: windows, mac, linux, deb, ""
Layout layoutFromName(const std::string& s);

// The marker the packaging writes beside the game (Mac: Contents/Resources/): "layout windows|mac|linux|deb".
constexpr const char* kMarker = "cz-install.txt";

struct Install {
    Layout layout = Layout::None;
    std::string live;      // what the update replaces: the Windows folder, the .app, ~/.local/share/crumplezone/app
    std::string staging;   // where the zip is unpacked (same volume as live, so the swap is a rename)
    std::string previous;  // where the version before is kept (rollback)
    std::string why;       // Layout::None (or a problem with the folder): why this build won't update itself
};
// From the running executable's folder. (Mac: exeDir is .../CrumpleZone.app/Contents/MacOS.)
Install detectInstall(const std::string& exeDir);
// The same paths for a given live location (the tests' temporary folders).
Install installAt(Layout l, const std::string& live);
// The top folder inside each platform's zip: CrumpleZone (windows), CrumpleZone.app (mac), crumplezone (linux).
const char* zipTopFolder(Layout l);
// Files in `previous` that aren't in the release zip but must stay (the Windows installer's Uninstall.exe).
std::vector<std::string> keepAcrossUpdates(Layout l);

// ---- the decision ----------------------------------------------------------------------------------------------------
enum class Verdict {
    UpToDate,       // nothing newer
    SelfUpdate,     // newer, with a file for this install: offer UPDATE NOW / LATER
    Manual,         // newer, but this install can't replace itself (the .deb, a dev build): say where to get it
    NotForThisOS,   // newer, but this release has no file for this platform yet
};
struct Decision {
    Verdict verdict = Verdict::UpToDate;
    const FileEntry* file = nullptr;  // SelfUpdate: the zip; Manual on a .deb: the .deb (may be null)
    std::string message;              // what the card says, in capitals
};
// `hostNet` > 0: asked because a multiplayer host speaks that protocol version (and this game speaks `ourNet`).
Decision decide(const Manifest& m, const std::string& current, const Install& inst, const std::string& manifestUrl, int hostNet = 0,
                int ourNet = 0);

// ---- unpacking and swapping ------------------------------------------------------------------------------------------
// Unpacks a zip (stored or deflated entries; unix modes and symlinks kept, the Mac's AppleDouble "._" and __MACOSX/ skipped) into
// `dir`, which must not exist. Refuses absolute paths, "..", and symlinks pointing out of the tree. Checks each entry's CRC.
bool unzip(const std::string& zipPath, const std::string& dir, std::string& why);

// live -> previous (an older previous is deleted first), fresh -> live, then each `keep` name is copied from previous into live.
// A failure part-way puts live back where it was. (Windows runs this in the helper after the game exits; Mac and Linux while
// running, since a running program can be renamed there.)
bool swapIn(const std::string& live, const std::string& fresh, const std::string& previous, const std::vector<std::string>& keep, std::string& why);
// Back to the version before: live and previous trade places. False (and nothing changed) when there is no previous.
bool rollBack(const std::string& live, const std::string& previous, std::string& why);
bool removeTree(const std::string& path);  // (true when it's gone)

// ---- what happened after a swap (update.txt in the config folder) -----------------------------------------------------
// Written before the restart; the new version reads it. If the new version fails to start twice in a row (it never reached the point
// where it calls markGood), the next start rolls back.
struct AfterSwap {
    std::string from, to;  // versions
    int starts = 0;        // starts of `to` that haven't been marked good yet
    std::string failed;    // the Windows helper's reason, when its swap failed (the old version is still installed)
    bool rolledBack = false;
    std::string skip;      // a version that was rolled back from: the launch check doesn't offer it again (an explicit check still does)
};
std::string writeAfterSwap(const AfterSwap& s);
AfterSwap readAfterSwap(const std::string& text);
// What a start should do with it. `current`: this build's version.
enum class StartAction { None, Count, RollBack, ReportFailure, ReportRollBack };
StartAction onStart(const AfterSwap& s, const std::string& current);

}  // namespace update

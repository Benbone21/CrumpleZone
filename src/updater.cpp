#include "updater.h"

#include <chrono>
#include <cstdio>
#include <cstring>
#include <deque>
#include <filesystem>
#include <fstream>
#include <functional>
#include <mutex>
#include <sstream>
#include <thread>
#include <vector>

#include "httpget.h"
#include "platform.h"
#include "sha2.h"
#include "updatecore.h"
#include "updatekeys.h"

#ifdef _WIN32
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#else
#include <signal.h>
#include <spawn.h>
#include <unistd.h>
extern char** environ;
#endif

#ifndef CZ_VERSION
#define CZ_VERSION "0.0.0-dev"
#endif

namespace fs = std::filesystem;

namespace update {
namespace {

struct State {
    std::mutex m;
    bool inited = false;
    std::string url;          // the manifest's URL ("" = not set up)
    bool launchCheck = true;  // "check off" in update.txt, --no-update-check
    int ourNet = 0;
    Install inst;
    Status st;
    Manifest manifest;
    FileEntry file;           // the chosen zip (url resolved)
    bool busy = false;        // a worker thread is running
    bool laterChosen = false;
    std::string skip;
    std::string told, telling;  // a release the launch check already told a .deb (or other manual) install about (update-told.txt), and the one on the card now
    std::deque<std::string> notices;
    http::Progress progress;
    // (testSetup: the tests' config folder, install folder and key)
    std::string testConfig, testExeDir;
    std::vector<PublicKey> testKeys;
};
// (Never freed: a detached worker may still be running while the game exits.)
State& S() {
    static State* s = new State;
    return *s;
}

std::string cfgDir() { return S().testConfig.empty() ? configDir() : S().testConfig; }
std::string statePath() { return cfgDir() + "/update-state.txt"; }
std::string downloadPath() { return cfgDir() + "/update-download.zip"; }

std::string readFile(const std::string& path) {
    std::ifstream f(path, std::ios::binary);
    if (!f) return "";
    return std::string((std::istreambuf_iterator<char>(f)), std::istreambuf_iterator<char>());
}
void writeState(const AfterSwap& a) {
    makeDir(cfgDir());
    writeFileAtomic(statePath(), writeAfterSwap(a));
}

void logLine(const std::string& s) { std::fprintf(stderr, "crumplezone: update: %s\n", s.c_str()); }

void setInfo(const std::string& title, const std::string& message, const std::string& link = "") {
    State& s = S();
    s.st = Status();
    s.st.phase = Phase::Info;
    s.st.title = title;
    s.st.message = message;
    s.st.link = link;
}

// The executable inside an unpacked or installed copy of each layout.
std::string exeIn(Layout l, const std::string& root) {
    switch (l) {
        case Layout::Windows: return root + "/crumplezone.exe";
        case Layout::Mac: return root + "/Contents/MacOS/crumplezone";
        case Layout::LinuxUser: return root + "/crumplezone";
        default: return "";
    }
}
std::string markerIn(Layout l, const std::string& root) {
    return l == Layout::Mac ? root + "/Contents/Resources/" + kMarker : root + "/" + kMarker;
}

// ---- the check -------------------------------------------------------------------------------------------------------
// Runs on a worker. `explicitAsk`: from checkFor (show every outcome) rather than the launch check (show only an update).
void runCheck(bool explicitAsk, int hostNet) {
    State& s = S();
    std::string url;
    int ourNet;
    {
        std::lock_guard<std::mutex> g(s.m);
        url = s.url;
        ourNet = s.ourNet;
    }
    const std::vector<PublicKey> keys = s.testKeys.empty() ? builtinKeys() : s.testKeys;
    auto finish = [&](const std::function<void()>& f) {
        std::lock_guard<std::mutex> g(s.m);
        f();
        s.busy = false;
    };
    if (url.empty() || keys.empty()) {
        logLine(url.empty() ? "no update server set in this build" : "no update key in this build");
        finish([&] {
            if (explicitAsk)
                setInfo("UPDATES AREN'T SET UP IN THIS BUILD", "GET THE NEWEST VERSION FROM THE GAME'S RELEASES PAGE", "https://github.com/Benbone21/CrumpleZone/releases");
            else
                s.st = Status();
        });
        return;
    }
    std::string body, why;
    if (!http::fetch(url, 64 * 1024, body, why)) {
        logLine("check failed: " + why);
        finish([&] {
            if (explicitAsk) setInfo("COULDN'T CHECK FOR UPDATES", why);
            else {
                s.st = Status();
                s.notices.push_back("COULDN'T CHECK FOR UPDATES: " + why);
            }
        });
        return;
    }
    Manifest m;
    if (!readManifest(body, keys, m, why)) {
        logLine("manifest refused: " + why);
        finish([&] {
            if (explicitAsk) setInfo("COULDN'T CHECK FOR UPDATES", why);
            else {
                s.st = Status();
                s.notices.push_back("UPDATE CHECK: " + why);
            }
        });
        return;
    }
    finish([&] {
        s.manifest = m;
        const Decision d = decide(m, currentVersion(), s.inst, url, hostNet, ourNet);
        logLine("server has " + m.version + ", this is " + currentVersion() + ": " + d.message);
        const bool quiet = !explicitAsk && (s.laterChosen || m.version == s.skip || (d.verdict == Verdict::Manual && m.version == s.told));
        if (d.verdict == Verdict::UpToDate) {
            if (explicitAsk) setInfo(hostNet > ourNet ? "NO NEWER VERSION YET" : "UP TO DATE", d.message);
            else s.st = Status();
            return;
        }
        if (quiet) {
            s.st = Status();
            return;
        }
        if (d.verdict == Verdict::SelfUpdate) {
            s.file = *d.file;
            s.file.url = fileUrl(url, m, *d.file);
            s.st = Status();
            s.st.phase = Phase::Offer;
            s.st.title = "UPDATE " + m.version + " AVAILABLE";
            s.st.message = d.message;
            s.st.link = m.notes;
            return;
        }
        std::string link = m.notes;
        if (d.file) link = fileUrl(url, m, *d.file);
        setInfo("UPDATE " + m.version + " IS OUT", d.message, link);
        // (OK on the launch check's "get it by hand" card: not again on every launch for this release; a newer one is told again)
        if (!explicitAsk && d.verdict == Verdict::Manual) s.telling = m.version;
    });
}

void startWorker(std::function<void()> f) {
    State& s = S();
    s.busy = true;
    std::thread(std::move(f)).detach();
}

// ---- the install -----------------------------------------------------------------------------------------------------
void fail(const std::string& why) {
    State& s = S();
    logLine("update failed: " + why);
    std::lock_guard<std::mutex> g(s.m);
    setInfo("UPDATE FAILED", why + ". YOU'RE STILL ON " + currentVersion());
    s.busy = false;
}

void runInstall() {
    State& s = S();
    FileEntry f;
    Install inst;
    std::string version;
    {
        std::lock_guard<std::mutex> g(s.m);
        f = s.file;
        inst = s.inst;
        version = s.manifest.version;
    }
    if (inst.layout != Layout::Windows && inst.layout != Layout::Mac && inst.layout != Layout::LinuxUser) {
        // (never reached: decide() only offers UPDATE NOW to these. The .deb's files in /opt and /usr belong to the package manager.)
        fail("THIS INSTALL CAN'T REPLACE ITSELF");
        return;
    }
    makeDir(cfgDir());
    const std::string zip = downloadPath();
    std::string why;
    // 1. Download.
    if (!http::download(f.url, zip, &s.progress, why)) {
        std::lock_guard<std::mutex> g(s.m);
        if (why == "STOPPED") s.st = Status();
        else setInfo("UPDATE FAILED", "THE DOWNLOAD DIDN'T FINISH: " + why + ". YOU'RE STILL ON " + currentVersion());
        s.busy = false;
        return;
    }
    {
        std::lock_guard<std::mutex> g(s.m);
        s.st.phase = Phase::Installing;
        s.st.message = "CHECKING THE DOWNLOAD";
    }
    // 2. Check it is exactly the file the signed manifest names.
    std::error_code ec;
    const uint64_t size = (uint64_t)fs::file_size(zip, ec);
    if (ec || size != f.size || sha2::sha256File(zip) != f.sha256) {
        std::remove(zip.c_str());
        fail("THE DOWNLOAD DOESN'T MATCH THE SIGNED RELEASE (DAMAGED OR CHANGED ON THE WAY): TRY AGAIN LATER");
        return;
    }
    // 3. Unpack beside the install.
    {
        std::lock_guard<std::mutex> g(s.m);
        s.st.message = "UNPACKING";
    }
    removeTree(inst.staging);
    if (!unzip(zip, inst.staging, why)) {
        removeTree(inst.staging);
        fail(why);
        return;
    }
    const std::string fresh = inst.staging + "/" + zipTopFolder(inst.layout);
    if (!fs::exists(exeIn(inst.layout, fresh), ec) || readFile(markerIn(inst.layout, fresh)).find(std::string("layout ") + layoutName(inst.layout)) == std::string::npos) {
        removeTree(inst.staging);
        fail("THE DOWNLOADED RELEASE IS NOT FOR THIS KIND OF INSTALL");
        return;
    }
#ifndef _WIN32
    // (A zip made without unix modes would leave the program without its execute bit.)
    fs::permissions(exeIn(inst.layout, fresh), fs::perms::owner_exec | fs::perms::group_exec | fs::perms::others_exec, fs::perm_options::add, ec);
#endif
    AfterSwap a;
    a.from = currentVersion();
    a.to = version;
    {
        std::lock_guard<std::mutex> g(s.m);
        a.skip = s.skip;
    }
    // 4. Swap (Windows: the helper does it once the game has exited).
#ifndef _WIN32
    {
        std::lock_guard<std::mutex> g(s.m);
        s.st.message = "INSTALLING";
    }
    if (!swapIn(inst.live, fresh, inst.previous, keepAcrossUpdates(inst.layout), why)) {
        removeTree(inst.staging);
        fail(why);
        return;
    }
    removeTree(inst.staging);
    std::remove(zip.c_str());
#endif
    writeState(a);
    std::lock_guard<std::mutex> g(s.m);
    s.st.phase = Phase::Restarting;
    s.st.title = "UPDATE " + version + " INSTALLED";
    s.st.message = "RESTARTING";
    s.busy = false;
}

// ---- processes -------------------------------------------------------------------------------------------------------
#ifdef _WIN32
std::string quoteArg(const std::string& a) { return "\"" + a + "\""; }  // (Windows paths can't hold a quote)

bool startProcess(const std::string& exe, const std::vector<std::string>& args, const std::string& cwd) {
    std::string cmd = quoteArg(exe);
    for (const std::string& a : args) cmd += " " + quoteArg(a);
    STARTUPINFOA si;
    std::memset(&si, 0, sizeof si);
    si.cb = sizeof si;
    PROCESS_INFORMATION pi;
    std::vector<char> line(cmd.begin(), cmd.end());
    line.push_back(0);
    if (!CreateProcessA(exe.c_str(), line.data(), nullptr, nullptr, FALSE, 0, nullptr, cwd.empty() ? nullptr : cwd.c_str(), &si, &pi)) return false;
    CloseHandle(pi.hThread);
    CloseHandle(pi.hProcess);
    return true;
}

// Copies this executable (and SDL2.dll beside it) to %TEMP%\cz-swap-helper-<pid>\ and starts it as the helper.
bool startHelper(const std::string& op, const Install& inst, const std::string& fresh, const std::string& relaunch) {
    std::error_code ec;
    const fs::path dir = fs::temp_directory_path(ec) / ("cz-swap-helper-" + std::to_string(GetCurrentProcessId()));
    if (ec) return false;
    fs::create_directories(dir, ec);
    const std::string helper = (dir / "cz-update-helper.exe").string();
    fs::copy_file(exePath(), helper, fs::copy_options::overwrite_existing, ec);
    if (ec) return false;
    fs::copy_file(exeDir() + "/SDL2.dll", dir / "SDL2.dll", fs::copy_options::overwrite_existing, ec);
    return startProcess(helper, {"--cz-swap", op, std::to_string(GetCurrentProcessId()), inst.live, fresh, inst.previous, statePath(), relaunch},
                        dir.string());
}
#else
bool startProcess(const std::string& exe, const std::vector<std::string>& args) {
    std::vector<std::string> all = {exe};
    all.insert(all.end(), args.begin(), args.end());
    std::vector<char*> argv;
    for (std::string& a : all) argv.push_back(&a[0]);
    argv.push_back(nullptr);
    pid_t pid;
    return posix_spawn(&pid, exe.c_str(), nullptr, nullptr, argv.data(), environ) == 0;
}
#endif

// Starts `live` (the installed game) again: exec on Linux (never returns when it works), open on the Mac, a new process on Windows.
[[maybe_unused]] bool relaunch(Layout l, const std::string& live, int argc, char** argv) {
#ifdef _WIN32
    (void)argc, (void)argv;
    return startProcess(exeIn(l, live), {}, live);
#elif defined(__APPLE__)
    (void)l, (void)argc, (void)argv;
    return startProcess("/usr/bin/open", {"-n", live});
#else
    const std::string exe = exeIn(l, live);
    std::vector<std::string> args = {exe};
    for (int i = 1; i < argc; i++) args.push_back(argv[i]);
    std::vector<char*> cargs;
    for (std::string& a : args) cargs.push_back(&a[0]);
    cargs.push_back(nullptr);
    execv(exe.c_str(), cargs.data());
    return false;
#endif
}

void waitForExit(long pid) {
#ifdef _WIN32
    HANDLE h = OpenProcess(SYNCHRONIZE, FALSE, (DWORD)pid);
    if (!h) return;
    WaitForSingleObject(h, 120000);
    CloseHandle(h);
#else
    for (int i = 0; i < 1200 && pid > 0 && kill((pid_t)pid, 0) == 0; i++) std::this_thread::sleep_for(std::chrono::milliseconds(100));
#endif
}

}  // namespace

// ---- the API ---------------------------------------------------------------------------------------------------------
std::string currentVersion() { return CZ_VERSION; }

bool init(int argc, char** argv, int ourNet) {
    State& s = S();
    std::lock_guard<std::mutex> g(s.m);
    s.inited = true;
    s.ourNet = ourNet;
    s.url = kDefaultUpdateUrl;
    {  // <config>/update.txt: "url https://...", "check off"
        std::istringstream in(readFile(cfgDir() + "/update.txt"));
        std::string line;
        while (std::getline(in, line)) {
            std::istringstream ls(line);
            std::string k, v;
            ls >> k >> v;
            if (k == "url") s.url = v;
            else if (k == "check" && v == "off") s.launchCheck = false;
            if (k == "url" && v == "off") s.url.clear();
        }
    }
    for (int i = 1; i < argc; i++) {
        if (!std::strcmp(argv[i], "--update-url") && i + 1 < argc) s.url = argv[++i];
        else if (!std::strcmp(argv[i], "--no-update-check")) s.launchCheck = false;
    }
    if (std::getenv("CZ_NO_UPDATE")) s.launchCheck = false;
    if (!s.url.empty() && !allowedUrl(s.url)) {
        logLine("ignoring the update address (it must be https://): " + s.url);
        s.url.clear();
    }
    s.inst = detectInstall(s.testExeDir.empty() ? exeDir() : s.testExeDir);

    {
        std::istringstream told(readFile(cfgDir() + "/update-told.txt"));
        told >> s.told;
    }
    AfterSwap a = readAfterSwap(readFile(statePath()));
    s.skip = a.skip;
    switch (onStart(a, currentVersion())) {
        case StartAction::None:
            if (!a.to.empty() || a.starts) {  // (stale: another version is running now)
                AfterSwap keep;
                keep.skip = a.skip;
                writeState(keep);
            }
            break;
        case StartAction::Count:
            a.starts++;
            writeState(a);
            break;
        case StartAction::ReportFailure: {
            setInfo("THE UPDATE DIDN'T INSTALL", a.failed + ". YOU'RE STILL ON " + currentVersion());
            AfterSwap keep;
            keep.skip = a.skip;
            writeState(keep);
            break;
        }
        case StartAction::ReportRollBack: {
            // (a.from: the version that didn't start; this is the one before it)
            setInfo("BACK ON " + currentVersion(), a.from + " DIDN'T START TWICE, SO THE GAME WENT BACK TO " + currentVersion() + ". IT WON'T OFFER " +
                                                    a.from + " AGAIN ON LAUNCH");
            AfterSwap keep;
            keep.skip = a.from;
            s.skip = a.from;
            writeState(keep);
            break;
        }
        case StartAction::RollBack: {
            logLine(a.to + " failed to start twice: rolling back to " + a.from);
            if (s.inst.layout == Layout::None || s.inst.layout == Layout::Deb) {
                writeState(AfterSwap());
                break;
            }
            AfterSwap back;
            back.from = a.to;
            back.to = a.from;
            back.rolledBack = true;
#ifdef _WIN32
            writeState(back);
            if (startHelper("back", s.inst, "-", exeIn(s.inst.layout, s.inst.live))) return true;
            writeState(AfterSwap());
            setInfo("COULDN'T GO BACK TO " + a.from, "THE HELPER DIDN'T START");
#else
            std::string why;
            if (!rollBack(s.inst.live, s.inst.previous, why)) {
                writeState(AfterSwap());
                setInfo("COULDN'T GO BACK TO " + a.from, why);
                break;
            }
            writeState(back);
            if (relaunch(s.inst.layout, s.inst.live, argc, argv)) return true;
            setInfo("WENT BACK TO " + a.from, "RESTART THE GAME TO USE IT");
#endif
            break;
        }
    }
    // Leftovers of an earlier attempt.
    if (!s.inst.staging.empty() && a.to.empty()) removeTree(s.inst.staging);
#ifdef _WIN32
    {
        std::error_code ec;
        for (const auto& e : fs::directory_iterator(fs::temp_directory_path(ec), ec)) {
            const std::string n = e.path().filename().string();
            if (n.compare(0, 15, "cz-swap-helper-") == 0 && n != "cz-swap-helper-" + std::to_string(GetCurrentProcessId())) fs::remove_all(e.path(), ec);
        }
    }
#endif
    return false;
}

void checkOnLaunch() {
    State& s = S();
    std::lock_guard<std::mutex> g(s.m);
    if (!s.inited || !s.launchCheck || s.busy || s.st.phase != Phase::Idle) return;
    // A development build (no install marker) can't update itself: no launch check (no network) unless an address was given on
    // purpose (update.txt, --update-url, a build's UPDATE_URL). The multiplayer hook (checkFor) still asks.
    if (s.inst.layout == Layout::None && s.url == kDefaultUpdateUrlBuiltin) return;
    startWorker([] { runCheck(false, 0); });
}

void checkFor(int hostNet, const std::string& hostRelease) {
    State& s = S();
    std::lock_guard<std::mutex> g(s.m);
    if (!s.inited || s.busy || s.st.phase == Phase::Downloading || s.st.phase == Phase::Installing || s.st.phase == Phase::Restarting) return;
    s.st = Status();
    s.st.phase = Phase::Checking;
    s.st.title = "CHECKING FOR UPDATES";
    s.st.message = hostRelease.empty() ? "YOUR FRIEND'S GAME IS A NEWER VERSION" : "YOUR FRIEND HAS " + hostRelease + ", YOU HAVE " + currentVersion();
    startWorker([hostNet] { runCheck(true, hostNet); });
}

Status status() {
    State& s = S();
    std::lock_guard<std::mutex> g(s.m);
    Status st = s.st;
    if (st.phase == Phase::Downloading) {
        st.got = s.progress.got;
        st.total = s.progress.total ? (uint64_t)s.progress.total : s.file.size;
        st.progress = st.total ? std::min(1.0f, (float)((double)st.got / (double)st.total)) : -1.0f;
    }
    return st;
}

std::string takeNotice() {
    State& s = S();
    std::lock_guard<std::mutex> g(s.m);
    if (s.notices.empty()) return "";
    const std::string n = s.notices.front();
    s.notices.pop_front();
    return n;
}

void accept() {
    State& s = S();
    std::lock_guard<std::mutex> g(s.m);
    if (s.st.phase != Phase::Offer || s.busy) return;
    s.progress.cancel = false;
    s.progress.got = 0;
    s.progress.total = 0;
    s.st.phase = Phase::Downloading;
    s.st.message = "DOWNLOADING";
    startWorker(runInstall);
}

void later() {
    State& s = S();
    std::lock_guard<std::mutex> g(s.m);
    if (s.st.phase == Phase::Offer) s.laterChosen = true;
    if (s.st.phase == Phase::Offer || s.st.phase == Phase::Checking) s.st = Status();
}

void cancel() {
    State& s = S();
    std::lock_guard<std::mutex> g(s.m);
    if (s.st.phase == Phase::Downloading) s.progress.cancel = true;
}

void dismiss() {
    State& s = S();
    std::lock_guard<std::mutex> g(s.m);
    if (s.st.phase == Phase::Info || s.st.phase == Phase::Checking) s.st = Status();
    if (!s.telling.empty()) {
        s.told = s.telling;
        s.telling.clear();
        makeDir(cfgDir());
        writeFileAtomic(cfgDir() + "/update-told.txt", s.told + "\n");
    }
}

bool restart(int argc, char** argv) {
    State& s = S();
    Install inst;
    {
        std::lock_guard<std::mutex> g(s.m);
        if (s.st.phase != Phase::Restarting) return false;
        inst = s.inst;
    }
#ifdef _WIN32
    const bool ok = startHelper("in", inst, inst.staging + "/" + zipTopFolder(inst.layout), exeIn(inst.layout, inst.live));
#else
    const bool ok = relaunch(inst.layout, inst.live, argc, argv);
#endif
    (void)argc, (void)argv;
    if (ok) return true;
    std::lock_guard<std::mutex> g(s.m);
#ifdef _WIN32
    writeState(AfterSwap());
    setInfo("UPDATE FAILED", "THE INSTALL HELPER DIDN'T START. YOU'RE STILL ON " + currentVersion());
#else
    setInfo("UPDATE INSTALLED", "THE GAME COULDN'T RESTART ITSELF: QUIT AND START IT AGAIN TO USE " + s.manifest.version);
#endif
    return false;
}

void markGood() {
    State& s = S();
    std::lock_guard<std::mutex> g(s.m);
    AfterSwap a = readAfterSwap(readFile(statePath()));
    if (a.to.empty() || a.to != currentVersion()) return;
    AfterSwap keep;
    keep.skip = a.skip;
    writeState(keep);
    logLine(currentVersion() + " started fine: update kept");
}

void testSetup(const std::string& config, const std::string& exeDirectory, const std::vector<PublicKey>& keys) {
    State& s = S();
    for (int i = 0; i < 500 && s.busy; i++) std::this_thread::sleep_for(std::chrono::milliseconds(10));
    std::lock_guard<std::mutex> g(s.m);
    s.st = Status();
    s.notices.clear();
    s.laterChosen = false;
    s.launchCheck = true;
    s.inited = false;
    s.told.clear(), s.telling.clear();
    s.testConfig = config;
    s.testExeDir = exeDirectory;
    s.testKeys = keys;
}

bool isHelperCall(int argc, char** argv) { return argc > 1 && !std::strcmp(argv[1], "--cz-swap"); }

// --cz-swap in|back PID LIVE FRESH PREVIOUS STATEFILE RELAUNCH("-" = none)
int helperMain(int argc, char** argv) {
    if (argc < 9) return 2;
    const std::string op = argv[2], live = argv[4], fresh = argv[5], previous = argv[6], state = argv[7], again = argv[8];
    waitForExit(std::atol(argv[3]));
    std::string why;
    bool ok = false;
    for (int attempt = 0; attempt < 20 && !ok; attempt++) {  // (an antivirus scan can hold a file for a moment)
        if (attempt) std::this_thread::sleep_for(std::chrono::milliseconds(500));
        ok = op == "back" ? rollBack(live, previous, why) : swapIn(live, fresh, previous, {"Uninstall.exe"}, why);
    }
    AfterSwap a = readAfterSwap(readFile(state));
    if (!ok) {
        AfterSwap f;
        f.failed = op == "back" ? "COULDN'T GO BACK TO THE PREVIOUS VERSION: " + why : why;
        f.skip = a.skip;
        writeFileAtomic(state, writeAfterSwap(f));
    }
    if (ok && op == "in") {
        const std::string staging = fs::path(fresh).parent_path().string();
        removeTree(staging);
    }
    if (again != "-") {
#ifdef _WIN32
        startProcess(again, {}, live);
#else
        startProcess(again, {});
#endif
    }
    return ok ? 0 : 1;
}

}  // namespace update

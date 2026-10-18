// Scenario test runner.
//
//   build/run_tests                       run everything (in parallel)
//   build/run_tests braking ai/           only scenarios whose name contains any of the words
//   build/run_tests --list                list scenarios
//   build/run_tests --jobs 4              limit parallelism
//   build/run_tests --update-baseline     record current metric values as the baseline
//   build/run_tests --scenario-timeout S  wall-clock limit per scenario (default 300 s, a few long ones have their own);
//                                         a scenario over it prints "HANG name after N s" plus every thread's wait channel,
//                                         then the process _exit()s non-zero (a hung thread can't be joined)
//   build/run_tests --selftest             also run the "selftest/" scenarios (they hang on purpose: the watchdog's own test)
//
// Writes build/test_report.html (charts, metrics, checks) and compares every
// metric with tests/baseline.txt so behavior changes show up even when they
// still fall inside the expected range.
#include "../src/meshcache.h"
#include <unistd.h>

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cstdio>
#include <cstring>
#include <fstream>
#include <map>
#include <mutex>
#include <sstream>
#include <cstdlib>
#include <thread>

#include "../src/watchdog.h"
#include "harness.h"

namespace {

struct Result {
    const Scenario* sc;
    std::vector<CheckResult> checks;
    std::vector<MetricResult> metrics;
    std::vector<SeriesData> series;
    std::vector<std::string> notes;
    double wall = 0, simTime = 0;
    bool passed = false;
};

const char* kBaselinePath = "tests/baseline.txt";
const char* kReportPath = "build/test_report.html";
const double kDriftTolerance = 0.10;  // flag metrics that move more than 10% from the baseline

bool useColor = false;
const char* col(const char* code) { return useColor ? code : ""; }
#define GREEN col("\033[32m")
#define RED col("\033[31m")
#define YELLOW col("\033[33m")
#define DIM col("\033[2m")
#define BOLD col("\033[1m")
#define RESET col("\033[0m")

Result runScenario(const Scenario& sc) {
    Result r;
    r.sc = &sc;
    auto t0 = std::chrono::steady_clock::now();
    Sim sim;
    sim.setupEmpty();
    Ctx ctx(sim);
    try {
        sc.body(ctx);
    } catch (const std::exception& e) {
        ctx.check("scenario ran without throwing", false, e.what());
    }
    bool invariantsFailed = false;
    for (const CheckResult& c : ctx.checks) invariantsFailed |= c.name.rfind("invariant:", 0) == 0;
    if (!invariantsFailed) ctx.check("invariants held every frame (finite, no explosions, no tunnelling)", true);
    r.wall = std::chrono::duration<double>(std::chrono::steady_clock::now() - t0).count();
    r.simTime = ctx.t;
    r.checks = ctx.checks;
    r.metrics = ctx.metrics;
    r.series = ctx.series;
    r.notes = ctx.notes;
    r.passed = true;
    for (const CheckResult& c : r.checks) r.passed &= c.ok;
    for (const MetricResult& m : r.metrics) r.passed &= m.ok;
    return r;
}

// ---- hang watchdog -----------------------------------------------------------
// Each worker publishes the scenario it is running; a watchdog thread trips when one is over its limit.
const double kDefaultTimeout = 300;  // s of wall time (a scenario takes 1-100 s; under load and with --jobs 6 several times that)
double scenarioTimeout = kDefaultTimeout;
bool timeoutGiven = false;
// Scenarios that legitimately run long, with their own limit (the longest, net/crash-share, took 84 s at --jobs 6 on a busy machine; x3 is under the 300 s default, these get headroom).
const std::map<std::string, double> kLongScenarios = {{"net/crash-share", 400}, {"ai/traffic", 400}, {"vehicle-physics/top-speed", 400}};
double limitFor(const std::string& name) {
    auto it = kLongScenarios.find(name);
    return it != kLongScenarios.end() && !timeoutGiven ? std::max(it->second, scenarioTimeout) : scenarioTimeout;
}

struct Slot {
    std::atomic<int> idx{-1};
    std::atomic<double> start{0};
};

std::string key(const Result& r, const MetricResult& m) { return r.sc->name + " | " + m.name; }

std::map<std::string, double> loadBaseline() {
    std::map<std::string, double> b;
    std::ifstream in(kBaselinePath);
    std::string line;
    while (std::getline(in, line)) {
        size_t eq = line.rfind(" = ");
        if (line.empty() || line[0] == '#' || eq == std::string::npos) continue;
        b[line.substr(0, eq)] = std::atof(line.c_str() + eq + 3);
    }
    return b;
}

void saveBaseline(const std::vector<Result>& results) {
    std::ofstream out(kBaselinePath);
    out << "# Metric values recorded by `build/run_tests --update-baseline`.\n"
           "# The runner flags metrics that drift more than 10% from these.\n";
    for (const Result& r : results)
        for (const MetricResult& m : r.metrics) {
            char buf[64];
            std::snprintf(buf, sizeof(buf), "%.6g", m.value);
            out << key(r, m) << " = " << buf << "\n";
        }
}

// Relative change vs baseline, or NaN when there is no baseline value.
double drift(const std::map<std::string, double>& base, const Result& r, const MetricResult& m) {
    if (m.name.find("may vary") != std::string::npos) return NAN;  // timing: machine-dependent
    auto it = base.find(key(r, m));
    if (it == base.end()) return NAN;
    double b = it->second;
    double scale = std::max({std::fabs(b), std::fabs(m.hi - m.lo) * 0.05, 1e-9});
    return (m.value - b) / scale;
}

std::string fmt(double v) {
    char buf[32];
    if (!std::isfinite(v)) return "n/a";
    if (std::fabs(v) >= 1e5) std::snprintf(buf, sizeof(buf), "%.3g", v);
    else if (std::fabs(v) >= 100) std::snprintf(buf, sizeof(buf), "%.0f", v);
    else if (std::fabs(v) >= 10) std::snprintf(buf, sizeof(buf), "%.1f", v);
    else std::snprintf(buf, sizeof(buf), "%.2f", v);
    return buf;
}

std::string esc(const std::string& s) {
    std::string o;
    for (char c : s) {
        if (c == '<') o += "&lt;";
        else if (c == '>') o += "&gt;";
        else if (c == '&') o += "&amp;";
        else if (c == '"') o += "&quot;";
        else o += c;
    }
    return o;
}

void printResult(const Result& r, const std::map<std::string, double>& base, bool verbose) {
    std::printf("%s%s%s %s%-34s%s %s%5.1f s sim, %4.1f s wall%s\n", r.passed ? GREEN : RED, r.passed ? "PASS" : "FAIL",
                RESET, BOLD, r.sc->name.c_str(), RESET, DIM, r.simTime, r.wall, RESET);
    for (const CheckResult& c : r.checks)
        if (!c.ok || verbose)
            std::printf("     %s%s%s %s%s%s\n", c.ok ? GREEN : RED, c.ok ? "ok  " : "FAIL", RESET, c.name.c_str(),
                        c.detail.empty() ? "" : "  -- ", c.detail.c_str());
    for (const MetricResult& m : r.metrics) {
        double d = drift(base, r, m);
        bool drifted = std::isfinite(d) && std::fabs(d) > kDriftTolerance;
        if (!m.ok || verbose || drifted) {
            std::printf("     %s%s%s %s: %s%s%s %s  %s[%s .. %s]%s", m.ok ? GREEN : RED, m.ok ? "ok  " : "FAIL", RESET,
                        m.name.c_str(), BOLD, fmt(m.value).c_str(), RESET, m.unit.c_str(), DIM, fmt(m.lo).c_str(),
                        fmt(m.hi).c_str(), RESET);
            if (std::isfinite(d)) std::printf("  %s%+.0f%% vs baseline%s", drifted ? YELLOW : DIM, d * 100, RESET);
            std::printf("\n");
        }
    }
}

// ---- HTML report -------------------------------------------------------------

const char* kPalette[] = {"#2a78d6", "#d9822b", "#1f9d6e", "#c43d5a", "#7b5cc4", "#8c8c3a"};

std::string chartSvg(const std::string& title, const std::vector<const SeriesData*>& lines) {
    double t0 = 1e18, t1 = -1e18, v0 = 1e18, v1 = -1e18;
    for (const SeriesData* s : lines)
        for (size_t i = 0; i < s->t.size(); i++) {
            t0 = std::min(t0, s->t[i]);
            t1 = std::max(t1, s->t[i]);
            if (std::isfinite(s->v[i])) {
                v0 = std::min(v0, s->v[i]);
                v1 = std::max(v1, s->v[i]);
            }
        }
    if (t1 <= t0) t1 = t0 + 1;
    if (v1 <= v0) {
        v0 -= 1;
        v1 += 1;
    }
    double pad = (v1 - v0) * 0.08;
    v0 -= pad;
    v1 += pad;
    const double W = 560, H = 200, L = 52, R = 12, T = 12, B = 30;
    auto X = [&](double t) { return L + (t - t0) / (t1 - t0) * (W - L - R); };
    auto Y = [&](double v) { return T + (1 - (v - v0) / (v1 - v0)) * (H - T - B); };
    std::ostringstream o;
    o << "<figure class=chart><figcaption>" << esc(title);
    if (!lines.empty() && !lines[0]->unit.empty()) o << " <span class=unit>(" << esc(lines[0]->unit) << ")</span>";
    o << "</figcaption><svg viewBox='0 0 " << W << " " << H << "' role=img>";
    for (int k = 0; k <= 4; k++) {
        double v = v0 + (v1 - v0) * k / 4;
        o << "<line class=grid x1=" << L << " x2=" << W - R << " y1=" << Y(v) << " y2=" << Y(v) << " />";
        o << "<text class=axis x=" << L - 6 << " y=" << Y(v) + 4 << " text-anchor=end>" << fmt(v) << "</text>";
    }
    for (int k = 0; k <= 4; k++) {
        double t = t0 + (t1 - t0) * k / 4;
        o << "<text class=axis x=" << X(t) << " y=" << H - 10 << " text-anchor=middle>" << fmt(t) << " s</text>";
    }
    for (size_t li = 0; li < lines.size(); li++) {
        const SeriesData* s = lines[li];
        o << "<polyline fill=none stroke='" << kPalette[li % 6] << "' stroke-width=2 stroke-linejoin=round points='";
        size_t step = std::max<size_t>(1, s->t.size() / 600);
        for (size_t i = 0; i < s->t.size(); i += step)
            if (std::isfinite(s->v[i])) o << X(s->t[i]) << "," << Y(s->v[i]) << " ";
        o << "' />";
    }
    o << "</svg>";
    if (lines.size() > 1) {
        o << "<div class=legend>";
        for (size_t li = 0; li < lines.size(); li++)
            o << "<span><i style='background:" << kPalette[li % 6] << "'></i>" << esc(lines[li]->line) << "</span>";
        o << "</div>";
    }
    o << "</figure>";
    return o.str();
}

std::string rangeBar(const MetricResult& m) {
    // Expected band in the middle of the track, value marker on top.
    double span = m.hi - m.lo;
    double lo = m.lo - span * 0.5, hi = m.hi + span * 0.5;
    if (!std::isfinite(span) || span <= 0 || span > 1e6) return "";
    auto P = [&](double v) { return clampd((v - lo) / (hi - lo), 0, 1) * 100; };
    std::ostringstream o;
    o << "<div class=bar><div class=band style='left:" << P(m.lo) << "%;width:" << P(m.hi) - P(m.lo) << "%'></div>"
      << "<div class='mark " << (m.ok ? "ok" : "bad") << "' style='left:" << P(m.value) << "%'></div></div>";
    return o.str();
}

void writeReport(const std::vector<Result>& results, const std::map<std::string, double>& base, double wall) {
    int passed = 0, drifted = 0;
    for (const Result& r : results) {
        passed += r.passed;
        for (const MetricResult& m : r.metrics) {
            double d = drift(base, r, m);
            drifted += std::isfinite(d) && std::fabs(d) > kDriftTolerance;
        }
    }
    std::ofstream o(kReportPath);
    o << R"(<!doctype html><html lang=en><head><meta charset=utf-8><meta name=viewport content="width=device-width,initial-scale=1">
<title>Crumple Zone Tests</title><style>
:root{--bg:#f6f7f9;--card:#fff;--ink:#1d2330;--mute:#6b7280;--line:#e3e6eb;--ok:#1f9d6e;--bad:#c8364b;--warn:#c27c0e;--band:#d8efe5}
@media (prefers-color-scheme:dark){:root{--bg:#14171c;--card:#1d2128;--ink:#e6e8eb;--mute:#9aa3ae;--line:#2c323b;--ok:#3ccf95;--bad:#ff6b81;--warn:#f0b450;--band:#1f4536}}
*{box-sizing:border-box}body{margin:0;background:var(--bg);color:var(--ink);font:14px/1.45 system-ui,-apple-system,"Segoe UI",sans-serif}
main{max-width:1180px;margin:0 auto;padding:24px 16px 60px}h1{font-size:22px;margin:0 0 4px}.sub{color:var(--mute);margin:0 0 18px}
.stats{display:flex;gap:12px;flex-wrap:wrap;margin-bottom:16px}.stat{background:var(--card);border:1px solid var(--line);border-radius:10px;padding:10px 16px;min-width:120px}
.stat b{display:block;font-size:22px}.stat span{color:var(--mute);font-size:12px;text-transform:uppercase;letter-spacing:.04em}
.tools{display:flex;gap:8px;flex-wrap:wrap;margin:0 0 14px}.tools input{flex:1;min-width:200px;padding:8px 10px;border-radius:8px;border:1px solid var(--line);background:var(--card);color:var(--ink)}
.tools button{padding:8px 12px;border-radius:8px;border:1px solid var(--line);background:var(--card);color:var(--ink);cursor:pointer}.tools button.on{border-color:var(--ink)}
details{background:var(--card);border:1px solid var(--line);border-radius:10px;margin:0 0 8px}summary{cursor:pointer;padding:10px 14px;display:flex;gap:10px;align-items:baseline;flex-wrap:wrap}
.badge{font-weight:700;font-size:12px;padding:2px 8px;border-radius:999px;color:#fff}.pass{background:var(--ok)}.fail{background:var(--bad)}
.name{font-weight:600}.desc{color:var(--mute)}.time{margin-left:auto;color:var(--mute);font-size:12px}
.body{padding:0 14px 14px}table{width:100%;border-collapse:collapse;margin:6px 0 10px}td,th{padding:5px 6px;border-top:1px solid var(--line);text-align:left;vertical-align:middle}
th{color:var(--mute);font-weight:500;font-size:12px}td.v{font-variant-numeric:tabular-nums;white-space:nowrap}.ok{color:var(--ok)}.bad{color:var(--bad)}.warn{color:var(--warn)}
.bar{position:relative;height:8px;background:var(--line);border-radius:4px;min-width:120px}.band{position:absolute;top:0;bottom:0;background:var(--band);border-radius:4px}
.mark{position:absolute;top:-3px;width:4px;height:14px;margin-left:-2px;border-radius:2px}.mark.ok{background:var(--ok)}.mark.bad{background:var(--bad)}
.charts{display:grid;grid-template-columns:repeat(auto-fill,minmax(300px,1fr));gap:12px}figure.chart{margin:0}figcaption{font-size:12px;color:var(--mute);margin-bottom:2px}
svg{width:100%;height:auto;display:block}.grid{stroke:var(--line);stroke-width:1}.axis{fill:var(--mute);font-size:11px}.unit{opacity:.8}
.legend{display:flex;gap:10px;flex-wrap:wrap;font-size:12px;color:var(--mute)}.legend i{display:inline-block;width:10px;height:3px;margin-right:4px;vertical-align:middle}
ul.checks{list-style:none;padding:0;margin:6px 0}ul.checks li{padding:2px 0}.detail{color:var(--mute)}
@media (max-width:600px){.time{margin-left:0}.bar{min-width:70px}}
</style></head><body><main>
<h1>Crumple Zone scenario tests</h1>)";
    o << "<p class=sub>" << results.size() << " scenarios, run in " << fmt(wall)
      << " s. Ranges are physical expectations; the baseline column shows movement since the last <code>--update-baseline</code>.</p>";
    o << "<div class=stats><div class=stat><b class=ok>" << passed << "</b><span>passed</span></div>"
      << "<div class=stat><b class=" << (results.size() - passed ? "bad" : "ok") << ">" << results.size() - passed
      << "</b><span>failed</span></div><div class=stat><b class=" << (drifted ? "warn" : "ok") << ">" << drifted
      << "</b><span>metrics drifted &gt;10%</span></div></div>";
    o << "<div class=tools><input id=q placeholder='Filter by name...'><button data-f=all class=on>All</button>"
         "<button data-f=fail>Failed</button><button data-f=drift>Drifted</button></div>";
    for (const Result& r : results) {
        bool anyDrift = false;
        for (const MetricResult& m : r.metrics) {
            double d = drift(base, r, m);
            anyDrift |= std::isfinite(d) && std::fabs(d) > kDriftTolerance;
        }
        o << "<details data-name='" << esc(r.sc->name) << "' data-fail=" << !r.passed << " data-drift=" << anyDrift
          << (r.passed ? "" : " open") << "><summary><span class='badge " << (r.passed ? "pass'>PASS" : "fail'>FAIL")
          << "</span><span class=name>" << esc(r.sc->name) << "</span><span class=desc>" << esc(r.sc->description)
          << "</span><span class=time>" << fmt(r.simTime) << " s sim &middot; " << fmt(r.wall) << " s wall</span></summary><div class=body>";
        if (!r.metrics.empty()) {
            o << "<table><tr><th>Metric</th><th>Value</th><th>Expected</th><th></th><th>vs baseline</th></tr>";
            for (const MetricResult& m : r.metrics) {
                double d = drift(base, r, m);
                bool dr = std::isfinite(d) && std::fabs(d) > kDriftTolerance;
                o << "<tr><td>" << esc(m.name) << "</td><td class='v " << (m.ok ? "ok" : "bad") << "'>" << fmt(m.value) << " "
                  << esc(m.unit) << "</td><td class=v>" << fmt(m.lo) << " &ndash; " << fmt(m.hi) << "</td><td>" << rangeBar(m)
                  << "</td><td class='v " << (dr ? "warn" : "") << "'>";
                if (std::isfinite(d)) o << (d >= 0 ? "+" : "") << fmt(d * 100) << "%";
                else o << "&ndash;";
                o << "</td></tr>";
            }
            o << "</table>";
        }
        o << "<ul class=checks>";
        for (const CheckResult& c : r.checks)
            o << "<li><span class=" << (c.ok ? "ok>&#10003;" : "bad>&#10007;") << "</span> " << esc(c.name)
              << (c.detail.empty() ? "" : " <span class=detail>&mdash; " + esc(c.detail) + "</span>") << "</li>";
        o << "</ul>";
        std::vector<std::string> charts;
        for (const SeriesData& s : r.series)
            if (std::find(charts.begin(), charts.end(), s.chart) == charts.end()) charts.push_back(s.chart);
        if (!charts.empty()) {
            o << "<div class=charts>";
            for (const std::string& ch : charts) {
                std::vector<const SeriesData*> lines;
                for (const SeriesData& s : r.series)
                    if (s.chart == ch) lines.push_back(&s);
                o << chartSvg(ch, lines);
            }
            o << "</div>";
        }
        o << "</div></details>";
    }
    o << R"(<script>
const q=document.getElementById('q');let f='all';
function apply(){for(const d of document.querySelectorAll('details')){const n=d.dataset.name.includes(q.value.trim());
const m=f==='all'||(f==='fail'&&d.dataset.fail==='1')||(f==='drift'&&d.dataset.drift==='1');d.style.display=n&&m?'':'none';}}
q.oninput=apply;for(const b of document.querySelectorAll('.tools button'))b.onclick=()=>{f=b.dataset.f;
document.querySelectorAll('.tools button').forEach(x=>x.classList.toggle('on',x===b));apply();};
</script></main></body></html>)";
}

}  // namespace

int main(int argc, char** argv) {
    meshcache::setEnabled(false);  // (tests generate fresh and leave no cache about; the loading scenarios turn it on for themselves)
    useColor = isatty(1);
    std::vector<std::string> filters;
    bool list = false, update = false, verbose = false, selftest = false;
    int jobs = (int)std::max(1u, std::thread::hardware_concurrency());
    for (int i = 1; i < argc; i++) {
        std::string a = argv[i];
        if (a == "--list") list = true;
        else if (a == "--update-baseline") update = true;
        else if (a == "--verbose" || a == "-v") verbose = true;
        else if (a == "--jobs" && i + 1 < argc) jobs = std::max(1, std::atoi(argv[++i]));
        else if (a == "--scenario-timeout" && i + 1 < argc) scenarioTimeout = std::atof(argv[++i]), timeoutGiven = true;
        else if (a == "--selftest") selftest = true;
        else filters.push_back(a);
    }
    std::vector<const Scenario*> todo;
    for (const Scenario& s : scenarioRegistry()) {
        if (s.name.rfind("selftest/", 0) == 0 && !selftest) continue;  // (deliberately broken: only on request)
        bool match = filters.empty();
        for (const std::string& f : filters) match |= s.name.find(f) != std::string::npos;
        if (match) todo.push_back(&s);
    }
    if (list) {
        for (const Scenario* s : todo) std::printf("%-34s %s\n", s->name.c_str(), s->description.c_str());
        return 0;
    }
    if (todo.empty()) {
        std::puts("no scenarios match");
        return 1;
    }

    std::map<std::string, double> base = loadBaseline();
    std::vector<Result> results(todo.size());
    std::atomic<size_t> next{0};
    std::mutex printMutex;
    auto t0 = std::chrono::steady_clock::now();
    std::vector<std::thread> pool;
    std::vector<std::atomic<char>> done(todo.size());
    for (auto& d : done) d = 0;
    std::vector<Slot> slots(std::min<int>(jobs, (int)todo.size()));
    std::vector<std::string> hung;
    // On a hang: say which scenarios and what every thread waits on, still print the summary, leave non-zero without joining.
    wd::Watchdog dog(
        [&]() -> std::string {
            std::string msg;
            const double now = wd::nowSec();
            for (Slot& sl : slots) {
                int i = sl.idx;
                if (i < 0 || done[i]) continue;
                double lim = limitFor(todo[i]->name), el = now - sl.start;
                if (el > lim) {
                    char buf[256];
                    std::snprintf(buf, sizeof(buf), "HANG %s after %.0f s", todo[i]->name.c_str(), el);
                    msg += (msg.empty() ? "" : "\n") + std::string(buf);
                    hung.push_back(todo[i]->name);
                }
            }
            return msg;
        },
        [&](const std::string& msg) {
            std::lock_guard<std::mutex> lock(printMutex);
            std::printf("%s%s%s\n", RED, msg.c_str(), RESET);
            wd::dumpThreads();
            size_t fin = 0;
            std::string failedNames;
            for (size_t i = 0; i < todo.size(); i++)
                if (done[i]) {
                    fin++;
                    if (!results[i].passed) failedNames += " " + todo[i]->name;
                }
            for (const std::string& h : hung) failedNames += " " + h;
            std::printf("\n%s%zu scenarios: %zu finished, %zu HUNG (FAIL), %zu not finished, %.1f s. No report written.%s\nFailed:%s\n", RED, todo.size(), fin,
                        hung.size(), todo.size() - fin - hung.size(), std::chrono::duration<double>(std::chrono::steady_clock::now() - t0).count(), RESET,
                        failedNames.c_str());
            wd::hardExit(1);
        });
    for (size_t j = 0; j < slots.size(); j++)
        pool.emplace_back([&, j] {
            for (size_t i; (i = next++) < todo.size();) {
                slots[j].start = wd::nowSec();
                slots[j].idx = (int)i;
                results[i] = runScenario(*todo[i]);
                done[i] = 1;
                slots[j].idx = -1;
                std::lock_guard<std::mutex> lock(printMutex);
                printResult(results[i], base, verbose);
                std::fflush(stdout);
            }
        });
    for (std::thread& t : pool) t.join();
    double wall = std::chrono::duration<double>(std::chrono::steady_clock::now() - t0).count();

    int failed = 0, drifted = 0;
    for (const Result& r : results) {
        failed += !r.passed;
        for (const MetricResult& m : r.metrics) {
            double d = drift(base, r, m);
            drifted += std::isfinite(d) && std::fabs(d) > kDriftTolerance;
        }
    }
    writeReport(results, base, wall);
    if (update) {
        saveBaseline(results);
        std::printf("baseline written to %s\n", kBaselinePath);
    }
    std::printf("\n%s%zu scenarios: %zu passed, %d failed%s, %d metrics drifted from baseline, %.1f s. Report: %s\n",
                failed ? RED : GREEN, results.size(), results.size() - failed, failed, RESET, drifted, wall, kReportPath);
    if (failed) {
        std::printf("Failed:");
        for (const Result& r : results)
            if (!r.passed) std::printf(" %s", r.sc->name.c_str());
        std::printf("\n");
    }
    return failed ? 1 : 0;
}

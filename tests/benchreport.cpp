// The bench report file and --bench-suite's bookkeeping (benchreport.h). No SDL or GL.
#include <cmath>
#include <cstdio>
#include <cstring>
#include <string>

#include "../src/benchreport.h"
#include "../src/perf.h"
#include "../src/platform.h"
#include "../src/world.h"
#include "harness.h"

static bool has(const std::string& s, const char* part) { return s.find(part) != std::string::npos; }

static MachineInfo fakeMachine() {
    MachineInfo m;
    m.gpu = "TEST GPU 9000", m.vendor = "TEST VENDOR", m.glVersion = "4.6.0 Test Driver 1.2", m.glsl = "4.60", m.videoDriver = "x11";
    m.cpu = "Test CPU @ 3 GHz", m.os = "TestOS 1", m.cores = 8, m.ramMb = 16000, m.refreshHz = 144, m.display = "Panel 1920x1080";
    return m;
}

static void feed(BenchStats& s, double ms, int n) {
    double cpu[FP_COUNT] = {}, gpu[FP_COUNT];
    for (int i = 0; i < FP_COUNT; i++) gpu[i] = -1;
    cpu[FP_SIM] = ms / 2, gpu[FP_TERRAIN] = ms / 4;
    for (int i = 0; i < n; i++) s.add(ms, cpu, gpu);
}

SCENARIO(benchFileScn, "perf/bench-file", "The bench report file: machine, settings, the stage table and the csv line") {
    BenchStats st;
    feed(st, 8, 50);
    BenchRunInfo ri;
    ri.map = "FRONTIER", ri.preset = "MEDIUM", ri.width = 1280, ri.height = 720, ri.drawW = 2560, ri.drawH = 1440, ri.frames = 600, ri.warm = 120;
    ri.settings = "scale 1.00, shadows 2";
    const std::string t = benchFileText(fakeMachine(), ri, st.result());
    ctx.check("GPU, vendor, driver", has(t, "TEST GPU 9000") && has(t, "TEST VENDOR") && has(t, "4.6.0 Test Driver 1.2"));
    ctx.check("CPU, cores, RAM, OS", has(t, "Test CPU @ 3 GHz") && has(t, "8 logical") && has(t, "16000 MB") && has(t, "TestOS 1"));
    ctx.check("refresh rate", has(t, "144 Hz"));
    ctx.check("settings and window", has(t, "scale 1.00, shadows 2") && has(t, "1280x720") && has(t, "2560x1440") && has(t, "600 after 120"));
    ctx.check("stage table and csv", has(t, "bench:   stage") && has(t, "bench-csv: avg=8.00") && has(t, "FRONTIER"));
    MachineInfo q;
    queryMachine(q);
    ctx.check("this machine's CPU, OS, cores and RAM are found", q.cpu != "unknown" && q.os != "unknown" && q.cores > 0 && q.ramMb > 0);
    const std::string stem = benchStem("20261017-142530", "OPEN COUNTRY", "HIGH", 1920, 1080);
    ctx.check("file stem: date-time, map, quality, size, nothing odd", stem == "20261017-142530-open_country-high-1920x1080");
    const std::string path = configDir() + "/bench-test.txt";
    ctx.check("written and read back", writeTextFile(path, t));
    FILE* f = std::fopen(path.c_str(), "rb");
    std::string back;
    for (int c; f && (c = std::fgetc(f)) != EOF;) back += (char)c;
    if (f) std::fclose(f);
    std::remove(path.c_str());
    ctx.check("same text", back == t);
    std::vector<unsigned char> px(4 * 2 * 3, 7);
    const std::string bmp = configDir() + "/bench-test.bmp";
    ctx.check("a BMP is written", writeBMP(bmp, px, 4, 2));
    f = std::fopen(bmp.c_str(), "rb");
    char head[2] = {};
    long size = 0;
    if (f) std::fread(head, 1, 2, f), std::fseek(f, 0, SEEK_END), size = std::ftell(f), std::fclose(f);
    std::remove(bmp.c_str());
    ctx.check("BMP header and size (54 + 2 rows of 12 bytes)", head[0] == 'B' && head[1] == 'M' && size == 54 + 24);
}

SCENARIO(benchSuiteScn, "perf/bench-suite", "The suite: Frontier, Open Country and Boulder Gully at four presets, each run's numbers kept apart") {
    BenchSuite plan;
    ctx.check("twelve runs", plan.size() == 12);
    BenchSuite s;
    int frontier = 0, wilds = 0, gully = 0;
    bool potato = false, high = false;
    while (s.next()) {
        const BenchSuite::Entry& e = s.current();
        frontier += e.map == MAP_FRONTIER, wilds += e.map == MAP_WILDS, gully += e.map == MAP_MOUNTAIN && e.spawn;
        potato |= e.preset == Q_POTATO, high |= e.preset == Q_HIGH;
        ctx.check("a run starts with nothing from the last", s.stats().count() == 0);
        feed(s.stats(), 5.0 + s.index(), 20 + s.index());
        s.record("MAP", "PRESET");
    }
    ctx.check("three maps of four", frontier == 4 && wilds == 4 && gully == 4 && potato && high);
    ctx.check("finished", s.finished() && (int)s.done().size() == 12);
    bool apart = true;
    for (int i = 0; i < 12; i++) apart = apart && s.done()[i].r.frames == 20 + i && std::fabs(s.done()[i].r.avgMs - (5.0 + i)) < 1e-3;
    ctx.check("each run kept its own frames and average", apart);
    const std::string t = s.summary(fakeMachine(), "20261017-142530", 1280, 720, 600, 120);
    int csv = 0;
    for (size_t p = t.find("suite-csv: "); p != std::string::npos; p = t.find("suite-csv: ", p + 1)) csv++;
    ctx.check("one csv line per run, the machine, the table", csv == 12 && has(t, "TEST GPU 9000") && has(t, "Boulder Gully") && has(t, "avg ms"));
}

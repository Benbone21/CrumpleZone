// Offline audio checks: no device, speakers or window required.
#include "../src/audio.h"
#include "../src/horn.h"
#include "../src/mods.h"
#include "../src/softbody.h"
#include "../src/sdl_min.h"
#include "../src/worldsound.h"
#include "../src/toolsounds.h"
#include "../src/fixes.h"
#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <chrono>
#include <thread>
#include <vector>
#include <limits>
#include <functional>
#include <new>

// Trace C++ allocations only around offline calls to the actual callback mixer.
static std::atomic<bool> traceAllocations{false};
static std::atomic<unsigned> callbackAllocations{0};
void* operator new(std::size_t bytes) {
    if (traceAllocations.load(std::memory_order_relaxed)) callbackAllocations++;
    if (void* p = std::malloc(bytes ? bytes : 1)) return p;
    throw std::bad_alloc();
}
void* operator new[](std::size_t bytes) { return ::operator new(bytes); }
void operator delete(void* p) noexcept { std::free(p); }
void operator delete[](void* p) noexcept { std::free(p); }
void operator delete(void* p, std::size_t) noexcept { std::free(p); }
void operator delete[](void* p, std::size_t) noexcept { std::free(p); }

void recordedEngineTests(const std::function<void(const char*, bool)>& check, void (*render)(Audio&, float*, int));
void soundscapeTests(const std::function<void(const char*, bool)>& check, void (*render)(Audio&, float*, int));

struct AudioTest {
    static void render(Audio& audio, float* out, int frames) { audio.render(out, frames); }
    static bool hissRates() {
        for (int rate : {8000, 22050, 44100, 48000, 192000}) {
            Audio audio;
            audio.rate_ = rate;
            audio.setEngine(0, 0); audio.setHiss(1);
            std::vector<float> samples(rate * 2);
            audio.render(samples.data(), rate);
            double energy = 0;
            for (float value : samples) {
                if (!std::isfinite(value) || std::fabs(value) > 1) return false;
                energy += value * value;
            }
            if (energy < 1e-5) return false;
        }
        return true;
    }
    static bool coherent(Audio& audio) {
        audio.updateRemoteVoices();
        const auto& frame = audio.sources_[audio.frontSources_];
        return frame.count == 0 || (frame.count == 1 && frame.engines[0].engine.rpm == frame.engines[0].id);
    }
};

namespace {
struct Energy { double left = 0, right = 0; bool finite = true; };
std::vector<float> engineWave(EngineInput input, int sampleRate = 48000) {
    EngineSynth engine;
    const float dt = 1.0f / sampleRate;
    for (int i = 0; i < sampleRate / 2; i++) engine.sample(input, dt);
    std::vector<float> wave(sampleRate);
    for (float& x : wave) x = engine.sample(input, dt);
    return wave;
}
double rms(const std::vector<float>& wave) {
    double e = 0; for (float x : wave) e += x * x;
    return std::sqrt(e / wave.size());
}
double tone(const std::vector<float>& wave, double hz, int sampleRate = 48000) {
    double re = 0, im = 0;
    for (size_t i = 0; i < wave.size(); i++) {
        const double phase = 2 * M_PI * hz * i / sampleRate;
        re += wave[i] * std::cos(phase); im += wave[i] * std::sin(phase);
    }
    return (re * re + im * im) / (wave.size() * wave.size());
}
Energy measure(Audio& audio) {
    float block[1024];
    Energy energy;
    for (int k = 0; k < 80; k++) {
        AudioTest::render(audio, block, 512);
        for (int i = 0; i < 512; i++) {
            energy.finite &= std::isfinite(block[2 * i]) && std::isfinite(block[2 * i + 1]) &&
                             std::fabs(block[2 * i]) <= 1 && std::fabs(block[2 * i + 1]) <= 1;
            if (k > 40) { energy.left += block[2 * i] * block[2 * i]; energy.right += block[2 * i + 1] * block[2 * i + 1]; }
        }
    }
    return energy;
}
void setup(Sim& sim, const V3& position) {
    sim.setupEmpty();
    sim.player = -1;
    sim.addCar(vehicleCatalog()[2], position, 0, V3(0.4, 0.5, 0.6));
    Car& car = sim.cars[0];
    car.remote = true; car.netId = 65537; car.rpm = 4500; car.throttle = 0.7; car.boost = 0.5;
}
}

int main() {
    int failures = 0;
    auto check = [&](const char* name, bool ok) { std::printf("%s %s\n", ok ? "PASS" : "FAIL", name); failures += !ok; };
    const V3 listener(-40, 0.7, -40), right(0, 0, 1);
    Sim sim;
    setup(sim, V3(-40, 0.05, -35));
    Audio rightSide;
    rightSide.setEngine(0, 0);
    rightSide.setNearbyEngines(sim, listener, right, 1);
    const Energy rightEnergy = measure(rightSide);
    check("nearby remote engine is audible", rightEnergy.left + rightEnergy.right > 1);
    check("right-side source is louder in the right channel", rightEnergy.right > rightEnergy.left * 10);
    sim.cars[0].translate(V3(0, 0, -10));
    Audio leftSide;
    leftSide.setEngine(0, 0);
    leftSide.setNearbyEngines(sim, listener, right, 1);
    const Energy leftEnergy = measure(leftSide);
    check("left-side source is louder in the left channel", leftEnergy.left > leftEnergy.right * 10);
    sim.cars[0].translate(V3(0, 0, -95));
    Audio distant;
    distant.setEngine(0, 0);
    distant.setNearbyEngines(sim, listener, right, 1);
    const Energy distantEnergy = measure(distant);
    check("distance attenuates remote engines", distantEnergy.left + distantEnergy.right < (leftEnergy.left + leftEnergy.right) * 0.02);
    sim.cars.clear();
    leftSide.setNearbyEngines(sim, listener, right, 1);
    measure(leftSide);  // let a departing source fade rather than click off
    const Energy gone = measure(leftSide);
    check("departing player fades to silence", gone.left + gone.right < 1e-8);
    check("stereo mix is finite and bounded", rightEnergy.finite && leftEnergy.finite && distantEnergy.finite && gone.finite);
    bool finiteEngines = true;
    for (const VehicleSpec& v : vehicleCatalog()) {
        EngineSynth engine;
        const EngineInput input{(float)v.redline * 0.8f, 0.9f, 0.8f, v.cylinders, (int)v.induction,
                                (float)v.displacement, (float)v.redline, v.flatPlane};
        double energy = 0;
        for (int i = 0; i < 12000; i++) { const float s = engine.sample(input, 1.0f / 48000); finiteEngines &= std::isfinite(s); energy += s * s; }
        finiteEngines &= energy > 0.1;
        if (energy <= 0.1) std::printf("  %s energy %.3f\n", v.id, energy);
    }
    check("shared engine synth covers all seven vehicle voices", finiteEngines);
    bool clean = true, responds = true;
    for (const VehicleSpec& v : vehicleCatalog()) {
        EngineInput input{3000, 0.05f, 0, v.cylinders, (int)v.induction, (float)v.displacement, (float)v.redline, v.flatPlane};
        const auto quiet = engineWave(input);
        input.load = 0.9f; input.boost = 0.7f;
        const auto loaded = engineWave(input);
        responds &= rms(loaded) > rms(quiet) * 1.3;
        double sum = 0, e = 0, bright = 0; float low = 0, peak = 0;
        const float a = 1 - std::exp(-2 * (float)M_PI * 4000 / 48000);
        for (float x : loaded) {
            clean &= std::isfinite(x) && std::fabs(x) < 0.8;
            sum += x; e += x * x; low += (x - low) * a; bright += (x - low) * (x - low);
            peak = std::max(peak, std::fabs(x));
        }
        clean &= std::fabs(sum / loaded.size()) < 0.002 && bright / e < 0.06;
        if (peak >= 0.8 || std::fabs(sum / loaded.size()) >= 0.002 || bright / e >= 0.06)
            std::printf("  %s peak %.4f DC %.6f brightness %.4f\n", v.id, peak, sum / loaded.size(), bright / e);
    }
    check("all engine layouts remain centered and avoid harsh high-frequency noise", clean);
    check("engine load produces a stronger combustion note for every vehicle", responds);
    EngineInput four{3000, 0.8f, 0, 4, 0, 1.6f, 6500, false};
    const auto lowRpm = engineWave(four);
    four.rpm = 6000; const auto highRpm = engineWave(four);
    check("four-cylinder firing note follows RPM (100 to 200 Hz)", tone(lowRpm, 100) > tone(lowRpm, 200) && tone(highRpm, 200) > tone(highRpm, 100) * 5);
    EngineInput eight{3000, 0.8f, 0, 8, 0, 5, 8500, false};
    const auto cross = engineWave(eight);
    eight.flatPlane = true; const auto flat = engineWave(eight);
    // This bank grouping's third cycle order is below its 200 Hz firing note.
    check("cross-plane V8 retains bank rumble distinct from flat-plane", tone(cross, 75) > tone(flat, 75) * 10);
    bool stable = true;
    for (int rate : {22050, 44100, 48000, 96000}) {
        EngineSynth engine; EngineInput input = eight; float previous = 0;
        for (int i = 0; i < rate * 2; i++) {
            input.rpm = i < rate / 2 ? 950 : i < rate ? 8200 : 0;
            input.load = i < rate ? 1 : 0;
            const float x = engine.sample(input, 1.0f / rate);
            stable &= std::isfinite(x) && std::fabs(x) < 1 && std::fabs(x - previous) < 0.6f;
            if (i > rate * 1.9) stable &= std::fabs(x) < 1e-6f;
            previous = x;
        }
    }
    check("RPM jumps and engine stalls stay smooth across device sample rates", stable);
    setup(sim, V3(-40, 0.05, -35));
    Audio exchange;
    std::atomic<bool> done{false};
    bool coherent = true;
    std::thread consumer([&] {
        while (!done.load(std::memory_order_acquire)) coherent &= AudioTest::coherent(exchange);
        coherent &= AudioTest::coherent(exchange);
    });
    for (uint32_t i = 1; i <= 10000; i++) {
        sim.cars[0].netId = i; sim.cars[0].rpm = i;
        exchange.setNearbyEngines(sim, listener, right, 1);
    }
    done.store(true, std::memory_order_release);
    consumer.join();
    check("audio callback receives coherent frames during concurrent publication", coherent);
    recordedEngineTests(check, AudioTest::render);
    soundscapeTests(check, AudioTest::render);
    check("tire hiss stays finite and audible from 8 to 192 kHz", AudioTest::hissRates());
    Audio allocationCheck;
    allocationCheck.setEngine(3000, 0.8f);
    allocationCheck.setEnvironment(4, 8, 0.6f);
    float callbackBlock[1024];
    traceAllocations = true;
    for (int k = 0; k < 500; k++) {
        allocationCheck.setMusic(k < 50 ? 1 : 4, 0.5f);
        AudioTest::render(allocationCheck, callbackBlock, 512);
    }
    traceAllocations = false;
    check("mixer and music map changes perform no C++ allocations in the callback", callbackAllocations == 0);
    {  // the vehicle volume: scales the car's sounds only
        auto level = [&](float vehicle, bool car, bool hiss = false) {
            Audio a;
            a.loadAmbience("assets/audio/ambience");
            a.setVolume(1); a.setVehicleVolume(vehicle);
            a.setEnvironment(0, 12, car ? 0.0f : 0.8f);
            a.setEngine(car ? 2500.0f : 0.0f, car ? 0.3f : 0.0f);
            if (car) a.setSpeed(20), a.setTires(0.6f);
            if (hiss) a.setHiss(1);
            std::vector<float> buf(512 * 2);
            double e = 0;
            for (int k = 0; k < 300; k++) {
                AudioTest::render(a, buf.data(), 512);
                if (k >= 100) for (float x : buf) e += x * x;
            }
            return std::sqrt(e / (200.0 * 1024));
        };
        const double full = level(1, true), half = level(0.5f, true), off = level(0, true);
        const double ambFull = level(1, false), ambOff = level(0, false), hissOff = level(0, false, true);
        std::printf("  vehicle volume rms: full %.4f half %.4f off %.6f; ambience %.4f/%.4f; hiss at 0 %.4f\n", full, half, off, ambFull, ambOff, hissOff);
        check("vehicle volume 0 silences the engine and tires", full > 0.01 && off < full * 1e-4);
        check("vehicle volume 50% is about half the amplitude", half > full * 0.4 && half < full * 0.6);
        check("ambience and tire air still play with the vehicle volume at 0", ambFull > 1e-4 && std::fabs(ambOff - ambFull) < ambFull * 1e-3 && hissOff > 0.001);
    }
    {  // recorded effects: the files load; the winch's sound follows line speed and load; nothing allocates in the callback
        Audio fx;
        const int loaded = fx.loadSfx("assets/audio/sfx");
        check("all thirteen effect recordings load (mono 48 kHz PCM16)", loaded == 13);
        const WinchSound slow = WinchSound::at(0.1f, 0), fast = WinchSound::at(0.8f, 0), loadedUp = WinchSound::at(0.3f, 0.9f), out = WinchSound::at(-0.8f, 0), stalled = WinchSound::at(0, 1);
        check("winch hum rises with line speed and sinks and strains under load", fast.motorRate > slow.motorRate * 1.3f && loadedUp.strain > 0.5f && fast.strain == 0 && loadedUp.motorGain > WinchSound::at(0.3f, 0).motorGain * 1.5f && loadedUp.motorRate < WinchSound::at(0.3f, 0).motorRate);
        check("pawl clicks follow the cable speed, paying out and reeling in", fast.clicksPerSecond > slow.clicksPerSecond * 4 && std::fabs(out.clicksPerSecond - fast.clicksPerSecond) < 1e-4f && stalled.clicksPerSecond == 0 && WinchSound::at(50, 0).clicksPerSecond <= 28);
        auto winchEnergy = [&](float speed, float load, bool running) {
            Audio a;
            a.loadSfx("assets/audio/sfx");
            a.setEngine(0, 0);
            a.setVolume(1);
            a.setWinch(0, running, speed, load, 1, 0.3f);
            std::vector<float> buf(512 * 2);
            double e = 0;
            for (int k = 0; k < 200; k++) {
                AudioTest::render(a, buf.data(), 512);
                if (k >= 100) for (float v : buf) e += v * v, e += std::isfinite(v) ? 0 : 1e9;
            }
            return e;
        };
        const double idle = winchEnergy(0.5f, 0.2f, false), run = winchEnergy(0.5f, 0.2f, true), heavy = winchEnergy(0.5f, 0.9f, true);
        std::printf("  winch energy idle %.3g running %.3g heavy %.3g\n", idle, run, heavy);
        check("a running winch is audible, silent when stopped, louder under load", idle < 1e-9 && run > 1e-3 && heavy > run);
        fx.setWinch(0, true, 0.6f, 0.5f, 1, 0); fx.setAiring(-1);
        traceAllocations = true;
        callbackAllocations = 0;
        for (int k = 0; k < 300; k++) {
            if (k == 100) fx.play(SFX_DOOR_OPEN), fx.play(SFX_DOOR_SHUT, 1, 0, 0.1f);
            if (k == 150) fx.setAiring(1), fx.setWinch(1, true, -0.5f, 0.8f, 0.5f, -0.5f);
            AudioTest::render(fx, callbackBlock, 512);
        }
        traceAllocations = false;
        check("winch, one-shots and air loops perform no C++ allocations in the callback", callbackAllocations == 0);
    }
    {  // the horn: sounds while held, quiet when let go, pitched by vehicle, placed in the world, no allocations
        auto hornEnergy = [&](bool on, float pitch, float pan, bool* finite = nullptr, double* afterRelease = nullptr) {
            Audio a;
            a.loadSfx("assets/audio/sfx");
            a.setEngine(0, 0);
            a.setVolume(1);
            a.setHorn(0, on, 1, pan, pitch);
            std::vector<float> buf(512 * 2);
            double e = 0;
            bool fin = true;
            for (int k = 0; k < 200; k++) {  // (~2 s: far longer than the 0.56 s recording, so only a loop keeps it going)
                AudioTest::render(a, buf.data(), 512);
                if (k >= 150) for (float v : buf) e += v * v, fin &= std::isfinite(v);
            }
            if (finite) *finite = fin;
            if (afterRelease) {
                a.setHorn(0, false, 1, pan, pitch);
                for (int k = 0; k < 20; k++) AudioTest::render(a, buf.data(), 512);
                double q = 0;
                for (int k = 0; k < 10; k++) { AudioTest::render(a, buf.data(), 512); for (float v : buf) q += v * v; }
                *afterRelease = q;
            }
            return e;
        };
        bool finite = true;
        double quiet = 0;
        const double held = hornEnergy(true, 1, 0, &finite, &quiet), off = hornEnergy(false, 1, 0);
        std::printf("  horn energy held %.3g off %.3g after release %.3g\n", held, off, quiet);
        check("a held horn sounds, still, two seconds in (it loops past the recording's end)", held > 1.0 && finite);
        check("a horn that is off is silent, and goes quiet after being let go", off < 1e-9 && quiet < held * 1e-4);
        Audio p;
        p.loadSfx("assets/audio/sfx");
        p.setEngine(0, 0);
        p.setVolume(1);
        const V3 lis(0, 0, 0), rt(0, 0, 1);
        p.setHornAt(0, true, V3(2, 0, 3), lis, rt, 1);
        std::vector<float> hb(512 * 2);
        double hl = 0, hr = 0, near = 0, far = 0;
        for (int k = 0; k < 40; k++) { AudioTest::render(p, hb.data(), 512); if (k > 20) for (int i = 0; i < 512; i++) hl += hb[2 * i] * hb[2 * i], hr += hb[2 * i + 1] * hb[2 * i + 1]; }
        check("a horn to the listener's right is louder in the right ear", hr > hl * 1.5);
        auto at = [&](double dist) { Audio q; q.loadSfx("assets/audio/sfx"); q.setEngine(0, 0); q.setVolume(1); q.setHornAt(0, true, V3(dist, 0, 0), lis, rt, 1); double e = 0; for (int k = 0; k < 40; k++) { AudioTest::render(q, hb.data(), 512); if (k > 20) for (float v : hb) e += v * v; } return e; };
        near = at(5), far = at(80);
        check("a horn 80 m away is much fainter than at 5 m, and not gone", far < near * 0.3 && far > 0);
        check("small cars honk higher: a light car's horn is a higher pitch, heavy lower", horn::pitchFor(700) > 1.1f && horn::pitchFor(3000) < 0.95f && horn::pitchFor(1600) > 0.99f && horn::pitchFor(1600) < 1.01f);
        p.setHorn(1, true, 1, 0, 1.2f);
        traceAllocations = true;
        callbackAllocations = 0;
        for (int k = 0; k < 200; k++) {
            if (k == 100) p.setHorn(0, false, 1, 0, 1), p.setHorn(2, true, 0.5f, -0.5f, 0.9f);
            AudioTest::render(p, hb.data(), 512);
        }
        traceAllocations = false;
        check("horns perform no C++ allocations in the callback", callbackAllocations == 0);
    }
    {  // repair and tool sounds: every action makes a voice, at the work, in 3D
        auto energy = [&](const std::function<void(Audio&)>& drive, bool* finite = nullptr, float* leftShare = nullptr) {
            Audio a;
            a.loadSfx("assets/audio/sfx");
            a.setEngine(0, 0);
            a.setVolume(1);
            std::vector<float> buf(512 * 2);
            double e = 0, el = 0, er = 0;
            bool ok = true;
            drive(a);
            for (int k = 0; k < 150; k++) {
                AudioTest::render(a, buf.data(), 512);
                for (int i = 0; i < 512; i++) e += buf[2 * i] * buf[2 * i] + buf[2 * i + 1] * buf[2 * i + 1], el += buf[2 * i] * buf[2 * i], er += buf[2 * i + 1] * buf[2 * i + 1];
                for (float v : buf) ok &= std::isfinite(v);
            }
            if (finite) *finite = ok;
            if (leftShare) *leftShare = (float)(el / std::max(1e-12, el + er));
            return e;
        };
        const V3 eye(0, 1.7, 0), right(1, 0, 0), bench(0.4, 1.0, 1.0);
        const Sfx synth[] = {SFX_HAMMER, SFX_JACK_PUMP, SFX_JACK_LOWER, SFX_PRY_CREAK, SFX_TIRE_IRON, SFX_WHEEL_OFF, SFX_WHEEL_ON, SFX_BOLT_DROP, SFX_NUT_SQUEAL};
        bool allAudible = true, allFinite = true;
        for (Sfx w : synth) {
            bool fin = true;
            const double e = energy([&](Audio& a) { a.play(w, 1); }, &fin);
            allAudible &= e > 1e-2, allFinite &= fin;
        }
        check("each synthesized tool sound (hammer, jack pump/lower, pry bar, tire iron, wheel off/on, bolt drop, nut squeal) is audible and finite", allAudible && allFinite);
        const double base = energy([&](Audio& a) { a.play(SFX_HAMMER, 1); }), nearE = energy([&](Audio& a) { a.playAt(SFX_HAMMER, V3(0.5, 1.2, 0.8), eye, right); }), farE = energy([&](Audio& a) { a.playAt(SFX_HAMMER, V3(25, 1.2, 0), eye, right); });
        float leftSide = 0, rightSide = 0;
        energy([&](Audio& a) { a.playAt(SFX_HAMMER, V3(-1.5, 1.2, 0.5), eye, right); }, nullptr, &leftSide);
        energy([&](Audio& a) { a.playAt(SFX_HAMMER, V3(1.5, 1.2, 0.5), eye, right); }, nullptr, &rightSide);
        check("tool sounds are placed: louder near the hand than 25 m off, panned to the side the work is on", nearE > farE * 10 && leftSide > 0.7f && rightSide < 0.3f && base > 0);
        const double slow = energy([&](Audio& a) { a.play(SFX_CLUNK, 1, 0, 0, 0.6f); }), norm1 = energy([&](Audio& a) { a.play(SFX_CLUNK, 1, 0, 0, 1.0f); });
        check("pitch changes the playback rate (a lower clunk lasts longer: more of the clip's energy)", slow > norm1 * 0.5 && energy([&](Audio& a) { a.play(SFX_CLUNK, 1, 0, 0, 1.4f); }) > 1e-3);

        // Each job through ToolSounds: step the held-E time and see that a voice is made.
        auto job = [&](InteractKind kind, int tool, int part, double hold, double seconds, bool nutOff, bool swings) {
            return energy([&](Audio& a) {
                ToolSounds ts;
                Interact ia;
                ia.current.kind = kind, ia.current.tool = tool, ia.current.part = part, ia.current.hold = hold, ia.current.at = bench, ia.current.car = 0, ia.current.wheel = 0;
                ia.current.continuous = kind == IA_RAISE_JACK || kind == IA_LOWER_JACK;
                for (double t = 0.016; t < seconds; t += 0.016) {
                    ia.progress = t;
                    Handwork hw;
                    hw.hasWork = true, hw.workAt = bench;
                    if (swings) {
                        hw.swing = std::fmod(t, 0.52) / 0.52, hw.nut = (int)(t / 1.2), hw.nutOff = nutOff;
                        hw.hands[1].on = true, hw.hands[1].p = bench;
                    }
                    ts.update(a, ia, hw, eye, right);
                    // (the sounds are made on the game thread; here they queue and play out when rendered)
                }
            });
        };
        const double eHammer = job(IA_FIX, TOOL_HAMMER, -1, 3, 1.5, false, false), ePry = job(IA_FIX, TOOL_PRY_BAR, -1, 4, 0.5, false, false), eIron = job(IA_TIRE_WORK, TOOL_TIRE_IRONS, -1, 4, 0.5, false, false);
        const double eRaise = job(IA_RAISE_JACK, -1, -1, 0, 1.2, false, false), eLower = job(IA_LOWER_JACK, -1, -1, 0, 1.0, false, false);
        const double eOff = job(IA_REMOVE_WHEEL, TOOL_LUG_WRENCH, -1, 3, 2.9, true, false), eOn = job(IA_FIT_WHEEL, TOOL_LUG_WRENCH, -1, 3, 0.9, false, false);
        const double eRatchet = job(IA_FIX, TOOL_RATCHET, -1, 5, 2.6, true, true), eRatchetOn = job(IA_FIX, TOOL_RATCHET, -1, 5, 1.0, false, true), eCache = job(IA_OPEN_CACHE, -1, -1, 3, 0.3, false, false);
        std::printf("  tool job energy: hammer %.3g pry %.3g irons %.3g raise %.3g lower %.3g wheel off %.3g on %.3g ratchet off %.3g on %.3g cache %.3g\n", eHammer, ePry, eIron, eRaise, eLower, eOff, eOn, eRatchet, eRatchetOn, eCache);
        check("a hammer job rings out hammer blows", eHammer > 1e-2);
        check("a pry bar job creaks and a tire iron job clinks", ePry > 1e-2 && eIron > 1e-2 && eCache > 1e-2);
        check("raising the jack pumps and lowering it lets the saddle down", eRaise > 1e-2 && eLower > 1e-2);
        check("a wheel coming off thumps on the ground (louder than a job with no sounds) and one going on clinks onto its studs", eOff > 1e-2 && eOn > 1e-2);
        check("a wrench turning a nut off ticks, squeals and drops it (more than doing a nut up, which only ticks)", eRatchet > eRatchetOn && eRatchetOn > 1e-3);
        const double idleJob = job(IA_NONE, -1, -1, 0, 1.0, false, false), partJob = job(IA_FIX, TOOL_HAMMER, 3, 3, 1.5, false, false);
        check("no job, no sound; a new part going in (no tool) isn't a hammer job", idleJob < 1e-9 && partJob < 1e-9);
    }
    {  // placed sounds: repair shops and water found on the maps that have them; gains fall with distance; the callback allocates nothing
        const int maps[] = {MAP_PROVING_GROUND, MAP_MOUNTAIN, MAP_FRONTIER, MAP_BACKCOUNTRY};
        bool shopsFound = true, falls = true, waterSeen = false, lakeSeen = false, riverSeen = false, quietLake = true, waterVoice = true;
        float lakeLoud = 0, riverLoud = 0;
        for (int m : maps) {
            World w(m);
            WorldSounds ws;
            ws.prepare(w);
            int rivers = 0, streams = 0;
            for (const auto& b : ws.bodies()) (b.loop == WL_RIVER ? rivers : streams)++, riverLoud = std::max(riverLoud, b.loop == WL_RIVER ? b.loud : 0.0f);
            std::printf("  map %d: %d shops, %d bodies (%d river, %d stream/still)\n", m, (int)ws.shops().size(), (int)ws.bodies().size(), rivers, streams);
            shopsFound &= ws.shops().size() >= w.stations.size() && !ws.shops().empty();
            const V3 east(1, 0, 0);
            WorldSounds::Voice v[WorldSounds::kVoices - 1], distant[WorldSounds::kVoices - 1];
            if (!ws.shops().empty()) {
                const V3 s = ws.shops()[0];
                ws.update(V3(s.x + 5, s.y, s.z), east, v);
                ws.update(V3(s.x + 25, s.y, s.z), east, distant);
                falls &= v[0].loop == WL_SHOP && v[0].gain > distant[0].gain && distant[0].gain > 0;
                ws.update(V3(s.x + 90, s.y, s.z), east, distant);  // (nothing else of a shop that distant, unless another is closeBy)
                falls &= distant[0].loop != WL_SHOP || distant[0].dist < 55;
            }
            for (const auto& b : ws.bodies()) {
                waterSeen = true;
                riverSeen |= b.loop == WL_RIVER;
                lakeSeen |= b.disc;
                if (b.disc) lakeLoud = std::max(lakeLoud, b.loud);
                const V3 p = b.disc ? V3(b.c.x + b.r, b.c.y, b.c.z) : b.pts[b.pts.size() / 2];
                WorldSounds::Voice closeBy[WorldSounds::kVoices - 1], farther[WorldSounds::kVoices - 1];
                ws.update(V3(p.x + 4, p.y, p.z), east, closeBy);
                ws.update(V3(p.x + 4 + b.range * 0.6, p.y, p.z), east, farther);
                float gn = 0, gf = 0;
                for (int i = WorldSounds::kShopVoices; i < WorldSounds::kVoices - 1; i++) gn = std::max(gn, closeBy[i].gain), gf = std::max(gf, farther[i].gain);
                waterVoice &= gn > 0;
                if (ws.bodies().size() == 1) falls &= gn > gf;  // (with several, a farther spot may be by another one)
            }
        }
        check("every map's repair stations become shop sources", shopsFound);
        check("water bodies are found on the maps with a lake or a river", waterSeen && lakeSeen && riverSeen);
        check("a still lake is much quieter than a river", lakeLoud > 0 && lakeLoud < riverLoud * 0.4f);
        check("shop and water gains fall with distance and every body is audible up close", falls && waterVoice);
        check("the forest bed is a daytime pine-woods sound", WorldSounds::forest(2, 12).gain > 0.3f && WorldSounds::forest(2, 2).gain == 0 && WorldSounds::forest(0, 12).gain == 0);
        Audio placed;
        placed.loadAmbience("assets/audio/ambience");
        placed.setEngine(0, 0); placed.setVolume(1); placed.setEnvironment(4, 12, 0.8f);
        auto placedEnergy = [&](int loop, float gain, float pan) {
            Audio a;
            a.loadAmbience("assets/audio/ambience");
            a.setEngine(0, 0); a.setVolume(1); a.setEnvironment(0, 12, 0.8f);
            if (loop >= 0) a.setWorldVoice(0, loop, gain, pan);
            std::vector<float> buf(512 * 2);
            double l = 0, r = 0;
            for (int k = 0; k < 300; k++) {
                AudioTest::render(a, buf.data(), 512);
                if (k >= 100) for (int i = 0; i < 512; i++) l += buf[2 * i] * buf[2 * i], r += buf[2 * i + 1] * buf[2 * i + 1];
            }
            return std::pair<double, double>(l, r);
        };
        const auto none = placedEnergy(-1, 0, 0), shopClose = placedEnergy(WL_SHOP, 0.9f, 0), shopDistant = placedEnergy(WL_SHOP, 0.1f, 0), riverLeft = placedEnergy(WL_RIVER, 0.8f, -1);
        std::printf("  energy: none %.3g shop closeBy %.3g distant %.3g river left %.3g/%.3g\n", none.first, shopClose.first, shopDistant.first, riverLeft.first, riverLeft.second);
        check("a placed shop is audible, quieter far away, and a hard-left river sounds on the left", shopClose.first > none.first * 1.5 && shopClose.first > shopDistant.first * 5 && riverLeft.first > riverLeft.second * 20);
        const auto fallsClose = placedEnergy(WL_FALLS, 0.9f, 0), fallsFar = placedEnergy(WL_FALLS, 0.05f, 0);
        std::printf("  energy: falls closeBy %.3g far %.3g\n", fallsClose.first, fallsFar.first);
        check("a waterfall roars (the river slowed, dulled, and brown noise), much quieter far off", fallsClose.first > none.first * 3 && fallsClose.first > fallsFar.first * 20);
        placed.setWorldVoice(0, WL_SHOP, 0.9f, 0.3f); placed.setWorldVoice(1, WL_RIVER, 0.5f, -0.5f); placed.setWorldVoice(4, WL_FALLS, 0.6f, 0);
        placed.setWorldVoice(5, WL_FOREST, 0.5f, 0);
        traceAllocations = true;
        callbackAllocations = 0;
        for (int k = 0; k < 300; k++) {
            if (k == 100) placed.setWorldVoice(0, WL_STREAM, 0.4f, 0.8f), placed.setWorldVoice(2, WL_SHOP, 0.2f, 0);
            AudioTest::render(placed, callbackBlock, 512);
        }
        traceAllocations = false;
        check("placed shop, water, falls and forest loops perform no C++ allocations in the callback", callbackAllocations == 0);
    }
    if (std::getenv("CZ_AUDIO_DEVICE_TEST")) {
        // With SDL_AUDIODRIVER=dummy this exercises SDL's real callback lifecycle silently.
        const bool initialized = SDL_Init(SDL_INIT_AUDIO) == 0;
        Audio device;
        device.setNearbyEngines(sim, listener, right, 1);
        const bool started = initialized && device.start();
        if (started) std::this_thread::sleep_for(std::chrono::milliseconds(40));
        device.stop();
        SDL_Quit();
        check("SDL callback starts, mixes nearby engines and shuts down", started);
    }
    {
        // Every engine in the garage sounds like itself: its spectrum at idle and at 3,000 rpm
        // under load (48 bands, 40 Hz to 8 kHz) differs from every other engine's, even ones
        // with the same cylinder count (exhaust, cam, intake, valvetrain, its own pulses).
        struct Engine { std::string name; EngineInput in; float idle; };
        std::vector<Engine> engines;
        for (int v = 0; v < (int)vehicleCatalog().size(); v++)
            for (int o = 0; o < (int)modOptions(v, MOD_ENGINE).size(); o++) {
                ModSet ms; ms.pick[MOD_ENGINE] = o;
                const VehicleSpec& s = tunedSpec(v, ms);
                bool seen = false;
                for (const Engine& e : engines) seen |= e.name == s.engineName;
                if (seen) continue;
                EngineInput in{0, 0, 0, s.cylinders, (int)s.induction, (float)s.displacement, (float)s.redline, s.flatPlane};
                in.voice = voiceFor(s);
                engines.push_back({s.engineName, in, (float)s.idleRpm});
            }
        const int kBands = 48;
        auto spectrum = [&](EngineInput in, float rpm, float load) {
            EngineSynth synth;
            in.rpm = rpm, in.load = load, in.boost = load;
            const int n = 48000, skip = 12000;
            std::vector<float> x(n);
            for (int i = 0; i < skip; i++) synth.sample(in, 1.0f / 48000);
            for (int i = 0; i < n; i++) x[i] = synth.sample(in, 1.0f / 48000);
            std::vector<double> band(kBands);
            double total = 1e-12;
            for (int b = 0; b < kBands; b++) {
                const double f = 40 * std::pow(200.0, (b + 0.5) / kBands), w = 2 * M_PI * f / 48000, c = 2 * std::cos(w);
                double s0 = 0, s1 = 0, s2 = 0;
                for (int i = 0; i < n; i++) s0 = x[i] + c * s1 - s2, s2 = s1, s1 = s0;
                band[b] = s1 * s1 + s2 * s2 - c * s1 * s2;
                total += band[b];
            }
            for (double& v : band) v = std::log10(v / total + 1e-9);
            return band;
        };
        std::vector<std::vector<double>> idle, load;
        for (const Engine& e : engines) idle.push_back(spectrum(e.in, e.idle, 0.05f)), load.push_back(spectrum(e.in, 3000, 0.8f));
        double closest = 1e9;
        std::string pair;
        for (size_t a = 0; a < engines.size(); a++)
            for (size_t b = a + 1; b < engines.size(); b++) {
                double d = 0;
                for (int k = 0; k < kBands; k++) d += std::fabs(idle[a][k] - idle[b][k]) + std::fabs(load[a][k] - load[b][k]);
                d /= 2 * kBands;
                if (d < closest) closest = d, pair = engines[a].name + " / " + engines[b].name;
            }
        std::printf("  %zu engines; the closest two: %s (%.3f)\n", engines.size(), pair.c_str(), closest);
        if (std::getenv("TONE_DEBUG")) {
            std::vector<std::pair<double, std::string>> all;
            std::vector<std::vector<double>> idle0, load0;
            for (const Engine& e : engines) {
                EngineInput plain = e.in;
                plain.voice = EngineTone{};
                idle0.push_back(spectrum(plain, e.idle, 0.05f)), load0.push_back(spectrum(plain, 3000, 0.8f));
            }
            double closest0 = 1e9;
            for (size_t a = 0; a < engines.size(); a++)
                for (size_t b = a + 1; b < engines.size(); b++) {
                    double d = 0, d0 = 0;
                    for (int k = 0; k < kBands; k++) d += std::fabs(idle[a][k] - idle[b][k]) + std::fabs(load[a][k] - load[b][k]),
                                                      d0 += std::fabs(idle0[a][k] - idle0[b][k]) + std::fabs(load0[a][k] - load0[b][k]);
                    all.push_back({d / (2 * kBands), engines[a].name + " / " + engines[b].name});
                    closest0 = std::min(closest0, d0 / (2 * kBands));
                }
            std::sort(all.begin(), all.end());
            for (int k = 0; k < 8; k++) std::printf("    %.3f %s\n", all[k].first, all[k].second.c_str());
            std::printf("  without the voices (cylinders, size and induction only): closest %.3f\n", closest0);
        }
        check("every engine in the garage has its own sound", engines.size() > 20 && closest > 0.09);
        // A big block roars and rumbles: louder than a little four, with far more of its sound
        // down low (under 200 Hz), idling and pulling.
        auto find = [&](const char* name) -> const Engine* {
            for (const Engine& e : engines)
                if (std::string(e.name).find(name) != std::string::npos) return &e;
            return nullptr;
        };
        auto character = [&](const Engine& e, float rpm, float load, double& rmsOut) {
            EngineSynth synth;
            EngineInput in = e.in;
            in.rpm = rpm, in.load = load, in.boost = load;
            const int n = 48000;
            std::vector<float> x(n);
            for (int i = 0; i < 12000; i++) synth.sample(in, 1.0f / 48000);
            double e2 = 0;
            for (int i = 0; i < n; i++) x[i] = synth.sample(in, 1.0f / 48000), e2 += x[i] * x[i];
            rmsOut = std::sqrt(e2 / n);
            // (Under 90 Hz, below any of these engines' firing notes at 3,000 rpm: the thump of
            // its uneven pulses, the deep end of its pipe.)
            const float a = 1 - std::exp(-2 * (float)M_PI * 90 / 48000);
            double low = 0, l1 = 0, l2 = 0;
            for (float v : x) l1 += (v - l1) * a, l2 += (l1 - l2) * a, low += l2 * l2;
            return low / std::max(1e-12, e2);
        };
        const Engine *bigBlock = find("454"), *four = find("SOHC D16A6");
        if (bigBlock && four) {
            double rBig, rFour, rBigIdle, rFourIdle;
            const double lowBig = character(*bigBlock, 3000, 0.8f, rBig), lowFour = character(*four, 3000, 0.8f, rFour);
            const double lowBigIdle = character(*bigBlock, bigBlock->idle, 0.05f, rBigIdle), lowFourIdle = character(*four, four->idle, 0.05f, rFourIdle);
            std::printf("  454 vs Civic D16: pulling rms %.3f vs %.3f, under 90 Hz %.0f%% vs %.0f%%; idling rms %.3f vs %.3f, under 90 Hz %.0f%% vs %.0f%%\n", rBig, rFour,
                        lowBig * 100, lowFour * 100, rBigIdle, rFourIdle, lowBigIdle * 100, lowFourIdle * 100);
            if (std::getenv("TONE_DEBUG"))
                for (const char* name : {"350 V8 2", "350 V8 4", "ZZ4", "LS7", "6.2L LS V8", "5.0L TWIN-TURBO V8", "900CC TURBO I3", "2GR-FKS"})
                    if (const Engine* e = find(name)) {
                        double r, ri;
                        const double lo = character(*e, 3000, 0.8f, r), loI = character(*e, e->idle, 0.05f, ri);
                        std::printf("    %-28s pulling rms %.3f low %.0f%%, idling rms %.3f low %.0f%%\n", e->name.c_str(), r, lo * 100, ri, loI * 100);
                    }
            check("the 454 big block roars and rumbles: louder and deeper than a little four", rBig > rFour * 1.3 && lowBig > lowFour * 1.5 && rBigIdle > rFourIdle * 1.3);
        } else {
            check("the 454 big block and the Civic's D16 are in the garage", false);
        }
    }
    return failures ? 1 : 0;
}

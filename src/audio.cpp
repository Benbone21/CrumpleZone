#include "audio.h"

#include <algorithm>
#include <cmath>
#include <cstring>
#include <functional>
#include <cstdio>

#include "sdl_min.h"
#include "softbody.h"

void Audio::setNearbyEngines(const Sim& sim, const V3& listener, const V3& right, float timeScale) {
    // The player's own engine should recede when walking away or flying the drone.
    if (sim.player >= 0 && sim.player < (int)sim.cars.size()) {
        const double distance = len(sim.cars[sim.player].frame().com - listener);
        localGain_ = (float)(1 / (1 + std::max(0.0, distance * distance - 64) / 144));
    }
    Sources& frame = sources_[backSources_];
    frame.count = 0;
    std::array<double, kNearbyEngines> distances{};
    for (size_t i = 0; i < sim.cars.size(); i++) {
        const Car& c = sim.cars[i];
        if ((int)i == sim.player || c.isProp()) continue;
        const V3 offset = c.frame().com - listener;
        const double distance = len(offset);
        if (distance > 160) continue;
        int slot = 0;
        while (slot < frame.count && distances[slot] <= distance) slot++;
        if (slot >= kNearbyEngines) continue;
        const int end = std::min(frame.count, kNearbyEngines - 1);
        for (int k = end; k > slot; k--) {
            frame.engines[k] = frame.engines[k - 1];
            distances[k] = distances[k - 1];
        }
        Source& out = frame.engines[slot];
        const VehicleSpec& v = *c.spec;
        out.id = c.netId ? c.netId : 0x80000000u | (uint32_t)(i + 1);
        out.engine = EngineInput{(float)(c.engineDead || sim.paused ? 0 : c.rpm) * timeScale,
            (float)clampd(c.throttle * c.tcCut, 0, 1), (float)c.boost,
            v.cylinders, (int)v.induction, (float)v.displacement, (float)v.redline, v.flatPlane,
            (uint8_t)engineSamples::forVehicle(v), voiceFor(v)};
        out.gain = (float)(0.55 / (1 + distance * distance / 144));
        out.pan = (float)clampd(distance > 1e-6 ? dot(offset / distance, right) : 0, -1, 1);
        distances[slot] = distance;
        frame.count = std::min(frame.count + 1, kNearbyEngines);
    }
    backSources_ = middleSources_.exchange(backSources_ | 4u, std::memory_order_acq_rel) & 3u;
}

void Audio::updateRemoteVoices() {
    if (middleSources_.load(std::memory_order_acquire) & 4u)
        frontSources_ = middleSources_.exchange(frontSources_, std::memory_order_acq_rel) & 3u;
    const Sources& frame = sources_[frontSources_];
    for (auto& voice : nearby_) voice.targetGain = 0;
    for (int i = 0; i < frame.count; i++) {
        const Source& source = frame.engines[i];
        NearbyVoice* voice = nullptr;
        for (auto& v : nearby_) if (v.id == source.id) { voice = &v; break; }
        if (!voice) {
            float quietest = 1e9f;
            for (auto& v : nearby_) {
                bool needed = false;
                for (int k = 0; k < frame.count; k++) needed |= frame.engines[k].id == v.id;
                if (!needed && v.gain < quietest) { quietest = v.gain; voice = &v; }
            }
            if (!voice) continue;
            *voice = NearbyVoice{};
            voice->id = source.id;
        }
        voice->input = source.engine;
        voice->targetGain = source.gain;
        voice->targetLeft = std::sqrt(0.5f * (1 - source.pan));
        voice->targetRight = std::sqrt(0.5f * (1 + source.pan));
    }
}

int Audio::loadEngineRecordings(const std::string& directory) {
    if (device_) return -1; // don't mutate buffers an active callback might be reading
    engine_ = EngineVoice{};
    for (auto& voice : nearby_) voice = NearbyVoice{};
    std::vector<std::string> errors;
    const int count = recordings_.load(directory, &errors);
    recordingsReady_ = true;
    recordingCount_ = count;
    for (const std::string& error : errors) std::fprintf(stderr, "Engine recording: %s\n", error.c_str());
    return count;
}

// A mono 16-bit PCM WAV into floats (-1..1); false if it isn't one.
static bool readMono16(const std::string& path, std::vector<float>& out) {
    std::FILE* f = std::fopen(path.c_str(), "rb");
    if (!f) return false;
    std::vector<unsigned char> b;
    unsigned char chunk[4096];
    for (size_t n; (n = std::fread(chunk, 1, sizeof chunk, f)) > 0;) b.insert(b.end(), chunk, chunk + n);
    std::fclose(f);
    if (b.size() < 44 || std::memcmp(b.data(), "RIFF", 4) || std::memcmp(b.data() + 8, "WAVE", 4)) return false;
    auto u32 = [&](size_t at) { return (uint32_t)b[at] | (uint32_t)b[at + 1] << 8 | (uint32_t)b[at + 2] << 16 | (uint32_t)b[at + 3] << 24; };
    bool ok = false;
    for (size_t at = 12; at + 8 <= b.size();) {
        const uint32_t size = u32(at + 4);
        if (!std::memcmp(&b[at], "fmt ", 4) && size >= 16 && at + 8 + 16 <= b.size()) ok = b[at + 8] == 1 && b[at + 10] == 1 && b[at + 22] == 16 && u32(at + 12) == 48000;  // PCM, mono, 16 bit, 48 kHz
        else if (!std::memcmp(&b[at], "data", 4)) {
            if (!ok) return false;
            const size_t n = std::min<size_t>(size, b.size() - at - 8) / 2;
            out.resize(n);
            for (size_t i = 0; i < n; i++) out[i] = (int16_t)(b[at + 8 + 2 * i] | b[at + 9 + 2 * i] << 8) / 32768.0f;
            return n > 0;
        }
        at += 8 + size + (size & 1);
    }
    return false;
}

int Audio::loadAmbience(const std::string& directory) {
    if (device_) return -1;
    ambienceLoaded_ = true;
    int n = 0;
    n += readMono16(directory + "/campfire.wav", fireLoop_);
    n += readMono16(directory + "/wind.wav", windLoop_);
    n += readMono16(directory + "/stream.wav", streamLoop_);
    n += readMono16(directory + "/shop.wav", shopLoop_);
    n += readMono16(directory + "/river.wav", riverLoop_);
    n += readMono16(directory + "/forest.wav", forestLoop_);
    return n;
}

namespace {
constexpr size_t kHornLoopStart = 3000, kHornLoopEnd = 13846;
constexpr size_t kClickSlot = 3360, kWrenchSlot = 7680;  // (the banks' slots, samples: tools/sfx_prepare.py)
}

int Audio::loadSfx(const std::string& directory) {
    if (device_) return -1;
    sfxLoaded_ = true;
    int n = 0;
    struct { const char* file; Sfx what; } one[] = {
        {"door_open.wav", SFX_DOOR_OPEN}, {"door_shut.wav", SFX_DOOR_SHUT}, {"clunk.wav", SFX_CLUNK}, {"toolbox_rummage.wav", SFX_TOOLBOX_OPEN},
        {"toolbox_shut.wav", SFX_TOOLBOX_SHUT}, {"wrench_ratchet.wav", SFX_WRENCH}, {"air_hiss.wav", SFX_AIR_BURST}, {"horn.wav", SFX_HORN},
        {"winch_click.wav", SFX_WINCH_CLICK}, {"ui_click.wav", SFX_UI_CLICK}};
    for (auto& e : one) n += readMono16(directory + "/" + e.file, sfx_[e.what]);
    if (!sfx_[SFX_UI_CLICK].empty()) {  // the hover tick: the same click, pitched up (resampled 1.15x)
        const std::vector<float>& c = sfx_[SFX_UI_CLICK];
        std::vector<float>& h = sfx_[SFX_UI_HOVER];
        h.clear();
        for (double at = 0; at + 1 < c.size(); at += 1.15) h.push_back((float)(c[(size_t)at] * (1 - (at - (size_t)at)) + c[(size_t)at + 1] * (at - (size_t)at)));
    }
    n += readMono16(directory + "/winch_motor.wav", winchMotor_);
    n += readMono16(directory + "/compressor.wav", compressor_);
    n += readMono16(directory + "/air_blow.wav", airBlow_);
    synthesizeTools();
    sfxCount_ = n;
    return n;
}

void Audio::setHorn(int slot, bool on, float gain, float pan, float pitch) {
    if (slot < 0 || slot >= kHorns) return;
    HornSlot& h = hornIn_[slot];
    h.gain = std::isfinite(gain) ? std::clamp(gain, 0.0f, 1.0f) : 0.0f;
    h.pan = std::isfinite(pan) ? std::clamp(pan, -1.0f, 1.0f) : 0.0f;
    h.pitch = std::isfinite(pitch) ? std::clamp(pitch, 0.5f, 2.0f) : 1.0f;
    h.on = on ? 1.0f : 0.0f;
}

void Audio::setHornAt(int slot, bool on, const V3& at, const V3& listener, const V3& right, float pitch) {
    const V3 d = at - listener;
    const double dist = std::sqrt(d.x * d.x + d.y * d.y + d.z * d.z);
    const double rl = std::sqrt(right.x * right.x + right.y * right.y + right.z * right.z);
    if (!std::isfinite(dist)) return setHorn(slot, false, 0, 0, 1);
    const float pan = dist > 0.3 && rl > 1e-6 ? (float)std::clamp((d.x * right.x + d.y * right.y + d.z * right.z) / (rl * dist), -1.0, 1.0) * 0.8f : 0.0f;
    const float att = 1.0f / (1.0f + (float)dist * 0.04f + (float)(dist * dist) * 0.0006f);  // (a horn carries: faint at 100 m)
    setHorn(slot, on, att, pan, pitch);
}

void Audio::playAt(Sfx what, const V3& at, const V3& listener, const V3& right, float gain, float delay, float pitch) {
    const V3 d = at - listener;
    const double dist = std::sqrt(d.x * d.x + d.y * d.y + d.z * d.z);
    if (!std::isfinite(dist)) return;
    const double rl = std::sqrt(right.x * right.x + right.y * right.y + right.z * right.z);
    const float pan = dist > 0.3 && rl > 1e-6 ? (float)std::clamp((d.x * right.x + d.y * right.y + d.z * right.z) / (rl * dist), -1.0, 1.0) * 0.8f : 0.0f;
    const float att = 1.0f / (1.0f + (float)dist * 0.22f + (float)(dist * dist) * 0.012f);  // (a hand's reach: nearly full; 30 m: faint)
    if (att * gain < 0.01f) return;
    play(what, gain * att, pan, delay, pitch);
}

// The repair and tool sounds, made from noise and ringing tones when the effects load: a hammer face on steel, the jack's pawl,
// a pry bar's creak, a tire iron's clink, a wheel thumping off or onto its studs, a nut bouncing, a seized nut's squeal.
void Audio::synthesizeTools() {
    uint32_t seed = 0x9e3779b9u;
    auto rnd = [&]() { seed ^= seed << 13, seed ^= seed >> 17, seed ^= seed << 5; return (seed >> 8) * (1.0f / 8388608.0f) - 1.0f; };
    const float sr = 48000.0f, tau = 6.2831853f;
    auto make = [&](Sfx what, float seconds, const std::function<float(float, int)>& f) {
        std::vector<float>& d = sfx_[what];
        d.assign((size_t)(seconds * sr), 0.0f);
        float peak = 1e-6f;
        for (size_t i = 0; i < d.size(); i++) d[i] = f(i / sr, (int)i), peak = std::max(peak, std::fabs(d[i]));
        const size_t fade = std::min<size_t>(d.size() / 8, 240);
        for (size_t i = 0; i < d.size(); i++) d[i] *= 0.9f / peak * (i + fade > d.size() ? (float)(d.size() - i) / fade : 1.0f);
    };
    auto ring = [&](float t, float hz, float decay) { return std::sin(tau * hz * t) * std::exp(-t * decay); };
    // A hammer's face on steel: a sharp tick, the head's ring, a dull thud under it.
    {
        float lp = 0;
        make(SFX_HAMMER, 0.35f, [&](float t, int) {
            lp += (rnd() - lp) * 0.35f;
            return 0.7f * lp * std::exp(-t * 700) + 0.5f * ring(t, 1180, 38) + 0.35f * ring(t, 2870, 55) + 0.2f * ring(t, 4410, 90) + 0.7f * ring(t, 130, 45);
        });
    }
    // One stroke of the jack's handle: the pawl clicking over two teeth and the lever's creak between them.
    {
        float lp = 0;
        make(SFX_JACK_PUMP, 0.30f, [&](float t, int) {
            lp += (rnd() - lp) * 0.5f;
            const float c2 = t - 0.075f, creak = t > 0.1f ? std::sin(tau * (210 + 60 * t) * t) * std::sin(3.14159f * (t - 0.1f) / 0.2f) * (0.5f + 0.5f * rnd()) : 0.0f;
            return 0.8f * lp * std::exp(-t * 900) * 0.8f + 0.5f * ring(t, 2300, 160) + (c2 > 0 ? (0.6f * lp * std::exp(-c2 * 900) + 0.4f * ring(c2, 3100, 190)) : 0.0f) + (t > 0.1f && t < 0.3f ? 0.12f * creak : 0.0f);
        });
    }
    // Letting the jack down: a slow, rough valve hiss with a groan of the saddle.
    {
        float lp = 0, bp = 0, bp2 = 0;
        make(SFX_JACK_LOWER, 0.55f, [&](float t, int) {
            const float n = rnd();
            lp += (n - lp) * 0.2f, bp += (lp - bp) * 0.12f, bp2 += (n - bp2) * 0.6f;
            const float env = std::sin(3.14159f * std::min(1.0f, t / 0.55f)), moan = std::sin(tau * (95 - 25 * t) * t) * 0.5f;
            return env * (1.6f * (lp - bp) + 0.12f * (n - bp2) + 0.22f * moan * (0.7f + 0.3f * rnd()));
        });
    }
    // A pry bar bending a stuck part: a rising, wobbling groan of steel with grit in it.
    {
        float ph = 0, lp = 0;
        make(SFX_PRY_CREAK, 0.7f, [&](float t, int) {
            const float wob = 1 + 0.06f * std::sin(tau * 23 * t) + 0.03f * rnd();
            ph += tau * (520 + 380 * (t / 0.7f) * (t / 0.7f)) * wob / sr;
            lp += (rnd() - lp) * 0.25f;
            const float env = std::pow(std::sin(3.14159f * t / 0.7f), 1.5f), saw = (std::fmod(ph, tau) / tau) * 2 - 1;
            return env * (0.5f * saw + 0.3f * std::sin(ph) + 0.35f * lp) + 0.4f * lp * std::exp(-t * 400);
        });
    }
    // A tire iron slipping against a rim: a bright clink and a short scrape.
    {
        float lp = 0, hp = 0;
        make(SFX_TIRE_IRON, 0.4f, [&](float t, int) {
            const float n = rnd();
            lp += (n - lp) * 0.5f, hp += (lp - hp) * 0.05f;
            return 0.6f * ring(t, 1760, 30) + 0.5f * ring(t, 2640, 45) + 0.25f * ring(t, 4180, 80) + (t < 0.12f ? 1.2f * (lp - hp) * (1 - t / 0.12f) : 0.0f) + 0.5f * lp * std::exp(-t * 600);
        });
    }
    // A wheel coming off its studs: the rim scraping out, then a heavy rubber thump on the ground.
    {
        float lp = 0, lp2 = 0;
        make(SFX_WHEEL_OFF, 0.6f, [&](float t, int) {
            const float n = rnd();
            lp += (n - lp) * 0.15f, lp2 += (n - lp2) * 0.04f;
            const float tt = t - 0.16f;
            return (t < 0.16f ? 0.6f * lp * std::sin(3.14159f * t / 0.16f) + 0.25f * ring(t, 1500, 18) : 0.0f) + (tt > 0 ? 1.0f * ring(tt, 70, 14) + 0.9f * lp2 * std::exp(-tt * 28) + 0.4f * ring(tt, 150, 30) : 0.0f);
        });
    }
    // A wheel going onto its studs: the holes finding the studs (two clinks) and a solid bump home.
    {
        float lp = 0;
        make(SFX_WHEEL_ON, 0.5f, [&](float t, int) {
            lp += (rnd() - lp) * 0.3f;
            const float a = t - 0.05f, b = t - 0.17f, c = t - 0.26f;
            return (a > 0 ? 0.5f * ring(a, 2050, 60) + 0.3f * lp * std::exp(-a * 500) : 0.0f) + (b > 0 ? 0.45f * ring(b, 2400, 70) : 0.0f) + (c > 0 ? 0.9f * ring(c, 95, 22) + 0.6f * ring(c, 620, 40) + 0.5f * lp * std::exp(-c * 150) : 0.0f);
        });
    }
    // A nut falling and bouncing on the ground: pings that come quicker and quieter.
    {
        make(SFX_BOLT_DROP, 0.5f, [&](float t, int) {
            float v = 0, at = 0, gap = 0.11f, amp = 1;
            for (int k = 0; k < 6 && at < t; k++, at += gap, gap *= 0.62f, amp *= 0.55f) v += amp * (ring(t - at, 3300 + 160 * k, 140) + 0.4f * ring(t - at, 5200, 220));
            return v;
        });
    }
    // A seized nut letting go: a short, rising squeal with a stick-slip buzz.
    {
        float ph = 0;
        make(SFX_NUT_SQUEAL, 0.38f, [&](float t, int) {
            const float stick = 0.55f + 0.45f * std::sin(tau * 95 * t), x = t / 0.38f;
            ph += tau * (1250 + 900 * x + 120 * rnd()) / sr;
            return std::sin(ph) * stick * std::pow(std::sin(3.14159f * std::pow(x, 0.7f)), 1.2f) + 0.2f * std::sin(2 * ph) * stick;
        });
    }
}

WinchSound WinchSound::at(float lineSpeed, float load) {
    const float v = std::isfinite(lineSpeed) ? std::fabs(lineSpeed) : 0, ld = std::isfinite(load) ? std::clamp(load, 0.0f, 1.0f) : 0;
    const float vn = std::min(1.0f, v / 1.2f);  // (a reel-in at the default 4x is 0.5 m/s, paying out 1 m/s)
    const float k = std::clamp((ld - 0.45f) / 0.5f, 0.0f, 1.0f);
    WinchSound w;
    w.motorRate = (0.8f + 0.55f * vn) * (1 - 0.2f * ld);  // faster line, higher hum; a load pulls it down
    w.motorGain = 0.10f + 0.16f * vn + 0.30f * ld;
    w.strain = k * k * (3 - 2 * k);
    w.clicksPerSecond = ld > 0.97f && v < 0.03f ? 0.0f : std::min(28.0f, v * 22.0f);  // (~22 pawl teeth a metre of line; past 28/s they'd just be a buzz)
    return w;
}

void Audio::setWinch(int slot, bool running, float lineSpeed, float load, float gain, float pan) {
    if (slot < 0 || slot >= kWinches) return;
    WinchSlot& w = winchIn_[slot];
    w.speed = std::isfinite(lineSpeed) ? lineSpeed : 0, w.load = std::isfinite(load) ? load : 0, w.pan = std::isfinite(pan) ? pan : 0;
    w.gain = running && std::isfinite(gain) ? gain : 0;
    w.running = running ? 1.0f : 0.0f;
}

void Audio::setWinches(const Sim& sim, const V3& listener, const V3& right, double simDt) {
    struct Cand { double d; float speed, load, pan; bool running; };
    std::vector<Cand> cands;
    for (RopeSeen& r : ropesSeen_) r.live = false;
    for (const Rope& r : sim.ropes) {
        if (r.kind != ROPE_WINCH || r.a.kind != ANCHOR_CAR || r.a.car < 0 || r.a.car >= (int)sim.cars.size()) continue;
        const Car& c = sim.cars[r.a.car];
        const uint32_t key = (uint32_t)(r.a.car * 4 + r.a.hook + 1);
        RopeSeen* seen = nullptr;
        for (RopeSeen& x : ropesSeen_) if (x.key == key) seen = &x;
        if (!seen) { ropesSeen_.push_back({key, r.length, 0, true}); seen = &ropesSeen_.back(); }
        seen->live = true;
        if (simDt > 1e-6) {
            const double raw = (seen->length - r.length) / simDt, tau = c.remote ? 0.3 : 0.12;  // (+ reeling in; a remote's length arrives in steps)
            seen->speed += (raw - seen->speed) * std::min(1.0, simDt / tau);
        } else seen->speed = 0;
        seen->length = r.length;
        const V3 at = c.hookPoint(r.a.hook), off = at - listener;
        const double d = len(off);
        if (d > 90) continue;
        const bool running = c.remote ? std::fabs(seen->speed) > 0.03 : r.winch != 0 && simDt > 0;
        cands.push_back({d, (float)seen->speed, (float)(r.stallN > 1 ? r.tension / r.stallN : 0), (float)clampd(d > 1e-6 ? dot(off / d, right) : 0, -1, 1), running});
    }
    ropesSeen_.erase(std::remove_if(ropesSeen_.begin(), ropesSeen_.end(), [](const RopeSeen& x) { return !x.live; }), ropesSeen_.end());
    std::sort(cands.begin(), cands.end(), [](const Cand& a, const Cand& b) { return a.d < b.d; });
    for (int i = 0; i < kWinches; i++) {
        if (i < (int)cands.size()) setWinch(i, cands[i].running, cands[i].speed, cands[i].load, (float)(1 / (1 + cands[i].d * cands[i].d / 36)), cands[i].pan);
        else setWinch(i, false, 0, 0, 0, 0);
    }
}

void Audio::setWorldVoice(int slot, int loop, float gain, float pan) {
    if (slot < 0 || slot >= kWorldVoices) return;
    if (loop < 0 || loop > 3 || !std::isfinite(gain) || !std::isfinite(pan)) loop = -1, gain = 0, pan = 0;
    worldIn_[slot].gain = std::clamp(gain, 0.0f, 1.0f);
    worldIn_[slot].pan = std::clamp(pan, -1.0f, 1.0f);
    worldIn_[slot].loop = loop;
}

// WorldLoop (worldsound.h): 0 the repair shop, 1 the river, 2 the brook, 3 the forest.
const std::vector<float>* Audio::worldLoop(int loop) const {
    switch (loop) {
        case 0: return &shopLoop_;
        case 1: return &riverLoop_;
        case 2: return &streamLoop_;
        case 3: return &forestLoop_;
        default: return nullptr;
    }
}

void Audio::renderWorldVoices(float* out, int frames, float volume) {
    static constexpr float trim[4] = {1.3f, 1.6f, 0.4f, 1.2f};  // (the loops' own levels, evened up)
    const float dt = 1.0f / rate_, a = 1 - std::exp(-dt / 0.6f);  // (gains and pans glide over about half a second)
    for (int s = 0; s < kWorldVoices; s++) {
        WorldState& st = worldState_[s];
        const int want = worldIn_[s].loop;
        float target = worldIn_[s].gain * volume;
        const float tpan = worldIn_[s].pan;
        if (want != st.loop) {  // a different loop in this voice: out with the old one first
            target = 0;
            if (st.gain < 1e-4f) { st.loop = want; st.gain = 0; st.pos = (size_t)s * 48611; }
        }
        const std::vector<float>* data = worldLoop(st.loop);
        if (!data || data->empty()) { st.gain = 0; continue; }
        if (target == 0 && st.gain < 1e-5f) { st.gain = 0; continue; }
        const size_t size = data->size();
        if (st.pos >= size) st.pos %= size;
        const float cut = 0.12f + 0.88f * std::sqrt(std::clamp((float)worldIn_[s].gain, 0.0f, 1.0f));  // (farther is duller)
        for (int i = 0; i < frames; i++) {
            st.gain += (target - st.gain) * a;
            st.pan += (tpan - st.pan) * a;
            st.lp += ((*data)[st.pos] - st.lp) * cut;
            if (++st.pos >= size) st.pos = 0;
            const float v = st.lp * trim[st.loop] * st.gain;
            const float angle = (st.pan + 1) * 0.7853982f;  // constant power
            out[2 * i] += v * std::cos(angle);
            out[2 * i + 1] += v * std::sin(angle);
        }
    }
}

void Audio::play(Sfx what, float gain, float pan, float delay, float pitch) {
    if (what < 0 || what >= SFX_COUNT || sfx_[what].empty() || !std::isfinite(gain) || !std::isfinite(pan)) return;
    const unsigned head = pendingHead_.load(std::memory_order_relaxed);
    if (head - pendingTail_.load(std::memory_order_acquire) >= kPending) return;
    pending_[head % kPending] = {what, std::clamp(gain, 0.0f, 2.0f), std::clamp(pan, -1.0f, 1.0f), std::isfinite(pitch) ? std::clamp(pitch, 0.25f, 4.0f) : 1.0f, (uint32_t)(std::clamp(std::isfinite(delay) ? delay : 0.0f, 0.0f, 10.0f) * rate_)};
    pendingHead_.store(head + 1, std::memory_order_release);
}

bool Audio::setAiring(int direction) {
    direction = std::clamp(direction, -1, 1);
    const bool have = !compressor_.empty() && !airBlow_.empty();
    if (!have) return false;
    if (direction < 0 && airingDir_ != -1) play(SFX_AIR_BURST, 0.5f);  // (the first "psst" of the valve)
    airingDir_ = direction;
    return true;
}

void Audio::startSfx(Sfx what, float gain, float pan, uint32_t wait, float pitch) {
    SfxVoice* v = &sfxVoices_[0];
    for (SfxVoice& x : sfxVoices_) {
        if (x.what == SFX_COUNT) { v = &x; break; }
        if (x.pos > v->pos) v = &x;  // (none free: take the one furthest along)
    }
    const std::vector<float>& d = sfx_[what];
    size_t from = 0, to = d.size();
    if (what == SFX_WINCH_CLICK || what == SFX_WRENCH) {  // a bank: one slot, a touch louder or softer each time
        const size_t slot = what == SFX_WINCH_CLICK ? kClickSlot : kWrenchSlot, slots = std::max<size_t>(1, d.size() / slot);
        const size_t k = (size_t)((noise() * 0.5f + 0.5f) * slots) % slots;
        from = k * slot, to = std::min(d.size(), from + slot);
        gain *= 0.85f + 0.15f * noise();
    }
    v->what = what, v->pos = (double)from, v->end = to, v->wait = wait, v->rate = pitch;
    v->left = gain * std::sqrt(0.5f * (1 - pan)), v->right = gain * std::sqrt(0.5f * (1 + pan));
}

bool Audio::start() {
    if (device_) return true;
    if (!ambienceLoaded_) std::printf("Ambience recordings: %d of 6 loaded; missing ones stay synthesized\n", loadAmbience("assets/audio/ambience"));
    if (!sfxLoaded_) std::printf("Sound effects: %d of 13 loaded; missing ones stay silent\n", loadSfx("assets/audio/sfx"));
    const int banks = recordingsReady_ ? recordingCount_ : loadEngineRecordings("assets/audio/engines");
    std::printf("Engine recordings: %d banks loaded; missing banks use procedural audio\n", banks);
    SDL_AudioSpec want{}, have{};
    want.freq = 48000;
    want.format = AUDIO_F32SYS;
    want.channels = 2;
    want.samples = 512;
    want.callback = &Audio::callback;
    want.userdata = this;
    device_ = SDL_OpenAudioDevice(nullptr, 0, &want, &have, 0);
    if (!device_) return false;
    rate_ = have.freq;
    music_.setRate(rate_);  // before the callback starts: this allocates
    SDL_PauseAudioDevice(device_, 0);
    return true;
}

void Audio::stop() {
    if (device_) SDL_CloseAudioDevice(device_);
    device_ = 0;
}

void Audio::impact(float strength) {
    float cur = pendingImpact_.load();
    if (strength > cur) pendingImpact_ = strength;
}

void Audio::glass(float strength) {
    float cur = pendingGlass_.load();
    if (strength > cur) pendingGlass_ = strength;
}

void Audio::pop() { pendingPop_ = 1.0f; }
void Audio::setHiss(float level) { pendingHiss_ = std::isfinite(level) ? std::clamp(level, 0.0f, 1.0f) : 0; }

void Audio::callback(void* user, uint8_t* stream, int len) {
    static_cast<Audio*>(user)->render(reinterpret_cast<float*>(stream), len / (int)(2 * sizeof(float)));
}

// Recorded one-shots, the winch (motor hum + pawl clicks) and the tire air, added over the mix.
void Audio::renderEffects(float* out, int frames, float dt, float sceneGain) {
    for (unsigned tail = pendingTail_.load(std::memory_order_relaxed), head = pendingHead_.load(std::memory_order_acquire); tail != head; tail++) {
        const PendingSfx& e = pending_[tail % kPending];
        startSfx(e.what, e.gain, e.pan, e.delay, e.pitch);
        pendingTail_.store(tail + 1, std::memory_order_release);
    }
    const float smooth = 1.0f - std::exp(-dt * 12.0f);
    WinchSound ws[kWinches];
    float target[kWinches];
    const bool motor = winchMotor_.size() > 2;
    for (int k = 0; k < kWinches; k++) {
        const WinchSlot& in = winchIn_[k];
        WinchState& st = winchState_[k];
        st.speed = in.speed, st.load = in.load;
        ws[k] = WinchSound::at(in.speed, in.load);
        target[k] = in.running.load() > 0.5f ? in.gain.load() * ws[k].motorGain : 0.0f;
        st.pan = in.pan;
    }
    // The horn: horn.wav is a steady two-tone (392 and 628 Hz) for its first 0.3 s; 3000..13846 (10846 samples at 48 kHz) repeats it to 0.999 correlation.
    const bool hornOn = sfx_[SFX_HORN].size() > kHornLoopEnd + 2;
    struct HornTarget { float on, gain, pan, pitch; } hornT[kHorns];
    for (int k = 0; k < kHorns; k++) hornT[k] = {hornIn_[k].on.load(), hornIn_[k].gain.load(), hornIn_[k].pan.load(), hornIn_[k].pitch.load()};
    const float hornUp = 1.0f - std::exp(-dt * 200.0f), hornDown = 1.0f - std::exp(-dt * 40.0f);  // (about 5 ms up, 25 ms down)
    const int air = airingDir_;
    const float airTarget[2] = {air > 0 && !compressor_.empty() ? 0.28f : 0.0f, air < 0 && !airBlow_.empty() ? 0.30f : 0.0f};
    for (int i = 0; i < frames; i++) {
        float l = 0, r = 0, ul = 0, ur = 0;
        for (int k = 0; k < kWinches; k++) {
            WinchState& st = winchState_[k];
            st.gain += (target[k] - st.gain) * smooth;
            st.rate += (ws[k].motorRate - st.rate) * smooth;
            st.strain += (ws[k].strain - st.strain) * smooth;
            if (st.gain < 1e-4f && target[k] == 0) { st.clickAcc = 0; continue; }
            if (motor) {
                st.t += dt;
                st.pos += st.rate * (1 + 0.03f * st.strain * std::sin(6.2831853f * 7.0f * st.t));
                const double n = (double)winchMotor_.size();
                if (st.pos >= n) st.pos -= n;
                const size_t a = (size_t)st.pos, b = a + 1 >= winchMotor_.size() ? 0 : a + 1;
                const float f = (float)(st.pos - a), m = winchMotor_[a] * (1 - f) + winchMotor_[b] * f;
                const float drive = 1 + 4 * st.strain;
                const float s = std::tanh(m * drive) / std::sqrt(drive) * st.gain;  // (a load drives it harder: rougher, fuller)
                l += s * std::sqrt(0.5f * (1 - st.pan)), r += s * std::sqrt(0.5f * (1 + st.pan));
            }
            st.clickAcc += ws[k].clicksPerSecond * dt;
            if (st.clickAcc >= 1) {  // a pawl tooth: the click rate follows the line, either way
                st.clickAcc -= 1;
                if (st.clickAcc > 1) st.clickAcc = 0;
                if (!sfx_[SFX_WINCH_CLICK].empty() && target[k] > 0)
                    startSfx(SFX_WINCH_CLICK, std::min(1.0f, target[k] / std::max(0.05f, ws[k].motorGain)) * (st.speed < 0 ? 0.55f : 0.7f), st.pan, 0);
            }
        }
        if (hornOn) {  // the horn: the steady part of the recording, looped (whole periods, so no click), faded in and out
            for (int k = 0; k < kHorns; k++) {
                HornState& st = hornState_[k];
                const bool wanted = hornT[k].on > 0.5f;
                if (!wanted && st.env < 1e-4f) { st.pos = kHornLoopStart; continue; }
                st.env += ((wanted ? 1.0f : 0.0f) - st.env) * (wanted ? hornUp : hornDown);
                st.gain += (hornT[k].gain - st.gain) * smooth, st.pan += (hornT[k].pan - st.pan) * smooth, st.pitch += (hornT[k].pitch - st.pitch) * smooth;
                const std::vector<float>& d = sfx_[SFX_HORN];
                const size_t a = (size_t)st.pos;
                const float s = (d[a] + (d[a + 1] - d[a]) * (float)(st.pos - (double)a)) * st.env * st.gain * 0.8f;
                l += s * std::sqrt(0.5f * (1 - st.pan)), r += s * std::sqrt(0.5f * (1 + st.pan));
                if ((st.pos += st.pitch) >= (double)kHornLoopEnd) st.pos -= (double)(kHornLoopEnd - kHornLoopStart);
            }
        }
        for (int a = 0; a < 2; a++) {
            airGain_[a] += (airTarget[a] - airGain_[a]) * smooth;
            const std::vector<float>& d = a == 0 ? compressor_ : airBlow_;
            if (airGain_[a] < 1e-4f && airTarget[a] == 0) continue;
            double& pos = a == 0 ? compPos_ : blowPos_;
            if (++pos >= d.size()) pos = 0;
            const float s = d[(size_t)pos] * airGain_[a];
            l += s, r += s;
        }
        for (SfxVoice& v : sfxVoices_) {
            if (v.what == SFX_COUNT) continue;
            if (v.wait) { v.wait--; continue; }
            const std::vector<float>& d = sfx_[v.what];
            const size_t a = (size_t)v.pos, b = std::min(a + 1, v.end - 1);
            const float s = d[a] + (d[b] - d[a]) * (float)(v.pos - (double)a);
            if (v.what == SFX_UI_CLICK || v.what == SFX_UI_HOVER) ul += s * v.left, ur += s * v.right;  // (menu sounds aren't ducked with the scene)
            else l += s * v.left, r += s * v.right;
            if ((v.pos += v.rate) >= (double)v.end - 1) v.what = SFX_COUNT;
        }
        out[2 * i] += l * sceneGain + ul, out[2 * i + 1] += r * sceneGain + ur;
    }
}

void Audio::render(float* out, int frames) {
    const float dt = 1.0f / rate_;
    auto level = [](float value, float fallback = 0.0f) {
        return std::isfinite(value) ? std::clamp(value, 0.0f, 1.0f) : fallback;
    };
    const float tRpm = rpm_, tLoad = load_, tSlip = slip_, tSpeed = speed_, tBoost = boost_;
    const float master = level(master_), sceneGain = level(sceneGain_), localGain = level(localGain_);
    const float exposure = level(exposure_, 1);
    float imp = pendingImpact_.exchange(0);
    if (imp > 0) {
        crunchEnv_ = std::max(crunchEnv_, imp);
        thumpEnv_ = std::max(thumpEnv_, imp);
    }
    float gl = pendingGlass_.exchange(0);
    if (gl > 0) {
        glassEnv_ = std::max(glassEnv_, gl);
        for (int k = 0; k < 3; k++) {
            pingEnv_[k] = gl * (0.5f + 0.5f * (noise() * 0.5f + 0.5f));
            pingFreq_[k] = 2600.0f + 3600.0f * (noise() * 0.5f + 0.5f);
        }
    }
    if (pendingPop_.exchange(0) > 0) {
        popEnv_ = 1.0f;
        thumpEnv_ = std::max(thumpEnv_, 0.6f);
    }
    const float targetHiss = pendingHiss_.load();
    const float hissSmooth = 1.0f - std::exp(-dt * 100.0f);
    const float hissLowA = 1.0f - std::exp(-2 * (float)M_PI * std::min(1500.0f, 0.10f / dt) * dt);
    const float hissHighA = 1.0f - std::exp(-2 * (float)M_PI * std::min(3200.0f, 0.20f / dt) * dt);
    const EngineTone voice{vExhaust_, vMuffler_, vLope_, vIntake_, vMech_, vRasp_, vCrackle_, vSeed_, vRumble_, vRoar_};
    const EngineInput engineInput{tRpm, tLoad, tBoost, (int)cyl_, (int)induction_, (float)liters_, (float)redline_, (bool)flatPlane_,
                                 (uint8_t)std::clamp((int)recording_, 0, engineSamples::kBanks - 1), voice};
    const auto* localBank = recordings_.bank(engineInput.recording);
    updateRemoteVoices();
    const float smooth = 1.0f - std::exp(-dt * 30.0f);
    const float cabinA = 1 - std::exp(-2 * (float)M_PI * 1100 * dt);
    for (int i = 0; i < frames; i++) {
        sSlip_ += (tSlip - sSlip_) * smooth * 0.5f;
        sSpeed_ += (tSpeed - sSpeed_) * smooth;

        sLocalGain_ += (localGain - sLocalGain_) * smooth;
        const float engine = engine_.sample(engineInput, dt, localBank) * sLocalGain_;
        const float ph = engine_.phase();

        // Tire squeal: resonant band-pass noise, pitch wobbles a little.
        float f0 = 820.0f + 120.0f * std::sin(ph * 6.28f);
        float w = 2.0f * std::sin((float)M_PI * f0 * dt), damp = 0.12f;
        float high = noise() - bp2_ - damp * bp1_;  // Chamberlin state-variable filter
        bp1_ += w * high;                           // band
        bp2_ += w * bp1_;                           // low
        float squeal = bp1_ * 0.12f * std::min(1.0f, std::max(0.0f, (sSlip_ - 0.12f) * 2.5f)) *
                       std::min(1.0f, sSpeed_ / 3.0f);

        // Wind.
        float wa = 1.0f - std::exp(-2.0f * (float)M_PI * 500.0f * dt);
        windLp_ += (noise() - windLp_) * wa;
        float wind = windLp_ * std::min(1.0f, sSpeed_ * sSpeed_ / 2500.0f) * 0.5f;

        // Impact: low thump + crunchy filtered noise, both decaying.
        thumpPhase_ += 52.0f * dt;
        if (thumpPhase_ > 1) thumpPhase_ -= 1;
        float thump = std::sin(2.0f * (float)M_PI * thumpPhase_) * thumpEnv_;
        float ca = 1.0f - std::exp(-2.0f * (float)M_PI * 1800.0f * dt);
        crunchLp_ += (noise() * (noise() > 0.6f ? 2.5f : 0.6f) - crunchLp_) * ca;
        float crunch = crunchLp_ * crunchEnv_;
        crunchEnv_ *= std::exp(-dt / 0.22f);
        thumpEnv_ *= std::exp(-dt / 0.12f);

        // Glass: bright band-passed noise plus a few decaying pings.
        float gw = 2.0f * std::sin((float)M_PI * std::min(4800.0f, 0.15f / dt) * dt);
        float gh = noise() - glassLow_ - 0.5f * glassBand_;
        glassBand_ += gw * gh;
        glassLow_ += gw * glassBand_;
        float glassSnd = glassBand_ * 0.25f * glassEnv_;
        for (int k = 0; k < 3; k++) {
            pingPhase_[k] += pingFreq_[k] * dt;
            if (pingPhase_[k] > 1) pingPhase_[k] -= 1;
            glassSnd += std::sin(2.0f * (float)M_PI * pingPhase_[k]) * pingEnv_[k] * 0.12f;
            pingEnv_[k] *= std::exp(-dt / 0.18f);
        }
        glassEnv_ *= std::exp(-dt / 0.12f);

        // Tire blowout: sharp low-passed bang.
        float pa = 1.0f - std::exp(-2.0f * (float)M_PI * 1400.0f * dt);
        popLp_ += (noise() - popLp_) * pa;
        float popSnd = popLp_ * popEnv_ * 2.5f;
        popEnv_ *= std::exp(-dt / 0.05f);

        // Two stable one-pole filters, safe at every supported sample rate.
        // Advance the envelope per sample, independently of callback block size.
        hissEnv_ += (targetHiss - hissEnv_) * hissSmooth;
        float hissSnd = 0;
        if (hissEnv_ > 0.000001f) {
            const float n = noise();
            hissBp_ += (n - hissBp_) * hissHighA;
            hissBp2_ += (n - hissBp2_) * hissLowA;
            hissSnd = (hissBp_ - hissBp2_) * hissEnv_ * 1.2f;
        }

        float mix = engine + (squeal + wind) * sLocalGain_ + crunch * 0.9f + thump * 0.8f + glassSnd + popSnd + hissSnd;
        float nearbyLeft = 0, nearbyRight = 0;
        for (auto& voice : nearby_) {
            if (!voice.id) continue;
            voice.gain += (voice.targetGain - voice.gain) * smooth;
            voice.left += (voice.targetLeft - voice.left) * smooth;
            voice.right += (voice.targetRight - voice.right) * smooth;
            if (voice.gain < 1e-6f && voice.targetGain == 0) { voice.id = 0; continue; }
            const float sound = voice.synth.sample(voice.input, dt, recordings_.bank(voice.input.recording)) * voice.gain;
            nearbyLeft += sound * voice.left;
            nearbyRight += sound * voice.right;
        }
        const float left = std::tanh((mix + nearbyLeft) * 1.2f);
        const float right = std::tanh((mix + nearbyRight) * 1.2f);
        cabinLowL_ += (left - cabinLowL_) * cabinA;
        cabinLowR_ += (right - cabinLowR_) * cabinA;
        sExposure_ += (exposure - sExposure_) * smooth;
        const float cabin = (1 - sExposure_) * 0.65f;
        out[2 * i] = (left + (cabinLowL_ - left) * cabin) * sceneGain;
        out[2 * i + 1] = (right + (cabinLowR_ - right) * cabin) * sceneGain;
    }
    renderEffects(out, frames, dt, sceneGain);
    // Music goes on top, quieter than the car (its own level is about -18 dBFS before the
    // volume setting). A soft knee catches the rare moment both peak together.
    // Leave space for engine detail during hard acceleration and impacts.
    const float duck = 1 - 0.25f * level(tLoad) - 0.25f * std::min(1.0f, thumpEnv_);
    const float mv = level(musicVol_) * duck;
    music_.render(out, frames, musicMap_, mv, musicBiome_);
    ambience_.render(out, frames, rate_, {(int)ambienceMap_, (float)hours_, level(ambienceVol_) * sceneGain, exposure});
    if (ambienceMap_ >= 0) renderWorldVoices(out, frames, level(ambienceVol_) * sceneGain * sExposure_);
    else for (WorldState& st : worldState_) st.gain = 0;
    if (const float camp = campfire_; camp > 0 || cfGain_ > 1e-4f) {  // the main menu's camp: the fire (the focus), a stream, wind in the trees, crickets. All synthesized
        const float target = camp * sceneGain, amb = level(ambienceVol_) * 0.6f + 0.25f;  // (the ambience setting scales the beds, never to silence)
        const float panF = campPanFire_, panS = campPanStream_;
        const float lF = std::sqrt(0.5f * (1 - panF)), rF = std::sqrt(0.5f * (1 + panF)), lS = std::sqrt(0.5f * (1 - panS)), rS = std::sqrt(0.5f * (1 + panS));
        for (int i = 0; i < frames; i++) {
            cfGain_ += (target - cfGain_) * 0.00004f;  // (about a second and a half to come in or go out)
            const float g = cfGain_;
            const float n = noise();
            cfT_ += 1.0f / rate_;
            // Fire: the recording (a low flutter of flame under sharp little pops), or, with no file, a synthesized one.
            cfRoar_ += (n - cfRoar_) * 0.03f;
            cfLow_ += (n - cfLow_) * 0.004f;
            if (std::fabs(noise()) > 0.9996f) cfPop_ = 0.35f + 0.65f * std::fabs(noise());
            if (std::fabs(noise()) > 0.99985f) cfPop_ = 1.0f;
            cfHp_ = n - cfHp_ * 0.4f;
            float fire = (cfRoar_ * 0.5f * (0.55f + 0.45f * cfLow_ * 12.0f) + cfHp_ * cfPop_ * 0.28f) * 0.9f;
            cfPop_ *= 0.9985f;
            if (!fireLoop_.empty()) {
                fire = fireLoop_[firePos_] * 4.0f;
                if (++firePos_ >= fireLoop_.size()) firePos_ = 0;
            }
            // Stream: bright, burbling water (band-passed noise swelling a little), off to one side.
            const float n2 = noise();
            stLp1_ += (n2 - stLp1_) * 0.22f;
            stLp2_ += (n2 - stLp2_) * 0.03f;
            const float burble = 0.65f + 0.35f * std::sin(cfT_ * 9.0f + 2.0f * std::sin(cfT_ * 1.7f));
            float stream = (stLp1_ - stLp2_) * burble * 0.55f;
            if (!streamLoop_.empty()) {  // (the recorded brook, a quiet bed off to its side)
                stream = streamLoop_[streamPos_] * 2.2f;
                if (++streamPos_ >= streamLoop_.size()) streamPos_ = 0;
            }
            // Wind: a low rush with gusts, and a leafier hush on top of it.
            const float n3 = noise();
            wnLp_ += (n3 - wnLp_) * 0.012f;
            wnHp_ += (n3 - wnHp_) * 0.12f;
            const float gust = 0.35f + 0.65f * std::max(0.0f, std::sin(cfT_ * 0.43f) * 0.6f + std::sin(cfT_ * 0.17f + 1.0f) * 0.4f);
            float wind = (wnLp_ * 2.2f + (wnHp_ - wnLp_) * 0.35f * gust) * gust * 0.5f;
            if (!windLoop_.empty()) {  // (the recorded air, a gentle bed: no gusts of our own on top)
                wind = windLoop_[windPos_] * 0.9f;
                if (++windPos_ >= windLoop_.size()) windPos_ = 0;
            }
            // Crickets: three voices chirping in threes, each a little out of step with the others.
            float crick = 0, crickL = 0, crickR = 0;
            for (int v = 0; v < 3; v++) {
                const float rate = 1.9f + 0.37f * v, ph = std::fmod(cfT_ * rate + v * 0.31f, 1.0f);
                const float chirp = ph < 0.55f ? 0.5f - 0.5f * std::cos(std::fmod(ph * 3.0f / 0.55f, 1.0f) * 6.2831853f) : 0.0f;
                cfPhase_[v] += (4300.0f + 330.0f * v + 60.0f * chirp) / rate_ * 6.2831853f;
                if (cfPhase_[v] > 6.2831853f) cfPhase_[v] -= 6.2831853f;
                const float c = std::sin(cfPhase_[v]) * chirp * chirp * 0.05f;
                crick += c;
                crickL += c * (0.75f - 0.35f * v), crickR += c * (0.15f + 0.35f * v);
            }
            (void)crick;
            const float bed = amb * 0.5f;
            const float l = fire * lF * 0.9f + stream * lS * 0.2f * bed + (wind + 0) * 0.5f * bed + crickL * 1.3f * bed;
            const float r = fire * rF * 0.9f + stream * rS * 0.2f * bed + (wind + 0) * 0.5f * bed + crickR * 1.3f * bed;
            out[2 * i] += l * g;
            out[2 * i + 1] += r * g;
        }
    }
    const float volumeA = 1 - std::exp(-dt / 0.025f);
    for (int i = 0; i < frames; i++) {
        sMaster_ += (master - sMaster_) * volumeA;
        if (master == 0 && sMaster_ < 1e-6f) sMaster_ = 0;
        for (int c = 0; c < 2; c++) {
            float& sample = out[2 * i + c];
            const float a = std::fabs(sample);
            if (a > 0.8f) sample = std::copysign(0.8f + 0.2f * std::tanh((a - 0.8f) / 0.2f), sample);
            sample *= sMaster_;
        }
    }
}

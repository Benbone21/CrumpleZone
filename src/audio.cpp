#include "audio.h"

#include <cmath>
#include <cstdio>

#include "sdl_min.h"
#include "softbody.h"

void Audio::setNearbyEngines(const Sim& sim, const V3& listener, const V3& right, float timeScale) {
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
            (uint8_t)engineSamples::forVehicle(v)};
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
        voice->left = std::sqrt(0.5f * (1 - source.pan));
        voice->right = std::sqrt(0.5f * (1 + source.pan));
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

bool Audio::start() {
    if (device_) return true;
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

void Audio::callback(void* user, uint8_t* stream, int len) {
    static_cast<Audio*>(user)->render(reinterpret_cast<float*>(stream), len / (int)(2 * sizeof(float)));
}

void Audio::render(float* out, int frames) {
    const float dt = 1.0f / rate_;
    const float tRpm = rpm_, tLoad = load_, tSlip = slip_, tSpeed = speed_, tBoost = boost_, master = master_;
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
    const EngineInput engineInput{tRpm, tLoad, tBoost, (int)cyl_, (int)induction_, (float)liters_, (float)redline_, (bool)flatPlane_,
                                 (uint8_t)std::clamp((int)recording_, 0, engineSamples::kBanks - 1)};
    const auto* localBank = recordings_.bank(engineInput.recording);
    updateRemoteVoices();
    const float smooth = 1.0f - std::exp(-dt * 30.0f);
    for (int i = 0; i < frames; i++) {
        sSlip_ += (tSlip - sSlip_) * smooth * 0.5f;
        sSpeed_ += (tSpeed - sSpeed_) * smooth;

        const float engine = engine_.sample(engineInput, dt, localBank);
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
        float gw = 2.0f * std::sin((float)M_PI * 4800.0f * dt);
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

        float mix = engine + squeal + wind + crunch * 0.9f + thump * 0.8f + glassSnd + popSnd;
        float nearbyLeft = 0, nearbyRight = 0;
        for (auto& voice : nearby_) {
            if (!voice.id) continue;
            voice.gain += (voice.targetGain - voice.gain) * smooth;
            if (voice.gain < 1e-6f && voice.targetGain == 0) { voice.id = 0; continue; }
            const float sound = voice.synth.sample(voice.input, dt, recordings_.bank(voice.input.recording)) * voice.gain;
            nearbyLeft += sound * voice.left;
            nearbyRight += sound * voice.right;
        }
        out[2 * i] = std::tanh((mix + nearbyLeft) * 1.2f) * master;
        out[2 * i + 1] = std::tanh((mix + nearbyRight) * 1.2f) * master;
    }
    // Music goes on top, quieter than the car (its own level is about -18 dBFS before the
    // volume setting). A soft knee catches the rare moment both peak together.
    const float mv = musicVol_;
    music_.render(out, frames, musicMap_, mv);
    if (mv > 0)
        for (int i = 0; i < 2 * frames; i++) {
            float a = std::fabs(out[i]);
            if (a > 0.8f) out[i] = std::copysign(0.8f + 0.2f * std::tanh((a - 0.8f) / 0.2f), out[i]);
        }
}

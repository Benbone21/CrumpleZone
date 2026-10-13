#include "audio.h"

#include <cmath>

#include "sdl_min.h"

bool Audio::start() {
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
    const float smooth = 1.0f - std::exp(-dt * 30.0f);
    for (int i = 0; i < frames; i++) {
        sRpm_ += (tRpm - sRpm_) * smooth;
        sLoad_ += (tLoad - sLoad_) * smooth;
        sSlip_ += (tSlip - sSlip_) * smooth * 0.5f;
        sSpeed_ += (tSpeed - sSpeed_) * smooth;
        sBoost_ += (tBoost - sBoost_) * smooth;

        // ---- Engine ------------------------------------------------------------
        // Each cylinder fires once every two revolutions, so pulses come at
        // rpm/60 * cylinders/2 Hz. Small differences between cylinders (and, on a
        // cross-plane V8 or odd-fire engine, uneven spacing) give the half-order
        // "lump"; a flat-plane V8 fires evenly, like two fours, and howls instead.
        // Bigger engines sound deeper (lower exhaust resonance), turbos muffle the
        // pulses and whistle, a Roots blower whines with the crank.
        const int cyl = std::max(1, std::min(12, (int)cyl_)), ind = induction_;
        const float liters = std::max(0.6f, (float)liters_), redline = std::max(3000.0f, (float)redline_);
        if (cyl != voiceCyl_) {
            voiceCyl_ = cyl;
            for (int c = 0; c < 12; c++) cylGain_[c] = 1.0f - 0.18f * (0.5f + 0.5f * noise());
        }
        float fire = sRpm_ / 60.0f * cyl * 0.5f;
        phase_ += fire * dt;
        subPhase_ += sRpm_ / 60.0f * 0.5f * dt;  // once per cycle (two revolutions)
        if (phase_ > 1) {
            phase_ -= std::floor(phase_);
            fireCount_ = (fireCount_ + 1) % cyl;
        }
        if (subPhase_ > 1) subPhase_ -= std::floor(subPhase_);
        float ph = (float)phase_;
        const float sharp = 5.0f + 2.5f * sLoad_ + (flatPlane_ ? 3.0f : 0.0f);
        // The lump: alternate pulses a little stronger and weaker (strong on fours and V6s,
        // faint on a flat-plane V8), which puts a half-order under the firing note.
        float lumpAmt = flatPlane_ ? 0.04f : cyl <= 4 ? 0.28f : cyl == 6 ? 0.2f : 0.34f;
        float lump = 1.0f + lumpAmt * ((fireCount_ & 1) ? 1.0f : -1.0f) * std::cos(2.0f * (float)M_PI * (float)subPhase_);
        float pulse = (std::exp(-ph * sharp) - 1.0f / sharp) * cylGain_[fireCount_] * lump;
        float rasp = noise() * std::exp(-ph * 4.0f) * (0.2f + 0.5f * sLoad_) * (ind == 1 ? 0.6f : 1.0f);
        float raw = pulse * 1.4f + rasp;
        // Exhaust resonance: deeper for big engines, brighter with load and revs.
        float cutoff = (250.0f + 2200.0f * sLoad_) * std::pow(1.6f / liters, 0.35f) + sRpm_ * 0.28f;
        if (ind == 1) cutoff *= 0.75f;  // the turbine soaks up the sharp edges
        if (flatPlane_) cutoff *= 1.5f;
        float a = 1.0f - std::exp(-2.0f * (float)M_PI * cutoff * dt);
        engLp_ += (raw - engLp_) * a;
        engLp2_ += (engLp_ - engLp2_) * a;
        // Intake honk near the top of the rev range on naturally aspirated engines.
        float ia = 1.0f - std::exp(-2.0f * (float)M_PI * 900.0f * dt);
        intakeLp_ += (pulse - intakeLp_) * ia;
        float intake = ind == 0 ? intakeLp_ * 0.25f * sLoad_ * std::max(0.0f, sRpm_ / redline - 0.55f) * 2.0f : 0.0f;
        float size = 0.8f + 0.12f * std::min(liters, 5.0f);
        float engine = (engLp2_ * (0.28f + 0.45f * sLoad_) * size + intake) * std::min(1.0f, sRpm_ / 400.0f);  // silent when stalled

        // Turbo: spool whistle rising with boost, and a blow-off "pssh" when the throttle
        // closes on boost.
        float forced = 0;
        if (ind == 1) {
            spoolPhase_ += (1800.0f + 5200.0f * sBoost_) * sRpm_ / redline * dt;
            if (spoolPhase_ > 1) spoolPhase_ -= std::floor(spoolPhase_);
            forced += std::sin(2.0f * (float)M_PI * (float)spoolPhase_) * 0.05f * sBoost_ * sBoost_;
            if (prevLoad_ - sLoad_ > 0.25f * dt * 30.0f && sBoost_ > 0.4f) bovEnv_ = std::max(bovEnv_, sBoost_);
            float bw = 2.0f * std::sin((float)M_PI * 2400.0f * dt);
            float bh = noise() - bovLow_ - 0.7f * bovBand_;
            bovBand_ += bw * bh;
            bovLow_ += bw * bovBand_;
            forced += bovBand_ * 0.22f * bovEnv_;
            bovEnv_ *= std::exp(-dt / 0.25f);
        } else if (ind == 2) {
            // Roots blower: belt-driven at ~2.5x crank, three-lobe rotors, so its whine
            // tracks the revs exactly.
            whinePhase_ += sRpm_ / 60.0f * 2.5f * 6.0f * dt;
            if (whinePhase_ > 1) whinePhase_ -= std::floor(whinePhase_);
            float wp = 2.0f * (float)M_PI * (float)whinePhase_;
            forced += (std::sin(wp) + 0.3f * std::sin(2.0f * wp)) * 0.06f * (0.3f + 0.7f * sLoad_) * std::min(1.0f, sRpm_ / 2500.0f);
        }
        prevLoad_ = sLoad_;
        engine += forced;

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
        mix = std::tanh(mix * 1.2f) * master;
        out[2 * i] = mix;
        out[2 * i + 1] = mix;
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

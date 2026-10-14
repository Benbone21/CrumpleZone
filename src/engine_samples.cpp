#include "engine_samples.h"
#include "vehicles.h"
#include <algorithm>
#include <cmath>
#include <cstring>
#include <fstream>
#include <sstream>

namespace engineSamples {
namespace {
const char* kKeys[kBanks] = {"none", "honda_d16a6", "honda_b16a", "honda_b18c", "jesko_5l",
    "ford_27_ecoboost", "ford_23_ecoboost", "ford_30_raptor", "toyota_5vz", "toyota_5vz_supercharged",
    "toyota_1gr", "toyota_1gr_supercharged", "gm_ls3", "gm_ls7", "gm_ls3_supercharged",
    "rotax_900_turbo", "toyota_2gr", "toyota_2gr_supercharged"};
uint16_t u16(const unsigned char* p) { return p[0] | uint16_t(p[1]) << 8; }
uint32_t u32(const unsigned char* p) { return u16(p) | uint32_t(u16(p + 2)) << 16; }
bool wav(const std::string& path, Loop& loop, std::string& why) {
    std::ifstream f(path, std::ios::binary | std::ios::ate);
    if (!f) { why = "cannot open WAV"; return false; }
    const auto bytes = f.tellg();
    if (bytes < 44 || bytes > 16 * 1024 * 1024) { why = "invalid WAV size (maximum 16 MB)"; return false; }
    std::vector<unsigned char> b((size_t)bytes);
    f.seekg(0); f.read((char*)b.data(), bytes);
    if (!f || std::memcmp(b.data(), "RIFF", 4) || std::memcmp(b.data() + 8, "WAVE", 4)) { why = "expected RIFF/WAVE"; return false; }
    const size_t end = (size_t)u32(b.data() + 4) + 8;
    if (end > b.size() || end < 12) { why = "truncated RIFF"; return false; }
    int format = 0, channels = 0, bits = 0, align = 0;
    const unsigned char* data = nullptr; size_t dataBytes = 0;
    for (size_t p = 12; p + 8 <= end;) {
        const size_t n = u32(b.data() + p + 4), at = p + 8;
        if (n > end - at) { why = "truncated chunk"; return false; }
        if (!std::memcmp(b.data() + p, "fmt ", 4)) {
            if (n < 16) { why = "short format chunk"; return false; }
            const auto* h = b.data() + at;
            format = u16(h); channels = u16(h + 2); loop.rate = (int)u32(h + 4);
            align = u16(h + 12); bits = u16(h + 14);
            if (format == 0xfffe && n >= 40 && u16(h + 16) >= 22) {
                // Standard PCM/IEEE float WAVE_FORMAT_EXTENSIBLE GUID only.
                const unsigned char guidTail[14] = {0,0,0,0,0x10,0,0x80,0,0,0xaa,0,0x38,0x9b,0x71};
                if (std::memcmp(h + 26, guidTail, 14) || u16(h + 18) != bits) { why = "unsupported extensible format"; return false; }
                format = u16(h + 24);
            }
        } else if (!std::memcmp(b.data() + p, "data", 4)) { data = b.data() + at; dataBytes = n; }
        p = at + n + (n & 1);
    }
    if (!data || channels < 1 || channels > 2 || loop.rate < 8000 || loop.rate > 192000 ||
        !((format == 1 && (bits == 16 || bits == 24 || bits == 32)) || (format == 3 && bits == 32)) ||
        align != channels * bits / 8 || dataBytes % align) { why = "unsupported WAV: use mono/stereo PCM16/24/32 or float32"; return false; }
    const size_t frames = dataBytes / align;
    if (frames < (size_t)loop.rate / 10 || frames > (size_t)loop.rate * 4) { why = "loop must be 0.1 to 4 seconds"; return false; }
    auto& wave = loop.wave[0]; wave.resize(frames);
    double sum = 0;
    for (size_t i = 0; i < frames; i++) {
        float x = 0;
        for (int c = 0; c < channels; c++) {
            const auto* s = data + i * align + c * (bits / 8);
            float v;
            if (format == 3) { const uint32_t raw = u32(s); std::memcpy(&v, &raw, 4); }
            else if (bits == 16) v = (int16_t)u16(s) / 32768.0f;
            else if (bits == 24) { int n = s[0] | int(s[1]) << 8 | int(s[2]) << 16; if (n & 0x800000) n -= 0x1000000; v = n / 8388608.0f; }
            else v = (int32_t)u32(s) / 2147483648.0f;
            if (!std::isfinite(v) || std::fabs(v) > 16) { why = "non-finite or out-of-range WAV"; return false; }
            x += v / channels;
        }
        wave[i] = x; sum += x;
    }
    const float mean = (float)(sum / frames); float peak = 0;
    for (float& x : wave) { x -= mean; peak = std::max(peak, std::fabs(x)); }
    if (peak < 1e-5f) { why = "silent loop"; return false; }
    if (peak > 1) for (float& x : wave) x /= peak;
    // Overlap the tail into the beginning: the wrap is continuous even when the
    // source endpoints were imperfect. Most of the actual recording is untouched.
    const size_t overlap = std::min(frames / 8, (size_t)loop.rate / 200); // 5 ms
    for (size_t i = 0; i < overlap; i++) {
        const float t = (float)i / overlap;
        wave[i] = wave[frames - overlap + i] * (1 - t) + wave[i] * t;
    }
    wave.resize(frames - overlap);
    // Circular, low-pass decimation. Select these mips before resampling above
    // original pitch, rather than aliasing recorded exhaust harmonics.
    constexpr double pi = 3.141592653589793;
    for (int m = 1; m < kMips; m++) {
        const auto& prev = loop.wave[m - 1]; auto& next = loop.wave[m];
        next.resize(prev.size() / 2);
        for (size_t i = 0; i < next.size(); i++) {
            double value = 0, norm = 0;
            for (int tap = -12; tap <= 12; tap++) {
                const double a = 0.45 * tap;
                const double sinc = tap == 0 ? 0.45 : std::sin(pi * a) / (pi * tap);
                const double weight = sinc * (0.5 + 0.5 * std::cos(pi * tap / 13));
                const size_t index = (2 * i + prev.size() + tap) % prev.size();
                value += prev[index] * weight; norm += weight;
            }
            next[i] = (float)(value / norm);
        }
    }
    return true;
}
float read(const Loop& loop, double pos, double speed) {
    int mip = 0;
    while (mip < kMips - 1 && speed > 1.0) { speed *= 0.5; mip++; }
    const auto& wave = loop.wave[mip];
    // Fractional phase preserves position for odd-sized loops as mip levels change.
    const double p = pos / loop.wave[0].size() * wave.size();
    const size_t i = (size_t)p; const float f = (float)(p - i);
    return wave[i % wave.size()] + (wave[(i + 1) % wave.size()] - wave[i % wave.size()]) * f;
}
}
const char* key(int n) { return n >= 0 && n < kBanks ? kKeys[n] : kKeys[0]; }
int id(const std::string& name) { for (int n = 1; n < kBanks; n++) if (name == kKeys[n]) return n; return 0; }
int forVehicle(const VehicleSpec& v) {
    const std::string name = v.engineName;
    switch (v.model) {
        case 0: return name.find("B18") != std::string::npos ? 3 : name.find("B16") != std::string::npos ? 2 : 1;
        case 1: return 4;
        case 2: return v.cylinders == 4 ? 6 : v.displacement > 2.85 ? 7 : 5;
        case 3: return v.displacement > 3.7 ? (v.induction == VehicleSpec::SUPERCHARGER ? 11 : 10) : (v.induction == VehicleSpec::SUPERCHARGER ? 9 : 8);
        case 4: return v.displacement > 6.6 ? 13 : v.induction == VehicleSpec::SUPERCHARGER ? 14 : 12;
        case 5: return 15;
        case 6: return v.induction == VehicleSpec::SUPERCHARGER ? 17 : 16;
        default: return 0;
    }
}
int Library::load(const std::string& directory, std::vector<std::string>* errors) {
    banks_ = {}; int loaded = 0; size_t bytesLoaded = 0;
    for (int bankId = 1; bankId < kBanks; bankId++) {
        const std::string dir = directory + "/" + key(bankId) + "/";
        std::ifstream manifest(dir + "bank.txt"); if (!manifest) continue;
        Bank bank; std::string text, why; int line = 0; size_t bankBytes = 0;
        while (std::getline(manifest, text)) {
            line++; std::istringstream row(text); std::string field;
            if (!(row >> field) || field[0] == '#') continue;
            if (field == "source" || field == "author" || field == "license") {
                std::string value; std::getline(row >> std::ws, value);
                (field == "source" ? bank.source : field == "author" ? bank.author : bank.license) = value;
            } else if (field == "loop") {
                Loop loop; int on; std::string file, extra;
                if (!(row >> loop.rpm >> on >> loop.gain >> file) || row >> extra ||
                    !std::isfinite(loop.rpm) || loop.rpm < 300 || loop.rpm > 18000 || (on != 0 && on != 1) ||
                    !std::isfinite(loop.gain) || loop.gain <= 0 || loop.gain > 2 || bank.loops.size() >= kLoops ||
                    file.find_first_of("/\\:") != std::string::npos || file.find("..") != std::string::npos ||
                    file.size() < 5 || file.substr(file.size() - 4) != ".wav") { why = "invalid loop row"; break; }
                loop.loaded = on != 0;
                if (!wav(dir + file, loop, why)) break;
                for (const auto& mip : loop.wave) bankBytes += mip.size() * sizeof(float);
                if (bankBytes > 16 * 1024 * 1024 || bytesLoaded + bankBytes > 64 * 1024 * 1024) { why = "recording memory budget exceeded (16 MB/bank, 64 MB total)"; break; }
                bank.loops.push_back(std::move(loop));
            } else { why = "unknown manifest field"; break; }
        }
        if (why.empty() && (bank.source.empty() || bank.author.empty() || bank.license.empty())) why = "source, author and license are required";
        if (why.empty()) {
            for (bool on : {false, true}) {
                int count = 0; for (const auto& loop : bank.loops) count += loop.loaded == on;
                if (count < 2) why = "each load state needs at least two RPM loops";
            }
            std::sort(bank.loops.begin(), bank.loops.end(), [](const Loop& a, const Loop& b) { return a.loaded != b.loaded ? a.loaded < b.loaded : a.rpm < b.rpm; });
            for (size_t i = 1; i < bank.loops.size(); i++) if (bank.loops[i].loaded == bank.loops[i - 1].loaded && bank.loops[i].rpm == bank.loops[i - 1].rpm) why = "duplicate RPM in a load state";
        }
        if (!why.empty()) { if (errors) errors->push_back(dir + "bank.txt:" + std::to_string(line) + ": " + why); continue; }
        banks_[bankId] = std::move(bank); bytesLoaded += bankBytes; loaded++;
    }
    return loaded;
}

float Player::sample(const Bank& bank, const EngineInput& input, float dt) {
    if (bank.loops.empty() || bank.loops.size() > kLoops || !(dt > 0) || !std::isfinite(dt)) return 0;
    if (dt != dt_) { dt_ = dt; smooth_ = 1 - std::exp(-dt * 25); }
    const float wanted = std::isfinite(input.rpm) ? std::clamp(input.rpm, 0.0f, 18000.0f) : 0;
    rpm_ += (wanted - rpm_) * smooth_;
    const float load = std::isfinite(input.load) ? std::clamp(input.load, 0.0f, 1.0f) : 0;
    load_ += (load - load_) * smooth_;
    gate_ += ((wanted > 0 ? 1.0f : 0.0f) - gate_) * smooth_;
    if (wanted == 0 && gate_ < 1e-6f) return 0;
    cycle_ += rpm_ / 120 * input.cylinders * dt; cycle_ -= std::floor(cycle_);
    if (tick_-- <= 0) {
        tick_ = 31; target_.fill(0);
        for (int on = 0; on <= 1; on++) {
            int lo = -1, hi = -1;
            for (int k = 0; k < (int)bank.loops.size(); k++) if (bank.loops[k].loaded == (on != 0)) {
                if (lo < 0) lo = k;
                if (bank.loops[k].rpm <= rpm_) lo = k;
                if (hi < 0 && bank.loops[k].rpm >= rpm_) hi = k;
            }
            if (hi < 0) hi = lo;
            if (lo < 0) continue;
            const float fraction = hi == lo ? 0 : std::clamp((rpm_ - bank.loops[lo].rpm) / (bank.loops[hi].rpm - bank.loops[lo].rpm), 0.0f, 1.0f);
            const float loadWeight = on ? load_ : 1 - load_;
            target_[lo] += std::sqrt(loadWeight * (1 - fraction));
            if (hi != lo) target_[hi] += std::sqrt(loadWeight * fraction);
        }
    }
    float output = 0;
    for (size_t k = 0; k < bank.loops.size(); k++) {
        const Loop& loop = bank.loops[k];
        const double speed = std::clamp(rpm_ / loop.rpm, 0.2f, 4.0f) * loop.rate * dt;
        position_[k] += speed;
        if (position_[k] >= loop.wave[0].size()) position_[k] = std::fmod(position_[k], (double)loop.wave[0].size());
        gain_[k] += (target_[k] - gain_[k]) * smooth_;
        if (gain_[k] > 1e-6f) output += read(loop, position_[k], speed) * loop.gain * gain_[k];
    }
    return output * gate_;
}
}
float EngineVoice::sample(const EngineInput& input, float dt, const engineSamples::Bank* bank) {
    if (bank != bank_) {
        previous_ = recorded_; previousBank_ = bank_; bank_ = bank;
        recorded_ = engineSamples::Player{}; fade_ = 0;
    }
    const float current = bank_ ? recorded_.sample(*bank_, input, dt) : procedural_.sample(input, dt);
    if (fade_ >= 1) return current;
    const float old = previousBank_ ? previous_.sample(*previousBank_, input, dt) : bank_ ? procedural_.sample(input, dt) : current;
    fade_ = std::min(1.0f, fade_ + dt / 0.04f);
    return current * fade_ + old * (1 - fade_);
}

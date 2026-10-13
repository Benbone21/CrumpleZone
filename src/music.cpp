// Generative background music. Each map has a style; a seeded generator picks a key, mode
// and tempo, writes chord progressions with simple functional root motion, builds melodies
// from a short motif (repeat, sequence, cadence) snapped to chord tones, and moves through
// sections of 30–90 s that add or drop layers, change the progression or modulate.
// Everything is synthesized here: band-limited saws and pulses (PolyBLEP), FM keys and bells,
// Karplus-Strong plucks, simple drums, a feedback-delay-network reverb and a ping-pong echo.
#include "music.h"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <vector>

namespace {

constexpr float kPi = 3.14159265f;

// ---- Small DSP helpers ---------------------------------------------------------------

struct SineTable {
    float v[4097];
    SineTable() {
        for (int i = 0; i <= 4096; i++) v[i] = (float)std::sin(2.0 * M_PI * i / 4096.0);
    }
};
const SineTable kSine;

// Sine of a phase in cycles (any value; wrapped here).
inline float sine(float ph) {
    ph -= std::floor(ph);
    float x = ph * 4096.0f;
    int i = (int)x;
    float f = x - (float)i;
    return kSine.v[i] + (kSine.v[i + 1] - kSine.v[i]) * f;
}

inline float wrap1(float p) { return p >= 1.0f ? p - std::floor(p) : p; }

// PolyBLEP: smooths the jump in a saw or pulse so it doesn't alias into a hiss.
inline float blep(float t, float dt) {
    if (t < dt) {
        t /= dt;
        return t + t - t * t - 1.0f;
    }
    if (t > 1.0f - dt) {
        t = (t - 1.0f) / dt;
        return t * t + t + t + 1.0f;
    }
    return 0.0f;
}
inline float saw(float ph, float dt) { return 2.0f * ph - 1.0f - blep(ph, dt); }
inline float pulse(float ph, float dt, float pw) {
    float a = ph < pw ? 1.0f : -1.0f;
    a += blep(ph, dt);
    float p2 = ph - pw;
    if (p2 < 0) p2 += 1.0f;
    return a - blep(p2, dt);
}

// tan(x) for the filter coefficient; accurate to 0.1% below x = 0.8 (fc < rate/4).
inline float tanApprox(float x) {
    float x2 = x * x;
    return x * (15.0f - x2) / (15.0f - 6.0f * x2);
}

// Trapezoidal state-variable filter: stays stable while its cutoff is swept every sample.
struct Svf {
    float ic1 = 0, ic2 = 0, band = 0;
    float run(float x, float g, float k) {  // returns lowpass; `band` holds the bandpass
        float a1 = 1.0f / (1.0f + g * (g + k));
        float v3 = x - ic2;
        float v1 = a1 * ic1 + g * a1 * v3;
        float v2 = ic2 + g * v1;
        ic1 = 2.0f * v1 - ic1;
        ic2 = 2.0f * v2 - ic2;
        band = v1;
        return v2;
    }
};

struct Rng {
    uint32_t s = 1;
    uint32_t u() {
        s ^= s << 13;
        s ^= s >> 17;
        s ^= s << 5;
        return s;
    }
    float f() { return (u() >> 8) * (1.0f / 16777216.0f); }
    float r(float a, float b) { return a + (b - a) * f(); }
    int i(int n) { return std::min(n - 1, (int)(f() * n)); }
    bool p(float pr) { return f() < pr; }
    float bi() { return f() * 2.0f - 1.0f; }
    template <int N>
    int pick(const float (&w)[N]) {
        float sum = 0;
        for (float x : w) sum += x;
        float t = f() * sum;
        for (int k = 0; k < N; k++) {
            if (t < w[k]) return k;
            t -= w[k];
        }
        return N - 1;
    }
};

// Envelope: linear attack, exponential decay toward sustain, exponential release.
struct Env {
    float v = 0, aInc = 1, dCoef = 0, sus = 0, rCoef = 0;
    int stage = 0;  // 0 idle, 1 attack, 2 decay/sustain, 3 release
    void set(float attack, float decayTau, float sustain, float releaseTau, float rate) {
        aInc = 1.0f / std::max(1.0f, attack * rate);
        dCoef = std::exp(-1.0f / (decayTau * rate));
        sus = sustain;
        rCoef = std::exp(-1.0f / (releaseTau * rate));
        stage = 1;
        v = 0;
    }
    void release() {
        if (stage) stage = 3;
    }
    float next() {
        switch (stage) {
            case 1:
                v += aInc;
                if (v >= 1.0f) {
                    v = 1.0f;
                    stage = 2;
                }
                break;
            case 2:
                v = sus + (v - sus) * dCoef;
                if (sus <= 0 && v < 3e-4f) stage = 0;  // -70 dB: gone
                break;
            case 3:
                v *= rCoef;
                if (v < 3e-4f) stage = 0;
                break;
        }
        return v;
    }
};

// ---- Music theory ---------------------------------------------------------------------

enum Mode { IONIAN, DORIAN, MIXOLYDIAN, AEOLIAN, LYDIAN, MODES };
const int kModes[MODES][7] = {
    {0, 2, 4, 5, 7, 9, 11},  // major
    {0, 2, 3, 5, 7, 9, 10},
    {0, 2, 4, 5, 7, 9, 10},
    {0, 2, 3, 5, 7, 8, 10},  // natural minor
    {0, 2, 4, 6, 7, 9, 11},
};
const char* kModeNames[MODES] = {"major", "dorian", "mixolydian", "minor", "lydian"};
const char* kKeyNames[12] = {"C", "Db", "D", "Eb", "E", "F", "F#", "G", "Ab", "A", "Bb", "B"};

inline int fdiv7(int i) { return i >= 0 ? i / 7 : -((6 - i) / 7); }
inline int fmod7(int i) { return i - 7 * fdiv7(i); }
inline float mtof(float m) { return 440.0f * std::exp2((m - 69.0f) / 12.0f); }

// ---- Styles ---------------------------------------------------------------------------

enum Style { LOFI, MOUNTAIN, SYNTH, DESERT, STYLES };  // indexed by map; later maps reuse them in turn
enum Layer { CHORD, BASS, KICK, SNARE, HAT, ARP, LEAD, EXTRA, LAYERS };
enum Inst { OFF, EPIANO, PAD, PLUCK, SUB, SAWBASS, VIBES, FLUTE, SAWLEAD, SQARP, BELL, BD, SD, RIM, HH, SHAKER, TOM, CRASH };
const char* kLayerNames[LAYERS] = {"chords", "bass", "kick", "snare", "hats", "arp", "lead", "bells"};

constexpr unsigned bit(int l) { return 1u << l; }

struct StyleDef {
    float bpmLo, bpmHi, swingLo, swingHi;
    float modeW[MODES];
    float degBias[7];  // how much each scale degree is liked as a chord root
    float rt60, revDampHz, revWet, delayBeats, delayFb;
    float vol[LAYERS], rev[LAYERS], dly[LAYERS], duck[LAYERS];
    unsigned mask[4];   // layers at each intensity
    unsigned optional;  // layers a section may leave out
    float toneHz;       // master lowpass (lo-fi tape warmth); 0 = none
};

const StyleDef kStyles[STYLES] = {
    // Lo-fi: slow swung groove, 7th and 9th chords on electric piano, soft drums, vibes.
    {70, 84, 0.56f, 0.62f,
     {3, 3, 0.5f, 1, 1},
     {1.0f, 1.1f, 0.6f, 1.1f, 0.8f, 1.0f, 0},
     1.5f, 4200, 0.55f, 0.75f, 0.30f,
     {0.36f, 0.27f, 0.62f, 0.40f, 0.28f, 0.36f, 0.24f, 0.10f},
     {0.22f, 0.0f, 0.03f, 0.22f, 0.10f, 0.30f, 0.35f, 0.50f},
     {0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.12f, 0.18f, 0.0f},
     {0.18f, 0.25f, 0, 0, 0, 0.1f, 0.1f, 0},
     {bit(CHORD), bit(CHORD) | bit(BASS) | bit(KICK) | bit(HAT),
      bit(CHORD) | bit(BASS) | bit(KICK) | bit(SNARE) | bit(HAT) | bit(LEAD),
      bit(CHORD) | bit(BASS) | bit(KICK) | bit(SNARE) | bit(HAT) | bit(LEAD) | bit(ARP)},
     bit(LEAD) | bit(ARP) | bit(HAT),
     6500},
    // Mountain: airy pads, fingerpicked plucks, a breathy flute, bells; a frame drum and
    // shaker only when it's at its fullest.
    {64, 80, 0.5f, 0.54f,
     {3, 1, 2, 0, 2},
     {1.0f, 0.8f, 0.6f, 1.1f, 0.9f, 1.0f, 0.3f},
     3.6f, 6000, 0.75f, 0.75f, 0.35f,
     {0.24f, 0.22f, 0.36f, 0.0f, 0.25f, 0.40f, 0.18f, 0.12f},
     {0.50f, 0.05f, 0.25f, 0.0f, 0.30f, 0.45f, 0.55f, 0.80f},
     {0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.18f, 0.22f, 0.30f},
     {0, 0, 0, 0, 0, 0, 0, 0},
     {bit(CHORD) | bit(EXTRA), bit(CHORD) | bit(ARP) | bit(EXTRA), bit(CHORD) | bit(ARP) | bit(BASS) | bit(LEAD),
      bit(CHORD) | bit(ARP) | bit(BASS) | bit(LEAD) | bit(KICK) | bit(HAT) | bit(EXTRA)},
     bit(EXTRA) | bit(LEAD),
     0},
    // Synthwave: straight 16ths, minor keys, pumping saw bass and pads, pulse arpeggio,
    // gated snare in a big room, a saw lead with echo.
    {94, 112, 0.5f, 0.5f,
     {0, 2, 0, 4, 0},
     {1.0f, 0.5f, 0.9f, 1.0f, 0.7f, 1.2f, 1.1f},
     2.4f, 5000, 0.60f, 0.75f, 0.38f,
     {0.28f, 0.54f, 0.47f, 0.34f, 0.12f, 0.19f, 0.28f, 0.0f},
     {0.35f, 0.02f, 0.05f, 0.55f, 0.15f, 0.30f, 0.45f, 0.0f},
     {0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.25f, 0.32f, 0.0f},
     {0.55f, 0.6f, 0, 0, 0, 0.2f, 0.15f, 0},
     {bit(CHORD) | bit(ARP), bit(CHORD) | bit(BASS) | bit(ARP) | bit(HAT),
      bit(CHORD) | bit(BASS) | bit(KICK) | bit(SNARE) | bit(HAT) | bit(ARP),
      bit(CHORD) | bit(BASS) | bit(KICK) | bit(SNARE) | bit(HAT) | bit(LEAD) | bit(ARP)},
     bit(ARP) | bit(HAT),
     0},
    // Desert road trip (the frontier): open pads, a twangy picked guitar with a dotted echo,
    // a root-fifth bass, cross-stick and shaker, and a lonely flute.
    {84, 96, 0.5f, 0.54f,
     {1, 2, 3, 2, 0},
     {1.0f, 0.6f, 0.5f, 1.1f, 0.9f, 0.9f, 1.0f},
     2.8f, 5000, 0.60f, 0.75f, 0.40f,
     {0.25f, 0.24f, 0.45f, 0.45f, 0.25f, 0.50f, 0.18f, 0.0f},
     {0.45f, 0.03f, 0.08f, 0.30f, 0.20f, 0.35f, 0.50f, 0.0f},
     {0.0f, 0.0f, 0.0f, 0.10f, 0.0f, 0.30f, 0.25f, 0.0f},
     {0, 0, 0, 0, 0, 0, 0, 0},
     {bit(CHORD) | bit(ARP), bit(CHORD) | bit(ARP) | bit(BASS) | bit(HAT),
      bit(CHORD) | bit(ARP) | bit(BASS) | bit(KICK) | bit(SNARE) | bit(HAT) | bit(LEAD),
      bit(CHORD) | bit(ARP) | bit(BASS) | bit(KICK) | bit(SNARE) | bit(HAT) | bit(LEAD)},
     bit(HAT) | bit(LEAD),
     0},
};

// Layer -> instrument, per style.
const int kInst[STYLES][LAYERS] = {
    {EPIANO, SUB, BD, SD, HH, PLUCK, VIBES, BELL},
    {PAD, SUB, TOM, SD, SHAKER, PLUCK, FLUTE, BELL},
    {PAD, SAWBASS, BD, SD, HH, SQARP, SAWLEAD, BELL},
    {PAD, SUB, BD, RIM, SHAKER, PLUCK, FLUTE, BELL},
};

// ---- Reverb and echo ------------------------------------------------------------------

// Eight-line feedback delay network with a Householder mix, input diffusers and damping.
struct Reverb {
    std::vector<float> line[8], ap[4], pre;
    int len[8] = {}, pos[8] = {}, apLen[4] = {}, apPos[4] = {}, preLen = 0, prePos = 0;
    float g[8] = {}, lp[8] = {}, dampA = 0.5f;
    void init(int rate) {
        static const int kLen[8] = {1123, 1327, 1559, 1801, 2063, 2281, 2551, 2843};
        static const int kAp[4] = {211, 157, 331, 263};
        for (int k = 0; k < 8; k++) {
            len[k] = std::max(8, kLen[k] * rate / 48000);
            line[k].assign(len[k], 0.0f);
        }
        for (int k = 0; k < 4; k++) {
            apLen[k] = std::max(4, kAp[k] * rate / 48000);
            ap[k].assign(apLen[k], 0.0f);
        }
        preLen = rate / 50;  // 20 ms pre-delay keeps the attack clear
        pre.assign(preLen, 0.0f);
    }
    void set(float rt60, float dampHz, int rate) {
        for (int k = 0; k < 8; k++) g[k] = std::pow(10.0f, -3.0f * len[k] / (rt60 * rate));
        dampA = 1.0f - std::exp(-2.0f * kPi * dampHz / rate);
    }
    void clear() {
        for (auto& l : line) std::fill(l.begin(), l.end(), 0.0f);
        for (auto& a : ap) std::fill(a.begin(), a.end(), 0.0f);
        std::fill(pre.begin(), pre.end(), 0.0f);
        std::fill(lp, lp + 8, 0.0f);
    }
    void run(float in, float& outL, float& outR) {
        float x = pre[prePos];
        pre[prePos] = in;
        if (++prePos >= preLen) prePos = 0;
        for (int k = 0; k < 4; k++) {  // smear the input so the tail starts dense
            float d = ap[k][apPos[k]];
            float y = d - 0.62f * x;
            ap[k][apPos[k]] = x + 0.62f * y;
            if (++apPos[k] >= apLen[k]) apPos[k] = 0;
            x = y;
        }
        float o[8], sum = 0;
        for (int k = 0; k < 8; k++) {
            lp[k] += (line[k][pos[k]] - lp[k]) * dampA;
            o[k] = lp[k] * g[k];
            sum += o[k];
        }
        sum *= 0.25f;  // 2/N
        for (int k = 0; k < 8; k++) {
            line[k][pos[k]] = o[k] - sum + ((k & 1) ? -x : x);
            if (++pos[k] >= len[k]) pos[k] = 0;
        }
        outL = (o[0] - o[2] + o[4] - o[6]) * 0.5f;
        outR = (o[1] - o[3] + o[5] - o[7]) * 0.5f;
    }
};

// Ping-pong echo: the repeats bounce between the sides and get darker.
struct Echo {
    std::vector<float> l, r;
    int n = 1, w = 0, d = 1;
    float fb = 0.3f, lpL = 0, lpR = 0, hpL = 0, hpR = 0;
    void init(int rate) {
        n = rate * 2;
        l.assign(n, 0.0f);
        r.assign(n, 0.0f);
    }
    void clear() {
        std::fill(l.begin(), l.end(), 0.0f);
        std::fill(r.begin(), r.end(), 0.0f);
        lpL = lpR = hpL = hpR = 0;
    }
    void run(float in, float& outL, float& outR) {
        int rd = w - d;
        if (rd < 0) rd += n;
        float yl = l[rd], yr = r[rd];
        lpL += (yl - lpL) * 0.35f;  // ~3 kHz at 48 kHz
        lpR += (yr - lpR) * 0.35f;
        hpL += (lpL - hpL) * 0.01f;  // and no low rumble building up
        hpR += (lpR - hpR) * 0.01f;
        l[w] = in + fb * (lpR - hpR);
        r[w] = fb * (lpL - hpL);
        if (++w >= n) w = 0;
        outL = yl;
        outR = yr;
    }
};

struct Voice {
    int inst = OFF, layer = 0;
    float freq = 0, vel = 0, panL = 0.7f, panR = 0.7f;
    int wait = 0, hold = -1, age = 0, maxAge = 0;
    float ph[3] = {0, 0, 0};
    float mod = 0, modCoef = 0, aux = 0, auxCoef = 0, f2 = 0, cut = 0, z = 0;
    Env amp;
    Svf f;
    // Karplus-Strong string
    float* ks = nullptr;
    int ksPos = 0;
    float ksDelay = 0, ksLoss = 0, ksZ = 0;
};

constexpr int kVoices = 64;

struct MotifNote {
    int pos, len, contour;
};

}  // namespace

// ---- One piece ------------------------------------------------------------------------

struct Music::Song {
    int rate = 48000;
    float dt = 1.0f / 48000;
    int map = -1, style = 0;
    const StyleDef* def = &kStyles[0];
    Rng rng, nz;  // rng makes the musical choices; nz is noise and humanizing

    // Time
    float bpm = 80, swing = 0.5f, stepSamples = 1, stepLeft = 1;
    int spb = 16, step = 0, bar = 0, sectionBar = 0, progBar = 0;

    // Harmony
    int key = 0, mode = 0;
    int prog[8] = {}, flavor[8] = {}, progLen = 4, barsPerChord = 1;
    int voicing[6] = {}, voicingN = 0;

    // Section
    struct Section {
        int bars = 8, intensity = 0;
        unsigned mask = 0;
        int kick = 0, hat = 0, comp = 0, bass = 0, arp = 0;
        float ghost = 0, hat16 = 0;
        bool push = false, lead = true;
    } sec;
    int sectionNo = 0, sectionsInKey = 0;
    bool kickAlt = false, pushed = false;

    // Melody: a two-bar motif, realized as an eight-bar phrase.
    MotifNote motif[16];
    int motifN = 0;
    MotifNote phrase[64];
    int phraseN = 0, phraseIdx = 0;
    bool phraseOn = false;
    int leadAnchor = 0;  // scale index at the middle of the lead's range
    int lastLead = 0;    // the lead's last note (scale index)
    int arpPat[8] = {}, arpLen = 8, lick[6] = {}, lickPos[6] = {}, lickN = 0;

    // Sound
    Voice voices[kVoices];
    std::vector<float> ksMem;
    int ksSize = 1024;
    float layerGain[LAYERS] = {}, layerTarget[LAYERS] = {};
    float duckEnv = 0, duckTgt = 0, duckAtk = 0, duckRel = 0;
    float lfoPh = 0, wowPh = 0, tremPh = 0, panPh = 0, padBright = 1;
    Reverb rev;
    Echo echo;
    Svf toneL, toneR;
    float dcL = 0, dcR = 0, agcMs = 1e-3f, agcGain = 1, peakEnv = 0;

    void init(int r) {
        rate = r;
        dt = 1.0f / r;
        ksSize = 1;
        while (ksSize < r / 60) ksSize <<= 1;  // room for a string down to 60 Hz
        ksMem.assign((size_t)ksSize * kVoices, 0.0f);
        for (int k = 0; k < kVoices; k++) voices[k].ks = &ksMem[(size_t)k * ksSize];
        rev.init(r);
        echo.init(r);
    }

    // ---- Harmony helpers ----
    int midiOf(int idx) const { return 36 + key + 12 * fdiv7(idx) + kModes[mode][fmod7(idx)]; }
    int semis(int deg, int steps) const { return midiOf(deg + steps) - midiOf(deg); }
    bool goodRoot(int deg) const { return semis(deg, 4) == 7; }  // no diminished chords
    // The chord's pitch classes, root first. `rootless` swaps the root for the 9th (lo-fi
    // keys, where the bass already plays the root).
    int chordPcs(int c, int* pcs, bool rootless = false) const {
        const int d = prog[c], fl = flavor[c];
        int idx[5], n = 0;
        // flavor: 0 triad, 1 add9, 2 sus2, 3 sus4, 4 seventh, 5 seventh + 9th
        if (fl == 2) idx[n++] = d, idx[n++] = d + 1, idx[n++] = d + 4;
        else if (fl == 3) idx[n++] = d, idx[n++] = d + 3, idx[n++] = d + 4;
        else {
            idx[n++] = d;
            idx[n++] = d + 2;
            idx[n++] = d + 4;
            if (fl >= 4) idx[n++] = d + 6;
            if (fl == 1 || fl == 5) idx[n++] = d + 8;
        }
        if (rootless && n >= 4) {
            for (int k = 0; k < n - 1; k++) idx[k] = idx[k + 1];
            n--;
        }
        for (int k = 0; k < n; k++) pcs[k] = ((midiOf(idx[k]) % 12) + 12) % 12;
        return n;
    }
    int rootPc(int c) const { return ((midiOf(prog[c]) % 12) + 12) % 12; }
    static int atOrAbove(int pc, int lo) { return lo + (((pc - lo) % 12) + 12) % 12; }
    int chordAt(int pb) const { return ((pb < 0 ? 0 : pb) / barsPerChord) % progLen; }

    // Close voicing in [lo, hi] that moves least from the last one.
    int voice(int c, int lo, int hi, int* out, bool rootless) {
        int pcs[5];
        const int n = chordPcs(c, pcs, rootless);
        int best[6], bestN = 0;
        float bestCost = 1e9f;
        for (int rot = 0; rot < n; rot++)
            for (int base = lo; base < lo + 12; base++) {
                if (((base % 12) + 12) % 12 != pcs[rot]) continue;
                int cand[6], m = 0, note = base;
                for (int k = 0; k < n; k++) {
                    int pc = pcs[(rot + k) % n];
                    note = k == 0 ? base : atOrAbove(pc, note + 1);
                    cand[m++] = note;
                }
                if (cand[m - 1] > hi) continue;
                float cost = 0;
                if (voicingN) {
                    for (int a = 0; a < m; a++) {
                        int dmin = 99;
                        for (int b = 0; b < voicingN; b++) dmin = std::min(dmin, std::abs(cand[a] - voicing[b]));
                        cost += dmin;
                    }
                } else {
                    cost = std::abs((cand[0] + cand[m - 1]) * 0.5f - (lo + hi) * 0.5f);
                }
                cost += 0.2f * std::abs((cand[0] + cand[m - 1]) * 0.5f - (lo + hi) * 0.5f);  // stay centred
                if (cost < bestCost) {
                    bestCost = cost;
                    bestN = m;
                    std::copy(cand, cand + m, best);
                }
            }
        if (!bestN) {  // range too narrow for this chord: just stack it from lo
            bestN = n;
            int note = lo - 1;
            for (int k = 0; k < n; k++) best[k] = note = atOrAbove(pcs[k], note + 1);
        }
        std::copy(best, best + bestN, voicing);
        voicingN = bestN;
        std::copy(best, best + bestN, out);
        return bestN;
    }

    // Chord tones from `lo` upward: the ladder arpeggios climb.
    int ladder(int c, int lo, int* out, int count) const {
        int pcs[5];
        int n = chordPcs(c, pcs);
        std::sort(pcs, pcs + n);
        int m = 0, note = lo - 1;
        // start on the root, then the other tones in pitch order
        int rp = rootPc(c), first = 0;
        for (int k = 0; k < n; k++)
            if (pcs[k] == rp) first = k;
        while (m < count) {
            for (int k = 0; k < n && m < count; k++) out[m++] = note = atOrAbove(pcs[(first + k) % n], note + 1);
        }
        return m;
    }

    // ---- Generation ----
    void makeProgression() {
        static const float kMotion[7] = {0.0f, 1.4f, 0.8f, 3.0f, 1.3f, 1.6f, 1.2f};  // by root step up
        const bool eight = rng.p(0.35f);
        progLen = eight ? 8 : 4;
        barsPerChord = style == MOUNTAIN || style == DESERT ? (eight ? 1 : 2) : style == SYNTH ? (eight || rng.p(0.4f) ? 1 : 2) : 1;
        if (style == LOFI && !eight && rng.p(0.25f)) barsPerChord = 2;
        auto next = [&](int prev, int i, int last, int target) {
            float w[7];
            int used[7] = {};
            for (int k = 0; k < i; k++) used[prog[k]]++;
            for (int c = 0; c < 7; c++) {
                w[c] = goodRoot(c) && c != prev ? kMotion[(c - prev + 7) % 7] * def->degBias[c] : 0.0f;
                w[c] /= 1.0f + used[c] * 1.2f;
                if (last) w[c] *= c == target ? 0.0f : 0.4f + kMotion[(target - c + 7) % 7];
            }
            return rng.pick(w);
        };
        static const float kStart[7] = {6, 1, 0.3f, 1.5f, 0.5f, 1.2f, 0.3f};
        float sw[7];
        for (int c = 0; c < 7; c++) sw[c] = goodRoot(c) ? kStart[c] : 0.0f;
        prog[0] = rng.pick(sw);
        for (int i = 1; i < 4; i++) prog[i] = next(prog[i - 1], i, i == 3, prog[0]);
        if (eight) {  // the second half answers the first: same start, new ending
            const int keep = rng.p(0.5f) ? 2 : 1;
            for (int i = 4; i < 4 + keep; i++) prog[i] = prog[i - 4];
            for (int i = 4 + keep; i < 8; i++) prog[i] = next(prog[i - 1], i, i == 7, prog[0]);
        }
        for (int i = 0; i < progLen; i++) {
            const int d = prog[i];
            const bool nine = semis(d, 8) % 12 == 2;
            int fl = 0;
            if (style == LOFI) fl = nine && rng.p(0.65f) ? 5 : 4;
            else if (style == MOUNTAIN || style == DESERT) {
                float w[4] = {1.0f, nine ? 1.4f : 0.0f, nine ? 0.5f : 0.0f, semis(d, 3) == 5 && d != 0 ? 0.35f : 0.0f};
                fl = rng.pick(w);
            } else {
                float w[5] = {1.6f, nine ? 0.8f : 0.0f, 0.0f, 0.0f, semis(d, 6) == 11 ? 0.6f : 0.15f};
                fl = rng.pick(w);
            }
            flavor[i] = fl;
        }
    }

    void modulate() {
        static const float w[4] = {3, 2, 2, 2};
        switch (rng.pick(w)) {
            case 0: key = (key + 5) % 12; break;
            case 1: key = (key + 7) % 12; break;
            case 2: key = (key + (rng.p(0.5f) ? 2 : 10)) % 12; break;
            default: {  // same tonic, another of the style's modes
                float mw[MODES];
                for (int m = 0; m < MODES; m++) mw[m] = m == mode ? 0.0f : def->modeW[m];
                float sum = 0;
                for (float x : mw) sum += x;
                if (sum > 0) mode = rng.pick(mw);
                else key = (key + 5) % 12;
            }
        }
        sectionsInKey = 0;
        // Keep the lead's middle where it was in pitch.
        leadAnchor = anchorFor(leadCenter());
    }

    int leadCenter() const { return style == MOUNTAIN ? 75 : style == DESERT ? 74 : 72; }

    int anchorFor(int midi) const {
        int idx = 0;
        while (midiOf(idx) < midi) idx++;
        while (midiOf(idx) > midi) idx--;
        return idx;
    }

    void makeMotif() {
        // Rhythm: durations in steps and how much each is liked, per style; retried until the
        // two bars hold a singable number of notes.
        static const int kDur[6] = {2, 3, 4, 6, 8, 12};
        static const float kDurW[STYLES][6] = {{3, 2, 3, 1.5f, 0.8f, 0}, {1, 0, 3, 2, 2, 0.8f}, {3, 0, 3, 1, 2, 0.6f}, {2, 1, 3, 2, 2, 0.6f}};
        static const float kRest[STYLES] = {0.15f, 0.2f, 0.12f, 0.2f};
        static const int kMinN[STYLES] = {5, 3, 5, 4}, kMaxN[STYLES] = {8, 6, 9, 7};
        const int unit = 2 * spb;
        for (int tries = 0; tries < 12; tries++) {
            int pos = rng.p(0.35f) ? (style == MOUNTAIN ? 4 : rng.p(0.5f) ? 2 : 3) : 0;
            motifN = 0;
            while (pos < unit - 4 && motifN < 12) {
                if (motifN && motif[motifN - 1].len >= 3 && rng.p(kRest[style])) pos += 2;
                if (pos >= unit - 4) break;
                int len = std::min(kDur[rng.pick(kDurW[style])], unit - 2 - pos);
                if (len < 1) break;
                motif[motifN++] = {pos, len, 0};
                pos += len;
            }
            if (motifN >= kMinN[style] && motifN <= kMaxN[style]) break;
        }
        // Contour: a shape (arch, rise, fall or wave) a few scale steps tall, roughened, with
        // no note repeated twice running and a step back after a leap.
        const int shape = rng.i(4);
        const float peak = 3.0f + rng.i(3);
        int prev = 99, prevMove = 0;
        for (int k = 0; k < motifN; k++) {
            const float x = motifN > 1 ? (float)k / (motifN - 1) : 0.0f;
            float c = shape == 0 ? peak * std::sin(kPi * x)
                    : shape == 1 ? peak * x - 1.0f
                    : shape == 2 ? peak * (1.0f - x) - 1.0f
                                 : 0.6f * peak * std::sin(2.0f * kPi * x) + 1.0f;
            int ci = (int)std::lround(c + rng.bi() * 0.9f);
            if (std::abs(prevMove) >= 3 && prev != 99) ci = prev - (prevMove > 0 ? 1 : -1);
            if (ci == prev) ci += rng.p(0.5f) ? 1 : -1;
            ci = std::max(-3, std::min(7, ci));
            if (prev != 99) prevMove = ci - prev;
            motif[k].contour = prev = ci;
        }
        if (!motifN) motif[motifN++] = {0, 8, 0};
    }

    // An eight-bar phrase from the motif: statement, sequence (or answer), statement, cadence.
    void makePhrase() {
        const int unit = 2 * spb;
        phraseN = 0;
        phraseIdx = 0;
        const int seqShift = rng.p(0.6f) ? (rng.p(0.5f) ? 1 : 2) * (rng.p(0.7f) ? 1 : -1) : 0;
        for (int u = 0; u < 4; u++) {
            const int base = u * unit;
            int n = motifN;
            if (u == 3) n = std::max(1, (motifN + 1) / 2);  // cadence: half the motif, then a long note
            for (int k = 0; k < n && phraseN < 62; k++) {
                MotifNote m = motif[k];
                if (u == 1) m.contour += seqShift;
                if (u == 1 && seqShift == 0 && k >= motifN / 2) m.contour += rng.i(5) - 2;  // answer
                if (u == 2 && k == motifN - 1 && rng.p(0.5f)) m.contour += rng.p(0.5f) ? 1 : -1;
                phrase[phraseN++] = {base + m.pos, m.len, m.contour};
            }
            if (u == 3) {
                int pos = phrase[phraseN - 1].pos + phrase[phraseN - 1].len;
                pos = std::max(pos, base + spb);  // land on a downbeat-ish spot
                pos = std::min(pos - pos % 4, base + unit - 6);
                if (pos <= phrase[phraseN - 1].pos) pos = phrase[phraseN - 1].pos + 2;
                phrase[phraseN - 1].len = std::max(1, std::min(phrase[phraseN - 1].len, pos - phrase[phraseN - 1].pos));
                phrase[phraseN++] = {pos, base + unit - 2 - pos, 1000};  // 1000: land on the chord's root or third
            }
        }
    }

    void makeArp() {
        if (style == SYNTH) {
            static const int kPats[5][8] = {{0, 1, 2, 3, 4, 5, 6, 7}, {0, 1, 2, 3, 4, 3, 2, 1}, {0, 2, 1, 3, 2, 4, 3, 5},
                                            {0, 3, 1, 4, 2, 5, 1, 4}, {0, 2, 4, 2, 3, 5, 6, 5}};
            sec.arp = rng.i(5);
            std::copy(kPats[sec.arp], kPats[sec.arp] + 8, arpPat);
            arpLen = 8;
        } else if (style == MOUNTAIN || style == DESERT) {
            // Fingerpicking: start on the low tone, roll up, ease back down.
            arpLen = spb / 2;
            arpPat[0] = 0;
            int cur = 0;
            for (int j = 1; j < arpLen; j++) {
                bool up = j < arpLen * 0.6f;
                int stepv = up ? (rng.p(0.7f) ? 1 : 2) : -(rng.p(0.6f) ? 1 : 2);
                if (rng.p(0.2f)) stepv = -stepv;
                cur = std::max(1, std::min(6, cur + stepv));
                arpPat[j] = cur;
            }
        } else {
            // Lo-fi guitar: a short lick of chord tones in the gaps between the keys.
            lickN = 3 + rng.i(3);
            int pos = rng.p(0.5f) ? 2 : 6, cur = rng.i(3);
            const int dir = rng.p(0.6f) ? 1 : -1;
            for (int k = 0; k < lickN; k++) {
                lickPos[k] = pos;
                lick[k] = std::max(0, std::min(6, cur));
                pos += rng.p(0.7f) ? 2 : 3;
                cur += dir * (rng.p(0.75f) ? 1 : 2);
                if (pos > 15) {
                    lickN = k + 1;
                    break;
                }
            }
        }
    }

    void newSection(bool first) {
        sectionNo++;
        sectionsInKey++;
        int lv = sec.intensity;
        if (first) lv = 0;
        else {
            static const float kMove[5] = {0.1f, 0.3f, 0.15f, 0.35f, 0.1f};  // -2..+2
            int prev = lv;
            for (int tries = 0; tries < 8; tries++) {
                lv = std::max(0, std::min(3, prev + rng.pick(kMove) - 2));
                if (prev == 0 && lv == 0) continue;  // a quiet stretch never lasts two sections
                if (prev == 3 && lv == 3 && rng.p(0.5f)) continue;
                break;
            }
        }
        sec.intensity = lv;
        if (!first) {
            if (sectionsInKey >= 3 && rng.p(0.45f)) {
                modulate();
                makeProgression();
            } else if (rng.p(0.45f)) {
                makeProgression();
            }
        }
        // Length: 30–90 s, in whole eight-bar phrases (the intro is one phrase).
        const float barSec = spb * stepSamples * dt;
        int phrases = (int)std::lround(rng.r(36.0f, 80.0f) / (8.0f * barSec));
        const int fewest = (int)std::ceil(30.0f / (8.0f * barSec));
        phrases = std::max(fewest, std::min(lv == 0 ? fewest : 4, phrases));  // quiet stretches stay short
        sec.bars = first ? 8 : 8 * phrases;
        unsigned mask = def->mask[lv];
        for (int l = 0; l < LAYERS; l++)
            if ((def->optional & bit(l)) && (mask & bit(l)) && rng.p(0.25f)) mask &= ~bit(l);
        sec.mask = mask;
        sec.kick = rng.i(4);
        sec.hat = rng.i(3);
        sec.comp = rng.i(5);
        sec.bass = rng.i(4);
        sec.ghost = rng.r(0.05f, 0.3f);
        sec.hat16 = rng.r(0.0f, 0.25f);
        sec.push = rng.p(0.4f);
        sec.lead = true;
        if (first || rng.p(0.6f)) makeMotif();
        makeArp();
        for (int l = 0; l < LAYERS; l++) layerTarget[l] = (mask & bit(l)) ? 1.0f : 0.0f;
    }

    void begin(int m, uint32_t seed) {
        map = m;
        style = m % STYLES;
        def = &kStyles[style];
        rng.s = (seed * 2654435761u) ^ (uint32_t)(m + 1) * 0x9E3779B9u;
        if (!rng.s) rng.s = 1;
        for (int k = 0; k < 4; k++) rng.u();
        nz.s = rng.u() | 1u;
        bpm = std::round(rng.r(def->bpmLo, def->bpmHi));
        swing = rng.r(def->swingLo, def->swingHi);
        spb = style == MOUNTAIN && rng.p(0.45f) ? 12 : 16;  // the mountain sometimes waltzes
        stepSamples = rate * 60.0f / (bpm * 4.0f);
        key = rng.i(12);
        mode = rng.pick(def->modeW);
        leadAnchor = anchorFor(leadCenter());
        for (Voice& v : voices) v.inst = OFF;
        std::fill(layerGain, layerGain + LAYERS, 0.0f);
        voicingN = 0;
        rev.clear();
        rev.set(def->rt60, def->revDampHz, rate);
        echo.clear();
        echo.d = std::min(echo.n - 1, (int)(def->delayBeats * 60.0f / bpm * rate));
        echo.fb = def->delayFb;
        toneL = toneR = Svf();
        dcL = dcR = 0;
        agcMs = 1e-3f;
        agcGain = 1;
        peakEnv = 0;
        duckEnv = duckTgt = 0;
        duckAtk = 1.0f - std::exp(-dt / 0.004f);
        duckRel = std::exp(-dt / (style == SYNTH ? 0.14f : 0.1f));
        sec = Section();
        sectionNo = 0;
        sectionsInKey = 0;
        makeProgression();
        newSection(true);
        step = spb - 1;
        bar = sectionBar = progBar = -1;
        stepLeft = 1;
        phraseOn = false;
        phraseN = 0;
    }

    // ---- Notes ----
    Voice& alloc() {
        Voice* best = &voices[0];
        float bestV = 1e9f;
        for (Voice& v : voices) {
            if (v.inst == OFF) return v;
            float score = v.amp.v + (v.amp.stage == 3 ? 0.0f : 1.0f);  // steal a quiet, released voice
            if (score < bestV) {
                bestV = score;
                best = &v;
            }
        }
        return *best;
    }

    // `as` picks another sound than the layer's own (a rim click on the snare layer, a crash).
    void note(int layer, float midi, float vel, float lenSteps, float pan, float delaySec = 0, int as = OFF) {
        const int inst = as != OFF ? as : kInst[style][layer];
        Voice& v = alloc();
        v = Voice();
        v.ks = &ksMem[(size_t)(&v - voices) * ksSize];
        v.inst = inst;
        v.layer = layer;
        v.freq = mtof(midi);
        v.vel = vel;
        pan = std::max(-1.0f, std::min(1.0f, pan));
        v.panL = std::cos((pan + 1) * kPi * 0.25f);
        v.panR = std::sin((pan + 1) * kPi * 0.25f);
        v.wait = (int)(delaySec * rate);
        v.hold = lenSteps > 0 ? (int)(lenSteps * stepSamples) : -1;
        v.maxAge = rate * 12;
        const float r = (float)rate;
        auto tau = [&](float t) { return std::exp(-dt / t); };
        switch (inst) {
            case EPIANO:
                v.amp.set(0.002f, 1.7f * std::pow(220.0f / v.freq, 0.3f), 0.0f, 0.14f, r);
                v.mod = 0.05f + 0.22f * vel;  // FM index (in cycles): bright attack, mellow tail
                v.modCoef = tau(0.35f);
                v.f2 = v.freq * 7.0f < 9000.0f ? v.freq * 7.0f : 0.0f;  // the tine's ping
                v.aux = 0.08f * vel;
                v.auxCoef = tau(0.05f);
                break;
            case PAD:
                if (style == MOUNTAIN) v.amp.set(1.6f, 1.0f, 1.0f, 2.6f, r);
                else if (style == DESERT) v.amp.set(0.9f, 1.0f, 1.0f, 1.8f, r);
                else v.amp.set(0.5f, 1.0f, 1.0f, 1.1f, r);
                v.cut = style == MOUNTAIN ? 1000.0f : style == DESERT ? 1300.0f : 2100.0f;
                for (float& p : v.ph) p = nz.f();
                break;
            case PLUCK: {
                v.amp.set(0.0005f, 100.0f, 1.0f, 0.2f, r);
                v.freq = std::max(v.freq, 80.0f);
                v.ksDelay = std::min((float)ksSize - 4, r / v.freq - 0.5f);
                const float t60 = style == MOUNTAIN ? 3.2f : style == DESERT ? 2.2f : 1.6f;
                v.ksLoss = std::pow(10.0f, -3.0f / (t60 * v.freq));
                v.maxAge = (int)(t60 * 1.2f * r);
                // Excite the string with a burst of filtered noise (brighter when picked harder).
                std::fill(v.ks, v.ks + ksSize, 0.0f);
                const int n = (int)v.ksDelay + 2;
                float lp = 0, mean = 0, a = 0.15f + 0.35f * vel;
                for (int k = 0; k < n; k++) {
                    lp += (nz.bi() - lp) * a;
                    v.ks[ksSize - 1 - k] = lp;
                    mean += lp;
                }
                mean /= n;
                for (int k = 0; k < n; k++) v.ks[ksSize - 1 - k] -= mean;  // no DC trapped in the loop
                v.ksPos = 0;
                v.ksZ = 0;
                break;
            }
            case SUB:
                if (style == MOUNTAIN) v.amp.set(0.06f, 2.0f, 0.8f, 0.5f, r);
                else v.amp.set(0.006f, 1.5f, 0.7f, 0.08f, r);
                break;
            case SAWBASS:
                v.amp.set(0.002f, 0.4f, 0.75f, 0.05f, r);
                v.mod = 1.0f;
                v.modCoef = tau(0.09f);
                break;
            case VIBES:
                v.amp.set(0.001f, 1.5f, 0.0f, 0.35f, r);
                v.f2 = v.freq * 4.0f < 10000.0f ? v.freq * 4.0f : 0.0f;
                v.aux = 0.3f;
                v.auxCoef = tau(0.12f);
                break;
            case FLUTE:
                v.amp.set(0.08f, 0.6f, 0.85f, 0.3f, r);
                v.aux = 1.0f;
                v.auxCoef = tau(0.06f);
                break;
            case SAWLEAD:
                v.amp.set(0.012f, 0.8f, 0.75f, 0.25f, r);
                v.mod = 1.0f;
                v.modCoef = tau(0.35f);
                for (float& p : v.ph) p = nz.f();
                break;
            case SQARP:
                v.amp.set(0.001f, 0.17f, 0.0f, 0.06f, r);
                v.mod = 1.0f;
                v.modCoef = tau(0.07f);
                break;
            case BELL:
                v.amp.set(0.001f, 2.2f, 0.0f, 1.2f, r);
                v.mod = 1.0f;
                v.modCoef = tau(0.5f);
                break;
            case BD:
                {
                    const bool soft = style != SYNTH;
                    v.amp.set(0.0005f, soft ? 0.2f : 0.3f, 0.0f, 0.05f, r);
                    v.freq = soft ? 52.0f : 47.0f;
                    v.aux = soft ? 80.0f : 120.0f;  // pitch sweep, Hz
                }
                v.mod = 1.0f;
                v.modCoef = tau(0.035f);
                v.f2 = 1.0f;  // click
                v.auxCoef = tau(0.003f);
                duckTgt = 1.0f;
                break;
            case SD:
                v.amp.set(0.0005f, style == LOFI ? 0.09f : 0.16f, 0.0f, 0.05f, r);
                v.aux = 1.0f;
                v.auxCoef = tau(0.05f);
                break;
            case RIM:
                v.amp.set(0.0003f, 0.03f, 0.0f, 0.02f, r);
                break;
            case HH:
                v.amp.set(0.0003f, lenSteps > 2 ? 0.2f : 0.035f, 0.0f, 0.03f, r);
                v.hold = -1;
                break;
            case CRASH:
                v.amp.set(0.0005f, 1.1f, 0.0f, 0.3f, r);
                v.hold = -1;
                break;
            case SHAKER:
                v.amp.set(0.012f, 0.07f, 0.0f, 0.03f, r);
                break;
            case TOM:
                v.amp.set(0.002f, 0.4f, 0.0f, 0.1f, r);
                v.freq = 82.0f;
                v.mod = 1.0f;
                v.modCoef = tau(0.09f);
                break;
        }
    }

    // ---- Per-step sequencing ----
    bool on(int l) const { return layerTarget[l] > 0; }
    float human(float ms) { return nz.f() * ms * 0.001f; }

    void onStep() {
        if (++step >= spb) {
            step = 0;
            bar++;
            progBar++;
            if (++sectionBar >= sec.bars) {
                sectionBar = 0;
                progBar = 0;
                newSection(false);
            }
            kickAlt = (bar & 1) && rng.p(0.35f);
            if (sectionBar % 8 == 0) {
                makePhrase();
                phraseOn = sec.lead && (sectionBar % 16 == 0 || rng.p(style == MOUNTAIN ? 0.4f : 0.6f));
                if (style == MOUNTAIN && sectionBar % 16 == 0 && rng.p(0.3f)) phraseOn = false;
            }
            if (style == SYNTH) padBright = 0.75f + 0.5f * sectionBar / std::max(1, sec.bars);
        }
        const int c = chordAt(progBar);
        const bool chordStart = step == 0 && progBar % barsPerChord == 0;
        const bool lastBarOfChord = (progBar + 1) % barsPerChord == 0;
        const int nextC = chordAt(progBar + 1);
        if (on(CHORD)) chords(c, nextC, chordStart, lastBarOfChord);
        if (on(BASS)) bass(c, nextC, lastBarOfChord && nextC != c);
        drums();
        if (on(ARP)) arp(c);
        if (on(LEAD)) lead(c);
        if (on(EXTRA)) extra(c);
    }

    void chords(int c, int nextC, bool chordStart, bool lastBarOfChord) {
        int notes[6];
        if (style == LOFI) {
            static const int kComp[5][3][2] = {{{0, 14}, {-1, 0}, {-1, 0}},
                                               {{0, 6}, {6, 9}, {-1, 0}},
                                               {{0, 3}, {3, 5}, {10, 5}},
                                               {{0, 7}, {10, 2}, {12, 3}},
                                               {{2, 13}, {-1, 0}, {-1, 0}}};
            const bool pushNow = sec.push && step == 14 && lastBarOfChord && nextC != c;
            if (pushNow) {
                playKeys(nextC, 14, 0.55f);
                pushed = true;
                return;
            }
            if (step == 0 && pushed) {
                pushed = false;
                return;
            }
            if (step >= 14 && sec.push && lastBarOfChord && nextC != c) return;
            for (auto& e : kComp[sec.comp])
                if (e[0] == step) playKeys(c, e[1], step == 0 || e[0] == 2 ? 0.6f : 0.45f);
            return;
        }
        if (!chordStart) return;
        const float len = barsPerChord * spb + 1.0f;
        const int n = style == MOUNTAIN ? voice(c, 50, 74, notes, false) : voice(c, 53, 76, notes, false);
        for (int k = 0; k < n; k++) note(CHORD, notes[k], 0.55f, len, (k - (n - 1) * 0.5f) * 0.35f);
        if (style == MOUNTAIN) note(CHORD, atOrAbove(rootPc(c), 43), 0.4f, len, 0.0f);  // low root under the pad
    }

    void playKeys(int c, int len, float vel) {
        int notes[6];
        const int n = voice(c, 55, 77, notes, true);
        float strum = 0;
        for (int k = 0; k < n; k++) {
            note(CHORD, notes[k], vel * nz.r(0.85f, 1.05f), (float)len, (k - (n - 1) * 0.5f) * 0.3f, strum + human(8));
            strum += nz.r(0.008f, 0.02f);
        }
        if (!on(BASS)) note(CHORD, atOrAbove(rootPc(c), 43), vel * 0.7f, (float)len, 0.0f, human(6));
    }

    void bass(int c, int nextC, bool approach) {
        const int root = atOrAbove(rootPc(c), style == SYNTH ? 33 : 36);
        const int fifth = root + 7;
        auto approachNote = [&]() {
            const int target = atOrAbove(rootPc(nextC), style == SYNTH ? 33 : 36);
            // the scale tone just below the next root (or above, if that's nearer to here)
            for (int idx = -7; idx < 30; idx++)
                if (midiOf(idx) >= target) return midiOf(idx + (target > root ? -1 : 1));
            return target;
        };
        if (style == LOFI) {
            // {step, length, what}: 0 root, 1 fifth, 2 octave, 3 approach
            static const int kPat[4][4][3] = {{{0, 6, 0}, {10, 4, 0}, {14, 2, 3}, {-1, 0, 0}},
                                              {{0, 10, 0}, {12, 4, 1}, {-1, 0, 0}, {-1, 0, 0}},
                                              {{0, 3, 0}, {6, 2, 0}, {10, 4, 1}, {14, 2, 3}},
                                              {{0, 14, 0}, {14, 2, 3}, {-1, 0, 0}, {-1, 0, 0}}};
            for (auto& e : kPat[sec.bass])
                if (e[0] == step) {
                    int m = e[2] == 0 ? root : e[2] == 1 ? fifth : e[2] == 2 ? root + 12 : approach ? approachNote() : fifth;
                    note(BASS, (float)m, step == 0 ? 0.9f : 0.75f, e[1] - 0.3f, 0.0f, human(5));
                }
        } else if (style == DESERT) {
            // Root on one, fifth on three, a walk-up into a new chord.
            if (step == 0) note(BASS, (float)root, 0.85f, 6.0f, 0.0f);
            else if (step == 8) note(BASS, (float)(fifth > 47 ? fifth - 12 : fifth), 0.7f, approach ? 5.0f : 7.0f, 0.0f);
            else if (step == 14 && approach) note(BASS, (float)approachNote(), 0.6f, 1.6f, 0.0f);
        } else if (style == MOUNTAIN) {
            if (step == 0 && progBar % barsPerChord == 0) note(BASS, (float)root, 0.8f, barsPerChord * spb - 1.0f, 0.0f);
            else if (sec.bass >= 2 && step == spb / 2 + (spb == 12 ? 2 : 0) && !approach) note(BASS, (float)fifth - 12 + (fifth - 12 < 33 ? 12 : 0), 0.55f, spb / 2 - 1.0f, 0.0f);
        } else {
            const int kind = sec.bass;
            if (kind == 0 && step % 2 == 0) note(BASS, (float)root, step % 4 == 0 ? 0.9f : 0.75f, 1.6f, 0.0f);
            else if (kind == 1 && step % 2 == 0) note(BASS, (float)(step % 4 == 2 ? root + 12 : root), 0.85f, 1.5f, 0.0f);
            else if (kind == 2 && step % 4 != 1) note(BASS, (float)root, step % 4 == 0 ? 0.9f : 0.7f, 0.8f, 0.0f);
            else if (kind == 3 && step % 2 == 0)
                note(BASS, (float)(approach && step == 14 ? approachNote() : root), step % 4 == 0 ? 0.9f : 0.75f, 1.6f, 0.0f);
        }
    }

    void drums() {
        const int s = step;
        const bool lastBar = sectionBar == sec.bars - 1;
        if (style == LOFI) {
            static const unsigned kKicks[4] = {1u | 1u << 10, 1u | 1u << 7 | 1u << 10, 1u | 1u << 10 | 1u << 13, 1u | 1u << 6 | 1u << 10};
            unsigned k = kKicks[kickAlt ? (sec.kick + 1) % 4 : sec.kick];
            if (lastBar) k &= 0xFFu;  // the groove drops out under the turnaround
            if (on(KICK) && (k >> s & 1)) note(KICK, 60, s == 0 ? 0.95f : 0.78f, 0, 0.0f, human(4));
            if (on(SNARE)) {
                if (s == 4 || s == 12) note(SNARE, 60, nz.r(0.8f, 0.9f), 0, 0.1f, 0.006f + human(6));  // a touch late: lazy
                else if ((s == 7 || s == 9 || s == 15) && rng.p(sec.ghost)) note(SNARE, 60, 0.35f, 0, 0.15f, human(4), RIM);
                else if (lastBar && (s == 14 || s == 15) && rng.p(0.6f)) note(SNARE, 60, 0.3f, 0, 0.15f, human(4), RIM);
            }
            if (on(HAT)) {
                if (s % 2 == 0) {
                    const bool open = s == 14 && (bar & 3) == 3 && sec.hat == 1;
                    note(HAT, 60, (s % 4 == 0 ? 0.55f : 0.4f) * nz.r(0.8f, 1.1f), open ? 4.0f : 0.0f, 0.35f, human(5));
                } else if (rng.p(sec.hat16)) {
                    note(HAT, 60, 0.22f, 0, 0.35f, human(5));
                }
            }
        } else if (style == DESERT) {
            if (on(KICK) && (s == 0 || (s == 8 && !lastBar) || (s == 10 && kickAlt))) note(KICK, 60, s == 0 ? 0.85f : 0.6f, 0, 0.0f);
            if (on(SNARE) && (s == 4 || s == 12)) note(SNARE, 60, nz.r(0.75f, 0.9f), 0, -0.15f, human(4));  // cross-stick
            if (on(HAT) && s % 2 == 0) note(HAT, 60, (s % 4 == 2 ? 0.34f : 0.2f) * nz.r(0.85f, 1.1f), 0, 0.4f, human(6));
        } else if (style == MOUNTAIN) {
            if (on(KICK)) {
                if (s == 0) note(KICK, 60, 0.75f, 0, 0.0f);
                else if (s == 8 && (spb == 16 || rng.p(0.5f))) note(KICK, 60, 0.45f, 0, 0.0f);
            }
            if (on(HAT) && s % 2 == 0) note(HAT, 60, (s % 4 == 2 ? 0.32f : 0.2f) * nz.r(0.85f, 1.1f), 0, 0.4f, human(8));
        } else {
            const bool fill = lastBar && s >= 8;
            if (on(KICK)) {
                bool hit = sec.kick == 1 ? (s == 0 || s == 8 || (s == 10 && kickAlt)) : s % 4 == 0;
                if (fill && s > 8) hit = false;
                if (hit) note(KICK, 60, s == 0 ? 1.0f : 0.9f, 0, 0.0f);
            }
            if (on(SNARE)) {
                if (fill && sectionBar > 0) note(SNARE, 60, 0.3f + 0.6f * (s - 8) / 7.0f, 0, 0.1f);
                else if (s == 4 || s == 12) note(SNARE, 60, 0.9f, 0, 0.1f);
            }
            if (on(HAT)) {
                if (sec.hat == 0) note(HAT, 60, s % 4 == 2 ? 0.5f : s % 2 ? 0.2f : 0.3f, 0, 0.3f);
                else if (sec.hat == 1 && s % 4 == 2) note(HAT, 60, 0.45f, 3.0f, 0.3f);
                else if (sec.hat == 2 && s % 2 == 0) note(HAT, 60, s % 4 == 2 ? 0.5f : 0.3f, 0, 0.3f);
            }
            if (on(KICK) && s == 0 && sectionBar == 0 && sec.intensity >= 2) note(HAT, 60, 0.5f, 0, -0.3f, 0, CRASH);
        }
    }

    void arp(int c) {
        int lad[8];
        if (style == SYNTH) {
            if (sec.intensity == 0 && step % 2) return;  // 8ths in the intro
            ladder(c, 57, lad, 8);
            const int k = (sec.intensity == 0 ? step / 2 : step) % arpLen;
            note(ARP, (float)lad[arpPat[k]], step % 4 == 0 ? 0.85f : 0.65f, 0.9f, step % 2 ? 0.35f : -0.35f);
        } else if (style == MOUNTAIN || style == DESERT) {
            if (step % 2) return;
            ladder(c, 50, lad, 7);
            const int j = (step / 2) % arpLen;
            note(ARP, (float)lad[arpPat[j]], (step % 4 == 0 ? 0.75f : 0.55f) * nz.r(0.85f, 1.05f), 10.0f,
                 -0.4f + 0.12f * arpPat[j], human(10));
        } else {
            if ((bar & 1) == 0) return;  // answers the keys every other bar
            ladder(c, 55, lad, 7);
            for (int k = 0; k < lickN; k++)
                if (lickPos[k] == step) note(ARP, (float)lad[lick[k]], 0.6f * nz.r(0.85f, 1.05f), 3.0f, -0.4f, human(10));
        }
    }

    void lead(int c) {
        if (!phraseOn || !phraseN) return;
        const int pos = (sectionBar % 8) * spb + step;
        while (phraseIdx < phraseN && phrase[phraseIdx].pos < pos) phraseIdx++;
        if (phraseIdx >= phraseN || phrase[phraseIdx].pos != pos) return;
        const MotifNote& m = phrase[phraseIdx++];
        int pcs[5];
        const int n = chordPcs(c, pcs);
        auto isChordTone = [&](int idx) {
            int pc = ((midiOf(idx) % 12) + 12) % 12;
            for (int k = 0; k < n; k++)
                if (pcs[k] == pc) return true;
            return false;
        };
        int idx;
        if (m.contour == 1000) {  // cadence: the root or third nearest the anchor
            const int rp = rootPc(c), tp = pcs[1];
            idx = leadAnchor;
            for (int d = 0; d < 7; d++) {
                int pcA = ((midiOf(leadAnchor - d) % 12) + 12) % 12, pcB = ((midiOf(leadAnchor + d) % 12) + 12) % 12;
                if (pcA == rp || pcA == tp) { idx = leadAnchor - d; break; }
                if (pcB == rp || pcB == tp) { idx = leadAnchor + d; break; }
            }
        } else {
            idx = leadAnchor + m.contour;
            const bool strong = pos % 4 == 0 || m.len >= 4;  // downbeats and held notes
            if (strong && !isChordTone(idx)) {  // strong beats sit on chord tones (not the last note again)
                const int away = idx >= lastLead ? 1 : -1;
                if (isChordTone(idx + away) && idx + away != lastLead) idx += away;
                else if (isChordTone(idx - away) && idx - away != lastLead) idx -= away;
            }
        }
        lastLead = idx;
        float vel = (pos % 4 == 0 ? 0.8f : 0.65f) * nz.r(0.9f, 1.05f);
        note(LEAD, (float)midiOf(idx), vel, m.len - 0.4f, 0.12f, style == LOFI ? human(15) : human(4));
    }

    void extra(int c) {
        // Bells: an occasional high chord tone, scattered across the stereo field.
        if (step % 4 || !rng.p(style == MOUNTAIN ? 0.12f : 0.06f)) return;
        int lad[8];
        ladder(c, 74, lad, 6);
        note(EXTRA, (float)lad[rng.i(6)], nz.r(0.3f, 0.55f), 2.0f, nz.r(-0.8f, 0.8f), human(20));
    }

    // ---- Synthesis ----
    float voiceSample(Voice& v, float wow, float trem, float lfo) {
        if (v.wait > 0) {
            v.wait--;
            return 0.0f;
        }
        if (v.hold > 0 && --v.hold == 0) v.amp.release();
        const float t = v.age * dt;
        if (++v.age > v.maxAge) v.amp.stage = 0;
        const float e = v.amp.next();
        if (v.amp.stage == 0) {
            v.inst = OFF;
            return 0.0f;
        }
        float out = 0;
        switch (v.inst) {
            case EPIANO: {
                const float f = v.freq * wow;
                v.ph[0] = wrap1(v.ph[0] + f * dt);
                v.ph[1] = wrap1(v.ph[1] + f * 1.0007f * dt);
                v.mod *= v.modCoef;
                out = sine(v.ph[0] + (0.06f + v.mod) * sine(v.ph[1]));
                if (v.f2 > 0) {
                    v.ph[2] = wrap1(v.ph[2] + v.f2 * dt);
                    out += sine(v.ph[2]) * v.aux;
                    v.aux *= v.auxCoef;
                }
                out *= 0.8f;
                break;
            }
            case PAD: {
                const float f = v.freq, d0 = f * dt;
                v.ph[0] = wrap1(v.ph[0] + d0 * 1.004f);
                v.ph[1] = wrap1(v.ph[1] + d0 * 0.996f);
                float s = saw(v.ph[0], d0) + saw(v.ph[1], d0);
                if (style == MOUNTAIN || style == DESERT) {
                    v.ph[2] = wrap1(v.ph[2] + 2.0f * d0);
                    s += 0.6f * sine(v.ph[2]);  // airy octave
                } else {
                    v.ph[2] = wrap1(v.ph[2] + d0);
                    s += saw(v.ph[2], d0);
                }
                const float cut = std::min(v.cut * padBright * (1.0f + 0.25f * lfo), 0.2f * rate);
                out = v.f.run(s, tanApprox(kPi * cut * dt), 1.3f) * 0.33f;
                break;
            }
            case PLUCK: {
                const int mask = ksSize - 1;
                float rp = v.ksPos - v.ksDelay;
                int i0 = (int)std::floor(rp);
                const float fr = rp - (float)i0;
                const float a = v.ks[i0 & mask], b = v.ks[(i0 + 1) & mask];
                const float y = a + (b - a) * fr;
                v.ks[v.ksPos & mask] = v.ksLoss * 0.5f * (y + v.ksZ);
                v.ksZ = y;
                v.ksPos++;
                v.z += (y - v.z) * 0.4f;  // soften the pick
                out = v.z * 1.2f;
                break;
            }
            case SUB: {
                v.ph[0] = wrap1(v.ph[0] + v.freq * dt);
                const float s = sine(v.ph[0]);
                out = s * (1.5f - 0.5f * s * s);  // a little warmth so small speakers hear it
                break;
            }
            case SAWBASS: {
                const float d0 = v.freq * dt;
                v.ph[0] = wrap1(v.ph[0] + d0);
                v.ph[1] = wrap1(v.ph[1] + d0 * 0.5f);
                const float s = saw(v.ph[0], d0) + 0.5f * pulse(v.ph[1], d0 * 0.5f, 0.5f);
                v.mod *= v.modCoef;
                const float cut = std::min(150.0f + v.freq * 1.5f + 1700.0f * v.mod * v.vel, 0.2f * rate);
                out = v.f.run(s, tanApprox(kPi * cut * dt), 0.8f) * 0.45f;
                break;
            }
            case VIBES: {
                v.ph[0] = wrap1(v.ph[0] + v.freq * wow * dt);
                out = sine(v.ph[0]);
                if (v.f2 > 0) {
                    v.ph[1] = wrap1(v.ph[1] + v.f2 * dt);
                    out += sine(v.ph[1]) * v.aux;
                    v.aux *= v.auxCoef;
                }
                out *= 1.0f - 0.2f * trem;
                break;
            }
            case FLUTE: {
                const float vib = 1.0f + 0.004f * std::min(1.0f, std::max(0.0f, (t - 0.25f) / 0.5f)) * sine(v.ph[2]);
                v.ph[2] = wrap1(v.ph[2] + 5.0f * dt);
                v.ph[0] = wrap1(v.ph[0] + v.freq * vib * dt);
                const float p = v.ph[0];
                const float tone = sine(p) + 0.22f * sine(2.0f * p) + 0.06f * sine(3.0f * p);
                const float cut = std::min(v.freq * 2.0f, 0.2f * rate);
                v.f.run(nz.bi(), tanApprox(kPi * cut * dt), 1.0f);
                v.aux *= v.auxCoef;
                out = tone * 0.8f + v.f.band * (0.10f + 0.3f * v.aux);
                break;
            }
            case SAWLEAD: {
                const float vib = 1.0f + 0.005f * std::min(1.0f, std::max(0.0f, (t - 0.3f) / 0.5f)) * sine(v.ph[2]);
                v.ph[2] = wrap1(v.ph[2] + 5.5f * dt);
                const float d0 = v.freq * vib * dt;
                v.ph[0] = wrap1(v.ph[0] + d0 * 1.003f);
                v.ph[1] = wrap1(v.ph[1] + d0 * 0.997f);
                const float s = saw(v.ph[0], d0) + saw(v.ph[1], d0);
                v.mod *= v.modCoef;
                const float cut = std::min(1500.0f + 2200.0f * v.mod, 0.2f * rate);
                out = v.f.run(s, tanApprox(kPi * cut * dt), 1.0f) * 0.4f;
                break;
            }
            case SQARP: {
                const float d0 = v.freq * dt;
                v.ph[0] = wrap1(v.ph[0] + d0);
                v.mod *= v.modCoef;
                const float cut = std::min(500.0f + 3200.0f * v.mod * v.vel, 0.2f * rate);
                out = v.f.run(pulse(v.ph[0], d0, 0.35f), tanApprox(kPi * cut * dt), 1.2f) * 0.5f;
                break;
            }
            case BELL: {
                v.ph[0] = wrap1(v.ph[0] + v.freq * dt);
                v.ph[1] = wrap1(v.ph[1] + v.freq * 3.5f * dt);
                v.mod *= v.modCoef;
                out = sine(v.ph[0] + (0.04f + 0.22f * v.mod) * sine(v.ph[1]));
                break;
            }
            case BD: {
                v.mod *= v.modCoef;
                v.ph[0] = wrap1(v.ph[0] + (v.freq + v.aux * v.mod) * dt);
                v.f2 *= v.auxCoef;
                v.z += (nz.bi() * v.f2 - v.z) * 0.3f;
                out = sine(v.ph[0]) + v.z * (style == SYNTH ? 0.3f : 0.15f);
                break;
            }
            case SD: {
                v.ph[0] = wrap1(v.ph[0] + 185.0f * dt);
                v.ph[1] = wrap1(v.ph[1] + 330.0f * dt);
                const float tone = (sine(v.ph[0]) + 0.5f * sine(v.ph[1])) * v.aux;
                v.aux *= v.auxCoef;
                const float cut = style == LOFI ? 1700.0f : 2400.0f;
                v.f.run(nz.bi(), tanApprox(kPi * cut * dt), style == LOFI ? 1.0f : 0.7f);
                out = tone * 0.5f + v.f.band * 1.2f;
                break;
            }
            case RIM: {
                v.ph[0] = wrap1(v.ph[0] + 820.0f * dt);
                v.f.run(nz.bi(), tanApprox(kPi * 2500.0f * dt), 1.0f);
                out = sine(v.ph[0]) * 0.5f + v.f.band;
                break;
            }
            case HH:
            case CRASH: {
                const float x = nz.bi();
                const float lp = v.f.run(x, tanApprox(kPi * (v.inst == CRASH ? 4500.0f : 7000.0f) * dt), 1.0f);
                float hp = x - lp - v.f.band;
                // Lo-fi: dull, like it came off a record. Synthwave: just the fizz off the top.
                v.z += (hp - v.z) * (style == LOFI ? 0.6f : 0.8f);
                hp = v.z;
                out = hp;
                break;
            }
            case SHAKER: {
                v.f.run(nz.bi(), tanApprox(kPi * 5500.0f * dt), 1.2f);
                out = v.f.band;
                break;
            }
            case TOM: {
                v.mod *= v.modCoef;
                v.ph[0] = wrap1(v.ph[0] + (v.freq + 30.0f * v.mod) * dt);
                v.z += (nz.bi() - v.z) * 0.04f;
                out = sine(v.ph[0]) + v.z * v.mod * 0.8f;
                break;
            }
        }
        return out * e * v.vel;
    }

    void render(float* out, int n) {
        const float lfoInc = 0.07f * dt, wowInc = 0.45f * dt, tremInc = 4.5f * dt;
        const float gainA = 1.0f - std::exp(-dt / 1.2f);
        const float agcA = 1.0f - std::exp(-dt / 2.0f), agcUp = 1.0f - std::exp(-dt / 3.0f), agcDown = 1.0f - std::exp(-dt / 0.7f);
        const float peakRel = std::exp(-dt / 0.15f);
        const float toneG = def->toneHz > 0 ? tanApprox(kPi * def->toneHz * dt) : 0.0f;
        const float wet = def->revWet;
        for (int i = 0; i < n; i++) {
            if (--stepLeft <= 0) {
                onStep();
                stepLeft += 2.0f * stepSamples * ((step & 1) ? 1.0f - swing : swing);
            }
            lfoPh = wrap1(lfoPh + lfoInc);
            wowPh = wrap1(wowPh + wowInc);
            tremPh = wrap1(tremPh + tremInc);
            const float lfo = sine(lfoPh);
            // Lo-fi tape wow: a slow few-cent drift on the keys and vibes.
            const float wow = style == LOFI ? 1.0f + 0.0022f * sine(wowPh) + 0.0008f * sine(wowPh * 3.0f) : 1.0f;
            const float trem = 0.5f + 0.5f * sine(tremPh);
            panPh = wrap1(panPh + 2.2f * dt);
            const float pan = sine(panPh);
            duckTgt *= duckRel;
            duckEnv += (duckTgt - duckEnv) * duckAtk;

            float lL[LAYERS] = {}, lR[LAYERS] = {};
            for (Voice& v : voices) {
                if (v.inst == OFF) continue;
                const float s = voiceSample(v, wow, trem, lfo);
                lL[v.layer] += s * v.panL;
                lR[v.layer] += s * v.panR;
            }
            float L = 0, R = 0, revIn = 0, dlyIn = 0;
            for (int l = 0; l < LAYERS; l++) {
                layerGain[l] += (layerTarget[l] - layerGain[l]) * gainA;
                const float g = layerGain[l] * def->vol[l] * (1.0f - def->duck[l] * duckEnv);
                float a = lL[l] * g, b = lR[l] * g;
                if (style == LOFI && l == CHORD) {  // a gentle stereo tremolo, like a Rhodes
                    a *= 1.0f + 0.25f * pan;
                    b *= 1.0f - 0.25f * pan;
                }
                L += a;
                R += b;
                revIn += (a + b) * def->rev[l];
                dlyIn += (a + b) * def->dly[l];
            }
            float rl, rr, el, er;
            rev.run(revIn * 0.5f, rl, rr);
            echo.run(dlyIn * 0.5f, el, er);
            L += rl * wet + el * 0.6f;
            R += rr * wet + er * 0.6f;
            if (toneG > 0) {
                L = toneL.run(L, toneG, 0.9f);
                R = toneR.run(R, toneG, 0.9f);
            }
            // DC blocker (~10 Hz) and a slow leveler, so sparse and full sections sit at a
            // similar loudness (within -4/+3 dB). It backs off faster than it pushes up, so a
            // section that comes in full doesn't blare.
            dcL += (L - dcL) * 0.0013f;
            dcR += (R - dcR) * 0.0013f;
            L -= dcL;
            R -= dcR;
            agcMs += ((L * L + R * R) * 0.5f - agcMs) * agcA;
            const float want = std::min(1.4f, std::max(0.63f, 0.12f / std::sqrt(agcMs + 1e-7f)));
            agcGain += (want - agcGain) * (want < agcGain ? agcDown : agcUp);
            L *= agcGain;
            R *= agcGain;
            // Peak limiter: holds peaks under -3 dBFS so a kick on a full bar never clips.
            const float pk = std::max(std::fabs(L), std::fabs(R));
            peakEnv = pk > peakEnv ? pk : peakEnv * peakRel + pk * (1.0f - peakRel);
            const float lim = peakEnv > 0.7f ? 0.7f / peakEnv : 1.0f;
            out[2 * i] = L * lim;
            out[2 * i + 1] = R * lim;
        }
    }

    std::string status() const {
        char buf[256];
        std::string layers;
        for (int l = 0; l < LAYERS; l++)
            if (sec.mask & bit(l)) layers += std::string(layers.empty() ? "" : "+") + kLayerNames[l];
        std::string progs;
        static const char* kRoman[7] = {"I", "II", "III", "IV", "V", "VI", "VII"};
        for (int k = 0; k < progLen; k++) progs += std::string(k ? "-" : "") + kRoman[prog[k]];
        std::snprintf(buf, sizeof buf, "%s %s, %.0f bpm %s, section %d (bar %d/%d, level %d): %s | %s x%d bar", kKeyNames[key],
                      kModeNames[mode], bpm, spb == 12 ? "3/4" : "4/4", sectionNo, sectionBar + 1, sec.bars, sec.intensity,
                      layers.c_str(), progs.c_str(), barsPerChord);
        return buf;
    }
};

// ---- Music ------------------------------------------------------------------------------

Music::Music() : cur_(new Song), old_(new Song) {
    seed_ = (uint32_t)std::chrono::high_resolution_clock::now().time_since_epoch().count();
    setRate(48000);
}

Music::~Music() = default;

void Music::setRate(int rate) {
    rate_ = rate;
    cur_->init(rate);
    old_->init(rate);
    playing_ = false;
}

std::string Music::status() const { return playing_ ? cur_->status() : "off"; }

void Music::render(float* out, int frames, int map, float volume) {
    volume = std::min(1.0f, std::max(0.0f, volume));
    if (volume <= 0.0f && vol_ < 1e-4f) {  // off: no work at all
        vol_ = 0;
        return;
    }
    if (map < 0) return;
    if (!playing_ || (map != cur_->map && vol_ < 1e-3f)) {
        cur_->begin(map, seed_++);
        xfade_ = 1;
        playing_ = true;
    } else if (map != cur_->map && xfade_ >= 1) {
        std::swap(cur_, old_);  // the old piece fades out while the new one starts
        cur_->begin(map, seed_++);
        xfade_ = 0;
    }
    const float xfadeStep = 1.0f / (4.0f * rate_);  // 4 s equal-power crossfade
    const float volA = 1.0f - std::exp(-1.0f / (0.25f * rate_));
    float buf[2 * 64];
    for (int i0 = 0; i0 < frames; i0 += 64) {
        const int n = std::min(64, frames - i0);
        const float v0 = vol_, x0 = xfade_;
        for (int k = 0; k < n; k++) vol_ += (volume - vol_) * volA;
        const float x1 = std::min(1.0f, xfade_ + n * xfadeStep);
        cur_->render(buf, n);
        for (int k = 0; k < n; k++) {
            const float u = (k + 1.0f) / n;
            const float g = (v0 + (vol_ - v0) * u) * std::sin((x0 + (x1 - x0) * u) * kPi * 0.5f);
            out[2 * (i0 + k)] += buf[2 * k] * g;
            out[2 * (i0 + k) + 1] += buf[2 * k + 1] * g;
        }
        if (x0 < 1.0f) {
            old_->render(buf, n);
            for (int k = 0; k < n; k++) {
                const float u = (k + 1.0f) / n;
                const float g = (v0 + (vol_ - v0) * u) * std::cos((x0 + (x1 - x0) * u) * kPi * 0.5f);
                out[2 * (i0 + k)] += buf[2 * k] * g;
                out[2 * (i0 + k) + 1] += buf[2 * k + 1] * g;
            }
        }
        xfade_ = x1;
    }
}

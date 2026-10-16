// Procedural background music: one generative piece per map, synthesized at runtime (no
// samples, no recorded melodies). Proving ground: mellow lo-fi. Mountain: airy pads and
// plucked arpeggios. Freeway: synthwave pulse. Backcountry: warm keys and picked strings.
// Beyond the map's own piece, each biome (biome.h) has one: dry airy dunes, woody pines, a
// tense echoing canyon, a flowing river and sparse bright alpine bells. It runs in the audio
// callback, under the engine sounds.
#pragma once
#include <cstdint>
#include <memory>
#include <string>

class Music {
public:
    Music();
    ~Music();
    // Sizes the buffers for this sample rate (allocates). Call before playback starts.
    void setRate(int rate);
    void setSeed(uint32_t seed) { seed_ = seed; }
    // Audio thread: adds `frames` interleaved stereo samples of `map`'s piece into `out`,
    // scaled by `volume`. At volume 0 nothing is rendered (no CPU). A change of map or biome
    // crossfades (the caller holds the biome steady: BiomeTracker).
    void render(float* out, int frames, int map, float volume, int biome = 0);
    // The piece a map plays in a biome (the map's own at biome 0), and which one is sounding (-1 off).
    static int pieceFor(int map, int biome);
    int piece() const;
    // What's playing (key, tempo, section, layers), for the offline review tool.
    std::string status() const;

private:
    struct Song;
    std::unique_ptr<Song> cur_, old_;  // old_ only sounds while it crossfades out
    uint32_t seed_;
    int rate_ = 48000;
    float vol_ = 0;     // smoothed volume
    float xfade_ = 1;   // crossfade progress, old_ -> cur_
    float xfadeLen_ = 4;  // seconds
    bool playing_ = false;
};

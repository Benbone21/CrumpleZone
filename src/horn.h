// The horn: pitch per vehicle, and how a held horn travels in multiplayer.
//
// Multiplayer sends no new message type (so net::kVersion stays): the player says "horn 1" when the
// key goes down and "horn 0" when it comes up, through the race-chatter channel (NetSession::sendGame,
// reliable, relayed by the host to the others), plus "horn 1" again every kHornRefresh seconds while
// held. A listener treats a horn as off kHornExpiry seconds after the last "horn 1" (a guest who
// vanished mid-honk). An older build just ignores the text.
#pragma once
#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <string>

namespace horn {

constexpr double kRefresh = 2.0, kExpiry = 5.0;
constexpr int kPlayers = 8;

// Small light cars honk higher, big heavy trucks lower: 1 at 1600 kg, about 1.2 at 700 kg, 0.9 at 3000 kg.
inline float pitchFor(double massKg) {
    if (!std::isfinite(massKg) || massKg < 100) return 1.0f;
    return (float)std::clamp(std::pow(1600.0 / massKg, 0.22), 0.85, 1.3);
}

inline std::string message(bool on) { return on ? "horn 1" : "horn 0"; }
inline bool parse(const std::string& text, bool& on) {
    if (text != "horn 1" && text != "horn 0") return false;
    on = text[5] == '1';
    return true;
}

// What this machine sends for its own horn: call once a frame with the key state.
struct Sender {
    bool on = false;
    double last = 0;
    // Returns true when a message is due (then `message(on)` is what to send).
    bool update(bool held, double now) {
        if (held != on) return on = held, last = now, true;
        if (on && now - last >= kRefresh) return last = now, true;
        return false;
    }
};

// Other players' horns, by player number.
struct Remote {
    bool on[kPlayers] = {};
    double heard[kPlayers] = {};
    void hear(int player, bool state, double now) {
        if (player < 0 || player >= kPlayers) return;
        on[player] = state, heard[player] = now;
    }
    bool sounding(int player, double now) const {
        return player >= 0 && player < kPlayers && on[player] && now - heard[player] < kExpiry;
    }
};

}  // namespace horn

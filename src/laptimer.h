// The proving ground's lap timer: a start/finish line on the loop road. Crossing it forward starts a lap; the loop has
// checkpoints in order round it, and crossing the line again completes the lap only if every checkpoint was passed in order
// (so driving back and forth over the line, or cutting across the middle, never counts). Leaving the road voids the lap.
// The best lap is kept per vehicle in configDir()/laps.txt. No GL, no Sim: the HUD feeds in the car's position.
#pragma once
#include <map>
#include <string>

#include "world.h"

class LapTimer {
public:
    static constexpr int kChecks = 8;         // checkpoints between one crossing of the line and the next
    static constexpr double kRoadReach = 45;  // farther than this from the loop road and the lap is void (m)
    enum Event { NONE, STARTED, LAP, BEST, VOID };

    bool running = false;
    double lap = 0, last = 0;  // the lap running (s), the last one finished
    int next = 0;              // checkpoints passed so far this lap
    int laps = 0;              // laps completed this session
    Event event = NONE;        // what the last update did (the HUD reads it, then clears it)

    // The loop to time (the world's loop road); resets the lap. The line is at lineIndex(w).
    void setLoop(const World& w);
    // A different vehicle: its best lap (from the table) is the one shown.
    void setVehicle(const std::string& name);
    double best() const;                         // this vehicle's best (0: none yet)
    void update(const V3& pos, double dt);       // every frame, with the player's position
    void reset() { running = false, next = 0, havePrev = false; }

    // The best laps by vehicle name.
    std::map<std::string, double> bests;
    std::string savePath;  // where a new best is written at once ("": nowhere; the game sets defaultPath())
    bool load(const std::string& path);
    bool save(const std::string& path) const;
    static std::string defaultPath();            // configDir()/laps.txt

    // Where the line stands on a world's loop road: the sample, and its position and heading (the game's yaw).
    static size_t lineIndex(const World& w);
    static void linePose(const World& w, V3& p, double& yaw);

private:
    const World* world = nullptr;
    double L = 0, s0 = 0, prevS = 0;
    bool havePrev = false;
    std::string vehicle;
    double wrap(double d) const;
};

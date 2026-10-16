// Career: you start with the factory vehicles and earn the rest. Parts are found in supply
// caches out on the trails, won (cash for jobs and medals, a part for a level's first gold)
// and bought at the repair stations. The garage only fits what you own, and only at a
// station. Sandbox (the default) has everything, everywhere.
//
// A part is owned once and fits any vehicle that offers it (mods.h: modPartKey). Progress is
// each player's own (~/.config/crumplezone/career.txt) and comes with them into anyone's
// session; a cache opened in a session counts for everyone there.
#pragma once
#include <set>
#include <string>
#include <vector>

#include "mathx.h"

class World;

// A supply cache: a crate somewhere worth the trip, with a part or some cash in it.
struct CacheSpot {
    std::string id;    // "WALL TOP": unique on its map
    int map = 0;
    V3 at;             // on the ground (y from the map)
    std::string part;  // part key (mods.h), or "" for cash
    int cash = 0;
};

// The caches on a map, on the ground. (None on the proving ground or the freeway.)
std::vector<CacheSpot> cacheSpots(const World& world);

struct Career {
    bool on = false;  // career mode (else sandbox: everything owned)
    int cash = 0;
    std::set<std::string> owned;    // part keys
    std::set<std::string> opened;   // caches: "MAP:ID"
    std::set<std::string> claimed;  // medals paid: "LEVEL:NAME:MEDAL"

    static constexpr int kStartCash = 1000, kJobPay = 1200;

    // Whether option `option` of `vehicle`'s `slot` can be fitted (a vehicle's factory part
    // always can; in sandbox everything can).
    bool owns(int vehicle, int slot, int option) const;
    // 1 everyday (shops), 2 serious kit (shops, dear; caches; rewards), 3 the best (caches and
    // gold medals only).
    static int tier(int vehicle, int slot, int option);
    // What a station charges for it (0: not for sale).
    static int price(int vehicle, int slot, int option);
    // Buys it: false (and why) if not for sale, owned already, or short of cash.
    bool buy(int vehicle, int slot, int option, std::string& why);
    // Adds a part; returns what it's called.
    std::string grant(const std::string& key);

    bool isOpened(const CacheSpot& c) const { return opened.count(std::to_string(c.map) + ":" + c.id) > 0; }
    // Opens it (once): its part or cash is yours. Returns what to announce ("" if opened already).
    std::string openCache(const CacheSpot& c);
    // A job done: pays. Returns what to announce.
    std::string jobDone();
    // A level finished with a medal (0 none, 1 bronze, 2 silver, 3 gold): pays for each medal
    // the first time; a level's first gold also gives one of the best parts. Returns what to announce.
    std::string medal(const std::string& level, int medal);

    void startFresh();
    bool load(const std::string& path);
    bool save(const std::string& path) const;
    static std::string defaultPath();  // ~/.config/crumplezone/career.txt
};

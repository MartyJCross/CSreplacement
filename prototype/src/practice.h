// Grenade practice (v0.35): your last throw (to throw it again) and saved lineups (lineups.txt next to the game).
#pragma once
#include <string>
#include <vector>
#include "vecmath.h"

// A throw, all of it: where you stood and looked, which grenade and how hard (1 full, 0.6 both buttons, 0.3 the lob),
// whether you were in the air (a jump throw), and the grenade's own start and velocity (a rethrow flies exactly this).
struct GrenadeThrow {
    Vec3 stand;
    float yaw = 0, pitch = 0;
    int type = 0;  // 0 smoke, 1 flash, 2 HE, 3 molotov
    float strength = 1.0f;
    bool air = false;
    Vec3 start, vel;
};

// A saved throw on a map (0 the Lab, 1 Dust, 2 Harbor, 3 Inferno), with its name ("SMOKE: OUTSIDE LONG > CT CROSS").
struct Lineup {
    int map = 0;
    std::string name;
    GrenadeThrow t;
};

struct LineupBook {
    std::vector<Lineup> all;
    bool load(const std::string& path);        // (a missing file: none yet)
    bool save(const std::string& path) const;
    std::vector<int> onMap(int map) const;     // indices into `all`, in the order they were saved
};

const char* grenadeName(int type);  // "SMOKE", "FLASH", "HE", "MOLOTOV"
// "SMOKE: T SPAWN > CT CROSS" (where you stood > where it went off; either may be empty on the Lab).
std::string lineupName(int type, const std::string& from, const std::string& to);
// "FULL THROW", "LOB (MOUSE 2)", "MEDIUM (BOTH BUTTONS)", plus " + JUMP" for a jump throw.
std::string throwKind(const GrenadeThrow& t);

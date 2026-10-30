// Key bindings (v0.41): which keys, mouse buttons or wheel directions do each game action. Codes are SDL's
// scancodes (0..511: USB HID usage numbers, the same on every keyboard layout's physical key), 512 + n for mouse
// button n (1 left, 2 middle, 3 right, 4 and 5 the side buttons) and kWheelUp / kWheelDown. No SDL here: the sim
// lib reads and writes them (config.cpp) and the tests check them; main.cpp asks `binds.has(action, code)`.
#pragma once
#include <string>

enum BindAction {
    kBindForward, kBindBack, kBindLeft, kBindRight, kBindJump, kBindCrouch, kBindWalk,
    kBindFire, kBindAltFire, kBindReload, kBindUse,
    kBindPrimary, kBindSecondary, kBindKnife, kBindGrenade, kBindLastWeapon, kBindInspect, kBindDrop,
    kBindBuy, kBindScores, kBindCall, kBindPing, kBindNoclip, kBindClearDecals,
    kBindCount
};

constexpr int kBindSlots = 3;  // keys per action
constexpr int kMouseCode = 512, kWheelUp = 520, kWheelDown = 521, kNoKey = -1;

struct BindInfo {
    const char* key;    // the config name after "bind_" (forward, jump...)
    const char* label;  // the Controls page's row
};
const BindInfo& bindInfo(int action);

struct Binds {
    int keys[kBindCount][kBindSlots];
    Binds();  // the defaults: CS's keys
    bool has(int action, int code) const;
    // Binds `code` to `action`: as its main key (`replace`: the first slot; the others stay, so jump keeps its wheel)
    // or added in a free slot. It's taken off every other action first (one key, one action).
    void set(int action, int code, bool replace);
    void clear(int action);
    // "W", "SPACE MWHEELDOWN", "" (nothing): the action's keys by name, and back. False if a name is unknown.
    std::string text(int action) const;
    bool parse(int action, const std::string& names);
};

// A code's name ("W", "SPACE", "MOUSE1", "MWHEELUP", "LCTRL"), "" for none; kNoKey for an unknown name.
std::string keyName(int code);
int keyCode(const std::string& name);

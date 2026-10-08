// Skins, knives, cases and your inventory. Pure data and dice: the game draws the skins (fx.cpp + the box
// shader's painted surface) and runs the menus; the sim tests check the odds and the save file.
#pragma once
#include <cstdint>
#include <string>
#include <vector>

// Rarity, like CS cases: blue, purple, pink, red, and the rare gold (a knife).
enum Rarity { kMilSpec, kRestricted, kClassified, kCovert, kRareSpecial, kRarities };
const char* rarityName(int rarity);
uint32_t rarityColor(int rarity);  // 0xRRGGBB
// Odds out of 10000 (79.92%, 15.98%, 3.2%, 0.64%, 0.26%).
int rarityOdds(int rarity);

// Paint patterns, drawn per pixel by the box shader in the weapon's own space.
enum Pattern {
    kPatSolid, kPatFade, kPatCamo, kPatTiger, kPatMarble, kPatHardened, kPatWeb, kPatHazard, kPatFlames, kPatCarbon,
    kPatNeon, kPatTwoTone, kPatterns
};

// Knives: the default one everybody has, and the six from cases.
enum KnifeType { kKnifeDefault, kKnifeButterfly, kKnifeKarambit, kKnifeM9, kKnifeTalon, kKnifeBowie, kKnifeKukri, kKnifeTypes };
const char* knifeName(int type);

struct SkinDef {
    std::string key;   // stable id in the save file
    std::string name;  // shown, e.g. "DEAGLE | BLAZE" or "KARAMBIT | FADE"
    int weapon;        // WeaponId (kWKnife = 2 for knives)
    int knife;         // knives: KnifeType; guns: -1
    int rarity;
    int pattern;
    uint32_t a, b, c;  // the pattern's colours
    float gloss;       // 0 matte .. 1 polished
};
const std::vector<SkinDef>& allSkins();
int findSkin(const std::string& key);  // -1 if unknown

// Wear 0..1 (the "float"): FACTORY NEW < 0.07 < MINIMAL WEAR < 0.15 < FIELD-TESTED < 0.38 < WELL-WORN < 0.45
// < BATTLE-SCARRED. More wear, more scratches.
const char* wearName(float wear);
const char* wearShort(float wear);  // FN, MW, FT, WW, BS

struct Item {
    int skin = -1;
    float wear = 0;
};

// What you hold: a skin and wear per weapon (skin -1 = plain).
struct Equipped {
    int skin = -1;
    float wear = 0;
};

constexpr int kSlots = 9;  // one per WeaponId

struct Inventory {
    std::vector<Item> items;
    int cases = 0;
    int progress = 0;          // kills towards the next case
    Equipped equip[kSlots];    // by WeaponId; knives sit in kWKnife's slot
    // A kill (in a mode that counts): true if it just earned a case.
    bool addKill(int killsPerCase);
    // Opens a case: the item rolled (also added to items), or -1 with no case left.
    int open(uint32_t& rng);
    bool save(const std::string& path) const;
    bool load(const std::string& path);  // false if there's no file (a fresh inventory)
};

// One roll of the case: rarity by the odds, then a skin of that rarity, then its wear.
Item rollCase(uint32_t& rng);
float rollUnit(uint32_t& rng);  // 0..1, xorshift

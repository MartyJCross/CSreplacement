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

// Paint patterns, drawn per pixel by the box shader in the weapon's own space (render.cpp skin()).
enum Pattern {
    kPatSolid, kPatFade, kPatCamo, kPatTiger, kPatMarble, kPatHardened, kPatWeb, kPatHazard, kPatFlames, kPatCarbon,
    kPatNeon, kPatTwoTone,
    kPatPrint,      // pearl under sweeping ribbons of fine ink lines, an ink wave from below (a printed look)
    kPatLotus,      // flowers outlined in gold on a leafy ground
    kPatHalftone,   // dots growing along the gun
    kPatFacets,     // crystal shards with bright edges
    kPatWaves,      // overlapping fans of arcs
    kPatHolo,       // shimmers through the rainbow as it turns
    kPatDecals,     // lines of tiny text, bar codes, tags
    kPatDamascus,   // folded steel
    kPatGalaxy,     // a swirling nebula with stars
    kPatScales,     // rows of overlapping scales
    kPatSplatter,   // thrown paint and drips
    kPatStripes,    // racing stripes
    kPatGradient,   // bottom-to-top colour, with a flake
    kPatBlocks,     // bold mecha panels on the slant
    kPatterns
};

// The parts of a weapon a skin paints separately (fx.cpp tags every part of every gun; knives: blade, handle).
enum SkinRegion { kRegBody, kRegTop, kRegBarrel, kRegMuzzle, kRegFore, kRegStock, kRegGrip, kRegMag, kRegScope, kRegAccent,
                  kRegBlade, kRegHandle, kSkinRegions };
// One paint: a pattern, its three colours, its scale (1 = as designed; smaller = finer), how polished (0..1).
struct SkinLayer {
    int pattern = kPatSolid;
    uint32_t a = 0x808080, b = 0x404040, c = 0x202020;
    float gloss = 0.3f, scale = 1.0f;
};
constexpr int kSkinLayers = 4;
constexpr uint8_t kRegionPlain = 255;  // a region the skin leaves in the weapon's own colours

// Knives: the default one everybody has, and the six from cases.
enum KnifeType { kKnifeDefault, kKnifeButterfly, kKnifeKarambit, kKnifeM9, kKnifeTalon, kKnifeBowie, kKnifeKukri, kKnifeTypes };
const char* knifeName(int type);

// A skin: up to kSkinLayers paints and which one each region of the weapon gets (barrel, mag, stock...: kRegionPlain
// leaves it in its own colours), so a design can be a print on the body, a gloss-black barrel and a decal-covered mag.
struct SkinDef {
    std::string key;   // stable id in the save file
    std::string name;  // shown, e.g. "DEAGLE | BLAZE" or "KARAMBIT | FADE"
    int weapon;        // WeaponId (kWKnife = 2 for knives)
    int knife;         // knives: KnifeType; guns: -1
    int rarity;
    SkinLayer layers[kSkinLayers];
    uint8_t regions[kSkinRegions];
};
const std::vector<SkinDef>& allSkins();
int findSkin(const std::string& key);  // -1 if unknown

// Wear 0..1 (the "float"): FACTORY NEW < 0.07 < MINIMAL WEAR < 0.15 < FIELD-TESTED < 0.38 < WELL-WORN < 0.45
// < BATTLE-SCARRED. Since v0.28 every skin is factory new (owner): the float only offsets the pattern, so two of a
// skin rarely look exactly alike; cases roll it under 0.07.
const char* wearName(float wear);
const char* wearShort(float wear);  // FN, MW, FT, WW, BS

struct Item {
    int skin = -1;
    float wear = 0;
    bool st = false;  // StatTrak: counts your kills with it (shown on the gun)
    int kills = 0;
};

// What you hold: an item per weapon (skin -1 = plain).
using Equipped = Item;

constexpr float kStatTrakOdds = 0.1f;  // a case's gun or knife is StatTrak one time in ten, like CS

constexpr int kSlots = 14;  // one per WeaponId (items.cpp checks it against kWeaponCount)

constexpr int kKillsPerKnifeCase = 100;  // a knife case (knives only) every 100 kills (owner, v0.27)

struct Inventory {
    std::vector<Item> items;
    int cases = 0;
    int progress = 0;          // kills towards the next case
    int knifeCases = 0;        // knife cases (every kKillsPerKnifeCase kills): a knife, any type, any finish
    int knifeProgress = 0;     // kills towards the next knife case
    Equipped equip[kSlots];    // by WeaponId; knives sit in kWKnife's slot
    // A kill (in a mode that counts): true if it just earned a case.
    bool addKill(int killsPerCase);
    // Opens a case: the item rolled (also added to items), or -1 with no case left.
    int open(uint32_t& rng);
    // A kill towards a knife case: true if it just earned one.
    bool addKnifeKill(int killsPerKnifeCase = kKillsPerKnifeCase);
    // Opens a knife case: the knife rolled (also added to items), or -1 with none left.
    int openKnife(uint32_t& rng);
    bool save(const std::string& path) const;
    bool load(const std::string& path);  // false if there's no file (a fresh inventory)
};

// One roll of the case: rarity by the odds, then a skin of that rarity, then its wear.
Item rollCase(uint32_t& rng);
// One roll of a knife case: any knife skin, evenly, then its wear.
Item rollKnifeCase(uint32_t& rng);
float rollUnit(uint32_t& rng);  // 0..1, xorshift

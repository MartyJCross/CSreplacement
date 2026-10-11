#include "items.h"
#include <algorithm>
#include <cstdio>
#include <cstring>
#include <fstream>
#include <sstream>
#include "combat.h"

const char* rarityName(int r) {
    static const char* const kNames[kRarities] = {"MIL-SPEC", "RESTRICTED", "CLASSIFIED", "COVERT", "RARE SPECIAL"};
    return kNames[std::clamp(r, 0, kRarities - 1)];
}

uint32_t rarityColor(int r) {
    static const uint32_t kColors[kRarities] = {0x4b69ff, 0x8847ff, 0xd32ce6, 0xeb4b4b, 0xe4ae39};
    return kColors[std::clamp(r, 0, kRarities - 1)];
}

int rarityOdds(int r) {
    static const int kOdds[kRarities] = {7992, 1598, 320, 64, 26};
    return kOdds[std::clamp(r, 0, kRarities - 1)];
}

const char* knifeName(int type) {
    static const char* const kNames[kKnifeTypes] = {"KNIFE", "BUTTERFLY", "KARAMBIT", "M9 BAYONET", "TALON", "BOWIE", "KUKRI"};
    return kNames[std::clamp(type, 0, kKnifeTypes - 1)];
}

static_assert(kSlots == kWeaponCount, "one equip slot per weapon");

namespace {

SkinLayer L(int pattern, uint32_t a, uint32_t b, uint32_t c, float gloss = 0.3f, float scale = 1.0f) {
    SkinLayer l;
    l.pattern = pattern;
    l.a = a;
    l.b = b;
    l.c = c;
    l.gloss = gloss;
    l.scale = scale;
    return l;
}

// Which layer each region gets, as 12 characters in SkinRegion order - body, top (slide / dust cover), barrel,
// muzzle (and suppressor), handguard, stock, grip, mag, scope, accents (sights, rails, levers), blade, handle - each
// '0'..'3', or '-' to leave that part in its own colours.
constexpr const char* kFurniture = "0---00000---";  // the furniture and mag painted, the metal left bare
constexpr const char* kAllOver = "00-000000-0-";     // everything but the bore and accents

std::vector<SkinDef> buildSkins() {
    std::vector<SkinDef> s;
    auto add = [&](int weapon, int knife, const std::string& key, const std::string& name, int rarity,
                   const std::vector<SkinLayer>& layers, const char* regions) {
        SkinDef d{key, name, weapon, knife, rarity, {}, {}};
        int k = 0;
        for (const SkinLayer& l : layers)
            if (k < kSkinLayers) d.layers[k++] = l;
        const size_t len = std::strlen(regions);  // (12 in SkinRegion order; a short one leaves the rest plain)
        for (int r = 0; r < kSkinRegions; ++r) {
            const char ch = size_t(r) < len ? regions[r] : '-';
            d.regions[r] = ch >= '0' && ch <= '3' ? uint8_t(ch - '0') : kRegionPlain;
        }
        s.push_back(d);
    };
    auto gun = [&](int weapon, const char* wkey, const char* wname, const char* skin, int rarity,
                   const std::vector<SkinLayer>& layers, const char* regions) {
        add(weapon, -1, std::string(wkey) + "_" + skin, std::string(wname) + " | " + skin, rarity, layers, regions);
    };
    // Palettes used across the guns.
    const uint32_t kInk = 0x101012, kPearl = 0xf2efe8, kGilt = 0xd2a64a, kTeal = 0x0f2a26, kPetal = 0xf2a6c0;

    // ---- AK-47 ----
    gun(kWRifle, "RIFLE", "AK-47", "DESERT STORM", kMilSpec, {L(kPatCamo, 0xc2a878, 0x8a6f48, 0x5e4a30, 0.15f)}, kFurniture);
    gun(kWRifle, "RIFLE", "AK-47", "JUNGLE", kMilSpec, {L(kPatCamo, 0x4a5a32, 0x2f3a22, 0x6f7f42, 0.15f)}, kFurniture);
    gun(kWRifle, "RIFLE", "AK-47", "CRIMSON", kRestricted,
        {L(kPatSolid, 0x9e1a22, 0x8a1620, 0, 0.6f), L(kPatSolid, 0x1b1b1e, 0x222226, 0, 0.5f),
         L(kPatStripes, 0x1b1b1e, 0xd8d8d8, 0x9e1a22, 0.5f)}, "01-10012-1--");
    gun(kWRifle, "RIFLE", "AK-47", "CARBON RED", kRestricted,
        {L(kPatCarbon, 0x1a1a1c, 0x2e2e33, 0xc0202a, 0.6f), L(kPatSolid, 0xc0202a, 0xa01820, 0, 0.7f)}, "00-10001-1--");
    gun(kWRifle, "RIFLE", "AK-47", "POLAR", kClassified,
        {L(kPatBlocks, 0xe8ecf0, 0x1f6fd0, 0x15171a, 0.45f), L(kPatSolid, 0xe8ecf0, 0xdde2e8, 0, 0.5f),
         L(kPatDecals, 0xe8ecf0, 0x15171a, 0x1f6fd0, 0.4f), L(kPatSolid, 0x15171a, 0x1c1e22, 0, 0.4f)}, "01-30032-3--");
    gun(kWRifle, "RIFLE", "AK-47", "GOLD", kClassified,
        {L(kPatDamascus, 0xd8b24a, 0xa8822a, 0xfff0b0, 0.95f), L(kPatSolid, 0x2a2418, 0x332b1c, 0, 0.6f)}, "00-00010-0--");
    gun(kWRifle, "RIFLE", "AK-47", "WILDFIRE", kCovert,
        {L(kPatFlames, 0xffd23a, 0xff5a1a, 0x2a0c08, 0.5f), L(kPatSolid, 0x2a0c08, 0x34100a, 0, 0.5f),
         L(kPatSolid, 0xff5a1a, 0xe04a10, 0, 0.6f)}, "01-10010-2--");
    gun(kWRifle, "RIFLE", "AK-47", "ROYAL", kCovert,
        {L(kPatMarble, 0xd8b24a, 0x1d3f8f, 0x0c0d10, 0.8f), L(kPatSolid, 0xd8b24a, 0xc8a03a, 0, 0.95f),
         L(kPatSolid, 0x0c0d10, 0x14161a, 0, 0.6f)}, "02-10020-1--");
    gun(kWRifle, "RIFLE", "AK-47", "LOTUS GARDEN", kCovert,
        {L(kPatLotus, kTeal, kPetal, kGilt, 0.45f), L(kPatSolid, kGilt, 0xc0943a, 0, 0.9f), L(kPatSolid, 0x0c1a18, 0x10221f, 0, 0.4f)},
        "00-10020-1--");
    gun(kWRifle, "RIFLE", "AK-47", "PRESSWORK", kCovert,
        {L(kPatPrint, kPearl, kInk, kGilt, 0.8f), L(kPatSolid, kInk, 0x18181b, 0, 0.85f), L(kPatDecals, kPearl, kInk, kGilt, 0.7f)},
        "01-10012-1--");
    gun(kWRifle, "RIFLE", "AK-47", "NEBULA", kClassified,
        {L(kPatGalaxy, 0x0a0a20, 0x5a2aa0, 0x30c0e0, 0.7f), L(kPatSolid, 0x0a0a14, 0x10101c, 0, 0.6f)}, "01-10000-1--");
    gun(kWRifle, "RIFLE", "AK-47", "KOI", kRestricted,
        {L(kPatScales, 0xff7a2a, 0xf2f2ee, 0x1a1a1a, 0.55f, 0.8f), L(kPatWaves, 0x1a3a6a, 0xe8f0ff, 0x2a5a9a, 0.4f),
         L(kPatSolid, 0x15171a, 0x1c1e22, 0, 0.4f)}, "02-21120-2--");

    // ---- Pistol (USP-S) ----
    gun(kWPistol, "PISTOL", "PISTOL", "FOREST", kMilSpec, {L(kPatCamo, 0x55663c, 0x2c3522, 0x8a8a5a, 0.15f)}, "0---0000----");
    gun(kWPistol, "PISTOL", "PISTOL", "COBALT", kRestricted,
        {L(kPatSolid, 0x2a54c8, 0x2448b0, 0, 0.85f), L(kPatSolid, 0x0f1420, 0x151a28, 0, 0.6f)}, "00-1001--1--");
    gun(kWPistol, "PISTOL", "PISTOL", "SUNSET", kClassified,
        {L(kPatGradient, 0x6a3fd0, 0xff5e7e, 0xffb347, 0.8f), L(kPatSolid, 0x1a1020, 0x241630, 0, 0.6f)}, "00-1001--1--");
    gun(kWPistol, "PISTOL", "PISTOL", "PRESSWORK", kCovert,
        {L(kPatPrint, kPearl, kInk, kGilt, 0.85f, 0.6f), L(kPatSolid, kInk, 0x18181b, 0, 0.85f), L(kPatDecals, kPearl, kInk, kGilt, 0.7f, 0.6f)},
        "10-2001--1--");
    gun(kWPistol, "PISTOL", "PISTOL", "KOI POND", kClassified,
        {L(kPatWaves, 0x1a3a6a, 0xe8f0ff, 0x2a5a9a, 0.5f, 0.6f), L(kPatScales, 0xff7a2a, 0xf2f2ee, 0x1a1a1a, 0.6f, 0.5f),
         L(kPatSolid, 0x15171a, 0x1c1e22, 0, 0.4f)}, "21-0002--2--");

    // ---- Dual Berettas ----
    gun(kWBerettas, "BERETTAS", "DUAL BERETTAS", "URBAN", kMilSpec, {L(kPatCamo, 0x8c9096, 0x55595f, 0x2e3136, 0.15f)}, "00-0000--0--");
    gun(kWBerettas, "BERETTAS", "DUAL BERETTAS", "GILDED", kRestricted,
        {L(kPatSolid, 0xd8b24a, 0xc8a03a, 0, 0.9f), L(kPatSolid, 0x141416, 0x1a1a1d, 0, 0.6f), L(kPatDamascus, 0x6a4a2a, 0x3a2414, 0xa07a40, 0.5f)},
        "10-1002--1--");
    gun(kWBerettas, "BERETTAS", "DUAL BERETTAS", "COBRA", kClassified,
        {L(kPatScales, 0x2bd16a, 0x0f2a18, 0x0a140c, 0.6f, 0.5f), L(kPatSolid, 0x0a140c, 0x10200f, 0, 0.6f)}, "00-1001--1--");
    gun(kWBerettas, "BERETTAS", "DUAL BERETTAS", "LOTUS", kCovert,
        {L(kPatLotus, kTeal, kPetal, kGilt, 0.5f, 0.5f), L(kPatSolid, kGilt, 0xc0943a, 0, 0.9f)}, "10-1000--1--");

    // ---- Deagle ----
    gun(kWDeagle, "DEAGLE", "DEAGLE", "SAND DUNE", kMilSpec, {L(kPatSolid, 0xc9a978, 0xa88a5a, 0, 0.1f)}, "00-00-0--0--");
    gun(kWDeagle, "DEAGLE", "DEAGLE", "MIDNIGHT", kRestricted,
        {L(kPatCarbon, 0x10162a, 0x1e2a4a, 0x6a8cff, 0.7f), L(kPatSolid, 0x6a8cff, 0x5a7cef, 0, 0.8f)}, "00-1000--1--");
    gun(kWDeagle, "DEAGLE", "DEAGLE", "CODE RED", kClassified,
        {L(kPatBlocks, 0xeeeeee, 0xd01c24, 0x111214, 0.5f, 0.6f), L(kPatSolid, 0x111214, 0x18191c, 0, 0.5f),
         L(kPatSolid, 0xd01c24, 0xb81820, 0, 0.6f)}, "00-2001--2--");
    gun(kWDeagle, "DEAGLE", "DEAGLE", "BLAZE", kCovert, {L(kPatFlames, 0xffe066, 0xff6a00, 0x120604, 0.6f), L(kPatSolid, 0x120604, 0x1a0806, 0, 0.6f)},
        "00-0001--1--");
    gun(kWDeagle, "DEAGLE", "DEAGLE", "PRESSWORK", kCovert,
        {L(kPatPrint, kPearl, kInk, kGilt, 0.85f, 0.6f), L(kPatSolid, kInk, 0x18181b, 0, 0.85f), L(kPatSolid, kGilt, 0xc0943a, 0, 0.9f)},
        "10-2001--2--");
    gun(kWDeagle, "DEAGLE", "DEAGLE", "KUMICHO", kClassified,
        {L(kPatWaves, 0x8a1a1a, 0xf0e0c8, 0x4a0a0a, 0.6f, 0.5f), L(kPatSolid, 0x1a1214, 0x221a1c, 0, 0.6f), L(kPatSolid, kGilt, 0xc0943a, 0, 0.9f)},
        "10-2001--2--");

    // ---- Nova ----
    gun(kWNova, "NOVA", "NOVA", "SWAMP", kMilSpec, {L(kPatCamo, 0x5a5a3a, 0x3a3a26, 0x7a6a44, 0.1f)}, kFurniture);
    gun(kWNova, "NOVA", "NOVA", "HYPER", kRestricted, {L(kPatNeon, 0x14161c, 0x22e6ff, 0x0a3a50, 0.5f), L(kPatSolid, 0x14161c, 0x1a1c24, 0, 0.5f)},
        "01-10010-1--");
    gun(kWNova, "NOVA", "NOVA", "TIGER", kClassified, {L(kPatTiger, 0xf08a1a, 0x1a1008, 0x2a1a08, 0.45f), L(kPatSolid, 0x1a1008, 0x22160c, 0, 0.4f)},
        "00-10010-1--");
    gun(kWNova, "NOVA", "NOVA", "SPLASH", kRestricted,
        {L(kPatSplatter, 0x20242a, 0x22c0ff, 0xff3a8a, 0.4f), L(kPatSolid, 0x20242a, 0x282c32, 0, 0.4f)}, "00-10010-1--");

    // ---- MAC-10 ----
    gun(kWMac10, "MAC10", "MAC-10", "TARNISH", kMilSpec, {L(kPatHardened, 0x5a6470, 0x7a6a4a, 0x3a3f46, 0.3f)}, "00-00000-0--");
    gun(kWMac10, "MAC10", "MAC-10", "CANDY", kRestricted,
        {L(kPatFade, 0xff7ad0, 0xb07aff, 0x4ae0ff, 0.75f), L(kPatSolid, 0x1a1020, 0x241630, 0, 0.6f)}, "00-10010-1--");
    gun(kWMac10, "MAC10", "MAC-10", "NEON", kClassified, {L(kPatNeon, 0x120c18, 0xff2ad0, 0x40104a, 0.5f), L(kPatSolid, 0xff2ad0, 0xe020c0, 0, 0.6f)},
        "00-10000-1--");
    gun(kWMac10, "MAC10", "MAC-10", "PRISM", kCovert,
        {L(kPatHolo, 0xe8e8f0, 0x40404a, 0xc0e0ff, 0.9f, 0.6f), L(kPatSolid, 0x18181c, 0x202026, 0, 0.6f)}, "00-10010-1--");

    // ---- AWP ----
    gun(kWSniper, "SNIPER", "AWP", "SAFARI", kMilSpec, {L(kPatCamo, 0xb8a070, 0x6e5a36, 0x3c3020, 0.1f)}, "0---00000---");
    gun(kWSniper, "SNIPER", "AWP", "PINK ICE", kRestricted,
        {L(kPatSolid, 0xff8ac8, 0xf07ab8, 0, 0.7f), L(kPatSolid, 0xf2f4f6, 0xe2e6ea, 0, 0.7f), L(kPatHalftone, 0xf2f4f6, 0xff8ac8, 0x8a8f96, 0.6f)},
        "00-1221111--");
    gun(kWSniper, "SNIPER", "AWP", "HAZARD", kClassified,
        {L(kPatBlocks, 0xf2f2ee, 0xff7a1a, 0x121314, 0.45f), L(kPatSolid, 0x121314, 0x1a1b1d, 0, 0.5f),
         L(kPatDecals, 0xf2f2ee, 0x121314, 0xff7a1a, 0.4f)}, "00-1001211--");
    gun(kWSniper, "SNIPER", "AWP", "LIGHTNING", kCovert, {L(kPatNeon, 0x0c1020, 0x5ab4ff, 0x1a2a60, 0.6f)}, "00-0000000--");
    gun(kWSniper, "SNIPER", "AWP", "DRAGON", kCovert,
        {L(kPatScales, 0xc8201e, 0x3a0808, 0xf0b040, 0.7f, 0.8f), L(kPatSolid, 0xf0b040, 0xe0a030, 0, 0.9f), L(kPatSolid, 0x200808, 0x2a0a0a, 0, 0.5f)},
        "00-1002001--");
    gun(kWSniper, "SNIPER", "AWP", "PRESSWORK", kCovert,
        {L(kPatPrint, kPearl, kInk, kGilt, 0.8f), L(kPatSolid, kInk, 0x18181b, 0, 0.85f), L(kPatDecals, kPearl, kInk, kGilt, 0.7f)},
        "00-1001211--");
    gun(kWSniper, "SNIPER", "AWP", "AURORA", kClassified,
        {L(kPatGradient, 0x0a1a3a, 0x20a080, 0xa0f0d0, 0.75f), L(kPatSolid, 0x0a1220, 0x101a2c, 0, 0.6f)}, "00-1001011--");

    // ---- M4A1-S (the save keys are still "M4A4_...", from when it was one) ----
    gun(kWM4A1S, "M4A4", "M4A1-S", "URBAN DDPAT", kMilSpec, {L(kPatCamo, 0x7a7f86, 0x4a4e55, 0x2a2d32, 0.15f)}, kFurniture);
    gun(kWM4A1S, "M4A4", "M4A1-S", "DESERT TECH", kRestricted,
        {L(kPatSolid, 0xc9a978, 0xb89868, 0, 0.3f), L(kPatSolid, 0x2a2c30, 0x323438, 0, 0.45f), L(kPatDecals, 0xc9a978, 0x2a2c30, 0x5e4a30, 0.3f)},
        "01-10002-1--");
    gun(kWM4A1S, "M4A4", "M4A1-S", "ICE COLD", kClassified,
        {L(kPatFacets, 0xe8f6ff, 0x6ab8ff, 0x2a4aa0, 0.8f), L(kPatSolid, 0xe8f6ff, 0xd8e8f8, 0, 0.8f)}, "01-10000-1--");
    gun(kWM4A1S, "M4A4", "M4A1-S", "HOWL", kCovert,
        {L(kPatFlames, 0xffb020, 0xd02818, 0x180808, 0.55f), L(kPatSolid, 0x180808, 0x200a0a, 0, 0.5f)}, "01-10010-1--");
    gun(kWM4A1S, "M4A1S", "M4A1-S", "PRINTSTREAM", kCovert,
        {L(kPatPrint, kPearl, kInk, kGilt, 0.85f), L(kPatSolid, kInk, 0x18181b, 0, 0.9f), L(kPatDecals, kPearl, kInk, kGilt, 0.7f)},
        "01-10012-1--");
    gun(kWM4A1S, "M4A1S", "M4A1-S", "LOTUS GARDEN", kCovert,
        {L(kPatLotus, kTeal, kPetal, kGilt, 0.45f), L(kPatSolid, kGilt, 0xc0943a, 0, 0.9f), L(kPatSolid, 0x0c1a18, 0x10221f, 0, 0.4f)},
        "02-10020-1--");
    gun(kWM4A1S, "M4A1S", "M4A1-S", "MECHA", kClassified,
        {L(kPatBlocks, 0xf2f2ee, 0xff6a1a, 0x16181c, 0.5f), L(kPatSolid, 0x16181c, 0x1e2024, 0, 0.5f),
         L(kPatStripes, 0x16181c, 0xf2f2ee, 0xff6a1a, 0.5f)}, "01-12012-1--");

    // ---- XM1014 ----
    gun(kWXm1014, "XM1014", "XM1014", "BLUE STEEL", kMilSpec, {L(kPatHardened, 0x3a5a9a, 0x6a7a8a, 0x2a2d32, 0.6f)}, "00-00000-0--");
    gun(kWXm1014, "XM1014", "XM1014", "TRANQUILITY", kRestricted,
        {L(kPatWaves, 0x8ad0d8, 0xe8f6ff, 0x2a6a8a, 0.5f), L(kPatSolid, 0x2a4a5a, 0x324e5e, 0, 0.5f)}, "01-10010-1--");
    gun(kWXm1014, "XM1014", "XM1014", "INCINEGATOR", kClassified,
        {L(kPatFlames, 0xffd23a, 0xff3a1a, 0x1a0806, 0.5f), L(kPatSolid, 0x1a0806, 0x220a08, 0, 0.5f)}, "01-10010-1--");

    // ---- Galil AR ----
    gun(kWGalil, "GALIL", "GALIL AR", "SAGE", kMilSpec, {L(kPatCamo, 0x8a9a6a, 0x5a6a42, 0x3a4228, 0.15f)}, kFurniture);
    gun(kWGalil, "GALIL", "GALIL AR", "ROCKET POP", kRestricted,
        {L(kPatHalftone, 0xf2f2ee, 0xff3a6a, 0x2a6aff, 0.5f), L(kPatSolid, 0x2a6aff, 0x2258e0, 0, 0.6f)}, "01-00010-1--");
    gun(kWGalil, "GALIL", "GALIL AR", "CERBERUS", kClassified,
        {L(kPatTiger, 0xd04a1a, 0x1a0e08, 0x5a1a08, 0.5f), L(kPatSolid, 0x1a0e08, 0x22140c, 0, 0.45f)}, "01-10010-1--");
    gun(kWGalil, "GALIL", "GALIL AR", "STREET ART", kRestricted,
        {L(kPatSplatter, 0xf0f0e8, 0xff2a8a, 0x2ad0ff, 0.4f), L(kPatSolid, 0x18181c, 0x202026, 0, 0.4f), L(kPatDecals, 0xf0f0e8, 0x18181c, 0xff2a8a, 0.4f)},
        "01-10012-1--");

    // ---- SSG 08 ----
    gun(kWSsg08, "SSG08", "SSG 08", "LICHEN", kMilSpec, {L(kPatCamo, 0x7a8a6a, 0xb0b89a, 0x3a4630, 0.1f)}, "0---00000---");
    gun(kWSsg08, "SSG08", "SSG 08", "ABYSS", kRestricted,
        {L(kPatMarble, 0x1a4a6a, 0x0a1a2a, 0x5ab4d0, 0.6f), L(kPatSolid, 0x0a1a2a, 0x10222f, 0, 0.6f)}, "00-1001010--");
    gun(kWSsg08, "SSG08", "SSG 08", "BLOOD IN THE WATER", kClassified,
        {L(kPatWaves, 0x2a7ab0, 0xe8f0ff, 0x0a1a2a, 0.55f), L(kPatSolid, 0xd02020, 0xb81818, 0, 0.6f), L(kPatSolid, 0x0a1a2a, 0x10222f, 0, 0.5f)},
        "02-1002021--");
    gun(kWSsg08, "SSG08", "SSG 08", "NEBULA", kCovert,
        {L(kPatGalaxy, 0x0a0a20, 0x5a2aa0, 0x30c0e0, 0.75f), L(kPatSolid, 0x0a0a14, 0x10101c, 0, 0.6f)}, "00-1001011--");

    // ---- UMP-45 ----
    gun(kWUmp45, "UMP45", "UMP-45", "CARBON", kMilSpec, {L(kPatCarbon, 0x1a1a1c, 0x2e2e33, 0x5a5e66, 0.4f)}, "00-00000-0--");
    gun(kWUmp45, "UMP45", "UMP-45", "PRIMAL", kRestricted,
        {L(kPatTiger, 0x8a5a2a, 0x1a1008, 0x3a2410, 0.3f), L(kPatSolid, 0x1a1008, 0x22160c, 0, 0.3f)}, "00-10010-1--");
    gun(kWUmp45, "UMP45", "UMP-45", "BLAZE", kClassified,
        {L(kPatFlames, 0xffe066, 0xff6a00, 0x120604, 0.6f), L(kPatSolid, 0x120604, 0x1a0806, 0, 0.6f)}, "00-10010-1--");
    gun(kWUmp45, "UMP45", "UMP-45", "MOMENTUM", kClassified,
        {L(kPatStripes, 0xf2f2ee, 0x16181c, 0xff3a3a, 0.6f), L(kPatSolid, 0x16181c, 0x1e2024, 0, 0.5f), L(kPatDecals, 0xf2f2ee, 0x16181c, 0xff3a3a, 0.5f)},
        "01-10012-1--");

    // ---- Knives: every case knife in every finish (the gold). Blade, and for some the handle too. ----
    struct Finish { const char* name; std::vector<SkinLayer> layers; const char* regions; };
    const Finish finishes[] = {
        {"VANILLA", {L(kPatSolid, 0xc3c8cf, 0xa6abb3, 0, 0.95f)}, "----------0-"},
        {"FADE", {L(kPatFade, 0xf6d878, 0xc95fb0, 0x6f5bd0, 0.95f)}, "----------0-"},
        {"SAPPHIRE", {L(kPatGalaxy, 0x081040, 0x3a6aff, 0x8ab0ff, 0.95f, 0.5f)}, "----------0-"},
        {"RUBY", {L(kPatGalaxy, 0x300408, 0xe0182a, 0xff6a78, 0.95f, 0.5f)}, "----------0-"},
        {"EMERALD", {L(kPatGalaxy, 0x02200e, 0x18d070, 0x80ffb8, 0.95f, 0.5f)}, "----------0-"},
        {"TIGER", {L(kPatTiger, 0xf0a020, 0x2a1a08, 0x1a0e04, 0.8f)}, "----------0-"},
        {"HARDENED", {L(kPatHardened, 0x2a5ad0, 0xc8a040, 0x5a6068, 0.75f)}, "----------0-"},
        {"WEB", {L(kPatWeb, 0x8a1414, 0x101010, 0x2a0606, 0.5f), L(kPatSolid, 0x8a1414, 0x7a1010, 0, 0.5f)}, "----------01"},
        {"NIGHT", {L(kPatSolid, 0x2a2d33, 0x1a1c20, 0, 0.35f), L(kPatSolid, 0x16171a, 0x1c1d20, 0, 0.3f)}, "----------01"},
        {"LOTUS", {L(kPatLotus, kTeal, kPetal, kGilt, 0.7f, 0.35f), L(kPatSolid, kGilt, 0xc0943a, 0, 0.9f)}, "----------01"},
        {"PRESSWORK", {L(kPatPrint, kPearl, kInk, kGilt, 0.9f, 0.5f), L(kPatDecals, kInk, kPearl, kGilt, 0.6f, 0.4f)}, "----------01"},
        {"DAMASCUS", {L(kPatDamascus, 0xb8c0c8, 0x50565e, 0xf0f4f8, 0.85f, 0.6f)}, "----------0-"},
        {"NEBULA", {L(kPatGalaxy, 0x0a0a20, 0x5a2aa0, 0x30c0e0, 0.9f, 0.5f), L(kPatSolid, 0x0a0a14, 0x10101c, 0, 0.6f)}, "----------01"},
        {"SCALEWORK", {L(kPatScales, 0x2bd16a, 0x0f2a18, 0xd2a64a, 0.85f, 0.35f), L(kPatSolid, 0x0f2a18, 0x14301e, 0, 0.6f)}, "----------01"},
        {"PRISM", {L(kPatHolo, 0xe8e8f0, 0x40404a, 0xc0e0ff, 0.95f, 0.5f)}, "----------0-"},
    };
    for (int k = kKnifeButterfly; k < kKnifeTypes; ++k)
        for (const Finish& f : finishes)
            add(kWKnife, k, std::string("KNIFE_") + knifeName(k) + "_" + f.name, std::string(knifeName(k)) + " | " + f.name,
                kRareSpecial, f.layers, f.regions);
    for (SkinDef& d : s) std::replace(d.key.begin(), d.key.end(), ' ', '_');
    return s;
}

}  // namespace

const std::vector<SkinDef>& allSkins() {
    static const std::vector<SkinDef> skins = buildSkins();
    return skins;
}

int findSkin(const std::string& key) {
    const std::vector<SkinDef>& s = allSkins();
    for (size_t k = 0; k < s.size(); ++k)
        if (s[k].key == key) return int(k);
    return -1;
}

const char* wearName(float w) {
    return w < 0.07f ? "FACTORY NEW" : w < 0.15f ? "MINIMAL WEAR" : w < 0.38f ? "FIELD-TESTED" : w < 0.45f ? "WELL-WORN" : "BATTLE-SCARRED";
}
const char* wearShort(float w) { return w < 0.07f ? "FN" : w < 0.15f ? "MW" : w < 0.38f ? "FT" : w < 0.45f ? "WW" : "BS"; }

float rollUnit(uint32_t& rng) {
    rng ^= rng << 13;
    rng ^= rng >> 17;
    rng ^= rng << 5;
    return float(rng & 0xFFFFFF) / float(0x1000000);
}

Item rollCase(uint32_t& rng) {
    int roll = int(rollUnit(rng) * 10000.0f), rarity = 0;
    while (rarity < kRarities - 1 && roll >= rarityOdds(rarity)) roll -= rarityOdds(rarity++);
    std::vector<int> pool;
    const std::vector<SkinDef>& s = allSkins();
    for (size_t k = 0; k < s.size(); ++k)
        if (s[k].rarity == rarity) pool.push_back(int(k));
    Item it;
    it.skin = pool[std::min(pool.size() - 1, size_t(rollUnit(rng) * float(pool.size())))];
    it.wear = rollUnit(rng);
    return it;
}

bool Inventory::addKill(int killsPerCase) {
    if (++progress < std::max(1, killsPerCase)) return false;
    progress = 0;
    ++cases;
    return true;
}

int Inventory::open(uint32_t& rng) {
    if (cases <= 0) return -1;
    --cases;
    items.push_back(rollCase(rng));
    return int(items.size()) - 1;
}

// The file: "cases N", "progress N", "item KEY WEAR" and "equip WEAPON KEY WEAR" lines.
bool Inventory::save(const std::string& path) const {
    std::ofstream out(path);
    if (!out) return false;
    const std::vector<SkinDef>& s = allSkins();
    out << "// Crisp inventory: your cases and skins (written by the game)\n";
    out << "cases " << cases << "\nprogress " << progress << "\n";
    char w[32];
    for (const Item& it : items) {
        if (it.skin < 0 || size_t(it.skin) >= s.size()) continue;
        std::snprintf(w, sizeof(w), "%.6f", double(it.wear));
        out << "item " << s[size_t(it.skin)].key << " " << w << "\n";
    }
    for (int k = 0; k < kSlots; ++k) {
        if (equip[k].skin < 0 || size_t(equip[k].skin) >= s.size()) continue;
        std::snprintf(w, sizeof(w), "%.6f", double(equip[k].wear));
        out << "equip " << k << " " << s[size_t(equip[k].skin)].key << " " << w << "\n";
    }
    return bool(out);
}

bool Inventory::load(const std::string& path) {
    std::ifstream in(path);
    if (!in) return false;
    *this = Inventory{};
    std::string line;
    while (std::getline(in, line)) {
        std::istringstream ss(line);
        std::string key;
        ss >> key;
        if (key == "cases") ss >> cases;
        else if (key == "progress") ss >> progress;
        else if (key == "item" || key == "equip") {
            int slot = 0;
            std::string skinKey;
            float wear = 0;
            if (key == "equip") ss >> slot;
            ss >> skinKey >> wear;
            const int skin = findSkin(skinKey);
            if (skin < 0) continue;  // (a skin that's gone from the game)
            wear = std::clamp(wear, 0.0f, 1.0f);
            if (key == "item") items.push_back({skin, wear});
            else if (slot >= 0 && slot < kSlots) equip[slot] = {skin, wear};
        }
    }
    cases = std::max(0, cases);
    progress = std::max(0, progress);
    return true;
}

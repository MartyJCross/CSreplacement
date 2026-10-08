#include "items.h"
#include <algorithm>
#include <cstdio>
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

namespace {

std::vector<SkinDef> buildSkins() {
    std::vector<SkinDef> s;
    auto gun = [&](int weapon, const char* wkey, const char* wname, const char* skin, int rarity, int pattern, uint32_t a,
                   uint32_t b, uint32_t c, float gloss) {
        s.push_back({std::string(wkey) + "_" + skin, std::string(wname) + " | " + skin, weapon, -1, rarity, pattern, a, b, c, gloss});
    };
    // Rifle
    gun(kWRifle, "RIFLE", "RIFLE", "DESERT STORM", kMilSpec, kPatCamo, 0xc2a878, 0x8a6f48, 0x5e4a30, 0.15f);
    gun(kWRifle, "RIFLE", "RIFLE", "JUNGLE", kMilSpec, kPatCamo, 0x4a5a32, 0x2f3a22, 0x6f7f42, 0.15f);
    gun(kWRifle, "RIFLE", "RIFLE", "CRIMSON", kRestricted, kPatTwoTone, 0x9e1a22, 0x1b1b1e, 0x34343a, 0.55f);
    gun(kWRifle, "RIFLE", "RIFLE", "CARBON RED", kRestricted, kPatCarbon, 0x1a1a1c, 0x2e2e33, 0xc0202a, 0.6f);
    gun(kWRifle, "RIFLE", "RIFLE", "POLAR", kClassified, kPatHazard, 0xe8ecf0, 0x1f6fd0, 0x15171a, 0.45f);
    gun(kWRifle, "RIFLE", "RIFLE", "GOLD", kClassified, kPatSolid, 0xd8b24a, 0x9c7a28, 0x2a2418, 0.9f);
    gun(kWRifle, "RIFLE", "RIFLE", "WILDFIRE", kCovert, kPatFlames, 0xffd23a, 0xff5a1a, 0x2a0c08, 0.5f);
    gun(kWRifle, "RIFLE", "RIFLE", "ROYAL", kCovert, kPatMarble, 0xd8b24a, 0x1d3f8f, 0x0c0d10, 0.8f);
    // Pistol
    gun(kWPistol, "PISTOL", "PISTOL", "FOREST", kMilSpec, kPatCamo, 0x55663c, 0x2c3522, 0x8a8a5a, 0.15f);
    gun(kWPistol, "PISTOL", "PISTOL", "COBALT", kRestricted, kPatSolid, 0x2a54c8, 0x16306e, 0x0f1420, 0.85f);
    gun(kWPistol, "PISTOL", "PISTOL", "SUNSET", kClassified, kPatFade, 0xffb347, 0xff5e7e, 0x6a3fd0, 0.8f);
    // Dual Berettas
    gun(kWBerettas, "BERETTAS", "DUAL BERETTAS", "URBAN", kMilSpec, kPatCamo, 0x8c9096, 0x55595f, 0x2e3136, 0.15f);
    gun(kWBerettas, "BERETTAS", "DUAL BERETTAS", "GILDED", kRestricted, kPatTwoTone, 0xd8b24a, 0x141416, 0x3a3020, 0.85f);
    gun(kWBerettas, "BERETTAS", "DUAL BERETTAS", "COBRA", kClassified, kPatTiger, 0x2bd16a, 0x0f2a18, 0x0a140c, 0.6f);
    // Deagle
    gun(kWDeagle, "DEAGLE", "DEAGLE", "SAND DUNE", kMilSpec, kPatSolid, 0xc9a978, 0xa88a5a, 0x5e4a30, 0.1f);
    gun(kWDeagle, "DEAGLE", "DEAGLE", "MIDNIGHT", kRestricted, kPatCarbon, 0x10162a, 0x1e2a4a, 0x6a8cff, 0.7f);
    gun(kWDeagle, "DEAGLE", "DEAGLE", "CODE RED", kClassified, kPatHazard, 0xeeeeee, 0xd01c24, 0x111214, 0.5f);
    gun(kWDeagle, "DEAGLE", "DEAGLE", "BLAZE", kCovert, kPatFlames, 0xffe066, 0xff6a00, 0x120604, 0.6f);
    // Nova
    gun(kWNova, "NOVA", "NOVA", "SWAMP", kMilSpec, kPatCamo, 0x5a5a3a, 0x3a3a26, 0x7a6a44, 0.1f);
    gun(kWNova, "NOVA", "NOVA", "HYPER", kRestricted, kPatNeon, 0x14161c, 0x22e6ff, 0x0a3a50, 0.5f);
    gun(kWNova, "NOVA", "NOVA", "TIGER", kClassified, kPatTiger, 0xf08a1a, 0x1a1008, 0x2a1a08, 0.45f);
    // MAC-10
    gun(kWMac10, "MAC10", "MAC-10", "TARNISH", kMilSpec, kPatHardened, 0x5a6470, 0x7a6a4a, 0x3a3f46, 0.3f);
    gun(kWMac10, "MAC10", "MAC-10", "CANDY", kRestricted, kPatFade, 0xff7ad0, 0xb07aff, 0x4ae0ff, 0.75f);
    gun(kWMac10, "MAC10", "MAC-10", "NEON", kClassified, kPatNeon, 0x120c18, 0xff2ad0, 0x40104a, 0.5f);
    // Sniper
    gun(kWSniper, "SNIPER", "SNIPER", "SAFARI", kMilSpec, kPatCamo, 0xb8a070, 0x6e5a36, 0x3c3020, 0.1f);
    gun(kWSniper, "SNIPER", "SNIPER", "PINK ICE", kRestricted, kPatTwoTone, 0xff8ac8, 0xf2f4f6, 0x8a8f96, 0.7f);
    gun(kWSniper, "SNIPER", "SNIPER", "HAZARD", kClassified, kPatHazard, 0xf2f2ee, 0xff7a1a, 0x121314, 0.45f);
    gun(kWSniper, "SNIPER", "SNIPER", "LIGHTNING", kCovert, kPatNeon, 0x0c1020, 0x5ab4ff, 0x1a2a60, 0.6f);
    gun(kWSniper, "SNIPER", "SNIPER", "DRAGON", kCovert, kPatMarble, 0xc8201e, 0xf0b040, 0x200808, 0.7f);
    // Knives: every case knife in every finish (the gold).
    struct Finish { const char* name; int pattern; uint32_t a, b, c; float gloss; };
    const Finish finishes[] = {
        {"VANILLA", kPatSolid, 0xc3c8cf, 0xa6abb3, 0x8a9098, 0.95f},
        {"FADE", kPatFade, 0xf6d878, 0xc95fb0, 0x6f5bd0, 0.95f},
        {"SAPPHIRE", kPatMarble, 0x3a6aff, 0x0a1a80, 0x8ab0ff, 0.95f},
        {"RUBY", kPatMarble, 0xe0182a, 0x5a0610, 0xff6a78, 0.95f},
        {"EMERALD", kPatMarble, 0x18d070, 0x05401e, 0x80ffb8, 0.95f},
        {"TIGER", kPatTiger, 0xf0a020, 0x2a1a08, 0x1a0e04, 0.8f},
        {"HARDENED", kPatHardened, 0x2a5ad0, 0xc8a040, 0x5a6068, 0.75f},
        {"WEB", kPatWeb, 0x8a1414, 0x101010, 0x2a0606, 0.5f},
        {"NIGHT", kPatSolid, 0x2a2d33, 0x1a1c20, 0x0e0f12, 0.35f},
    };
    for (int k = kKnifeButterfly; k < kKnifeTypes; ++k)
        for (const Finish& f : finishes) {
            std::string key = std::string("KNIFE_") + knifeName(k) + "_" + f.name;
            std::replace(key.begin(), key.end(), ' ', '_');
            s.push_back({key, std::string(knifeName(k)) + " | " + f.name, kWKnife, k, kRareSpecial, f.pattern, f.a, f.b, f.c, f.gloss});
        }
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

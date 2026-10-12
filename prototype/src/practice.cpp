#include "practice.h"
#include <fstream>
#include <sstream>

bool LineupBook::load(const std::string& path) {
    std::ifstream in(path);
    if (!in) return false;
    all.clear();
    std::string line;
    while (std::getline(in, line)) {
        if (line.empty() || line[0] == '#') continue;
        std::istringstream ls(line);
        Lineup l;
        GrenadeThrow& t = l.t;
        int air = 0;
        if (!(ls >> l.map >> t.type >> t.strength >> air >> t.stand.x >> t.stand.y >> t.stand.z >> t.yaw >> t.pitch >>
              t.start.x >> t.start.y >> t.start.z >> t.vel.x >> t.vel.y >> t.vel.z))
            continue;
        t.air = air != 0;
        std::getline(ls >> std::ws, l.name);
        all.push_back(l);
    }
    return true;
}

bool LineupBook::save(const std::string& path) const {
    std::ofstream out(path);
    if (!out) return false;
    out << "# Crisp grenade lineups: map type strength jump  stand xyz  yaw pitch  start xyz  velocity xyz  name\n";
    out.precision(9);
    for (const Lineup& l : all) {
        const GrenadeThrow& t = l.t;
        out << l.map << ' ' << t.type << ' ' << t.strength << ' ' << int(t.air) << ' ' << t.stand.x << ' ' << t.stand.y << ' '
            << t.stand.z << ' ' << t.yaw << ' ' << t.pitch << ' ' << t.start.x << ' ' << t.start.y << ' ' << t.start.z << ' '
            << t.vel.x << ' ' << t.vel.y << ' ' << t.vel.z << ' ' << l.name << '\n';
    }
    return bool(out);
}

std::vector<int> LineupBook::onMap(int map) const {
    std::vector<int> out;
    for (size_t k = 0; k < all.size(); ++k)
        if (all[k].map == map) out.push_back(int(k));
    return out;
}

const char* grenadeName(int type) {
    static const char* kNames[4] = {"SMOKE", "FLASH", "HE", "MOLOTOV"};
    return kNames[type < 0 || type > 3 ? 0 : type];
}

std::string lineupName(int type, const std::string& from, const std::string& to) {
    std::string n = std::string(grenadeName(type)) + ":";
    if (!from.empty()) n += " " + from;
    if (!to.empty()) n += (from.empty() ? " " : " > ") + to;
    return n;
}

std::string throwKind(const GrenadeThrow& t) {
    const std::string kind = t.strength > 0.8f ? "FULL THROW" : t.strength < 0.45f ? "LOB (MOUSE 2)" : "MEDIUM (BOTH BUTTONS)";
    return kind + (t.air ? " + JUMP" : "");
}

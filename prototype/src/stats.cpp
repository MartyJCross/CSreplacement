#include "stats.h"
#include <algorithm>
#include <cmath>
#include <fstream>
#include <sstream>

int botRating(float botLevel) { return int(std::lround(700.0f + 250.0f * std::clamp(botLevel, 0.0f, 3.0f))); }

int ratingChange(int rating, float botLevel, float score) {
    const float expected = 1.0f / (1.0f + std::pow(10.0f, float(botRating(botLevel) - rating) / 400.0f));
    return int(std::lround(32.0f * (std::clamp(score, 0.0f, 1.0f) - expected)));
}

const char* rankName(int rating) {
    static const char* const kRanks[] = {"RECRUIT", "PRIVATE", "CORPORAL", "SERGEANT", "LIEUTENANT",
                                         "CAPTAIN", "MAJOR", "COLONEL", "GENERAL"};
    const int step = std::clamp((rating - 700) / 125, 0, 8);  // 700, 825, 950, ... 1700+
    return kRanks[step];
}

const MatchRecord& Career::add(MatchRecord m) {
    m.ratingBefore = rating;
    rating = std::max(100, rating + ratingChange(rating, m.botLevel, m.score));
    m.ratingAfter = rating;
    best = std::max(best, rating);
    matches.push_back(std::move(m));
    return matches.back();
}

int Career::wins() const {
    int n = 0;
    for (const MatchRecord& m : matches) n += m.score >= 0.999f;
    return n;
}
int Career::kills() const {
    int n = 0;
    for (const MatchRecord& m : matches) n += m.kills;
    return n;
}
int Career::deaths() const {
    int n = 0;
    for (const MatchRecord& m : matches) n += m.deaths;
    return n;
}
int Career::hsKills() const {
    int n = 0;
    for (const MatchRecord& m : matches) n += m.hsKills;
    return n;
}
float Career::adr() const {
    float dmg = 0;
    int per = 0;
    for (const MatchRecord& m : matches) {
        if (m.rounds <= 0) continue;  // (deathmatch has no rounds)
        dmg += m.damage;
        per += m.rounds;
    }
    return per > 0 ? dmg / float(per) : 0.0f;
}

// The file: "rating N", "best N", then a "match" line per match (spaces in text fields become '_').
bool Career::save(const std::string& path) const {
    std::ofstream out(path);
    if (!out) return false;
    auto word = [](std::string s) {
        std::replace(s.begin(), s.end(), ' ', '_');
        return s.empty() ? std::string("-") : s;
    };
    out << "// Crisp career: your matches against bots and your rating (written by the game)\n";
    out << "rating " << rating << "\nbest " << best << "\n";
    for (const MatchRecord& m : matches)
        out << "match " << m.mode << " " << m.score << " " << m.kills << " " << m.deaths << " " << m.assists << " "
            << m.hsKills << " " << m.damage << " " << m.rounds << " " << m.botLevel << " " << m.ratingBefore << " "
            << m.ratingAfter << " " << word(m.when) << " " << word(m.result) << "\n";
    return bool(out);
}

bool Career::load(const std::string& path) {
    std::ifstream in(path);
    if (!in) return false;
    *this = Career{};
    std::string line;
    auto text = [](std::string s) {
        if (s == "-") return std::string();
        std::replace(s.begin(), s.end(), '_', ' ');
        return s;
    };
    while (std::getline(in, line)) {
        std::istringstream ss(line);
        std::string key;
        ss >> key;
        if (key == "rating") ss >> rating;
        else if (key == "best") ss >> best;
        else if (key == "match") {
            MatchRecord m;
            std::string when, result;
            if (ss >> m.mode >> m.score >> m.kills >> m.deaths >> m.assists >> m.hsKills >> m.damage >> m.rounds >> m.botLevel >>
                m.ratingBefore >> m.ratingAfter >> when >> result) {
                m.when = text(when);
                m.result = text(result);
                matches.push_back(m);
            }
        }
    }
    rating = std::max(100, rating);
    best = std::max(best, rating);
    return true;
}

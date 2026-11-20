// Your career: every finished match against bots (deathmatch, competitive), your stats in each, and a rating
// that goes up when you beat bots and down when they beat you. Pure data (no SDL): saved to stats.txt next to
// the game, and tested in sim_tests.
#pragma once
#include <string>
#include <vector>

struct MatchRecord {
    int mode = 1;          // 1 deathmatch, 3 competitive
    float score = 0;       // how it went, 0..1: competitive win 1, draw 0.5, loss 0; deathmatch by your place
    int kills = 0, deaths = 0, assists = 0, hsKills = 0;
    float damage = 0;
    int rounds = 0;        // competitive: rounds played (ADR = damage / rounds); deathmatch: 0 (per life)
    float botLevel = 2;    // the enemy bots' skill (0 easy .. 3 expert)
    int ratingBefore = 0, ratingAfter = 0;
    std::string when;      // "YYYY-MM-DD HH:MM"
    std::string result;    // shown: "WON 13-9", "2ND OF 9"...
    float adr() const { return damage / float(rounds > 0 ? rounds : deaths + 1); }
};

// Elo against the bots: a bot level counts as a rating (easy 700 .. expert 1450). `score` 0..1.
int ratingChange(int rating, float botLevel, float score);
int botRating(float botLevel);
// The rank for a rating: RECRUIT, PRIVATE, CORPORAL, SERGEANT, LIEUTENANT, CAPTAIN, MAJOR, COLONEL, GENERAL.
const char* rankName(int rating);

// Your last few matches of one mode, summed (the profile's cards, v0.48): totals, win rate, each match's K/D and
// result (oldest first: the form bars) and the best match among them.
struct RecentStats {
    int games = 0, wins = 0, kills = 0, deaths = 0, assists = 0, hsKills = 0, rounds = 0;
    float damage = 0;
    int bestKills = 0;
    float bestKd = 0, bestAdr = 0;  // (ADR: competitive only)
    std::vector<float> form;        // each match's K/D, oldest first
    std::vector<char> won;          // and whether you won it (deathmatch: topped it)
    float kd() const { return float(kills) / float(deaths > 0 ? deaths : 1); }
    int hsPercent() const { return kills > 0 ? hsKills * 100 / kills : 0; }
    float adr() const { return rounds > 0 ? damage / float(rounds) : 0.0f; }
    float killsPerGame() const { return games > 0 ? float(kills) / float(games) : 0.0f; }
    int winPercent() const { return games > 0 ? wins * 100 / games : 0; }
};

struct Career {
    int rating = 1000;
    int best = 1000;
    int arenaBest = 0;   // the highest Arena level you've reached (v0.49; 0 never played)
    int arenaTitles = 0; // rounds won on the Arena's top level
    std::vector<int> clutch;  // your best stars per clutch scenario, by key (map * 16 + scenario; v0.50)
    int clutchBest(int key) const { return key >= 0 && size_t(key) < clutch.size() ? clutch[size_t(key)] : 0; }
    // Keeps `stars` if it beats your best for `key`; true if it did.
    bool setClutch(int key, int stars);
    int clutchTotal() const;
    std::vector<MatchRecord> matches;  // oldest first

    // Adds a finished match: works out the rating change (fills ratingBefore / After) and returns the record.
    const MatchRecord& add(MatchRecord m);
    int wins() const;  // competitive wins + deathmatch matches you topped
    // Totals over all matches.
    int kills() const, deaths() const, hsKills() const;
    float adr() const;  // damage per round over the competitive matches
    // The last `count` matches of `mode` (1 deathmatch, 3 competitive); all of them with a big count.
    RecentStats recent(int mode, size_t count) const;
    bool save(const std::string& path) const;
    bool load(const std::string& path);  // false if there's no file (a fresh career)
};

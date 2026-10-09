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

struct Career {
    int rating = 1000;
    int best = 1000;
    std::vector<MatchRecord> matches;  // oldest first

    // Adds a finished match: works out the rating change (fills ratingBefore / After) and returns the record.
    const MatchRecord& add(MatchRecord m);
    int wins() const;  // competitive wins + deathmatch matches you topped
    // Totals over all matches.
    int kills() const, deaths() const, hsKills() const;
    float adr() const;  // damage per round / life over all matches
    bool save(const std::string& path) const;
    bool load(const std::string& path);  // false if there's no file (a fresh career)
};

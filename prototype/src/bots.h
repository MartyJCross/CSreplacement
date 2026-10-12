// Deathmatch bot brains: roam the whole map, react to what they see and hear, chase, stop to shoot.
// Pure simulation (no SDL), so the headless tests can run whole matches' worth of bot movement.
#pragma once
#include <cstdint>
#include <vector>
#include "combat.h"
#include "nav.h"

// Bot skill (difficulty), 0 easy .. 3 expert: reaction time (min + random range, seconds), aim error (x the
// normal ~0.8 degrees), time between bursts (x), how far behind your movement its eyes are (ticks; 26 = 0.2 s: half
// of it is perception, the rest its hand), how often it goes for the head instead of the chest; and (v0.31, real
// aim) how quick its hand is (`flick`: the aim spring's stiffness, rad/s), how much of a spray's recoil it pulls
// down (`control`), how long it takes to settle its aim on someone (`focusTime`: its aim error starts at 3x and
// shrinks to 1x) and how much of your movement it follows with its hand rather than chasing it (`lead`).
struct BotSkill {
    float reactMin, reactRange, aimError, fireScale;
    int lagTicks;
    float headChance;
    float flick, control, focusTime, lead;
    const char* name;
};
inline const BotSkill& botSkill(int level) {
    static const BotSkill kSkills[4] = {
        {0.45f, 0.40f, 1.80f, 1.30f, 32, 0.00f, 13.0f, 0.30f, 1.40f, 0.30f, "EASY"},
        {0.25f, 0.30f, 1.00f, 1.00f, 26, 0.00f, 18.0f, 0.50f, 1.00f, 0.50f, "NORMAL"},  // how they've always played
        {0.18f, 0.20f, 0.70f, 0.90f, 19, 0.20f, 24.0f, 0.65f, 0.70f, 0.70f, "HARD"},
        {0.12f, 0.15f, 0.50f, 0.85f, 13, 0.40f, 30.0f, 0.80f, 0.50f, 0.85f, "EXPERT"},
    };
    return kSkills[level < 0 ? 0 : level > 3 ? 3 : level];
}
// A skill between the levels (skill variance: a "hard" bot at 2.4 aims a bit better than 2, one at 1.6 a bit
// worse). Blends the two neighbouring levels; clamped to easy..expert.
inline BotSkill skillAt(float level) {
    level = level < 0 ? 0 : level > 3 ? 3 : level;
    const int lo = int(level), hi = lo < 3 ? lo + 1 : 3;
    const float t = level - float(lo);
    const BotSkill &a = botSkill(lo), &b = botSkill(hi);
    auto mix = [t](float x, float y) { return x + (y - x) * t; };
    return {mix(a.reactMin, b.reactMin), mix(a.reactRange, b.reactRange), mix(a.aimError, b.aimError),
            mix(a.fireScale, b.fireScale), int(mix(float(a.lagTicks), float(b.lagTicks)) + 0.5f),
            mix(a.headChance, b.headChance), mix(a.flick, b.flick), mix(a.control, b.control),
            mix(a.focusTime, b.focusTime), mix(a.lead, b.lead), t < 0.5f ? a.name : b.name};
}

struct BotBrain {
    std::vector<Vec3> path;
    size_t next = 0;
    int state = -1;           // -1 just spawned, 0 roam, 1 hold an angle, 2 fighting, 3 investigate
    float timer = 0;
    Vec3 lastSeen;            // where they last saw or heard you
    double alertUntil = 0;    // just got shot: aware all round for a moment
    double blindUntil = 0;    // flashed: sees nothing until then
    bool sees = false, aimed = false;
    int target = -2;          // who it's fighting: -1 = you, i = bot i, -2 = nobody
    bool strafing = false;    // fighting: jiggling sideways (doesn't shoot while moving)
    int strafeDir = 1;
    float strafeTimer = 0;
    // Cover: a spot hidden from the enemy (`cover`) next to one it can be shot from (`peek`). In a fight
    // the bot shoots from the peek spot, ducks back into cover, and peeks again.
    bool hasCover = false, inCover = false;
    bool firstShots = false;  // a fight just started: shoots from where it is before going to cover
    int coverFor = -3;        // the enemy the cover is from
    Vec3 cover, peek;
    float coverTimer = 0;
    double lastSawAt = 0;     // when it last saw its target (gives up the fight a while after)
    float surprise = 0;       // 0..1: nobody seen for a while before this fight (reacts a little slower)
    float sightOff = 180;     // how far off its aim (degrees) the enemy it just spotted was: pre-aimed = quicker
    // Retakes / competitive: an anchor that holds its angle (holdYaw, looking towards holdLook) instead
    // of roaming. It holds from beside cover and steps back into it now and then. It still turns on you,
    // checks noises and fights, then holds wherever it ends up.
    bool holdOnly = false;
    bool frozen = false;      // prefire targets: never move, just turn on you (and shoot, if bots shoot back)
    bool urgent = false;      // in a hurry (competitive: the bomb carrier, the clock): no cover, no chasing
    float holdYaw = 0;
    Vec3 holdLook;
    bool hasHoldLook = false, holdCoverChecked = false;
    // Competitive: walk here (then hold, if holdOnly) instead of roaming at random; through `via` first if hasVia
    // (a way onto a site: teammates come from different sides).
    Vec3 goal, via;
    bool hasGoal = false, hasVia = false;
    float gapWait = 0;        // waiting for a teammate ahead on the way to get on (keepGap)
    // Crosshair placement (preaim): walking somewhere, where it looks - an angle an enemy could be holding, at head
    // height, or the next corner of its route. Picked again at lookAgainAt.
    Vec3 lookAt;
    bool hasLookAt = false, lookAtAngle = false;  // lookAtAngle: it's one of the angles (kept while it's in view)
    double lookAgainAt = 0;
    // Checking spots (v0.31): the angle it's on (an index in BotSenses::angles, -1 none), how long its crosshair has
    // been on it, and the last few it has checked (looked at for a moment) and when: it moves on to the next one.
    int lookSpot = -1;
    float onSpotFor = 0;
    int checkedSpot[8] = {-1, -1, -1, -1, -1, -1, -1, -1};
    double checkedAt[8] = {};
    int checkedNext = 0;
    // Its spot (an anchor's angle, the execute's hold): after a chase or a fight it walks back here
    // instead of standing wherever the chase ended.
    Vec3 home;
    float exposed = 0;        // out of cover with someone in its sights: for how long (it ducks after a burst)
    Vec3 fireExit;            // in a fire: where it's stepping out to
    bool hasFireExit = false;
    bool hasHome = false;
    // Real aim (v0.31): its crosshair is d.yaw / d.pitch and its bullets go there (plus the recoil it doesn't pull
    // down: botShotDir). The crosshair is moved by a hand (a spring: a flick, a little overshoot, settling) towards
    // where it thinks its target is (`seenAt`, which lags the real one) plus its aim error (`aimErr`, wandering
    // towards `aimErrTo`, smaller the longer it tracks: `focus`).
    float yawVel = 0, pitchVel = 0;  // its hand's speed (degrees a second)
    Vec3 seenAt;
    float seenYaw = 0, seenPitch = 0;  // the angles to seenAt last tick (how fast the target crosses its view)
    Vec3 aimErr, aimErrTo;
    float errTimer = 0, headTimer = 0;
    float focus = 0;
    bool aimHead = false;
    // Reaction: drawn when it spots someone (`react`, seconds), `seenFor` counts from then (-1: nobody). It starts to
    // move its hand after a part of it (noticing) and fires once the whole has passed and it's on target.
    float react = 0, seenFor = -1;
    int aimTarget = -3;   // who it's aiming at (BotTarget::id)
    float spray = 0;      // shots into its spray (the recoil it's fighting), recovers like yours between bursts
    int burstLeft = -1;   // shots left in this burst (automatic guns: botNextShot)
};

// A teammate covering an angle (checking spots, v0.31): who (a bot's index), until when. The first to pick an angle
// has it while it keeps looking; the others check something else.
struct SpotClaim { double until = -10; int by = -1; };

// Someone a bot can fight: id -1 = you, i = bot i.
struct BotTarget { int id; Vec3 origin, eye; };

// What the bots get to know this tick.
struct BotSenses {
    const World* world = nullptr;
    const NavGrid* nav = nullptr;
    double now = 0;
    Vec3 playerOrigin, playerEye;
    bool playerUp = false;    // alive, not noclipping, match running
    // Competitive: the enemies this bot may fight (its own team's view). Null = just you (the player).
    const std::vector<BotTarget>* targets = nullptr;
    int self = -2;            // the bot being updated (it's left out of `targets`)
    bool huntYou = false;     // deathmatch with bots fighting each other: still roams towards you half the time
    bool noiseFresh = false;  // you made a sound this tick (footstep, shot)
    Vec3 noisePos;
    float noiseRadius = 0;
    // Optional extra sight blocker (smoke): returns true if the segment a->b is blocked.
    bool (*blocked)(const void* ctx, const Vec3& a, const Vec3& b) = nullptr;
    const void* blockCtx = nullptr;
    // Where enemies hold or come from (floor points: competitive CT spots and site holds for a T, the Ts' stages and
    // holds for a CT): walking somewhere, a bot keeps its crosshair on the nearest one it can see ahead (preaim). Null:
    // it looks along its route.
    const std::vector<Vec3>* angles = nullptr;
    // The first `keyAngles` of them are the known holds (checked first); the rest are corners (NavGrid::hidingSpots).
    size_t keyAngles = ~size_t(0);
    // Competitive: per angle, which of this bot's team is covering it (a bot leaves an angle a teammate has to them and
    // checks another: they split the angles). Null: on its own.
    std::vector<SpotClaim>* looked = nullptr;
    // Teammates (competitive): a bot walking somewhere keeps its distance from one just ahead on the way instead of
    // walking on its back. Null: none.
    const std::vector<BotTarget>* mates = nullptr;
    // Fires (molotovs) on the ground: a bot steps straight out of one and waits at its edge till it burns out.
    const std::vector<Vec3>* fires = nullptr;
    float fireRadius = 110.0f;
    BotSkill skill = botSkill(1);  // this bot's (how fast it reacts, how its hand moves, how well it aims)
};

// Spreads bots over ways onto a site so teammates come from different sides (CT retakes, rotations): for each bot
// (its position in `from`), the index of the entry in `entries` it should go through, or -1 (straight there). An entry
// counts for a bot if its route there keeps off the site (farther than `siteRadius` from `site`) and the trip through
// it is at most `maxDetour` times the straight one (plus 600 units). Each bot takes its shortest, a way a teammate
// already took counting `spreadCost` units longer, so two bots split up unless one way is much longer.
std::vector<int> spreadEntries(const NavGrid& nav, const std::vector<Vec3>& from, const std::vector<Vec3>& entries,
                               const Vec3& site, float siteRadius, float maxDetour = 2.5f, float spreadCost = 900.0f);

constexpr float kBotGap = 110.0f;  // a bot walking somewhere stays this far behind a teammate ahead on the way

constexpr float kBotRunSpeed = 215.0f;  // rifle run speed (estimates; a bot runs at its gun's maxSpeed)


float botRand(uint32_t& state);  // 0..1, xorshift (bots only: the player's shots never use randomness)

// A random standing spot anywhere on the walkable map: at least `minDist` from every watcher and out of
// their sight, and not on top of an occupied spot. Falls back to the best of the spots tried.
Vec3 randomSpawnPoint(const NavGrid& nav, const World& world, const std::vector<Vec3>& watcherEyes, float minDist,
                      const std::vector<Vec3>& occupied, uint32_t& rng);

// Puts a bot that has just come back to life at `at`, facing a random way, ready to roam.
void spawnDeathmatchBot(Dummy& d, BotBrain& b, const Vec3& at, uint32_t& rng);

// True when a live bot hasn't been placed since it (re)spawned: call spawnDeathmatchBot first.
inline bool needsSpawn(const Dummy& d, const BotBrain& b) { return d.alive() && b.state < 0; }

// Cover from someone whose eye is at `threatEye`, within `radius` of `around`: `cover` is a standing
// spot where a standing bot's head can't be seen from there, `peek` a spot next to it where it can.
// Both on the same level as `around` and walkable from it in a straight line. Returns false if none.
bool findCover(const BotSenses& s, const Vec3& around, const Vec3& threatEye, float radius, Vec3& cover, Vec3& peek);

// Advances one placed, living deathmatch bot by one tick (moves d.pos, turns d.yaw, sets b.sees / b.aimed).
void updateDeathmatchBot(Dummy& d, BotBrain& b, const BotSenses& s, uint32_t& rng);

// ---- What bots carry: the whole arsenal ----
// Competitive buying works like a CS team: the team calls the round's buy from its money, then each bot buys for
// its role (0 = the team's AWPer; T rifles the AK, CT the M4A1-S; forces get SMGs, shotguns, the Galil, the
// scout). Deathmatch bots get a random gun each life. All in the sim, so the tests check it.
enum BuyRound { kBuyPistol, kBuyEco, kBuyForce, kBuyFull };
// `avgMoney` per bot on the team; `pistolRound` the first round of a half; `mustWin` no next round to save for.
BuyRound teamBuyRound(int avgMoney, bool pistolRound, bool mustWin);
struct BotBuy { int gun; bool armor, helmet; int spent; };
// One bot's buy: `side` 0 T / 1 CT; `gun` what it already holds (a gun it kept from last round, else its pistol).
BotBuy botBuy(BuyRound round, int money, int side, int role, int gun, bool armor, bool helmet);
int deathmatchBotGun(float r01);  // weighted like CS deathmatch: rifles mostly, some AWPs, SMGs, shotguns, Deagles
// Seconds between a bot's trigger pulls with gun `w` (before skill): taps and short bursts, never faster than the
// gun fires.
float botShotGap(int w);
// Real aim (v0.31). The gap (seconds, before skill's fireScale on the pauses) until a bot's next trigger pull with gun
// `w` after a shot at someone `dist` away: automatic rifles and SMGs spray close (a burst at the gun's rate, then a
// pause for the recoil to settle), fire short bursts at mid range and tap far; the rest at botShotGap's pace.
// `r01` is a random 0..1 (the burst's length and the pause).
float botNextShot(int w, float dist, BotBrain& b, float r01, float fireScale);
// Where a bot's shot goes: along its crosshair, plus the recoil of its spray it doesn't pull down (BotSkill::control)
// and its gun's standing spread (the cheaper guns, like yours). Counts the shot into its spray. r1, r2: random 0..1.
Vec3 botShotDir(const Dummy& d, BotBrain& b, const WeaponDef& gun, const BotSkill& skill, float r1, float r2);
// Ready to fire this tick: it has seen its target for its reaction time and its crosshair is on where it means to aim.
inline bool botReady(const BotBrain& b) { return b.aimed && b.seenFor >= b.react; }

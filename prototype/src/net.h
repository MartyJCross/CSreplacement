// Online play: one player hosts (their game relays), the others join by address. ENet over UDP.
//
// Everyone simulates their own movement and shooting exactly as offline (128 ticks, zero lag for
// yourself) and sends a ~30-byte state every tick. The shooter's game decides hits against what it
// showed (what you see is what you hit) and tells the victim's game, which applies the damage and
// announces a death. That's a "trust each other" model: right for friends, no anti-cheat.
//
// Competitive: the host's game also runs the match (rounds, money rules, the bomb) and the bots. It sends
// the bots' positions, the match state and the round starts/ends; hits on bots go to the host.
// Grenades: the thrower sends the throw; every game flies it the same way (the flight is deterministic)
// and each game works out what it does to its own player.
#pragma once
#include <cstdint>
#include <string>
#include <vector>
#include "vecmath.h"

constexpr int kNetMaxPlayers = 8;        // ids 0..7: the host is 0
constexpr int kNetBots = 10;             // competitive: bot slots 8..17
constexpr int kNetSlots = kNetMaxPlayers + kNetBots;
constexpr uint16_t kNetDefaultPort = 27015;
constexpr uint32_t kNetProtocol = 8;     // bump when messages change: other versions can't join

struct NetState {                        // one player, one tick
    uint8_t id = 0;
    uint32_t tick = 0;
    Vec3 pos;
    float yaw = 0, pitch = 0;
    uint8_t flags = 0;                   // kNetAlive | kNetArmor | kNetHelmet
    uint8_t weapon = 0;                  // 0 rifle, 1 pistol, 2 knife, 3 grenade, 4 sniper
    uint8_t duck = 0;                    // crouch 0..255
};
constexpr uint8_t kNetAlive = 1, kNetArmor = 2, kNetHelmet = 4;

// Competitive, host -> everyone, a few times a second: where the match is.
struct NetMatch {
    uint8_t phase = 0, round = 0;        // phase: 0 freeze/buy, 1 live, 2 round over, 3 match over
    uint8_t sideA = 0;                   // the side team A (the host's team) is on: 0 T, 1 CT
    uint8_t score[2] = {0, 0};           // by team (A, B)
    float phaseLeft = 0, buyLeft = 0, bombLeft = 0;  // seconds
    uint8_t carrier = 255;               // the bomb: a slot, 254 dropped, 255 nobody
    Vec3 dropped;
    uint8_t planted = 0, bombActive = 0;
    Vec3 bombPos;
};

// Competitive, host -> everyone: a round starts (who plays on which team, spawn spots) or ends (the pay).
struct NetRound {
    uint8_t kind = 0;                    // 0 start, 1 end, 2 who's playing (for someone who just joined)
    uint8_t flags = 0;                   // start: kRoundHalf (sides swapped: fresh money), kRoundNewMatch
    uint8_t team[kNetSlots] = {};        // per slot: 0 team A, 1 team B, 255 not playing
    uint8_t spawn[kNetMaxPlayers] = {};  // start: each player's spawn spot
    uint32_t alive = 0;                  // bit per slot
    uint8_t sideA = 0;                   // the side team A is on this round
    uint8_t nameOffset = 0;              // the bots' names (everyone shows the same ones)
    uint8_t winnerSide = 0, why = 0, mvp = 255;  // end
    uint16_t pay[2] = {0, 0};            // end: money for each side (T, CT)
};
constexpr uint8_t kRoundHalf = 1, kRoundNewMatch = 2;

struct NetBot { uint8_t id = 0; Vec3 pos; float yaw = 0, pitch = 0; uint8_t flags = 0, weapon = 0; };

struct NetEvent {
    enum Type { Connected, Failed, Joined, Left, State, Fire, Hit, Death, Name, Nade, Bots, Match, Round, Plant, Defused } type;
    uint8_t from = 0;                    // who it's about (Joined/Left/State/Fire/Nade/Plant), the shooter (Hit),
                                         // the victim (Death)
    uint8_t other = 0;                   // Hit: the victim; Death: the killer
    NetState state;                      // State
    Vec3 a, b;                           // Fire: from, to; Nade: position, velocity; Plant: where
    float damage = 0;                    // Hit (before the victim's armor)
    uint8_t group = 1;                   // Hit: the hit group (0 head)
    bool head = false;                   // Hit, Death
    uint8_t weapon = 0;                  // Fire, Hit, Death; Nade: the grenade type
    float townScale = 0.6f;              // Connected: the host's map size (everyone must match)
    uint8_t game = 0;                    // Connected: bit 0: 0 deathmatch, 1 competitive; bits 1+: the town map
    std::string text;                    // Name: the player's name; Failed: why
    uint32_t tick = 0;                   // Bots: the host's tick
    uint8_t botCount = 0;
    NetBot bots[kNetBots];               // Bots
    NetMatch match;                      // Match
    NetRound round;                      // Round
};

class Net {
public:
    ~Net() { stop(); }
    bool host(uint16_t port, float townScale, uint8_t game, std::string& err);
    bool join(const std::string& address, uint16_t port, std::string& err);  // connects in the background (poll)
    void stop();
    bool active() const { return host_ != nullptr; }
    bool isHost() const { return isHost_; }
    bool ready() const { return myId_ >= 0; }  // hosting, or joined and given an id
    int myId() const { return myId_; }
    int players() const;                       // connected players, you included
    bool connected(int id) const;              // host: is player `id` here
    int pingMs() const;                        // to the host (0 when hosting)
    // Pumps the network (no waiting). New events go to `out`.
    void poll(std::vector<NetEvent>& out);
    void sendState(const NetState& s);
    // `as`: the host speaking for a bot (a slot); -1 = yourself.
    void sendFire(const Vec3& from, const Vec3& to, uint8_t weapon, int as = -1);
    void sendHit(uint8_t victim, float damage, uint8_t group, uint8_t weapon, int as = -1);
    void sendDeath(uint8_t victim, uint8_t killer, bool head, uint8_t weapon);  // clients: only their own
    void sendNade(int type, const Vec3& pos, const Vec3& vel, int as = -1);
    // Host only (competitive):
    void sendBots(uint32_t tick, const NetBot* bots, int count);
    void sendMatch(const NetMatch& m);
    void sendRound(const NetRound& r, int to = -1);  // -1 = everyone
    // Clients (competitive): you planted the bomb here / you defused it.
    void sendPlant(const Vec3& pos);
    void sendDefused();
    // Your name, shown to everyone (letters, digits, - and _; up to 15). Set before hosting/joining or any time.
    void setName(const std::string& name);
    static std::string cleanName(const std::string& name);

private:
    void* host_ = nullptr;   // ENetHost*
    void* server_ = nullptr; // ENetPeer* (clients: the host)
    void* peers_[kNetMaxPlayers] = {};  // host: ENetPeer* by player id
    bool isHost_ = false;
    int myId_ = -1;
    float townScale_ = 0.6f;
    uint8_t game_ = 0;
    double connectStarted_ = 0;
    std::string myName_ = "PLAYER";
    std::string names_[kNetMaxPlayers];  // host: everyone's, to tell newcomers
    void send(const std::vector<uint8_t>& msg, bool reliable);  // host: to everyone; client: to the host
    void sendTo(void* peer, const std::vector<uint8_t>& msg, bool reliable);
    void broadcastExcept(int except, const std::vector<uint8_t>& msg, bool reliable);
    void handle(const uint8_t* data, size_t len, int fromPeerId, std::vector<NetEvent>& out);
};

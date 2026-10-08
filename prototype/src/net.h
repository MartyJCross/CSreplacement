// Online play: one player hosts (their game relays), the others join by address. ENet over UDP.
//
// Everyone simulates their own movement and shooting exactly as offline (128 ticks, zero lag for
// yourself) and sends a ~30-byte state every tick. The shooter's game decides hits against what it
// showed (what you see is what you hit) and tells the victim's game, which applies the damage and
// announces a death. That's a "trust each other" model: right for friends, no anti-cheat.
#pragma once
#include <cstdint>
#include <string>
#include <vector>
#include "vecmath.h"

constexpr int kNetMaxPlayers = 8;        // ids 0..7: the host is 0
constexpr uint16_t kNetDefaultPort = 27015;

struct NetState {                        // one player, one tick
    uint8_t id = 0;
    uint32_t tick = 0;
    Vec3 pos;
    float yaw = 0, pitch = 0;
    uint8_t flags = 0;                   // kNetAlive | kNetDucked
    uint8_t weapon = 0;                  // 0 rifle, 1 pistol, 2 knife, 3 grenade, 4 sniper
};
constexpr uint8_t kNetAlive = 1, kNetDucked = 2;

struct NetEvent {
    enum Type { Connected, Failed, Joined, Left, State, Fire, Hit, Death } type;
    uint8_t from = 0;                    // who it's about (Joined/Left/State/Fire), the shooter (Hit), the victim (Death)
    uint8_t other = 0;                   // Hit: the victim; Death: the killer
    NetState state;                      // State
    Vec3 a, b;                           // Fire: from, to
    float damage = 0;                    // Hit
    bool head = false;                   // Hit, Death
    uint8_t weapon = 0;                  // Fire, Hit, Death
    float dustScale = 0.6f;              // Connected: the host's map size (everyone must match)
};

class Net {
public:
    ~Net() { stop(); }
    bool host(uint16_t port, float dustScale, std::string& err);
    bool join(const std::string& address, uint16_t port, std::string& err);  // connects in the background (poll)
    void stop();
    bool active() const { return host_ != nullptr; }
    bool isHost() const { return isHost_; }
    bool ready() const { return myId_ >= 0; }  // hosting, or joined and given an id
    int myId() const { return myId_; }
    int players() const;                       // connected players, you included
    int pingMs() const;                        // to the host (0 when hosting)
    // Pumps the network (no waiting). New events go to `out`.
    void poll(std::vector<NetEvent>& out);
    void sendState(const NetState& s);
    void sendFire(const Vec3& from, const Vec3& to, uint8_t weapon);
    void sendHit(uint8_t victim, float damage, bool head, uint8_t weapon);
    void sendDeath(uint8_t killer, bool head, uint8_t weapon);

private:
    void* host_ = nullptr;   // ENetHost*
    void* server_ = nullptr; // ENetPeer* (clients: the host)
    void* peers_[kNetMaxPlayers] = {};  // host: ENetPeer* by player id
    bool isHost_ = false;
    int myId_ = -1;
    float dustScale_ = 0.6f;
    double connectStarted_ = 0;
    void sendTo(void* peer, const std::vector<uint8_t>& msg, bool reliable);
    void broadcastExcept(int except, const std::vector<uint8_t>& msg, bool reliable);
    void handle(const uint8_t* data, size_t len, int fromPeerId, std::vector<NetEvent>& out);
};

#include "net.h"
#include <SDL3/SDL.h>
#include <enet/enet.h>
#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstdlib>
#include <cctype>
#include <cstring>

namespace {

// Messages: a type byte, then fixed little-endian fields. Channel 0 unreliable (states: a lost one
// is replaced by the next tick's), channel 1 reliable (everything else).
enum Msg : uint8_t { kWelcome = 1, kState, kFire, kHit, kDeath, kLeave, kName, kNade, kBots, kMatch, kRound, kPlant, kDefused };
// Why the host turned a connection away (the disconnect's data).
enum Reject : uint32_t { kRejectFull = 1, kRejectVersion = 2 };

struct Writer {
    std::vector<uint8_t> b;
    explicit Writer(Msg m) { b.push_back(m); }
    void u8(uint8_t v) { b.push_back(v); }
    void u16(uint16_t v) { b.push_back(uint8_t(v)); b.push_back(uint8_t(v >> 8)); }
    void u32(uint32_t v) { for (int k = 0; k < 4; ++k) b.push_back(uint8_t(v >> (8 * k))); }
    void f32(float v) { uint32_t u; std::memcpy(&u, &v, 4); u32(u); }
    void vec(const Vec3& v) { f32(v.x); f32(v.y); f32(v.z); }
    void str(const std::string& s) {
        u8(uint8_t(s.size()));
        b.insert(b.end(), s.begin(), s.end());
    }
    // A map position to a quarter unit (+-8191), for the bots' stream.
    void q16(float v) { u16(uint16_t(int16_t(std::lround(std::clamp(v, -8191.0f, 8191.0f) * 4.0f)))); }
};

struct Reader {
    const uint8_t* p;
    size_t n, i = 1;  // after the type byte
    bool ok = true;
    uint8_t u8() {
        if (i + 1 > n) { ok = false; return 0; }
        return p[i++];
    }
    uint16_t u16() {
        if (i + 2 > n) { ok = false; return 0; }
        uint16_t v = uint16_t(p[i] | (p[i + 1] << 8));
        i += 2;
        return v;
    }
    uint32_t u32() {
        if (i + 4 > n) { ok = false; return 0; }
        uint32_t v = 0;
        for (int k = 0; k < 4; ++k) v |= uint32_t(p[i++]) << (8 * k);
        return v;
    }
    float f32() { uint32_t u = u32(); float v; std::memcpy(&v, &u, 4); return std::isfinite(v) ? v : 0.0f; }
    Vec3 vec() { Vec3 v; v.x = f32(); v.y = f32(); v.z = f32(); return v; }
    float q16() { return float(int16_t(u16())) / 4.0f; }
    std::string str() {
        const size_t len = u8();
        if (i + len > n) { ok = false; return ""; }
        std::string s(reinterpret_cast<const char*>(p + i), len);
        i += len;
        return s;
    }
};

std::vector<uint8_t> nameMsg(int id, const std::string& name) {
    Writer w(kName);
    w.u8(uint8_t(id));
    w.str(name);
    return w.b;
}

bool g_enetReady = false;

bool initEnet(std::string& err) {
    if (g_enetReady) return true;
    if (enet_initialize() != 0) {
        err = "couldn't start networking";
        return false;
    }
    std::atexit(enet_deinitialize);
    g_enetReady = true;
    return true;
}

ENetHost* H(void* p) { return static_cast<ENetHost*>(p); }
ENetPeer* P(void* p) { return static_cast<ENetPeer*>(p); }
int peerId(ENetPeer* p) { return int(reinterpret_cast<intptr_t>(p->data)); }

// A copy of a client's message for everyone else, stamped with who really sent it (byte 1).
std::vector<uint8_t> stamped(const uint8_t* data, size_t len, int from) {
    std::vector<uint8_t> copy(data, data + len);
    copy[1] = uint8_t(from);
    return copy;
}

}  // namespace

bool Net::host(uint16_t port, float dustScale, uint8_t game, std::string& err) {
    stop();
    if (!initEnet(err)) return false;
    ENetAddress addr;
    addr.host = ENET_HOST_ANY;
    addr.port = port;
    host_ = enet_host_create(&addr, kNetMaxPlayers - 1, 2, 0, 0);
    if (!host_) {
        err = "couldn't open port " + std::to_string(port) + " (another game using it?)";
        return false;
    }
    isHost_ = true;
    myId_ = 0;
    dustScale_ = dustScale;
    game_ = game;
    names_[0] = myName_;
    return true;
}

bool Net::join(const std::string& address, uint16_t port, std::string& err) {
    stop();
    if (!initEnet(err)) return false;
    host_ = enet_host_create(nullptr, 1, 2, 0, 0);
    if (!host_) {
        err = "couldn't start networking";
        return false;
    }
    ENetAddress addr;
    if (enet_address_set_host(&addr, address.c_str()) != 0) {
        stop();
        err = "unknown address: " + address;
        return false;
    }
    addr.port = port;
    server_ = enet_host_connect(H(host_), &addr, 2, kNetProtocol);  // our version rides on the connect
    if (!server_) {
        stop();
        err = "couldn't connect";
        return false;
    }
    isHost_ = false;
    myId_ = -1;
    connectStarted_ = double(SDL_GetTicks()) / 1000.0;
    return true;
}

void Net::stop() {
    if (host_) {
        if (server_) enet_peer_disconnect_now(P(server_), 0);
        for (void*& p : peers_)
            if (p) enet_peer_disconnect_now(P(p), 0);
        enet_host_flush(H(host_));
        enet_host_destroy(H(host_));
    }
    host_ = server_ = nullptr;
    for (void*& p : peers_) p = nullptr;
    isHost_ = false;
    myId_ = -1;
}

int Net::players() const {
    if (!host_) return 0;
    int n = 1;
    for (void* p : peers_) n += p != nullptr;
    return n;
}

bool Net::connected(int id) const {
    if (id == myId_) return true;
    return isHost_ && id > 0 && id < kNetMaxPlayers && peers_[id] != nullptr;
}

int Net::pingMs() const { return server_ ? int(P(server_)->roundTripTime) : 0; }

void Net::sendTo(void* peer, const std::vector<uint8_t>& msg, bool reliable) {
    if (!peer) return;
    ENetPacket* pk = enet_packet_create(msg.data(), msg.size(), reliable ? ENET_PACKET_FLAG_RELIABLE : 0);
    enet_peer_send(P(peer), reliable ? 1 : 0, pk);
}

void Net::broadcastExcept(int except, const std::vector<uint8_t>& msg, bool reliable) {
    for (int id = 1; id < kNetMaxPlayers; ++id)
        if (id != except) sendTo(peers_[id], msg, reliable);
}

void Net::send(const std::vector<uint8_t>& msg, bool reliable) {
    if (isHost_) broadcastExcept(-1, msg, reliable);
    else sendTo(server_, msg, reliable);
}

void Net::poll(std::vector<NetEvent>& out) {
    if (!host_) return;
    ENetEvent e;
    while (host_ && enet_host_service(H(host_), &e, 0) > 0) {
        switch (e.type) {
            case ENET_EVENT_TYPE_CONNECT:
                if (isHost_) {  // a new player: give them an id, the map size and the game
                    if (e.data != kNetProtocol) {
                        enet_peer_disconnect(e.peer, kRejectVersion);
                        break;
                    }
                    int id = 1;
                    while (id < kNetMaxPlayers && peers_[id]) ++id;
                    if (id >= kNetMaxPlayers) {
                        enet_peer_disconnect(e.peer, kRejectFull);
                        break;
                    }
                    peers_[id] = e.peer;
                    e.peer->data = reinterpret_cast<void*>(intptr_t(id));
                    enet_peer_timeout(e.peer, 32, 4000, 10000);
                    Writer w(kWelcome);
                    w.u8(uint8_t(id));
                    w.f32(dustScale_);
                    w.u8(game_);
                    sendTo(e.peer, w.b, true);
                    for (int k = 0; k < kNetMaxPlayers; ++k)  // everyone's names so far
                        if (!names_[k].empty()) sendTo(e.peer, nameMsg(k, names_[k]), true);
                    NetEvent ev{NetEvent::Joined};
                    ev.from = uint8_t(id);
                    out.push_back(ev);
                }
                break;
            case ENET_EVENT_TYPE_RECEIVE:
                handle(e.packet->data, e.packet->dataLength, isHost_ ? peerId(e.peer) : 0, out);
                enet_packet_destroy(e.packet);
                break;
            case ENET_EVENT_TYPE_DISCONNECT:
                if (isHost_) {
                    const int id = peerId(e.peer);
                    if (id > 0 && id < kNetMaxPlayers && peers_[id] == e.peer) {
                        peers_[id] = nullptr;
                        names_[id].clear();
                        Writer w(kLeave);
                        w.u8(uint8_t(id));
                        broadcastExcept(-1, w.b, true);
                        NetEvent ev{NetEvent::Left};
                        ev.from = uint8_t(id);
                        out.push_back(ev);
                    }
                } else {  // lost the host (or never reached it)
                    NetEvent ev{myId_ < 0 ? NetEvent::Failed : NetEvent::Left};
                    ev.from = 0;
                    if (e.data == kRejectVersion) ev.text = "THE HOST HAS A DIFFERENT VERSION OF CRISP";
                    if (e.data == kRejectFull) ev.text = "THE GAME IS FULL";
                    out.push_back(ev);
                    server_ = nullptr;
                    stop();
                }
                break;
            default:
                break;
        }
    }
    if (host_ && !isHost_ && myId_ < 0 && double(SDL_GetTicks()) / 1000.0 - connectStarted_ > 6.0) {
        out.push_back(NetEvent{NetEvent::Failed});  // nobody answered
        stop();
    }
}

void Net::handle(const uint8_t* data, size_t len, int fromPeer, std::vector<NetEvent>& out) {
    if (len < 2) return;
    Reader r{data, len};
    NetEvent ev{NetEvent::State};
    const bool fromClient = isHost_;  // the host only hears from clients
    switch (data[0]) {
        case kWelcome:
            if (isHost_) return;
            myId_ = r.u8();
            ev.type = NetEvent::Connected;
            ev.dustScale = r.f32();
            ev.game = r.u8();
            if (!r.ok) { myId_ = -1; return; }
            enet_peer_timeout(P(server_), 32, 4000, 10000);
            sendTo(server_, nameMsg(myId_, myName_), true);  // tell everyone who you are
            break;
        case kState: {
            ev.state.id = r.u8();
            ev.state.tick = r.u32();
            ev.state.pos = r.vec();
            ev.state.yaw = r.f32();
            ev.state.pitch = r.f32();
            ev.state.flags = r.u8();
            ev.state.weapon = r.u8();
            ev.state.duck = r.u8();
            if (!r.ok) return;
            if (fromClient) {  // relay to everyone else, as sent (the id is the sender's)
                ev.state.id = uint8_t(fromPeer);
                broadcastExcept(fromPeer, stamped(data, len, fromPeer), false);
            }
            ev.from = ev.state.id;
            break;
        }
        case kFire:
            ev.type = NetEvent::Fire;
            ev.from = r.u8();
            ev.a = r.vec();
            ev.b = r.vec();
            ev.weapon = r.u8();
            if (!r.ok) return;
            if (fromClient) {
                ev.from = uint8_t(fromPeer);
                broadcastExcept(fromPeer, stamped(data, len, fromPeer), true);
            }
            break;
        case kHit:
            ev.type = NetEvent::Hit;
            ev.from = r.u8();
            ev.other = r.u8();
            ev.damage = r.f32();
            ev.group = r.u8();
            ev.weapon = r.u8();
            ev.head = ev.group == 0;
            if (!r.ok) return;
            if (fromClient) {  // for the player who was hit: forward it; for you or a bot (yours): it's yours
                ev.from = uint8_t(fromPeer);
                if (ev.other > 0 && ev.other < kNetMaxPlayers) {
                    sendTo(peers_[ev.other], stamped(data, len, fromPeer), true);
                    return;
                }
                if (ev.other >= kNetSlots) return;
            }
            break;
        case kDeath:
            ev.type = NetEvent::Death;
            ev.from = r.u8();   // victim
            ev.other = r.u8();  // killer
            ev.head = r.u8() != 0;
            ev.weapon = r.u8();
            if (!r.ok) return;
            if (fromClient) {  // a player tells everyone they died
                ev.from = uint8_t(fromPeer);
                broadcastExcept(fromPeer, stamped(data, len, fromPeer), true);
            }
            break;
        case kNade:
            ev.type = NetEvent::Nade;
            ev.from = r.u8();
            ev.weapon = r.u8();
            ev.a = r.vec();
            ev.b = r.vec();
            if (!r.ok || ev.weapon > 3) return;
            if (fromClient) {
                ev.from = uint8_t(fromPeer);
                broadcastExcept(fromPeer, stamped(data, len, fromPeer), true);
            }
            break;
        case kName:
            ev.type = NetEvent::Name;
            ev.from = r.u8();
            ev.text = cleanName(r.str());
            if (!r.ok) return;
            if (fromClient) {  // a client says who they are: remember it, tell the others
                ev.from = uint8_t(fromPeer);
                if (fromPeer > 0 && fromPeer < kNetMaxPlayers) names_[fromPeer] = ev.text;
                broadcastExcept(fromPeer, nameMsg(fromPeer, ev.text), true);
            }
            break;
        case kLeave:
            ev.type = NetEvent::Left;
            ev.from = r.u8();
            if (!r.ok) return;
            break;
        case kBots: {  // (host -> clients only)
            if (fromClient) return;
            ev.type = NetEvent::Bots;
            ev.tick = r.u32();
            ev.botCount = std::min<uint8_t>(r.u8(), uint8_t(kNetBots));
            for (int k = 0; k < ev.botCount; ++k) {
                NetBot& b = ev.bots[k];
                b.id = r.u8();
                b.pos.x = r.q16();
                b.pos.y = r.q16();
                b.pos.z = r.q16();
                b.yaw = float(int16_t(r.u16())) / 100.0f;
                b.pitch = float(int8_t(r.u8()));
                b.flags = r.u8();
                b.weapon = r.u8();
                if (b.id < kNetMaxPlayers || b.id >= kNetSlots) r.ok = false;
            }
            if (!r.ok) return;
            break;
        }
        case kMatch: {
            if (fromClient) return;
            ev.type = NetEvent::Match;
            NetMatch& m = ev.match;
            m.phase = r.u8();
            m.round = r.u8();
            m.sideA = r.u8() & 1;
            m.score[0] = r.u8();
            m.score[1] = r.u8();
            m.phaseLeft = r.f32();
            m.buyLeft = r.f32();
            m.bombLeft = r.f32();
            m.carrier = r.u8();
            m.dropped = r.vec();
            m.planted = r.u8();
            m.bombActive = r.u8();
            m.bombPos = r.vec();
            if (!r.ok) return;
            break;
        }
        case kRound: {
            if (fromClient) return;
            ev.type = NetEvent::Round;
            NetRound& rd = ev.round;
            rd.kind = r.u8();
            rd.flags = r.u8();
            for (uint8_t& t : rd.team) t = r.u8();
            for (uint8_t& s : rd.spawn) s = r.u8();
            rd.alive = r.u32();
            rd.sideA = r.u8() & 1;
            rd.nameOffset = r.u8();
            rd.winnerSide = r.u8() & 1;
            rd.why = r.u8();
            rd.mvp = r.u8();
            rd.pay[0] = r.u16();
            rd.pay[1] = r.u16();
            if (!r.ok) return;
            break;
        }
        case kPlant:  // (clients -> host only)
            if (!fromClient) return;
            ev.type = NetEvent::Plant;
            ev.from = uint8_t(fromPeer);
            r.u8();
            ev.a = r.vec();
            if (!r.ok) return;
            break;
        case kDefused:
            if (!fromClient) return;
            ev.type = NetEvent::Defused;
            ev.from = uint8_t(fromPeer);
            break;
        default:
            return;
    }
    out.push_back(ev);
}

std::string Net::cleanName(const std::string& name) {
    std::string out;
    for (char c : name) {
        const unsigned char u = static_cast<unsigned char>(c);
        if (std::isalnum(u) || c == '-' || c == '_') out.push_back(char(std::toupper(u)));
        if (out.size() >= 15) break;
    }
    return out.empty() ? "PLAYER" : out;
}

void Net::setName(const std::string& name) {
    myName_ = cleanName(name);
    if (!ready()) return;
    if (isHost_) {
        names_[0] = myName_;
        broadcastExcept(-1, nameMsg(0, myName_), true);
    } else {
        sendTo(server_, nameMsg(myId_, myName_), true);
    }
}

void Net::sendState(const NetState& s) {
    if (!ready()) return;
    Writer w(kState);
    w.u8(uint8_t(myId_));
    w.u32(s.tick);
    w.vec(s.pos);
    w.f32(s.yaw);
    w.f32(s.pitch);
    w.u8(s.flags);
    w.u8(s.weapon);
    w.u8(s.duck);
    send(w.b, false);
    enet_host_flush(H(host_));  // every tick, straight away: no waiting for the next poll
}

void Net::sendFire(const Vec3& from, const Vec3& to, uint8_t weapon, int as) {
    if (!ready()) return;
    Writer w(kFire);
    w.u8(uint8_t(as >= 0 ? as : myId_));
    w.vec(from);
    w.vec(to);
    w.u8(weapon);
    send(w.b, true);
}

void Net::sendHit(uint8_t victim, float damage, uint8_t group, uint8_t weapon, int as) {
    if (!ready()) return;
    Writer w(kHit);
    w.u8(uint8_t(as >= 0 ? as : myId_));
    w.u8(victim);
    w.f32(damage);
    w.u8(group);
    w.u8(weapon);
    if (isHost_) {
        if (victim > 0 && victim < kNetMaxPlayers) sendTo(peers_[victim], w.b, true);
    } else {
        sendTo(server_, w.b, true);
    }
    enet_host_flush(H(host_));
}

void Net::sendDeath(uint8_t victim, uint8_t killer, bool head, uint8_t weapon) {
    if (!ready()) return;
    Writer w(kDeath);
    w.u8(victim);
    w.u8(killer);
    w.u8(head ? 1 : 0);
    w.u8(weapon);
    send(w.b, true);
}

void Net::sendNade(int type, const Vec3& pos, const Vec3& vel, int as) {
    if (!ready()) return;
    Writer w(kNade);
    w.u8(uint8_t(as >= 0 ? as : myId_));
    w.u8(uint8_t(type));
    w.vec(pos);
    w.vec(vel);
    send(w.b, true);
}

void Net::sendBots(uint32_t tick, const NetBot* bots, int count) {
    if (!isHost_) return;
    Writer w(kBots);
    w.u32(tick);
    count = std::clamp(count, 0, kNetBots);
    w.u8(uint8_t(count));
    for (int k = 0; k < count; ++k) {
        const NetBot& b = bots[k];
        w.u8(b.id);
        w.q16(b.pos.x);
        w.q16(b.pos.y);
        w.q16(b.pos.z);
        float yaw = std::fmod(b.yaw, 360.0f);
        if (yaw > 180.0f) yaw -= 360.0f;
        if (yaw < -180.0f) yaw += 360.0f;
        w.u16(uint16_t(int16_t(std::lround(yaw * 100.0f))));
        w.u8(uint8_t(int8_t(std::lround(std::clamp(b.pitch, -89.0f, 89.0f)))));
        w.u8(b.flags);
        w.u8(b.weapon);
    }
    broadcastExcept(-1, w.b, false);
}

void Net::sendMatch(const NetMatch& m) {
    if (!isHost_) return;
    Writer w(kMatch);
    w.u8(m.phase);
    w.u8(m.round);
    w.u8(m.sideA);
    w.u8(m.score[0]);
    w.u8(m.score[1]);
    w.f32(m.phaseLeft);
    w.f32(m.buyLeft);
    w.f32(m.bombLeft);
    w.u8(m.carrier);
    w.vec(m.dropped);
    w.u8(m.planted);
    w.u8(m.bombActive);
    w.vec(m.bombPos);
    broadcastExcept(-1, w.b, true);
}

void Net::sendRound(const NetRound& rd, int to) {
    if (!isHost_) return;
    Writer w(kRound);
    w.u8(rd.kind);
    w.u8(rd.flags);
    for (uint8_t t : rd.team) w.u8(t);
    for (uint8_t s : rd.spawn) w.u8(s);
    w.u32(rd.alive);
    w.u8(rd.sideA);
    w.u8(rd.nameOffset);
    w.u8(rd.winnerSide);
    w.u8(rd.why);
    w.u8(rd.mvp);
    w.u16(rd.pay[0]);
    w.u16(rd.pay[1]);
    if (to > 0 && to < kNetMaxPlayers) sendTo(peers_[to], w.b, true);
    else broadcastExcept(-1, w.b, true);
}

void Net::sendPlant(const Vec3& pos) {
    if (!ready() || isHost_) return;
    Writer w(kPlant);
    w.u8(uint8_t(myId_));
    w.vec(pos);
    sendTo(server_, w.b, true);
}

void Net::sendDefused() {
    if (!ready() || isHost_) return;
    Writer w(kDefused);
    w.u8(uint8_t(myId_));
    sendTo(server_, w.b, true);
}

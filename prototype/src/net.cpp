#include "net.h"
#include <SDL3/SDL.h>
#include <enet/enet.h>
#include <cstdint>
#include <cstdlib>
#include <cstring>

namespace {

// Messages: a type byte, then fixed little-endian fields. Channel 0 unreliable (states: a lost one
// is replaced by the next tick's), channel 1 reliable (everything else).
enum Msg : uint8_t { kWelcome = 1, kState, kFire, kHit, kDeath, kLeave };

struct Writer {
    std::vector<uint8_t> b;
    explicit Writer(Msg m) { b.push_back(m); }
    void u8(uint8_t v) { b.push_back(v); }
    void u32(uint32_t v) { for (int k = 0; k < 4; ++k) b.push_back(uint8_t(v >> (8 * k))); }
    void f32(float v) { uint32_t u; std::memcpy(&u, &v, 4); u32(u); }
    void vec(const Vec3& v) { f32(v.x); f32(v.y); f32(v.z); }
};

struct Reader {
    const uint8_t* p;
    size_t n, i = 1;  // after the type byte
    bool ok = true;
    uint8_t u8() {
        if (i + 1 > n) { ok = false; return 0; }
        return p[i++];
    }
    uint32_t u32() {
        if (i + 4 > n) { ok = false; return 0; }
        uint32_t v = 0;
        for (int k = 0; k < 4; ++k) v |= uint32_t(p[i++]) << (8 * k);
        return v;
    }
    float f32() { uint32_t u = u32(); float v; std::memcpy(&v, &u, 4); return v; }
    Vec3 vec() { Vec3 v; v.x = f32(); v.y = f32(); v.z = f32(); return v; }
};

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

}  // namespace

bool Net::host(uint16_t port, float dustScale, std::string& err) {
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
    server_ = enet_host_connect(H(host_), &addr, 2, 0);
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

void Net::poll(std::vector<NetEvent>& out) {
    if (!host_) return;
    ENetEvent e;
    while (host_ && enet_host_service(H(host_), &e, 0) > 0) {
        switch (e.type) {
            case ENET_EVENT_TYPE_CONNECT:
                if (isHost_) {  // a new player: give them an id and the map size
                    int id = 1;
                    while (id < kNetMaxPlayers && peers_[id]) ++id;
                    if (id >= kNetMaxPlayers) {
                        enet_peer_disconnect(e.peer, 0);  // full
                        break;
                    }
                    peers_[id] = e.peer;
                    e.peer->data = reinterpret_cast<void*>(intptr_t(id));
                    enet_peer_timeout(e.peer, 32, 4000, 10000);
                    Writer w(kWelcome);
                    w.u8(uint8_t(id));
                    w.f32(dustScale_);
                    sendTo(e.peer, w.b, true);
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
    if (len < 1) return;
    Reader r{data, len};
    NetEvent ev{NetEvent::State};
    switch (data[0]) {
        case kWelcome:
            if (isHost_) return;
            myId_ = r.u8();
            ev.type = NetEvent::Connected;
            ev.dustScale = r.f32();
            if (!r.ok) return;
            enet_peer_timeout(P(server_), 32, 4000, 10000);
            break;
        case kState: {
            ev.state.id = r.u8();
            ev.state.tick = r.u32();
            ev.state.pos = r.vec();
            ev.state.yaw = r.f32();
            ev.state.pitch = r.f32();
            ev.state.flags = r.u8();
            ev.state.weapon = r.u8();
            if (!r.ok) return;
            if (isHost_) {  // relay to everyone else, as sent (the id is the sender's)
                ev.state.id = uint8_t(fromPeer);
                std::vector<uint8_t> copy(data, data + len);
                copy[1] = uint8_t(fromPeer);
                broadcastExcept(fromPeer, copy, false);
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
            if (isHost_) {
                std::vector<uint8_t> copy(data, data + len);
                copy[1] = uint8_t(fromPeer);
                ev.from = uint8_t(fromPeer);
                broadcastExcept(fromPeer, copy, true);
            }
            break;
        case kHit:
            ev.type = NetEvent::Hit;
            ev.from = r.u8();
            ev.other = r.u8();
            ev.damage = r.f32();
            ev.head = r.u8() != 0;
            ev.weapon = r.u8();
            if (!r.ok) return;
            if (isHost_) {  // for whoever was hit: forward it, or it's for you
                ev.from = uint8_t(fromPeer);
                if (ev.other != 0) {
                    std::vector<uint8_t> copy(data, data + len);
                    copy[1] = uint8_t(fromPeer);
                    if (ev.other < kNetMaxPlayers) sendTo(peers_[ev.other], copy, true);
                    return;
                }
            }
            break;
        case kDeath:
            ev.type = NetEvent::Death;
            ev.from = r.u8();   // victim
            ev.other = r.u8();  // killer
            ev.head = r.u8() != 0;
            ev.weapon = r.u8();
            if (!r.ok) return;
            if (isHost_) {
                std::vector<uint8_t> copy(data, data + len);
                copy[1] = uint8_t(fromPeer);
                ev.from = uint8_t(fromPeer);
                broadcastExcept(fromPeer, copy, true);
            }
            break;
        case kLeave:
            ev.type = NetEvent::Left;
            ev.from = r.u8();
            if (!r.ok) return;
            break;
        default:
            return;
    }
    out.push_back(ev);
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
    if (isHost_) broadcastExcept(-1, w.b, false);
    else sendTo(server_, w.b, false);
    enet_host_flush(H(host_));  // every tick, straight away: no waiting for the next poll
}

void Net::sendFire(const Vec3& from, const Vec3& to, uint8_t weapon) {
    if (!ready()) return;
    Writer w(kFire);
    w.u8(uint8_t(myId_));
    w.vec(from);
    w.vec(to);
    w.u8(weapon);
    if (isHost_) broadcastExcept(-1, w.b, true);
    else sendTo(server_, w.b, true);
}

void Net::sendHit(uint8_t victim, float damage, bool head, uint8_t weapon) {
    if (!ready()) return;
    Writer w(kHit);
    w.u8(uint8_t(myId_));
    w.u8(victim);
    w.f32(damage);
    w.u8(head ? 1 : 0);
    w.u8(weapon);
    if (isHost_) {
        if (victim < kNetMaxPlayers) sendTo(peers_[victim], w.b, true);
    } else {
        sendTo(server_, w.b, true);
    }
    enet_host_flush(H(host_));
}

void Net::sendDeath(uint8_t killer, bool head, uint8_t weapon) {
    if (!ready()) return;
    Writer w(kDeath);
    w.u8(uint8_t(myId_));
    w.u8(killer);
    w.u8(head ? 1 : 0);
    w.u8(weapon);
    if (isHost_) broadcastExcept(-1, w.b, true);
    else sendTo(server_, w.b, true);
}

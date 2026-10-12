#include "replay.h"
#include <algorithm>
#include <cmath>
#include <cstring>

namespace {

float lerpAngle(float a, float b, float t) {
    float d = std::fmod(b - a + 540.0f, 360.0f) - 180.0f;
    return a + d * t;
}

}  // namespace

void Replay::clear() {
    head_ = count_ = 0;
    shots_.clear();
    mark_ = -1;
}

void Replay::setCapacity(size_t frames, size_t shots) {
    clear();
    cap_ = std::max<size_t>(2, frames);
    shotCap_ = shots;
    if (ring_.size() > cap_) ring_.resize(cap_);
}

ReplayFrame& Replay::next(double t) {
    const size_t cap = cap_;
    if (ring_.size() < cap) ring_.resize(cap);
    ReplayFrame* f;
    if (count_ < cap) {
        f = &ring_[(head_ + count_) % cap];
        ++count_;
    } else {  // full: the oldest goes
        f = &ring_[head_];
        head_ = (head_ + 1) % cap;
    }
    f->t = t;
    f->agents.clear();  // (keeps their capacity)
    f->smokes.clear();
    f->nades.clear();
    f->fires.clear();
    // Shots older than what's kept go too.
    const double oldest = at(0).t;
    if (!shots_.empty() && shots_.front().t < oldest - 1.0)
        shots_.erase(shots_.begin(), std::find_if(shots_.begin(), shots_.end(), [&](const ReplayShot& s) { return s.t >= oldest; }));
    return *f;
}

void Replay::shot(const ReplayShot& s) {
    if (shots_.size() < shotCap_) shots_.push_back(s);
}

double Replay::start() const { return count_ ? at(0).t : 0.0; }
double Replay::end() const { return count_ ? at(count_ - 1).t : 0.0; }

bool Replay::sample(double t, ReplayFrame& out) const {
    if (count_ == 0) return false;
    t = std::clamp(t, start(), end());
    // The last frame at or before t (frames are in time order).
    size_t lo = 0, hi = count_ - 1;
    while (lo < hi) {
        const size_t mid = (lo + hi + 1) / 2;
        if (at(mid).t <= t) lo = mid;
        else hi = mid - 1;
    }
    const ReplayFrame& a = at(lo);
    const ReplayFrame& b = at(std::min(lo + 1, count_ - 1));
    const float k = b.t > a.t ? float((t - a.t) / (b.t - a.t)) : 0.0f;
    out.t = t;
    out.smokes = a.smokes;
    out.fires = a.fires;
    out.nades = a.nades;
    if (b.nades.size() == a.nades.size())  // (the same grenades still flying: in between)
        for (size_t i = 0; i < out.nades.size(); ++i)
            if (b.nades[i].type == a.nades[i].type) out.nades[i].pos = a.nades[i].pos + (b.nades[i].pos - a.nades[i].pos) * k;
    out.agents.resize(a.agents.size());
    for (size_t i = 0; i < a.agents.size(); ++i) {
        ReplayAgent r = a.agents[i];
        if (i < b.agents.size() && b.agents[i].alive == r.alive) {  // (no sliding across a respawn)
            const ReplayAgent& n = b.agents[i];
            r.pos = r.pos + (n.pos - r.pos) * k;
            r.yaw = lerpAngle(r.yaw, n.yaw, k);
            r.pitch += (n.pitch - r.pitch) * k;
            r.crouch += (n.crouch - r.crouch) * k;
            r.deadFor += (n.deadFor - r.deadFor) * k;
            r.stepDist += (n.stepDist - r.stepDist) * k;
        }
        out.agents[i] = r;
    }
    return true;
}

void Replay::shotsBetween(double t0, double t1, std::vector<ReplayShot>& out) const {
    out.clear();
    for (const ReplayShot& s : shots_)
        if (s.t > t0 && s.t <= t1) out.push_back(s);
}

// ---- Saved matches ----
// The file: "CRISPREC", version, map, mode, flags, the result (patched on close: scores, seconds), the date, the names;
// then records: 1 a frame, 2 a shot, 3 an event. Little-endian, floats as they are in memory (x86 / x64).
namespace {

constexpr char kTapeMagic[8] = {'C', 'R', 'I', 'S', 'P', 'R', 'E', 'C'};
constexpr uint32_t kTapeVersion = 1;
constexpr std::streamoff kResultAt = 8 + 4 + 3;  // where the scores and seconds are (patched on close)

void put8(std::vector<uint8_t>& b, uint8_t v) { b.push_back(v); }
void put16(std::vector<uint8_t>& b, int v) {
    const uint16_t u = uint16_t(int16_t(std::clamp(v, -32768, 32767)));
    b.push_back(uint8_t(u));
    b.push_back(uint8_t(u >> 8));
}
void putF(std::vector<uint8_t>& b, float v) {
    uint8_t x[4];
    std::memcpy(x, &v, 4);
    b.insert(b.end(), x, x + 4);
}
void putV(std::vector<uint8_t>& b, const Vec3& v) { putF(b, v.x); putF(b, v.y); putF(b, v.z); }
void putS(std::vector<uint8_t>& b, const std::string& s) {
    const size_t n = std::min<size_t>(s.size(), 255);
    b.push_back(uint8_t(n));
    b.insert(b.end(), s.begin(), s.begin() + std::ptrdiff_t(n));
}

struct In {
    std::vector<uint8_t> d;
    size_t i = 0;
    bool ok = true;
    bool need(size_t n) { ok = ok && i + n <= d.size(); return ok; }
    uint8_t u8() { return need(1) ? d[i++] : 0; }
    int s16() {
        if (!need(2)) return 0;
        const uint16_t u = uint16_t(d[i] | (d[i + 1] << 8));
        i += 2;
        return int16_t(u);
    }
    float f() {
        if (!need(4)) return 0;
        float v;
        std::memcpy(&v, &d[i], 4);
        i += 4;
        return std::isfinite(v) ? v : 0.0f;
    }
    Vec3 v() { Vec3 r; r.x = f(); r.y = f(); r.z = f(); return r; }
    std::string s() {
        const size_t n = u8();
        if (!need(n)) return "";
        std::string r(reinterpret_cast<const char*>(&d[i]), n);
        i += n;
        return r;
    }
};

}  // namespace

bool MatchWriter::open(const std::string& path, const MatchInfo& info, double startTime) {
    close(0, 0);
    file_.open(path, std::ios::binary);
    if (!file_) return false;
    path_ = path;
    t0_ = startTime;
    last_ = 0;
    buf_.clear();
    buf_.insert(buf_.end(), kTapeMagic, kTapeMagic + 8);
    for (int k = 0; k < 4; ++k) buf_.push_back(uint8_t(kTapeVersion >> (8 * k)));
    put8(buf_, uint8_t(info.map));
    put8(buf_, uint8_t(info.mode));
    put8(buf_, uint8_t((info.wingman ? 1 : 0) | (info.armsRace ? 2 : 0)));
    put16(buf_, 0);  // the result: patched on close
    put16(buf_, 0);
    putF(buf_, 0.0f);
    putS(buf_, info.date);
    put8(buf_, uint8_t(std::min<size_t>(info.names.size(), 255)));
    for (size_t k = 0; k < info.names.size() && k < 255; ++k) putS(buf_, info.names[k]);
    file_.write(reinterpret_cast<const char*>(buf_.data()), std::streamsize(buf_.size()));
    return bool(file_);
}

void MatchWriter::frame(const ReplayFrame& f) {
    if (!isOpen()) return;
    buf_.clear();
    put8(buf_, 1);
    last_ = f.t - t0_;
    putF(buf_, float(last_));
    put8(buf_, uint8_t(std::min<size_t>(f.agents.size(), 255)));
    for (size_t k = 0; k < f.agents.size() && k < 255; ++k) {
        const ReplayAgent& a = f.agents[k];
        putV(buf_, a.pos);
        put16(buf_, int(std::lround(a.yaw * 100.0f)));
        put16(buf_, int(std::lround(a.pitch * 100.0f)));
        put8(buf_, uint8_t(std::clamp(a.crouch, 0.0f, 1.0f) * 255.0f + 0.5f));
        put8(buf_, uint8_t(std::clamp(a.deadFor, 0.0f, 5.0f) * 50.0f + 0.5f));
        putF(buf_, std::fmod(a.stepDist, 4096.0f));
        put8(buf_, a.weapon);
        put8(buf_, a.nade);
        put8(buf_, a.team);
        put8(buf_, uint8_t((a.alive ? 1 : 0) | (a.friendly ? 2 : 0) | (a.lostHelmet ? 4 : 0)));
        for (float c : {a.hitDir.x, a.hitDir.y, a.hitDir.z}) put8(buf_, uint8_t(int8_t(std::clamp(c, -1.0f, 1.0f) * 127.0f)));
    }
    put8(buf_, uint8_t(std::min<size_t>(f.smokes.size(), 255)));
    for (size_t k = 0; k < f.smokes.size() && k < 255; ++k) { putV(buf_, f.smokes[k].pos); putF(buf_, float(f.smokes[k].start - t0_)); }
    put8(buf_, uint8_t(std::min<size_t>(f.nades.size(), 255)));
    for (size_t k = 0; k < f.nades.size() && k < 255; ++k) { putV(buf_, f.nades[k].pos); put8(buf_, f.nades[k].type); }
    put8(buf_, uint8_t(std::min<size_t>(f.fires.size(), 255)));
    for (size_t k = 0; k < f.fires.size() && k < 255; ++k) { putV(buf_, f.fires[k].pos); putF(buf_, float(f.fires[k].start - t0_)); }
    file_.write(reinterpret_cast<const char*>(buf_.data()), std::streamsize(buf_.size()));
}

void MatchWriter::shot(const ReplayShot& s) {
    if (!isOpen()) return;
    buf_.clear();
    put8(buf_, 2);
    putF(buf_, float(s.t - t0_));
    putV(buf_, s.from);
    putV(buf_, s.to);
    put8(buf_, uint8_t(std::clamp(s.shooter, 0, 255)));
    put8(buf_, s.weapon);
    file_.write(reinterpret_cast<const char*>(buf_.data()), std::streamsize(buf_.size()));
}

void MatchWriter::event(const MatchEvent& e) {
    if (!isOpen()) return;
    buf_.clear();
    put8(buf_, 3);
    putF(buf_, float(e.t - t0_));
    for (uint8_t v : {e.kind, e.round, e.scoreA, e.scoreB, e.killer, e.victim}) put8(buf_, v);
    file_.write(reinterpret_cast<const char*>(buf_.data()), std::streamsize(buf_.size()));
}

void MatchWriter::close(int scoreA, int scoreB) {
    if (!isOpen()) return;
    buf_.clear();
    put16(buf_, scoreA);
    put16(buf_, scoreB);
    putF(buf_, float(last_));
    file_.seekp(kResultAt);
    file_.write(reinterpret_cast<const char*>(buf_.data()), std::streamsize(buf_.size()));
    file_.close();
}

bool readMatch(const std::string& path, MatchInfo& info, Replay* into, std::vector<MatchEvent>* events) {
    std::ifstream file(path, std::ios::binary);
    if (!file) return false;
    In in;
    if (into || events) {
        in.d.assign(std::istreambuf_iterator<char>(file), std::istreambuf_iterator<char>());
    } else {  // (the list: the header is enough)
        in.d.resize(4096);
        file.read(reinterpret_cast<char*>(in.d.data()), std::streamsize(in.d.size()));
        in.d.resize(size_t(file.gcount()));
    }
    if (in.d.size() < 8 || std::memcmp(in.d.data(), kTapeMagic, 8) != 0) return false;
    in.i = 8;
    uint32_t version = 0;
    for (int k = 0; k < 4; ++k) version |= uint32_t(in.u8()) << (8 * k);
    if (version != kTapeVersion) return false;
    info = MatchInfo{};
    info.map = in.u8();
    info.mode = in.u8();
    const uint8_t flags = in.u8();
    info.wingman = flags & 1;
    info.armsRace = (flags & 2) != 0;
    info.scoreA = in.s16();
    info.scoreB = in.s16();
    info.seconds = in.f();
    info.date = in.s();
    const int names = in.u8();
    for (int k = 0; k < names; ++k) info.names.push_back(in.s());
    if (!in.ok) return false;
    if (!into && !events) return true;
    // Count the frames and shots first, to size the replay to hold all of it.
    size_t frames = 0, shots = 0;
    const size_t body = in.i;
    if (events) events->clear();
    for (int pass = 0; pass < 2; ++pass) {
        in.i = body;
        if (pass == 1 && into) into->setCapacity(frames + 1, shots + 1);
        while (in.ok && in.i < in.d.size()) {
            const uint8_t type = in.u8();
            const double t = in.f();
            if (type == 1) {
                ReplayFrame* f = pass == 1 && into ? &into->next(t) : nullptr;
                const int n = in.u8();
                for (int k = 0; k < n; ++k) {
                    ReplayAgent a;
                    a.pos = in.v();
                    a.yaw = float(in.s16()) / 100.0f;
                    a.pitch = float(in.s16()) / 100.0f;
                    a.crouch = float(in.u8()) / 255.0f;
                    a.deadFor = float(in.u8()) / 50.0f;
                    a.stepDist = in.f();
                    a.weapon = in.u8();
                    a.nade = in.u8();
                    a.team = in.u8();
                    const uint8_t fl = in.u8();
                    a.alive = fl & 1;
                    a.friendly = (fl & 2) != 0;
                    a.lostHelmet = (fl & 4) != 0;
                    for (float* c : {&a.hitDir.x, &a.hitDir.y, &a.hitDir.z}) *c = float(int8_t(in.u8())) / 127.0f;
                    if (f) f->agents.push_back(a);
                }
                for (int k = in.u8(); k > 0; --k) {
                    const Vec3 p = in.v();
                    const double st = in.f();
                    if (f) f->smokes.push_back({p, st});
                }
                for (int k = in.u8(); k > 0; --k) {
                    const Vec3 p = in.v();
                    const uint8_t ty = in.u8();
                    if (f) f->nades.push_back({p, ty});
                }
                for (int k = in.u8(); k > 0; --k) {
                    const Vec3 p = in.v();
                    const double st = in.f();
                    if (f) f->fires.push_back({p, st});
                }
                ++frames;
            } else if (type == 2) {
                ReplayShot s;
                s.t = t;
                s.from = in.v();
                s.to = in.v();
                s.shooter = in.u8();
                s.weapon = in.u8();
                if (pass == 1 && into && in.ok) into->shot(s);
                ++shots;
            } else if (type == 3) {
                MatchEvent e;
                e.t = t;
                e.kind = in.u8();
                e.round = in.u8();
                e.scoreA = in.u8();
                e.scoreB = in.u8();
                e.killer = in.u8();
                e.victim = in.u8();
                if (pass == 1 && events && in.ok) events->push_back(e);
            } else {
                break;  // (anything else: a broken end)
            }
        }
        if (pass == 0) in.ok = true;  // (a file cut short mid-record still plays up to there)
        else frames = 0;
    }
    return true;
}

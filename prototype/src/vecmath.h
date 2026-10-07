// Minimal vector/matrix math. World is Z-up, 1 unit ~= 1 inch (Source-style).
#pragma once
#include <cmath>

constexpr float kPi = 3.14159265358979323846f;
constexpr float kDegToRad = kPi / 180.0f;

struct Vec3 {
    float x = 0, y = 0, z = 0;
    Vec3() = default;
    constexpr Vec3(float x_, float y_, float z_) : x(x_), y(y_), z(z_) {}
    Vec3 operator+(const Vec3& o) const { return {x + o.x, y + o.y, z + o.z}; }
    Vec3 operator-(const Vec3& o) const { return {x - o.x, y - o.y, z - o.z}; }
    Vec3 operator*(float s) const { return {x * s, y * s, z * s}; }
    Vec3 operator-() const { return {-x, -y, -z}; }
    Vec3& operator+=(const Vec3& o) { x += o.x; y += o.y; z += o.z; return *this; }
    Vec3& operator-=(const Vec3& o) { x -= o.x; y -= o.y; z -= o.z; return *this; }
    Vec3& operator*=(float s) { x *= s; y *= s; z *= s; return *this; }
    float& operator[](int i) { return i == 0 ? x : (i == 1 ? y : z); }
    float operator[](int i) const { return i == 0 ? x : (i == 1 ? y : z); }
};

inline float dot(const Vec3& a, const Vec3& b) { return a.x * b.x + a.y * b.y + a.z * b.z; }
inline Vec3 cross(const Vec3& a, const Vec3& b) {
    return {a.y * b.z - a.z * b.y, a.z * b.x - a.x * b.z, a.x * b.y - a.y * b.x};
}
inline float length(const Vec3& v) { return std::sqrt(dot(v, v)); }
inline float length2d(const Vec3& v) { return std::sqrt(v.x * v.x + v.y * v.y); }
inline Vec3 normalize(const Vec3& v) {
    float l = length(v);
    return l > 0 ? v * (1.0f / l) : Vec3{};
}
inline Vec3 lerp(const Vec3& a, const Vec3& b, float t) { return a + (b - a) * t; }

// Source convention: pitch > 0 looks down, yaw > 0 turns left (counter-clockwise from +X).
inline Vec3 anglesToForward(float pitchDeg, float yawDeg) {
    float p = pitchDeg * kDegToRad, y = yawDeg * kDegToRad;
    return {std::cos(p) * std::cos(y), std::cos(p) * std::sin(y), -std::sin(p)};
}
inline Vec3 yawToRight(float yawDeg) {
    float y = yawDeg * kDegToRad;
    return {std::sin(y), -std::cos(y), 0};
}

// Column-major 4x4 matrix (OpenGL layout): m[col * 4 + row].
struct Mat4 {
    float m[16] = {};
};

inline Mat4 operator*(const Mat4& a, const Mat4& b) {
    Mat4 r;
    for (int c = 0; c < 4; ++c)
        for (int rr = 0; rr < 4; ++rr) {
            float s = 0;
            for (int k = 0; k < 4; ++k) s += a.m[k * 4 + rr] * b.m[c * 4 + k];
            r.m[c * 4 + rr] = s;
        }
    return r;
}

inline Mat4 perspective(float vfovRad, float aspect, float zNear, float zFar) {
    Mat4 r;
    float f = 1.0f / std::tan(vfovRad * 0.5f);
    r.m[0] = f / aspect;
    r.m[5] = f;
    r.m[10] = (zFar + zNear) / (zNear - zFar);
    r.m[11] = -1.0f;
    r.m[14] = 2.0f * zFar * zNear / (zNear - zFar);
    return r;
}

inline Mat4 viewFromAngles(const Vec3& eye, float pitchDeg, float yawDeg) {
    Vec3 f = anglesToForward(pitchDeg, yawDeg);
    Vec3 r = yawToRight(yawDeg);
    Vec3 u = cross(r, f);
    Mat4 v;
    v.m[0] = r.x;  v.m[4] = r.y;  v.m[8] = r.z;   v.m[12] = -dot(r, eye);
    v.m[1] = u.x;  v.m[5] = u.y;  v.m[9] = u.z;   v.m[13] = -dot(u, eye);
    v.m[2] = -f.x; v.m[6] = -f.y; v.m[10] = -f.z; v.m[14] = dot(f, eye);
    v.m[15] = 1.0f;
    return v;
}

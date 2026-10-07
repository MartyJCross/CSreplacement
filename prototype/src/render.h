// Minimal OpenGL 3.3 renderer: every 3D object is an instanced box, the HUD is one textured batch.
#pragma once
#include <cstdint>
#include <string>
#include <vector>
#include "vecmath.h"

struct BoxInstance {
    float mins[3];
    float maxs[3];
    uint8_t rgba[4];  // alpha selects shading: 255 = lit + world grid, 0 = lit, 128 = unlit (emissive)
    float rot[3] = {0, 0, 0};  // optional yaw: pivot x, pivot y, angle in radians (0 = axis-aligned)
};

BoxInstance makeBox(const Vec3& mins, const Vec3& maxs, uint32_t rgb, bool grid);
BoxInstance makeEmissive(const Vec3& mins, const Vec3& maxs, uint32_t rgb);
// Turns a box around the vertical axis through `pivot` (player models facing a direction).
inline void yawBox(BoxInstance& b, const Vec3& pivot, float yawRad) {
    b.rot[0] = pivot.x;
    b.rot[1] = pivot.y;
    b.rot[2] = yawRad;
}

struct HudVert {
    float x, y, u, v;
    uint8_t rgba[4];
};

struct HudBatch {
    std::vector<HudVert> verts;
    int fontScale = 2;

    void clear() { verts.clear(); }
    void rect(float x, float y, float w, float h, uint32_t rgba);
    void line(float x0, float y0, float x1, float y1, float thickness, uint32_t rgba);
    void text(float x, float y, const std::string& s, uint32_t rgba, int scale = 0);  // scale 0 = fontScale
    float textWidth(const std::string& s, int scale = 0) const;
};

class Renderer {
public:
    bool init(std::string& err);
    void setStaticBoxes(const std::vector<BoxInstance>& boxes);

    // Ring buffer of impact decals that persists between frames (updated only when it changes).
    void addDecal(const BoxInstance& b);
    void clearDecals();

    void beginFrame(int width, int height);
    void drawBoxes(const Mat4& viewProj, const Vec3& eye, const std::vector<BoxInstance>& dynamicBoxes);
    // Boxes in a local space transformed by `model` (weapon models, tracers).
    void drawModel(const Mat4& viewProj, const Mat4& model, const std::vector<BoxInstance>& boxes);
    // Starts the first-person weapon pass: fresh depth so the weapon never clips into walls.
    void clearDepth();
    // Uploads the HUD only when `changed` is true; otherwise redraws the last upload.
    void drawHud(const HudBatch& hud, bool changed);

    bool screenshot(const std::string& path);

private:
    unsigned boxProgram_ = 0, hudProgram_ = 0;
    void uploadDynamic(const std::vector<BoxInstance>& boxes, int n);
    int uViewProj_ = -1, uModel_ = -1, uEye_ = -1, uScreen_ = -1, uFont_ = -1;
    unsigned cubeVbo_ = 0;
    unsigned staticVao_ = 0, staticInst_ = 0;
    unsigned dynVao_ = 0, dynInst_ = 0;
    unsigned decalVao_ = 0, decalInst_ = 0;
    unsigned hudVao_ = 0, hudVbo_ = 0, fontTex_ = 0;
    int staticCount_ = 0, decalCount_ = 0, decalNext_ = 0, hudCount_ = 0;
    int width_ = 0, height_ = 0;
};

constexpr int kMaxDecals = 1024;
constexpr int kMaxDynamicBoxes = 1024;

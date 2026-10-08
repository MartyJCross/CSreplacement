// Minimal OpenGL 3.3 renderer: every 3D object is an instanced box, the HUD is one textured batch.
#pragma once
#include <cstdint>
#include <string>
#include <utility>
#include <vector>
#include "vecmath.h"

struct BoxInstance {
    float mins[3];
    float maxs[3];
    uint8_t rgba[4];  // alpha selects shading: 255 = lit + world grid, 0 = lit, 128 = unlit (emissive)
    float rot[3] = {0, 0, 0};  // optional yaw: pivot x, pivot y, angle in radians (0 = axis-aligned)
    float slope[2] = {0, 0};   // ramps: which way the top rises (world.h Slope), top height at the low edge
};

BoxInstance makeBox(const Vec3& mins, const Vec3& maxs, uint32_t rgb, bool grid);
BoxInstance makeEmissive(const Vec3& mins, const Vec3& maxs, uint32_t rgb);
// A skin-painted part (alpha 64): drawn with the model's PaintParams pattern; `shade` (grey) darkens it a
// little for grips and furniture so the parts still read apart.
BoxInstance makePainted(const Vec3& mins, const Vec3& maxs, uint8_t shade = 255);

// A skin for one drawModel call: the pattern (items.h Pattern), its colours, wear, polish and the model's
// length along z (fades and flames run along it).
struct PaintParams {
    int pattern = -1;  // -1 = no paint (painted parts then just use their shade)
    float a[3] = {1, 1, 1}, b[3] = {1, 1, 1}, c[3] = {1, 1, 1};
    float wear = 0, gloss = 0, seed = 0;
    float zMin = -24, zMax = 16;
};
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
    // Any four-cornered shape, corners in order (a wedge of the buy wheel).
    void quad(float x0, float y0, float x1, float y1, float x2, float y2, float x3, float y3, uint32_t rgba);
    void text(float x, float y, const std::string& s, uint32_t rgba, int scale = 0);  // scale 0 = fontScale
    float textWidth(const std::string& s, int scale = 0) const;
};

class Renderer {
public:
    bool init(std::string& err);
    void setStaticBoxes(const std::vector<BoxInstance>& boxes);
    // Surface textures (assets/textures/*.jpg, 512 px detail maps) for the stone, wood and metal surfaces.
    // Returns how many loaded; 0 = none (missing files): the shader falls back to its procedural patterns.
    int loadTextures(const std::string& dir);

    // Ring buffer of impact decals that persists between frames (updated only when it changes).
    void addDecal(const BoxInstance& b);
    void clearDecals();

    void beginFrame(int width, int height);
    void drawBoxes(const Mat4& viewProj, const Vec3& eye, const std::vector<BoxInstance>& dynamicBoxes);
    // Boxes in a local space transformed by `model` (weapon models, tracers); `paint` skins the painted parts.
    void drawModel(const Mat4& viewProj, const Mat4& model, const std::vector<BoxInstance>& boxes,
                   const PaintParams* paint = nullptr);
    // The sky, behind everything already drawn: camera forward, and right/up scaled by tan(half fov).
    void drawSky(const Vec3& fwd, const Vec3& right, const Vec3& up);
    // Starts the first-person weapon pass: fresh depth so the weapon never clips into walls.
    void clearDepth();
    // Depth pre-pass for the world: each pixel is shaded once (on by default; off for comparison).
    void setDepthPrepass(bool on) { depthPrepass_ = on; }
    // Uploads the HUD only when `changed` is true; otherwise redraws the last upload.
    void drawHud(const HudBatch& hud, bool changed);

    bool screenshot(const std::string& path);

private:
    unsigned boxProgram_ = 0, hudProgram_ = 0, depthProgram_ = 0, skyProgram_ = 0, skyVao_ = 0;
    int uSkyFwd_ = -1, uSkyRight_ = -1, uSkyUp_ = -1;
    unsigned texArray_ = 0;
    int uTex_ = -1, uHasTex_ = -1;
    int uDepthViewProj_ = -1, uDepthModel_ = -1;
    bool depthPrepass_ = true;
    // World boxes kept on the CPU so each frame can drop the ones off screen and draw the rest
    // nearest-first (the GPU then rejects hidden pixels early).
    std::vector<BoxInstance> staticCpu_, visible_;
    std::vector<std::pair<float, int>> order_;
    int visibleCount_ = 0;
    void cullStatic(const Mat4& viewProj, const Vec3& eye);
    void uploadDynamic(const std::vector<BoxInstance>& boxes, int n);
    int uViewProj_ = -1, uModel_ = -1, uEye_ = -1, uScreen_ = -1, uFont_ = -1;
    int uPaint_ = -1, uPA_ = -1, uPB_ = -1, uPC_ = -1, uPaintMisc_ = -1, uPaintZ_ = -1;
    void setPaint(const PaintParams* p);

    unsigned cubeVbo_ = 0;
    unsigned staticVao_ = 0, staticInst_ = 0;
    unsigned dynVao_ = 0, dynInst_ = 0;
    unsigned decalVao_ = 0, decalInst_ = 0;
    unsigned hudVao_ = 0, hudVbo_ = 0, fontTex_ = 0;
    int staticCount_ = 0, decalCount_ = 0, decalNext_ = 0, hudCount_ = 0;
    int width_ = 0, height_ = 0;
};

constexpr int kMaxDecals = 1024;
constexpr int kMaxDynamicBoxes = 4096;  // per draw: players (~40 boxes each), particles, smoke, fire

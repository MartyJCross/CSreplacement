// Minimal OpenGL 3.3 renderer: every 3D object is an instanced box, the HUD is one textured batch.
#pragma once
#include <cstdint>
#include <string>
#include <utility>
#include <vector>
#include "vecmath.h"

// Where the sun is (towards it): the sky draws it there and the shadows fall away from it.
constexpr Vec3 kSunDir{0.55f, 0.32f, 0.75f};

struct BoxInstance {
    float mins[3];
    float maxs[3];
    uint8_t rgba[4];  // alpha selects shading: 255 = lit + world grid, 0 = lit, 128 = unlit (emissive)
    float rot[3] = {0, 0, 0};  // optional yaw: pivot x, pivot y, angle in radians (0 = axis-aligned)
    float slope[2] = {0, 0};   // ramps: which way the top rises (world.h Slope), top height at the low edge
};

BoxInstance makeBox(const Vec3& mins, const Vec3& maxs, uint32_t rgb, bool grid);
BoxInstance makeEmissive(const Vec3& mins, const Vec3& maxs, uint32_t rgb);
// A bullet hole (a decal) made at game time `t`: it shrinks away after ~28 s (alpha 16 marks it, the time rides in
// slope[1]).
BoxInstance makeBulletHole(const Vec3& mins, const Vec3& maxs, uint32_t rgb, float t);
// A soft round shadow on the floor under someone: `floorRgb` the floor's colour, `paved` the sites' paving.
BoxInstance makeBlobShadow(const Vec3& centre, float radius, uint32_t floorRgb, bool paved);
// A skin-painted part (alpha 56 + region): drawn with the layer the model's PaintParams gives that region (the
// barrel, the mag...: items.h SkinRegion); `shade` (grey) darkens it a little for grips and furniture so the parts
// still read apart.
BoxInstance makePainted(const Vec3& mins, const Vec3& maxs, uint8_t shade = 255, int region = 0);

// A skin for one drawModel call: up to kPaintLayers paints (a pattern from items.h Pattern, its three colours, its
// scale, how polished) and which of them each region of the weapon gets; its wear, a seed, and the model's length
// along z (fades and flames run along it).
constexpr int kPaintLayers = 4, kPaintRegions = 12;
struct PaintLayer {
    int pattern = 0;
    float a[3] = {1, 1, 1}, b[3] = {1, 1, 1}, c[3] = {1, 1, 1};
    float scale = 1, gloss = 0;
};
struct PaintParams {
    int pattern = -1;  // -1 = no skin; >= 0 painted with the layers
    PaintLayer layers[kPaintLayers];
    uint8_t regionLayer[kPaintRegions] = {};  // the layer per region (255: left in the weapon's own colours)
    float wear = 0, seed = 0;
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
    void setStaticBoxes(const std::vector<BoxInstance>& boxes);  // (also builds the sun shadows for them)
    // Sun shadows: the shadow map's size (0 off, 2048, 4096); rebuilt for the current map when it changes.
    void setShadowSize(int size) {
        if (size == shadowSize_) return;
        shadowSize_ = size;
        if (!staticCpu_.empty()) buildShadows(staticCpu_);
    }
    // Shadows on the next draws (off for the inventory stage, which floats wherever the camera is).
    void setSunShadows(bool on);
    // Surface textures (assets/textures/*.jpg, 512 px detail maps) for the stone, wood and metal surfaces.
    // Returns how many loaded; 0 = none (missing files): the shader falls back to its procedural patterns.
    int loadTextures(const std::string& dir);
    // The HUD font (assets/fonts/*.ttf); false = missing or unreadable: the built-in bitmap font stays.
    bool loadFont(const std::string& path);

    // Ring buffer of impact decals that persists between frames (updated only when it changes).
    void addDecal(const BoxInstance& b);
    void clearDecals();
    // The game's clock (seconds), for bullet holes that fade (see makeBulletHole).
    void setTime(float t) { time_ = t; }

    void beginFrame(int width, int height);
    void drawBoxes(const Mat4& viewProj, const Vec3& eye, const std::vector<BoxInstance>& dynamicBoxes);
    // Boxes in a local space transformed by `model` (weapon models, tracers); `paint` skins the painted parts.
    void drawModel(const Mat4& viewProj, const Mat4& model, const std::vector<BoxInstance>& boxes,
                   const PaintParams* paint = nullptr);
    // The sky, behind everything already drawn: camera forward, and right/up scaled by tan(half fov).
    void drawSky(const Vec3& fwd, const Vec3& right, const Vec3& up);
    // The muzzle light for the next draws: a warm point light at `pos`, `strength` 0 (off) .. ~1.3.
    void setFlash(const Vec3& pos, float strength);
    // Starts the first-person weapon pass: fresh depth so the weapon never clips into walls.
    void clearDepth();
    // Depth pre-pass for the world: each pixel is shaded once (on by default; off for comparison).
    void setDepthPrepass(bool on) { depthPrepass_ = on; }
    // Post pass: the 3D scene is drawn on the window as usual, then copied (resolving its MSAA) and put back with
    // FXAA (when the window has no MSAA), the colour grade, a vignette and the stylized outlines. Takes effect at
    // once. (Drawing the scene into an offscreen picture instead cost the owner's AMD laptop half its FPS.)
    void setPost(bool on);
    // The colour grade: 0 the Lab, 1 Dust, 2 Harbor.
    void setGrade(int look);
    // The look: 0 classic (textured), 1 the clean stylized look (flat colour, soft gradients, outlines from the post
    // pass, a bolder grade).
    void setStyle(int style);
    // After the 3D passes, before the HUD: puts the post-processed scene on the screen (nothing when post is off).
    void endScene();
    // Uploads the HUD only when `changed` is true; otherwise redraws the last upload.
    void drawHud(const HudBatch& hud, bool changed);

    bool screenshot(const std::string& path);

private:
    unsigned boxProgram_ = 0, hudProgram_ = 0, depthProgram_ = 0, skyProgram_ = 0, skyVao_ = 0, postProgram_ = 0;
    // Post pass (setPost/endScene): the window's picture is copied into postTex_ (through copyFbo_).
    bool postOn_ = false, postOk_ = false;
    int postW_ = 0, postH_ = 0, windowSamples_ = 0;
    unsigned copyFbo_ = 0, postTex_ = 0;
    int uPostPx_ = -1, uPostFxaa_ = -1, uPostTint_ = -1, uPostGrade_ = -1, uPostOutline_ = -1, uStyle_ = -1, uNoEdge_ = -1;
    int style_ = 0, look_ = 0;
    bool makePostTargets(int w, int h);
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
    int uFlash_ = -1, uTime_ = -1, uDepthTime_ = -1;
    float time_ = 0;
    // Sun shadows (buildShadows): made once per map from the static boxes.
    int shadowSize_ = 4096, shadowBuilt_ = 0;
    bool hasShadow_ = false;
    unsigned shadowTex_ = 0, shadowFbo_ = 0;
    int uShadowTex_ = -1, uShadowVP_ = -1, uHasShadow_ = -1, uSun_ = -1, uShadowTexel_ = -1;
    void buildShadows(const std::vector<BoxInstance>& boxes);
    int uPaint_ = -1, uPaintMisc_ = -1, uPaintZ_ = -1;
    int uLayerPat_ = -1, uLayerA_ = -1, uLayerB_ = -1, uLayerC_ = -1, uLayerMisc_ = -1, uRegionLayer_ = -1;
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

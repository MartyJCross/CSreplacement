#include "render.h"
#include <SDL3/SDL.h>
#include <algorithm>
#include "font.h"
#include "gl.h"

namespace {

const char* kBoxVS = R"(#version 330 core
layout(location = 0) in vec3 aPos;     // unit cube corner, 0..1
layout(location = 1) in vec3 aNormal;
layout(location = 2) in vec3 iMin;
layout(location = 3) in vec3 iMax;
layout(location = 4) in vec4 iColor;
uniform mat4 uViewProj;
uniform mat4 uModel;
out vec3 vWorld;
out vec3 vNormal;
out vec4 vColor;
void main() {
    vec3 p = (uModel * vec4(iMin + aPos * (iMax - iMin), 1.0)).xyz;
    vWorld = p;
    vNormal = mat3(uModel) * aNormal;
    vColor = iColor;
    gl_Position = uViewProj * vec4(p, 1.0);
}
)";

const char* kBoxFS = R"(#version 330 core
in vec3 vWorld;
in vec3 vNormal;
in vec4 vColor;
uniform vec3 uEye;
out vec4 oColor;
float grid(vec2 p, float spacing) {
    vec2 g = abs(fract(p / spacing - 0.5) - 0.5) * spacing;
    vec2 w = fwidth(p) * 1.2;
    vec2 l = 1.0 - smoothstep(vec2(0.0), w, g);
    return max(l.x, l.y);
}
void main() {
    if (vColor.a > 0.4 && vColor.a < 0.6) { oColor = vec4(vColor.rgb, 1.0); return; }  // emissive
    vec3 n = normalize(vNormal);
    // Fixed directional light: tops brightest, sides shaded so shapes read clearly.
    float light = 0.45 + 0.55 * clamp(n.z, 0.0, 1.0) + 0.30 * abs(n.x) * (1.0 - abs(n.z)) + 0.20 * abs(n.y) * (1.0 - abs(n.z));
    light = min(light, 1.0);
    vec3 c = vColor.rgb * light;
    if (vColor.a > 0.9) {
        vec2 uv = abs(n.z) > 0.5 ? vWorld.xy : (abs(n.x) > 0.5 ? vWorld.yz : vWorld.xz);
        float g = grid(uv, 64.0) * 0.22 + grid(uv, 16.0) * 0.07;
        c *= 1.0 - g * vColor.a;
    }
    float d = length(vWorld - uEye);
    c = mix(c, vec3(0.62, 0.70, 0.78), clamp(d / 9000.0, 0.0, 0.35));
    oColor = vec4(c, 1.0);
}
)";

const char* kHudVS = R"(#version 330 core
layout(location = 0) in vec2 aPos;
layout(location = 1) in vec2 aUV;
layout(location = 2) in vec4 aColor;
uniform vec2 uScreen;
out vec2 vUV;
out vec4 vColor;
void main() {
    vUV = aUV;
    vColor = aColor;
    gl_Position = vec4(aPos.x / uScreen.x * 2.0 - 1.0, 1.0 - aPos.y / uScreen.y * 2.0, 0.0, 1.0);
}
)";

const char* kHudFS = R"(#version 330 core
in vec2 vUV;
in vec4 vColor;
uniform sampler2D uFont;
out vec4 oColor;
void main() {
    float a = vUV.x < 0.0 ? 1.0 : texture(uFont, vUV).r;
    oColor = vec4(vColor.rgb, vColor.a * a);
}
)";

constexpr int kAtlasCols = 16, kCellW = 6, kCellH = 8;
constexpr int kAtlasW = kAtlasCols * kCellW, kAtlasH = (kFontCount / kAtlasCols) * kCellH;

unsigned compileProgram(const char* vs, const char* fs, std::string& err) {
    auto compile = [&](unsigned type, const char* src) -> unsigned {
        unsigned s = glCreateShader(type);
        glShaderSource(s, 1, &src, nullptr);
        glCompileShader(s);
        GLint ok = 0;
        glGetShaderiv(s, GL_COMPILE_STATUS, &ok);
        if (!ok) {
            char log[2048];
            glGetShaderInfoLog(s, sizeof(log), nullptr, log);
            err = std::string("shader compile failed: ") + log;
            return 0;
        }
        return s;
    };
    unsigned v = compile(GL_VERTEX_SHADER, vs);
    if (!v) return 0;
    unsigned f = compile(GL_FRAGMENT_SHADER, fs);
    if (!f) return 0;
    unsigned p = glCreateProgram();
    glAttachShader(p, v);
    glAttachShader(p, f);
    glLinkProgram(p);
    glDeleteShader(v);
    glDeleteShader(f);
    GLint ok = 0;
    glGetProgramiv(p, GL_LINK_STATUS, &ok);
    if (!ok) {
        char log[2048];
        glGetProgramInfoLog(p, sizeof(log), nullptr, log);
        err = std::string("shader link failed: ") + log;
        return 0;
    }
    return p;
}

std::vector<float> buildUnitCube() {
    // 6 faces: origin corner, edge u, edge v with cross(u, v) = outward normal (CCW from outside).
    struct Face { float p[3], u[3], v[3], n[3]; };
    const Face faces[6] = {
        {{1, 0, 0}, {0, 1, 0}, {0, 0, 1}, {1, 0, 0}},  {{0, 0, 0}, {0, 0, 1}, {0, 1, 0}, {-1, 0, 0}},
        {{0, 1, 0}, {0, 0, 1}, {1, 0, 0}, {0, 1, 0}},  {{0, 0, 0}, {1, 0, 0}, {0, 0, 1}, {0, -1, 0}},
        {{0, 0, 1}, {1, 0, 0}, {0, 1, 0}, {0, 0, 1}},  {{0, 0, 0}, {0, 1, 0}, {1, 0, 0}, {0, 0, -1}},
    };
    std::vector<float> out;
    for (const Face& f : faces) {
        const float corners[6][2] = {{0, 0}, {1, 0}, {1, 1}, {0, 0}, {1, 1}, {0, 1}};
        for (const auto& c : corners) {
            for (int k = 0; k < 3; ++k) out.push_back(f.p[k] + f.u[k] * c[0] + f.v[k] * c[1]);
            for (int k = 0; k < 3; ++k) out.push_back(f.n[k]);
        }
    }
    return out;
}

void setupInstanceVao(unsigned vao, unsigned cubeVbo, unsigned instVbo) {
    glBindVertexArray(vao);
    glBindBuffer(GL_ARRAY_BUFFER, cubeVbo);
    glEnableVertexAttribArray(0);
    glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, 24, reinterpret_cast<void*>(0));
    glEnableVertexAttribArray(1);
    glVertexAttribPointer(1, 3, GL_FLOAT, GL_FALSE, 24, reinterpret_cast<void*>(12));
    glBindBuffer(GL_ARRAY_BUFFER, instVbo);
    const GLsizei stride = sizeof(BoxInstance);
    glEnableVertexAttribArray(2);
    glVertexAttribPointer(2, 3, GL_FLOAT, GL_FALSE, stride, reinterpret_cast<void*>(offsetof(BoxInstance, mins)));
    glVertexAttribDivisor(2, 1);
    glEnableVertexAttribArray(3);
    glVertexAttribPointer(3, 3, GL_FLOAT, GL_FALSE, stride, reinterpret_cast<void*>(offsetof(BoxInstance, maxs)));
    glVertexAttribDivisor(3, 1);
    glEnableVertexAttribArray(4);
    glVertexAttribPointer(4, 4, GL_UNSIGNED_BYTE, GL_TRUE, stride, reinterpret_cast<void*>(offsetof(BoxInstance, rgba)));
    glVertexAttribDivisor(4, 1);
}

void pushQuad(std::vector<HudVert>& v, float x0, float y0, float x1, float y1, float u0, float v0, float u1,
              float v1, uint32_t rgba) {
    uint8_t c[4] = {uint8_t(rgba >> 24), uint8_t(rgba >> 16), uint8_t(rgba >> 8), uint8_t(rgba)};
    HudVert a{x0, y0, u0, v0, {c[0], c[1], c[2], c[3]}}, b{x1, y0, u1, v0, {c[0], c[1], c[2], c[3]}};
    HudVert d{x1, y1, u1, v1, {c[0], c[1], c[2], c[3]}}, e{x0, y1, u0, v1, {c[0], c[1], c[2], c[3]}};
    v.insert(v.end(), {a, b, d, a, d, e});
}

}  // namespace

BoxInstance makeEmissive(const Vec3& mins, const Vec3& maxs, uint32_t rgb) {
    BoxInstance b = makeBox(mins, maxs, rgb, false);
    b.rgba[3] = 128;
    return b;
}

BoxInstance makeBox(const Vec3& mins, const Vec3& maxs, uint32_t rgb, bool grid) {
    BoxInstance b;
    b.mins[0] = mins.x; b.mins[1] = mins.y; b.mins[2] = mins.z;
    b.maxs[0] = maxs.x; b.maxs[1] = maxs.y; b.maxs[2] = maxs.z;
    b.rgba[0] = uint8_t(rgb >> 16); b.rgba[1] = uint8_t(rgb >> 8); b.rgba[2] = uint8_t(rgb);
    b.rgba[3] = grid ? 255 : 0;
    return b;
}

void HudBatch::rect(float x, float y, float w, float h, uint32_t rgba) {
    pushQuad(verts, x, y, x + w, y + h, -1, -1, -1, -1, rgba);
}

void HudBatch::line(float x0, float y0, float x1, float y1, float thickness, uint32_t rgba) {
    float dx = x1 - x0, dy = y1 - y0, len = std::sqrt(dx * dx + dy * dy);
    if (len <= 0) return;
    float nx = -dy / len * thickness * 0.5f, ny = dx / len * thickness * 0.5f;
    uint8_t c[4] = {uint8_t(rgba >> 24), uint8_t(rgba >> 16), uint8_t(rgba >> 8), uint8_t(rgba)};
    HudVert p0{x0 + nx, y0 + ny, -1, -1, {c[0], c[1], c[2], c[3]}}, p1{x1 + nx, y1 + ny, -1, -1, {c[0], c[1], c[2], c[3]}};
    HudVert p2{x1 - nx, y1 - ny, -1, -1, {c[0], c[1], c[2], c[3]}}, p3{x0 - nx, y0 - ny, -1, -1, {c[0], c[1], c[2], c[3]}};
    verts.insert(verts.end(), {p0, p1, p2, p0, p2, p3});
}

float HudBatch::textWidth(const std::string& s, int scale) const {
    int sc = scale > 0 ? scale : fontScale;
    return float(s.size() * kCellW * sc);
}

void HudBatch::text(float x, float y, const std::string& s, uint32_t rgba, int scale) {
    int sc = scale > 0 ? scale : fontScale;
    const uint32_t shadow = (rgba & 0xFF) * 3 / 4;  // black drop shadow at 75% of the text alpha
    for (int pass = 0; pass < 2; ++pass) {
        float off = pass == 0 ? float(sc) * 0.5f : 0.0f;
        uint32_t col = pass == 0 ? shadow : rgba;
        float cx = x;
        for (char ch : s) {
            int c = (ch >= 'a' && ch <= 'z') ? ch - 32 : ch;
            int idx = c - kFontFirst;
            if (idx >= 0 && idx < kFontCount && kFontGlyphs[idx]) {
                float u0 = float((idx % kAtlasCols) * kCellW) / kAtlasW;
                float v0 = float((idx / kAtlasCols) * kCellH) / kAtlasH;
                float u1 = u0 + 5.0f / kAtlasW, v1 = v0 + 7.0f / kAtlasH;
                pushQuad(verts, cx + off, y + off, cx + off + 5 * sc, y + off + 7 * sc, u0, v0, u1, v1, col);
            }
            cx += kCellW * sc;
        }
    }
}

bool Renderer::init(std::string& err) {
    boxProgram_ = compileProgram(kBoxVS, kBoxFS, err);
    if (!boxProgram_) return false;
    hudProgram_ = compileProgram(kHudVS, kHudFS, err);
    if (!hudProgram_) return false;
    uViewProj_ = glGetUniformLocation(boxProgram_, "uViewProj");
    uModel_ = glGetUniformLocation(boxProgram_, "uModel");
    uEye_ = glGetUniformLocation(boxProgram_, "uEye");
    uScreen_ = glGetUniformLocation(hudProgram_, "uScreen");
    uFont_ = glGetUniformLocation(hudProgram_, "uFont");

    std::vector<float> cube = buildUnitCube();
    glGenBuffers(1, &cubeVbo_);
    glBindBuffer(GL_ARRAY_BUFFER, cubeVbo_);
    glBufferData(GL_ARRAY_BUFFER, GLsizeiptr(cube.size() * sizeof(float)), cube.data(), GL_STATIC_DRAW);

    unsigned vaos[3], vbos[3];
    glGenVertexArrays(3, vaos);
    glGenBuffers(3, vbos);
    staticVao_ = vaos[0]; staticInst_ = vbos[0];
    dynVao_ = vaos[1]; dynInst_ = vbos[1];
    decalVao_ = vaos[2]; decalInst_ = vbos[2];

    glBindBuffer(GL_ARRAY_BUFFER, dynInst_);
    glBufferData(GL_ARRAY_BUFFER, kMaxDynamicBoxes * sizeof(BoxInstance), nullptr, GL_STREAM_DRAW);
    glBindBuffer(GL_ARRAY_BUFFER, decalInst_);
    glBufferData(GL_ARRAY_BUFFER, kMaxDecals * sizeof(BoxInstance), nullptr, GL_DYNAMIC_DRAW);
    setupInstanceVao(staticVao_, cubeVbo_, staticInst_);
    setupInstanceVao(dynVao_, cubeVbo_, dynInst_);
    setupInstanceVao(decalVao_, cubeVbo_, decalInst_);

    // HUD
    glGenVertexArrays(1, &hudVao_);
    glGenBuffers(1, &hudVbo_);
    glBindVertexArray(hudVao_);
    glBindBuffer(GL_ARRAY_BUFFER, hudVbo_);
    glEnableVertexAttribArray(0);
    glVertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE, sizeof(HudVert), reinterpret_cast<void*>(offsetof(HudVert, x)));
    glEnableVertexAttribArray(1);
    glVertexAttribPointer(1, 2, GL_FLOAT, GL_FALSE, sizeof(HudVert), reinterpret_cast<void*>(offsetof(HudVert, u)));
    glEnableVertexAttribArray(2);
    glVertexAttribPointer(2, 4, GL_UNSIGNED_BYTE, GL_TRUE, sizeof(HudVert), reinterpret_cast<void*>(offsetof(HudVert, rgba)));

    // Font atlas from the bitmap glyphs.
    std::vector<uint8_t> atlas(kAtlasW * kAtlasH, 0);
    for (int g = 0; g < kFontCount; ++g) {
        if (!kFontGlyphs[g]) continue;
        int ox = (g % kAtlasCols) * kCellW, oy = (g / kAtlasCols) * kCellH;
        for (int r = 0; r < 7; ++r)
            for (int c = 0; c < 5; ++c)
                if (kFontGlyphs[g][r * 5 + c] == '#') atlas[(oy + r) * kAtlasW + ox + c] = 255;
    }
    glGenTextures(1, &fontTex_);
    glBindTexture(GL_TEXTURE_2D, fontTex_);
    glPixelStorei(GL_UNPACK_ALIGNMENT, 1);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_R8, kAtlasW, kAtlasH, 0, GL_RED, GL_UNSIGNED_BYTE, atlas.data());
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);

    glEnable(GL_CULL_FACE);
    glCullFace(GL_BACK);
    glDepthFunc(GL_LEQUAL);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
    return true;
}

void Renderer::setStaticBoxes(const std::vector<BoxInstance>& boxes) {
    glBindBuffer(GL_ARRAY_BUFFER, staticInst_);
    glBufferData(GL_ARRAY_BUFFER, GLsizeiptr(boxes.size() * sizeof(BoxInstance)), boxes.data(), GL_STATIC_DRAW);
    staticCount_ = int(boxes.size());
}

void Renderer::addDecal(const BoxInstance& b) {
    glBindBuffer(GL_ARRAY_BUFFER, decalInst_);
    glBufferSubData(GL_ARRAY_BUFFER, GLintptr(decalNext_ * sizeof(BoxInstance)), sizeof(BoxInstance), &b);
    decalNext_ = (decalNext_ + 1) % kMaxDecals;
    decalCount_ = std::min(decalCount_ + 1, kMaxDecals);
}

void Renderer::clearDecals() { decalCount_ = 0; decalNext_ = 0; }

void Renderer::beginFrame(int width, int height) {
    width_ = width;
    height_ = height;
    glViewport(0, 0, width, height);
    glClearColor(0.62f, 0.70f, 0.78f, 1.0f);
    glDepthMask(GL_TRUE);
    glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
}

void Renderer::drawBoxes(const Mat4& viewProj, const Vec3& eye, const std::vector<BoxInstance>& dynamicBoxes) {
    glEnable(GL_DEPTH_TEST);
    glEnable(GL_CULL_FACE);
    glDisable(GL_BLEND);
    glUseProgram(boxProgram_);
    glUniformMatrix4fv(uViewProj_, 1, GL_FALSE, viewProj.m);
    Mat4 id = identity();
    glUniformMatrix4fv(uModel_, 1, GL_FALSE, id.m);
    glUniform3f(uEye_, eye.x, eye.y, eye.z);

    glBindVertexArray(staticVao_);
    glDrawArraysInstanced(GL_TRIANGLES, 0, 36, staticCount_);

    int n = std::min(int(dynamicBoxes.size()), kMaxDynamicBoxes);
    if (n > 0) {
        uploadDynamic(dynamicBoxes, n);
        glBindVertexArray(dynVao_);
        glDrawArraysInstanced(GL_TRIANGLES, 0, 36, n);
    }
    if (decalCount_ > 0) {
        glBindVertexArray(decalVao_);
        glDrawArraysInstanced(GL_TRIANGLES, 0, 36, decalCount_);
    }
}

void Renderer::uploadDynamic(const std::vector<BoxInstance>& boxes, int n) {
    glBindBuffer(GL_ARRAY_BUFFER, dynInst_);
    glBufferData(GL_ARRAY_BUFFER, kMaxDynamicBoxes * sizeof(BoxInstance), nullptr, GL_STREAM_DRAW);  // orphan
    glBufferSubData(GL_ARRAY_BUFFER, 0, GLsizeiptr(n * sizeof(BoxInstance)), boxes.data());
}

void Renderer::drawModel(const Mat4& viewProj, const Mat4& model, const std::vector<BoxInstance>& boxes) {
    int n = std::min(int(boxes.size()), kMaxDynamicBoxes);
    if (n == 0) return;
    glEnable(GL_DEPTH_TEST);
    glEnable(GL_CULL_FACE);
    glDisable(GL_BLEND);
    glUseProgram(boxProgram_);
    glUniformMatrix4fv(uViewProj_, 1, GL_FALSE, viewProj.m);
    glUniformMatrix4fv(uModel_, 1, GL_FALSE, model.m);
    uploadDynamic(boxes, n);
    glBindVertexArray(dynVao_);
    glDrawArraysInstanced(GL_TRIANGLES, 0, 36, n);
}

void Renderer::clearDepth() {
    glDepthMask(GL_TRUE);
    glClear(GL_DEPTH_BUFFER_BIT);
}

void Renderer::drawHud(const HudBatch& hud, bool changed) {
    glDisable(GL_DEPTH_TEST);
    glDisable(GL_CULL_FACE);  // 2D quads are wound clockwise once Y is flipped to screen space
    glEnable(GL_BLEND);
    glUseProgram(hudProgram_);
    glUniform2f(uScreen_, float(width_), float(height_));
    glUniform1i(uFont_, 0);
    glActiveTexture(GL_TEXTURE0);
    glBindTexture(GL_TEXTURE_2D, fontTex_);
    glBindVertexArray(hudVao_);
    if (changed) {
        glBindBuffer(GL_ARRAY_BUFFER, hudVbo_);
        glBufferData(GL_ARRAY_BUFFER, GLsizeiptr(hud.verts.size() * sizeof(HudVert)), hud.verts.data(), GL_STREAM_DRAW);
        hudCount_ = int(hud.verts.size());
    }
    if (hudCount_ > 0) glDrawArrays(GL_TRIANGLES, 0, hudCount_);
}

bool Renderer::screenshot(const std::string& path) {
    std::vector<uint8_t> px(size_t(width_) * height_ * 4), flipped(px.size());
    glPixelStorei(GL_PACK_ALIGNMENT, 1);
    glReadPixels(0, 0, width_, height_, GL_RGBA, GL_UNSIGNED_BYTE, px.data());
    const size_t row = size_t(width_) * 4;
    for (int y = 0; y < height_; ++y)
        std::copy_n(&px[size_t(height_ - 1 - y) * row], row, &flipped[size_t(y) * row]);
    SDL_Surface* s = SDL_CreateSurfaceFrom(width_, height_, SDL_PIXELFORMAT_RGBA32, flipped.data(), int(row));
    if (!s) return false;
    bool ok = SDL_SaveBMP(s, path.c_str());
    SDL_DestroySurface(s);
    return ok;
}

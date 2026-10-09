#include "render.h"
#include <SDL3/SDL.h>
#include <algorithm>
#include "font.h"
#include "stb_image.h"  // JPEG decoding (public domain), compiled in crisp_third_party
#include "gl.h"

namespace {

const char* kBoxVS = R"(#version 330 core
layout(location = 0) in vec3 aPos;     // unit cube corner, 0..1
layout(location = 1) in vec3 aNormal;
layout(location = 2) in vec3 iMin;
layout(location = 3) in vec3 iMax;
layout(location = 4) in vec4 iColor;
layout(location = 5) in vec3 iRot;  // pivot xy + yaw (radians)
layout(location = 6) in vec2 iSlope;  // ramps: rise direction (1 +x, 2 -x, 3 +y, 4 -y) and the low edge's top
uniform mat4 uViewProj;
uniform mat4 uModel;
out vec3 vWorld;
out vec3 vLocal;            // the model's own space (skins are painted in it, so they stick to the gun)
flat out vec3 vLocalN;
flat out vec3 vNormal;
flat out vec3 vLit;  // colour after lighting (constant per face, so worked out per vertex)
flat out vec3 vMin;  // the box's corners (world surfaces: frames round crate faces)
flat out vec3 vMax;
out vec4 vColor;
invariant gl_Position;  // the depth pre-pass and the colour pass must agree exactly
void main() {
    vec3 p = iMin + aPos * (iMax - iMin);
    vec3 n = aNormal;
    if (iSlope.x > 0.5) {
        // Ramp: lower the top corners along the rise; the top face gets the slope's normal.
        int dir = int(iSlope.x + 0.5);
        float t = dir == 1 ? aPos.x : dir == 2 ? 1.0 - aPos.x : dir == 3 ? aPos.y : 1.0 - aPos.y;
        if (aPos.z > 0.5) p.z = mix(iSlope.y, iMax.z, t);
        if (aNormal.z > 0.5) {
            float run = dir <= 2 ? iMax.x - iMin.x : iMax.y - iMin.y, k = (iMax.z - iSlope.y) / run;
            n = normalize(dir == 1 ? vec3(-k, 0, 1) : dir == 2 ? vec3(k, 0, 1) : dir == 3 ? vec3(0, -k, 1) : vec3(0, k, 1));
        }
    }
    if (iRot.z != 0.0) {
        float c = cos(iRot.z), s = sin(iRot.z);
        vec2 d = p.xy - iRot.xy;
        p.xy = iRot.xy + vec2(c * d.x - s * d.y, s * d.x + c * d.y);
        n.xy = vec2(c * n.x - s * n.y, s * n.x + c * n.y);
    }
    vLocal = p;
    vLocalN = n;
    p = (uModel * vec4(p, 1.0)).xyz;
    vWorld = p;
    vec3 wn = normalize(mat3(uModel) * n);
    vNormal = wn;
    vColor = iColor;
    vMin = iMin;
    vMax = iMax;
    // Fixed directional light: tops brightest, sides shaded so shapes read clearly.
    float light = 0.45 + 0.55 * clamp(wn.z, 0.0, 1.0) + 0.30 * abs(wn.x) * (1.0 - abs(wn.z)) + 0.20 * abs(wn.y) * (1.0 - abs(wn.z));
    light = min(light, 1.0);
    vLit = iColor.rgb * light * (wn.z > 0.5 ? vec3(1.04, 1.0, 0.93) : vec3(0.95, 0.98, 1.05));  // warm sun, cool shade
    gl_Position = uViewProj * vec4(p, 1.0);
}
)";

// Surfaces, picked by the colour's alpha: 255 dev grid (the Lab), 240 stone, 236 paving, 224 wood, 208 metal,
// 128 emissive, 0 plain. With textures loaded (uHasTex) the stone/wood/metal surfaces sample a detail map
// (128 = the box's own colour) from uTex (mipmapped, so no shimmer); without, they use procedural patterns
// (a few ALU ops that fade out with distance).
const char* kBoxFS = R"(#version 330 core
in vec3 vWorld;
in vec3 vLocal;
flat in vec3 vLocalN;
flat in vec3 vNormal;
flat in vec3 vLit;
flat in vec3 vMin;
flat in vec3 vMax;
in vec4 vColor;
uniform vec3 uEye;
uniform sampler2DArray uTex;  // layers: 0 sandstone, 1 plaster, 2 sand, 3 paving, 4 planks, 5 shutter, 6 plate
uniform int uHasTex;
// Skins (weapon models): the pattern (items.h Pattern, -1 none), its colours, wear/gloss/seed, z extent.
uniform int uPaint;
uniform vec3 uPA, uPB, uPC;
uniform vec3 uPaintMisc;  // wear, gloss, seed
uniform vec2 uPaintZ;     // the model's z range (back..front runs along it)
uniform vec4 uFlash;      // the muzzle light: position, strength (0 = off)
// A gunshot lights what's near it: warm, fading to nothing at 420 units, brightest on faces turned to it.
vec3 flashLit(vec3 c) {
    if (uFlash.w <= 0.0) return c;
    vec3 d = uFlash.xyz - vWorld;
    float dist = length(d);
    float k = uFlash.w * clamp(1.0 - dist / 420.0, 0.0, 1.0);
    k *= k;
    float facing = 0.25 + 0.75 * max(dot(normalize(vNormal), d / max(dist, 1.0)), 0.0);
    return c + (c * vec3(1.2, 0.85, 0.45) + vec3(0.16, 0.11, 0.04)) * k * facing;
}
out vec4 oColor;
float grid(vec2 p, float spacing) {
    vec2 g = abs(fract(p / spacing - 0.5) - 0.5) * spacing;
    vec2 w = fwidth(p) * 1.2;
    vec2 l = 1.0 - smoothstep(vec2(0.0), w, g);
    return max(l.x, l.y);
}
float hash12(vec2 p) {
    vec3 p3 = fract(vec3(p.xyx) * 0.1031);
    p3 += dot(p3, p3.yzx + 33.33);
    return fract((p3.x + p3.y) * p3.z);
}
float noise(vec2 p) {
    vec2 i = floor(p), f = fract(p);
    f = f * f * (3.0 - 2.0 * f);
    return mix(mix(hash12(i), hash12(i + vec2(1, 0)), f.x), mix(hash12(i + vec2(0, 1)), hash12(i + vec2(1, 1)), f.x), f.y);
}
// Joint lines `width` units wide between cells of `size` along one axis, anti-aliased.
float joint(float p, float size, float width) {
    float q = abs(fract(p / size - 0.5) - 0.5) * size;
    return 1.0 - smoothstep(width, width + fwidth(p), q);
}
// A hard-ish edge between two colours at `edge`, anti-aliased.
float cut(float v, float edge) { float w = fwidth(v) * 0.75 + 1e-4; return smoothstep(edge - w, edge + w, v); }
// The skin's colour at this point (model units: inches; z runs muzzle (-) to stock (+)). `glow` > 0: unlit.
vec3 skin(out float glow) {
    glow = 0.0;
    vec3 n = vLocalN;
    vec2 uv = abs(n.x) > 0.5 ? vLocal.zy : (abs(n.y) > 0.5 ? vLocal.zx : vLocal.xy);
    float seed = uPaintMisc.z;
    uv += vec2(seed * 7.13, seed * 3.71);
    float t = clamp((vLocal.z - uPaintZ.x) / max(uPaintZ.y - uPaintZ.x, 1.0), 0.0, 1.0);  // 0 front .. 1 back
    if (uPaint == 1) {  // fade: front colour into the middle into the back
        return t < 0.5 ? mix(uPA, uPB, smoothstep(0.0, 0.5, t)) : mix(uPB, uPC, smoothstep(0.5, 1.0, t));
    } else if (uPaint == 2) {  // camo: three-colour blotches
        float a = noise(uv * 0.32) * 0.7 + noise(uv * 0.9) * 0.3;
        float b = noise(uv * 0.45 + 11.0) * 0.7 + noise(uv * 1.3 + 5.0) * 0.3;
        vec3 c = mix(uPA, uPB, cut(a, 0.52));
        return mix(c, uPC, cut(b, 0.62));
    } else if (uPaint == 3) {  // tiger: wavy stripes across the gun
        float w = vLocal.z * 0.85 + noise(uv * 0.35) * 5.0 + noise(uv * 1.1) * 1.2;
        return mix(uPA, uPB, cut(sin(w), 0.45));
    } else if (uPaint == 4) {  // marble: swirled liquid colour with bright veins
        float m = noise(uv * 0.22) * 0.65 + noise(uv * 0.55) * 0.35;
        float s = sin(m * 11.0 + vLocal.z * 0.25);
        vec3 c = mix(uPB, uPA, smoothstep(-0.55, 0.55, s));
        return mix(c, uPC, smoothstep(0.82, 0.97, s));
    } else if (uPaint == 5) {  // case hardened: blue, gold and bare patina in patches
        float h = noise(uv * 0.28) * 0.6 + noise(uv * 0.8) * 0.4;
        vec3 c = mix(uPA, uPC, smoothstep(0.38, 0.5, h));
        return mix(c, uPB, smoothstep(0.58, 0.7, h));
    } else if (uPaint == 6) {  // web: a dark lattice over a deep colour
        float l = max(max(joint(uv.x + uv.y * 0.55, 3.2, 0.12), joint(uv.y - uv.x * 0.35, 2.7, 0.12)), joint(length(uv - vec2(2.0, 0.5)), 2.2, 0.1));
        return mix(mix(uPA, uPC, noise(uv * 0.5) * 0.5), uPB, l);
    } else if (uPaint == 7) {  // hazard: bold blocks of colour down the gun, a stripe of the accent
        float band = fract(vLocal.z / 11.0 + seed * 0.37);
        vec3 c = band < 0.58 ? uPA : (band < 0.8 ? uPB : uPC);
        float diag = fract((vLocal.z + vLocal.y * 1.5) / 3.0);
        if (band >= 0.58 && band < 0.8 && diag < 0.18) c = uPC;
        return c;
    } else if (uPaint == 8) {  // flames: licking back from the muzzle over a dark body
        float f = 1.0 - t + noise(vec2(uv.x * 0.6, vLocal.z * 0.12)) * 0.6 - 0.25 + noise(uv * 1.4) * 0.15;
        vec3 c = mix(uPC, uPB, smoothstep(0.42, 0.55, f));
        return mix(c, uPA, smoothstep(0.7, 0.85, f));
    } else if (uPaint == 9) {  // carbon: a fine weave, one accent stripe along the side
        float ch = mod(floor(uv.x * 2.2) + floor(uv.y * 2.2), 2.0);
        vec3 c = mix(uPA, uPB, ch);
        if (abs(n.x) > 0.5 && abs(vLocal.y - 0.15) < 0.22) c = uPC;
        return c;
    } else if (uPaint == 10) {  // neon: a dark body with glowing circuit lines
        float l = max(joint(uv.x, 2.6, 0.07), joint(uv.y + floor(uv.x / 2.6) * 0.9, 1.9, 0.07));
        float on = l * step(0.45, noise(floor(uv / 2.6) + 3.0));
        glow = on;
        return mix(uPA, uPB, on);
    } else if (uPaint == 11) {  // two-tone: top and bottom halves, a seam of the accent
        float y = vLocal.y + noise(uv * 0.6) * 0.15;
        vec3 c = mix(uPB, uPA, cut(y, 0.1));
        if (abs(y - 0.1) < 0.12) c = uPC;
        return c;
    }
    return mix(uPA, uPB, noise(uv * 0.4) * 0.18);  // solid, with a faint mottle
}
void main() {
    if (vColor.a > 0.4 && vColor.a < 0.6) { oColor = vec4(vColor.rgb, 1.0); return; }  // emissive
    if (vColor.a > 0.2 && vColor.a < 0.3 && uPaint >= 0) {  // a skin-painted weapon part
        float glow;
        vec3 p = skin(glow);
        vec3 n = vLocalN;
        vec2 uv = abs(n.x) > 0.5 ? vLocal.zy : (abs(n.y) > 0.5 ? vLocal.zx : vLocal.xy);
        float wear = uPaintMisc.x, gloss = uPaintMisc.y;
        // Wear: scratches and chips down to bare steel, more of them the higher the float; edges first.
        float scratch = noise(uv * vec2(4.0, 0.7) + uPaintMisc.z * 5.0) * noise(uv * 0.5 + 9.0);
        float chips = noise(uv * 1.6 + 4.0);
        float bare = max(step(scratch, wear * 0.32), step(chips, wear * 0.45 - 0.08));
        p = mix(p, vec3(0.50, 0.52, 0.55), bare * (1.0 - glow));
        p *= 1.0 - 0.22 * wear;
        vec3 c = p * vLit;
        if (glow > 0.0) c = mix(c, p * 1.25, glow);
        // Polish: a sky reflection and a sun glint that slide over the faces as the gun turns.
        vec3 V = normalize(uEye - vWorld), N = normalize(vNormal), L = normalize(vec3(0.35, 0.25, 0.9));
        vec3 R = reflect(-V, N);
        vec3 env = mix(vec3(0.42, 0.38, 0.34), vec3(0.85, 0.92, 1.0), clamp(R.z * 0.5 + 0.5, 0.0, 1.0));
        float g = gloss * (1.0 - bare * 0.7) * (1.0 - wear * 0.5);
        c = mix(c, c * env * 1.7, g * 0.35);
        c += vec3(pow(max(dot(N, normalize(L + V)), 0.0), 48.0)) * g * 0.6;
        c += env * pow(1.0 - max(dot(N, V), 0.0), 4.0) * g * 0.25;
        oColor = vec4(flashLit(c), 1.0);
        return;
    }
    vec3 n = vNormal;
    vec3 c = vLit;
    float a = vColor.a;
    bool top = abs(n.z) > 0.5;
    vec2 uv = top ? vWorld.xy : (abs(n.x) > 0.5 ? vWorld.yz : vWorld.xz);
    if (a > 0.97) {
        float g = grid(uv, 64.0) * 0.22 + grid(uv, 16.0) * 0.07;
        c *= 1.0 - g;
    } else if (a > 0.78) {
        vec2 fw = fwidth(uv);
        float near = 1.0 - clamp(max(fw.x, fw.y) / 5.0, 0.0, 1.0);  // fine detail fades with distance
        // Distance to the nearest edge of this face (frames round crates, doors, containers).
        vec3 lo = vWorld - vMin, hi = vMax - vWorld;
        vec3 e = min(lo, hi);
        float edge = top ? min(e.x, e.y) : (abs(n.x) > 0.5 ? min(e.y, e.z) : min(e.x, e.z));
        if (uHasTex == 1) {
            float layer = 0.0, scale = 128.0;  // world units per repeat
            if (a > 0.933) {  // stone: sand underfoot; walls sandstone, or plaster on some buildings
                if (top) { layer = 2.0; scale = 160.0; }
                else layer = hash12(floor(vMin.xy / 256.0)) < 0.3 ? 1.0 : 0.0;
            } else if (a > 0.91) {  // paving: the sites
                layer = top ? 3.0 : 0.0;
                scale = 112.0;
            } else if (a > 0.85) {  // wood
                layer = 4.0;
                scale = 64.0;
            } else {  // metal: painted (the blue container) or bare plate
                layer = vColor.b > vColor.r + 0.08 ? 5.0 : 6.0;
                scale = 96.0;
            }
            c *= texture(uTex, vec3(uv / scale, layer)).rgb * 2.0;
            if (a < 0.91)  // crates, doors, metal: keep a darker frame round each face
                c *= 1.0 - 0.18 * (1.0 - smoothstep(4.0, 4.0 + fwidth(edge), edge));
        } else if (a > 0.91) {  // stone: worn flagstones and sand on the ground, block courses on walls
            if (top) {
                float tile = hash12(floor(uv / 96.0));
                float j = max(joint(uv.x, 96.0, 0.8), joint(uv.y, 96.0, 0.8)) * near;
                float sand = noise(uv / 140.0) * 0.6 + noise(uv / 23.0) * 0.4 * near;
                c *= (0.95 + 0.07 * tile) * (0.90 + 0.14 * sand) * (1.0 - 0.10 * j);
            } else {
                float row = floor(uv.y / 32.0);
                vec2 b = vec2(uv.x + mod(row, 2.0) * 32.0, uv.y);
                float id = hash12(floor(b / vec2(64.0, 32.0)) + floor(vMin.xy / 512.0));
                float j = max(joint(b.x, 64.0, 0.9), joint(b.y, 32.0, 0.9)) * near;
                float weather = noise(uv / vec2(80.0, 46.0));
                c *= (0.94 + 0.08 * id) * (0.90 + 0.13 * weather) * (1.0 - 0.17 * j);
            }
        } else if (a > 0.85) {  // wood: planks with grain, a darker frame round each face
            float across = top ? uv.y : uv.y;  // planks stacked up the sides, side by side on tops
            float plank = hash12(vec2(floor(across / 12.0), floor(vMin.x * 0.1 + vMin.y * 0.37)));
            float grain = noise(vec2(uv.x / 26.0, across / 2.0));
            float j = joint(across, 12.0, 0.6) * near;
            float frame = 1.0 - smoothstep(5.0, 5.0 + fwidth(edge), edge);
            c *= (0.86 + 0.16 * plank) * (0.92 + 0.12 * grain) * (1.0 - 0.28 * j) * (1.0 - 0.24 * frame);
        } else {  // metal: corrugated ribs and a frame
            float rib = mix(0.5, 0.5 + 0.5 * cos(uv.x * 6.2832 / 12.0), near);
            float frame = 1.0 - smoothstep(3.0, 3.0 + fwidth(edge), edge);
            c *= (0.88 + 0.16 * rib) * (1.0 - 0.18 * frame);
        }
    }
    c = flashLit(c);
    float d = length(vWorld - uEye);
    c = mix(c, vec3(0.80, 0.84, 0.87), clamp(d / 9000.0, 0.0, 0.30));  // haze: the horizon colour
    oColor = vec4(c, 1.0);
}
)";

// Sky: one full-screen triangle at the far plane, drawn after the world, so only pixels nothing else
// covered get shaded. A gradient from a deep blue overhead to a warm, pale horizon, and a sun.
const char* kSkyVS = R"(#version 330 core
out vec2 vNdc;
void main() {
    vec2 p = vec2(gl_VertexID == 1 ? 3.0 : -1.0, gl_VertexID == 2 ? 3.0 : -1.0);
    vNdc = p;
    gl_Position = vec4(p, 1.0, 1.0);
}
)";

const char* kSkyFS = R"(#version 330 core
in vec2 vNdc;
uniform vec3 uFwd, uRight, uUp;  // camera basis; right/up scaled by tan(half fov)
out vec4 oColor;
void main() {
    vec3 d = normalize(uFwd + uRight * vNdc.x + uUp * vNdc.y);
    const vec3 zenith = vec3(0.30, 0.52, 0.86), horizon = vec3(0.80, 0.84, 0.87), ground = vec3(0.74, 0.70, 0.62);
    vec3 c = d.z >= 0.0 ? mix(horizon, zenith, pow(clamp(d.z, 0.0, 1.0), 0.5))
                        : mix(horizon, ground, clamp(-d.z * 4.0, 0.0, 1.0));
    float s = max(dot(d, normalize(vec3(0.5, 0.25, 0.83))), 0.0);  // the sun: high, the way the shading lights
    c += vec3(1.0, 0.86, 0.62) * (pow(s, 600.0) * 1.6 + pow(s, 24.0) * 0.16);
    oColor = vec4(c, 1.0);
}
)";

// Depth pre-pass: same vertex shader, no colour. Fills the depth buffer so the colour pass shades
// every pixel once instead of once per overlapping box (big win on integrated GPUs).
const char* kDepthFS = R"(#version 330 core
void main() {}
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
    glEnableVertexAttribArray(5);
    glVertexAttribPointer(5, 3, GL_FLOAT, GL_FALSE, stride, reinterpret_cast<void*>(offsetof(BoxInstance, rot)));
    glVertexAttribDivisor(5, 1);
    glEnableVertexAttribArray(6);
    glVertexAttribPointer(6, 2, GL_FLOAT, GL_FALSE, stride, reinterpret_cast<void*>(offsetof(BoxInstance, slope)));
    glVertexAttribDivisor(6, 1);
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

BoxInstance makePainted(const Vec3& mins, const Vec3& maxs, uint8_t shade) {
    BoxInstance b = makeBox(mins, maxs, (uint32_t(shade) << 16) | (uint32_t(shade) << 8) | shade, false);
    b.rgba[3] = 64;
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

void HudBatch::quad(float x0, float y0, float x1, float y1, float x2, float y2, float x3, float y3, uint32_t rgba) {
    uint8_t c[4] = {uint8_t(rgba >> 24), uint8_t(rgba >> 16), uint8_t(rgba >> 8), uint8_t(rgba)};
    HudVert p0{x0, y0, -1, -1, {c[0], c[1], c[2], c[3]}}, p1{x1, y1, -1, -1, {c[0], c[1], c[2], c[3]}};
    HudVert p2{x2, y2, -1, -1, {c[0], c[1], c[2], c[3]}}, p3{x3, y3, -1, -1, {c[0], c[1], c[2], c[3]}};
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
    depthProgram_ = compileProgram(kBoxVS, kDepthFS, err);
    if (!depthProgram_) return false;
    uDepthViewProj_ = glGetUniformLocation(depthProgram_, "uViewProj");
    uDepthModel_ = glGetUniformLocation(depthProgram_, "uModel");
    hudProgram_ = compileProgram(kHudVS, kHudFS, err);
    if (!hudProgram_) return false;
    skyProgram_ = compileProgram(kSkyVS, kSkyFS, err);
    if (!skyProgram_) return false;
    uSkyFwd_ = glGetUniformLocation(skyProgram_, "uFwd");
    uTex_ = glGetUniformLocation(boxProgram_, "uTex");
    uHasTex_ = glGetUniformLocation(boxProgram_, "uHasTex");
    glUseProgram(boxProgram_);
    glUniform1i(uHasTex_, 0);  // procedural surfaces until loadTextures()
    uSkyRight_ = glGetUniformLocation(skyProgram_, "uRight");
    uSkyUp_ = glGetUniformLocation(skyProgram_, "uUp");
    glGenVertexArrays(1, &skyVao_);  // no vertex data: the triangle comes from gl_VertexID
    uPaint_ = glGetUniformLocation(boxProgram_, "uPaint");
    uPA_ = glGetUniformLocation(boxProgram_, "uPA");
    uPB_ = glGetUniformLocation(boxProgram_, "uPB");
    uPC_ = glGetUniformLocation(boxProgram_, "uPC");
    uPaintMisc_ = glGetUniformLocation(boxProgram_, "uPaintMisc");
    uPaintZ_ = glGetUniformLocation(boxProgram_, "uPaintZ");
    uViewProj_ = glGetUniformLocation(boxProgram_, "uViewProj");
    uModel_ = glGetUniformLocation(boxProgram_, "uModel");
    uEye_ = glGetUniformLocation(boxProgram_, "uEye");
    uFlash_ = glGetUniformLocation(boxProgram_, "uFlash");
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
    staticCpu_ = boxes;
    visible_.reserve(boxes.size());
    order_.reserve(boxes.size());
    glBindBuffer(GL_ARRAY_BUFFER, staticInst_);
    glBufferData(GL_ARRAY_BUFFER, GLsizeiptr(boxes.size() * sizeof(BoxInstance)), nullptr, GL_STREAM_DRAW);
    staticCount_ = int(boxes.size());
}

void Renderer::cullStatic(const Mat4& vp, const Vec3& eye) {
    // Frustum planes from the view-projection matrix (column-major): row3 +- row0/1/2.
    float planes[6][4];
    for (int p = 0; p < 6; ++p) {
        int r = p / 2;
        float s = (p % 2) ? -1.0f : 1.0f;
        for (int k = 0; k < 4; ++k) planes[p][k] = vp.m[k * 4 + 3] + s * vp.m[k * 4 + r];
    }
    order_.clear();
    for (int i = 0; i < staticCount_; ++i) {
        const BoxInstance& b = staticCpu_[size_t(i)];
        bool inside = true;
        for (int p = 0; p < 6 && inside; ++p) {
            const float* q = planes[p];
            float x = q[0] > 0 ? b.maxs[0] : b.mins[0], y = q[1] > 0 ? b.maxs[1] : b.mins[1],
                  z = q[2] > 0 ? b.maxs[2] : b.mins[2];
            inside = q[0] * x + q[1] * y + q[2] * z + q[3] >= 0;
        }
        if (!inside) continue;
        float dx = std::max({b.mins[0] - eye.x, 0.0f, eye.x - b.maxs[0]});
        float dy = std::max({b.mins[1] - eye.y, 0.0f, eye.y - b.maxs[1]});
        float dz = std::max({b.mins[2] - eye.z, 0.0f, eye.z - b.maxs[2]});
        order_.push_back({dx * dx + dy * dy + dz * dz, i});
    }
    std::sort(order_.begin(), order_.end());
    visible_.clear();
    for (const auto& o : order_) visible_.push_back(staticCpu_[size_t(o.second)]);
    visibleCount_ = int(visible_.size());
    glBindBuffer(GL_ARRAY_BUFFER, staticInst_);
    glBufferData(GL_ARRAY_BUFFER, GLsizeiptr(staticCpu_.size() * sizeof(BoxInstance)), nullptr, GL_STREAM_DRAW);
    if (visibleCount_ > 0)
        glBufferSubData(GL_ARRAY_BUFFER, 0, GLsizeiptr(size_t(visibleCount_) * sizeof(BoxInstance)), visible_.data());
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
    glClearColor(0.80f, 0.84f, 0.87f, 1.0f);
    glDepthMask(GL_TRUE);
    glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
}

void Renderer::drawBoxes(const Mat4& viewProj, const Vec3& eye, const std::vector<BoxInstance>& dynamicBoxes) {
    glEnable(GL_DEPTH_TEST);
    glEnable(GL_CULL_FACE);
    glDisable(GL_BLEND);
    Mat4 id = identity();
    int n = std::min(int(dynamicBoxes.size()), kMaxDynamicBoxes);
    if (n > 0) uploadDynamic(dynamicBoxes, n);
    cullStatic(viewProj, eye);
    auto drawAll = [&]() {
        glBindVertexArray(staticVao_);
        if (visibleCount_ > 0) glDrawArraysInstanced(GL_TRIANGLES, 0, 36, visibleCount_);
        if (n > 0) {
            glBindVertexArray(dynVao_);
            glDrawArraysInstanced(GL_TRIANGLES, 0, 36, n);
        }
        if (decalCount_ > 0) {
            glBindVertexArray(decalVao_);
            glDrawArraysInstanced(GL_TRIANGLES, 0, 36, decalCount_);
        }
    };
    if (depthPrepass_) {
        glUseProgram(depthProgram_);
        glUniformMatrix4fv(uDepthViewProj_, 1, GL_FALSE, viewProj.m);
        glUniformMatrix4fv(uDepthModel_, 1, GL_FALSE, id.m);
        glColorMask(GL_FALSE, GL_FALSE, GL_FALSE, GL_FALSE);
        drawAll();
        glColorMask(GL_TRUE, GL_TRUE, GL_TRUE, GL_TRUE);
        glDepthMask(GL_FALSE);  // depth is final: the colour pass only shades the visible surface
    }
    glUseProgram(boxProgram_);
    glUniformMatrix4fv(uViewProj_, 1, GL_FALSE, viewProj.m);
    glUniformMatrix4fv(uModel_, 1, GL_FALSE, id.m);
    glUniform3f(uEye_, eye.x, eye.y, eye.z);
    setPaint(nullptr);
    drawAll();
    glDepthMask(GL_TRUE);
}

int Renderer::loadTextures(const std::string& dir) {
    static const char* const kNames[] = {"sandstone", "plaster", "sand", "paving", "planks", "shutter", "plate"};
    constexpr int kSize = 512, kLayers = int(sizeof(kNames) / sizeof(kNames[0]));
    std::vector<unsigned char> pixels(size_t(kSize) * kSize * 3 * kLayers);
    for (int k = 0; k < kLayers; ++k) {
        int w = 0, h = 0, n = 0;
        unsigned char* px = stbi_load((dir + "/" + kNames[k] + ".jpg").c_str(), &w, &h, &n, 3);
        const bool ok = px && w == kSize && h == kSize;
        if (ok) std::copy(px, px + size_t(kSize) * kSize * 3, pixels.begin() + std::ptrdiff_t(size_t(k) * kSize * kSize * 3));
        if (px) stbi_image_free(px);
        if (!ok) return 0;  // all or nothing: the procedural surfaces stay
    }
    glGenTextures(1, &texArray_);
    glActiveTexture(GL_TEXTURE1);
    glBindTexture(GL_TEXTURE_2D_ARRAY, texArray_);
    glPixelStorei(GL_UNPACK_ALIGNMENT, 1);
    glTexImage3D(GL_TEXTURE_2D_ARRAY, 0, GL_RGB8, kSize, kSize, kLayers, 0, GL_RGB, GL_UNSIGNED_BYTE, pixels.data());
    glGenerateMipmap(GL_TEXTURE_2D_ARRAY);
    glTexParameteri(GL_TEXTURE_2D_ARRAY, GL_TEXTURE_MIN_FILTER, GL_LINEAR_MIPMAP_LINEAR);
    glTexParameteri(GL_TEXTURE_2D_ARRAY, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D_ARRAY, GL_TEXTURE_WRAP_S, GL_REPEAT);
    glTexParameteri(GL_TEXTURE_2D_ARRAY, GL_TEXTURE_WRAP_T, GL_REPEAT);
    GLfloat maxAniso = 1.0f;  // anisotropic filtering keeps floors sharp at a glancing angle
    glGetFloatv(GL_MAX_TEXTURE_MAX_ANISOTROPY, &maxAniso);
    glTexParameterf(GL_TEXTURE_2D_ARRAY, GL_TEXTURE_MAX_ANISOTROPY, std::min(8.0f, std::max(1.0f, maxAniso)));
    glActiveTexture(GL_TEXTURE0);  // the HUD's font stays on unit 0
    glUseProgram(boxProgram_);
    glUniform1i(uTex_, 1);
    glUniform1i(uHasTex_, 1);
    return kLayers;
}

void Renderer::drawSky(const Vec3& fwd, const Vec3& right, const Vec3& up) {
    glEnable(GL_DEPTH_TEST);
    glDepthMask(GL_FALSE);  // at the far plane: only where the depth buffer is still clear
    glDisable(GL_CULL_FACE);
    glDisable(GL_BLEND);
    glUseProgram(skyProgram_);
    glUniform3f(uSkyFwd_, fwd.x, fwd.y, fwd.z);
    glUniform3f(uSkyRight_, right.x, right.y, right.z);
    glUniform3f(uSkyUp_, up.x, up.y, up.z);
    glBindVertexArray(skyVao_);
    glDrawArrays(GL_TRIANGLES, 0, 3);
    glDepthMask(GL_TRUE);
}

void Renderer::uploadDynamic(const std::vector<BoxInstance>& boxes, int n) {
    glBindBuffer(GL_ARRAY_BUFFER, dynInst_);
    glBufferData(GL_ARRAY_BUFFER, kMaxDynamicBoxes * sizeof(BoxInstance), nullptr, GL_STREAM_DRAW);  // orphan
    glBufferSubData(GL_ARRAY_BUFFER, 0, GLsizeiptr(n * sizeof(BoxInstance)), boxes.data());
}

void Renderer::setPaint(const PaintParams* p) {
    glUniform1i(uPaint_, p ? p->pattern : -1);
    if (!p || p->pattern < 0) return;
    glUniform3f(uPA_, p->a[0], p->a[1], p->a[2]);
    glUniform3f(uPB_, p->b[0], p->b[1], p->b[2]);
    glUniform3f(uPC_, p->c[0], p->c[1], p->c[2]);
    glUniform3f(uPaintMisc_, p->wear, p->gloss, p->seed);
    glUniform2f(uPaintZ_, p->zMin, p->zMax);
}

void Renderer::drawModel(const Mat4& viewProj, const Mat4& model, const std::vector<BoxInstance>& boxes,
                         const PaintParams* paint) {
    int n = std::min(int(boxes.size()), kMaxDynamicBoxes);
    if (n == 0) return;
    glEnable(GL_DEPTH_TEST);
    glEnable(GL_CULL_FACE);
    glDisable(GL_BLEND);
    glUseProgram(boxProgram_);
    glUniformMatrix4fv(uViewProj_, 1, GL_FALSE, viewProj.m);
    glUniformMatrix4fv(uModel_, 1, GL_FALSE, model.m);
    setPaint(paint);

    uploadDynamic(boxes, n);
    glBindVertexArray(dynVao_);
    glDrawArraysInstanced(GL_TRIANGLES, 0, 36, n);
}

void Renderer::setFlash(const Vec3& pos, float strength) {
    glUseProgram(boxProgram_);
    glUniform4f(uFlash_, pos.x, pos.y, pos.z, strength);
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

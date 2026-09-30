// Scene building: every model in the game is assembled at runtime from instanced
// primitives (cube / sphere / cone / cylinder), so the whole world renders in a
// handful of instanced draw calls.
#include "game.h"
#include <algorithm>

namespace {

inline void push(FrameScene& s, MeshId m, const mat4& M, vec3 col, float emissive = 0) {
    s.inst[m].push_back({M, vec4(col, emissive)});
}

inline void ell(FrameScene& s, const mat4& root, vec3 c, vec3 size, vec3 col, float e = 0) {
    push(s, MESH_SPHERE, root * translate(c) * scale(size), col, e);
}

inline void box(FrameScene& s, const mat4& root, vec3 c, vec3 size, vec3 col, float e = 0, float yaw = 0, float pitch = 0, float roll = 0) {
    push(s, MESH_CUBE, root * translate(c) * rotateY(yaw) * rotateX(pitch) * rotateZ(roll) * scale(size), col, e);
}

// Mesh stretched between two local points (cylinder / cone tip at b / cube).
inline void seg(FrameScene& s, MeshId m, const mat4& root, vec3 a, vec3 b, float thick, vec3 col, float e = 0) {
    vec3 d = b - a;
    float len = length(d);
    if (len < 1e-4f) return;
    vec3 y = d / len;
    vec3 ref = std::fabs(y.y) < 0.95f ? vec3(0, 1, 0) : vec3(1, 0, 0);
    vec3 x = normalize(cross(ref, y));
    vec3 z = cross(x, y);
    push(s, m, root * basis(x * thick, y * len, z * thick, (a + b) * 0.5f), col, e);
}

inline vec3 mixc(vec3 a, vec3 b, float t) { return lerp(a, b, t); }

}  // namespace

bool Game::sphereVisible(vec3 c, float r) const {
    for (int i = 0; i < 6; i++) {
        const vec4& p = frustum_[i];
        if (p.x * c.x + p.y * c.y + p.z * c.z + p.w < -r) return false;
    }
    return true;
}

// ---------------------------------------------------------------------------
void Game::setupLighting(FrameScene& s, vec3 camPos, vec3 camTarget, const mat4& view, const mat4& proj) {
    SceneUBO& u = s.ubo;
    float ang = (time_ - 0.25f) * TAU;
    vec3 sun = normalize(vec3(std::cos(ang), std::sin(ang), 0.35f));
    vec3 moon = normalize(vec3(-std::cos(ang), -std::sin(ang), -0.25f));
    float dayF = smoothstep(-0.08f, 0.2f, sun.y);
    float night = 1.0f - smoothstep(-0.18f, 0.06f, sun.y);
    float sunset = saturate(1.0f - std::fabs(sun.y) * 3.5f) * smoothstep(-0.2f, 0.0f, sun.y);
    bool hollowNight = nightActive_ || mode_ != GM_PLAY;
    float hollow = night * (hollowNight ? 1.0f : 0.35f);

    vec3 sunColor = lerp(vec3(1.0f, 0.45f, 0.2f) * 1.8f, vec3(1.0f, 0.95f, 0.88f) * 3.0f, smoothstep(0.0f, 0.35f, sun.y)) * dayF;
    vec3 keyDir = sun;
    float shadowStrength = dayF;
    if (sun.y < 0.02f) {
        keyDir = moon;
        sunColor = lerp(vec3(0.22f, 0.27f, 0.5f), vec3(0.4f, 0.18f, 0.2f), hollow * 0.35f) * 0.55f * night;
        shadowStrength = 0.55f * night;
    }
    vec3 zenithDay(0.18f, 0.40f, 0.85f), zenithNight(0.006f, 0.01f, 0.03f), zenithSunset(0.3f, 0.28f, 0.5f);
    vec3 horizDay(0.62f, 0.76f, 0.92f), horizNight(0.018f, 0.022f, 0.045f), horizSunset(0.95f, 0.5f, 0.3f);
    vec3 zenith = lerp(lerp(zenithNight, zenithDay, dayF), zenithSunset, sunset * 0.6f);
    vec3 horizon = lerp(lerp(horizNight, horizDay, dayF), horizSunset, sunset * 0.8f);
    horizon += vec3(0.04f, 0.005f, 0.008f) * hollow;

    u.sunDir = vec4(keyDir, shadowStrength);
    u.sunColor = vec4(sunColor, 1);
    u.skyColor = vec4(zenith * 1.1f + vec3(0.02f, 0.025f, 0.04f) * night, 1);
    u.groundColor = vec4(vec3(0.22f, 0.2f, 0.15f) * dayF + vec3(0.012f, 0.01f, 0.015f), 1);
    u.fogColor = vec4(horizon, lerpf(0.0014f, 0.003f, night));
    u.moonDir = vec4(moon, 1);
    u.skySunDir = vec4(sun, 1);
    u.hollowPos = vec4(0, -30, 0, HOLLOW_RIM - 6.0f);
    u.camPos = vec4(camPos, 1);

    mat4 vp = proj * view;
    u.viewProj = vp;
    u.invViewProj = inverse(vp);

    // Shadow map follows the camera, snapped to texels to avoid shimmering.
    vec3 center = camPos + normalize(camTarget - camPos) * 55.0f;
    const float R = 110.0f;
    vec3 up = std::fabs(keyDir.y) > 0.95f ? vec3(0, 0, 1) : vec3(0, 1, 0);
    mat4 lv = lookAt(center + keyDir * 400.0f, center, up);
    vec3 lc = lv.transformPoint(center);
    float texel = 2 * R / 2048.0f;
    vec3 snap(std::round(lc.x / texel) * texel - lc.x, std::round(lc.y / texel) * texel - lc.y, 0);
    lv = translate(snap) * lv;
    u.lightViewProj = ortho(-R, R, -R, R, 1.0f, 900.0f) * lv;

    // Point lights: gather candidates, keep the nearest.
    struct PL { vec3 pos; float radius; vec3 color; float intensity; float d; };
    std::vector<PL> lights;
    float flick = 0.85f + 0.15f * std::sin(totalTime_ * 13.0f) * std::sin(totalTime_ * 7.3f);
    for (const auto& st : structures_)
        if (st.alive && st.type == I_CAMPFIRE) lights.push_back({st.pos + vec3(0, 1.2f, 0), 18.0f, vec3(1.0f, 0.55f, 0.2f), 3.0f * flick, 0});
    if (mode_ == GM_PLAY && !player_.dead && selectedItem() == I_TORCH && player_.riding < 0)
        lights.push_back({player_.pos + vec3(0, 2.0f, 0) + dirFromYaw(player_.yaw) * 0.5f, 14.0f, vec3(1.0f, 0.6f, 0.25f), 2.5f * flick, 0});
    for (const auto& c : creatures_) {
        if (!c.alive || c.state == CS_DEAD) continue;
        const Species& sp = SPECIES[c.species];
        if (sp.glow >= 1.0f || c.species == S_FIRE_DRAGON) {
            float r = 6.0f + sp.size * 5.0f;
            lights.push_back({c.pos + vec3(0, creatureHeight(c), 0), r, sp.c3, 1.2f + (c.breathTime > 0 ? 2.0f : 0.0f), 0});
        }
    }
    for (const auto& e : infected_)
        if (e.alive && e.state != IS_DEAD && e.cls == IC_SHAMAN) lights.push_back({e.pos + vec3(0, 2.2f, 0), 7.0f, vec3(1.0f, 0.1f, 0.25f), 1.5f, 0});
    for (const auto& p : projectiles_)
        if (p.kind == PJ_BREATH || p.kind == PJ_BOLT) {
            lights.push_back({p.pos, 8.0f, p.color, 0.6f, 0});
            if (lights.size() > 80) break;
        }
    if (hollow > 0.05f) lights.push_back({vec3(0, -12, 0), 90.0f, vec3(1.0f, 0.1f, 0.04f), 2.5f * hollow, 0});
    for (auto& l : lights) l.d = length(l.pos - camPos) - l.radius;
    std::sort(lights.begin(), lights.end(), [](const PL& a, const PL& b) { return a.d < b.d; });
    int n = std::min((int)lights.size(), MAX_POINT_LIGHTS);
    for (int i = 0; i < n; i++) {
        u.lightPos[i] = vec4(lights[i].pos, lights[i].radius);
        u.lightColor[i] = vec4(lights[i].color, lights[i].intensity);
    }
    u.params = vec4(totalTime_, night, hollow, (float)n);
}

void Game::buildScene(FrameScene& s) {
    float aspect = (float)std::max(1, renderer_->width()) / (float)std::max(1, renderer_->height());
    view_ = lookAt(camPos_, camTarget_, vec3(0, 1, 0));
    proj_ = perspective(fov_, aspect, 0.3f, 2600.0f);
    viewProj_ = proj_ * view_;
    const mat4& m = viewProj_;
    auto row = [&](int r) { return vec4(m.m[r], m.m[4 + r], m.m[8 + r], m.m[12 + r]); };
    vec4 r0 = row(0), r1 = row(1), r2 = row(2), r3 = row(3);
    auto add = [](vec4 a, vec4 b, float sgn) { return vec4(a.x + sgn * b.x, a.y + sgn * b.y, a.z + sgn * b.z, a.w + sgn * b.w); };
    frustum_[0] = add(r3, r0, 1);
    frustum_[1] = add(r3, r0, -1);
    frustum_[2] = add(r3, r1, 1);
    frustum_[3] = add(r3, r1, -1);
    frustum_[4] = r2;
    frustum_[5] = add(r3, r2, -1);
    for (auto& p : frustum_) {
        float l = std::sqrt(p.x * p.x + p.y * p.y + p.z * p.z);
        p = vec4(p.x / l, p.y / l, p.z / l, p.w / l);
    }

    setupLighting(s, camPos_, camTarget_, view_, proj_);
    drawProps(s);
    for (const auto& c : creatures_)
        if (c.alive) drawCreature(s, c);
    for (const auto& e : infected_)
        if (e.alive) drawInfected(s, e);
    drawStructures(s);
    drawEggs(s);
    if (mode_ == GM_PLAY && !player_.dead) {
        Item held = selectedItem();
        if (player_.carryEgg >= 0 || player_.carryBaby >= 0) held = I_NONE;
        float walk = player_.riding >= 0 ? 0.0f : player_.anim;
        drawHumanoid(s, player_.pos, player_.riding >= 0 ? creatures_[player_.riding].yaw : player_.yaw, 1.0f,
                     vec3(0.85f, 0.65f, 0.5f), vec3(0.45f, 0.32f, 0.2f), vec3(0.3f, 0.2f, 0.12f), walk, player_.swing, held,
                     false, 0, 0, player_.riding >= 0 ? 1.0f : 0.0f);
    }
    // Trailer: a rider on the griffin.
    if (mode_ == GM_TRAILER && trailerGriffin_ >= 0 && trailerTime_ > 17.5f && trailerTime_ < 24.5f) {
        const Creature& g = creatures_[trailerGriffin_];
        drawHumanoid(s, creatureSeat(g), g.yaw, 1.0f, vec3(0.85f, 0.65f, 0.5f), vec3(0.45f, 0.32f, 0.2f), vec3(0.3f, 0.2f, 0.12f), 0, 0,
                     I_SPEAR, false, 0, 0, 1.0f);
    }
    drawEffects(s);
}

// ---------------------------------------------------------------------------
void Game::drawProps(FrameScene& s) {
    float windT = totalTime_;
    for (const auto& p : props_.props) {
        if (p.respawn > 0) continue;
        bool small = p.type >= P_EMBERBUSH;
        float maxD = small ? 120.0f : (p.type <= P_SNOWPINE ? 360.0f : 260.0f);
        vec3 d = p.pos - camPos_;
        float dist2 = d.x * d.x + d.z * d.z;
        if (dist2 > maxD * maxD) continue;
        float sc = p.scale;
        float bound = small ? 1.5f : 7.0f * sc;
        if (!sphereVisible(p.pos + vec3(0, bound * 0.5f, 0), bound)) continue;
        float var = (p.seed % 1000) / 1000.0f;
        mat4 root = translate(p.pos) * rotateY(p.yaw);
        float sway = std::sin(windT * 1.3f + p.pos.x * 0.1f) * 0.02f;
        bool far = dist2 > 140.0f * 140.0f;
        switch (p.type) {
            case P_OAK: {
                vec3 trunk(0.4f, 0.28f, 0.17f);
                vec3 leaf = mixc(vec3(0.25f, 0.5f, 0.18f), vec3(0.4f, 0.55f, 0.15f), var);
                seg(s, MESH_CYL, root, vec3(0, -0.3f, 0), vec3(0, 3.2f * sc, 0), 0.55f * sc, trunk);
                mat4 top = root * rotateZ(sway);
                ell(s, top, vec3(0, 4.6f * sc, 0), vec3(4.2f, 3.4f, 4.2f) * sc, leaf);
                if (!far) {
                    ell(s, top, vec3(1.2f, 3.9f, 0.6f) * sc, vec3(2.6f, 2.2f, 2.6f) * sc, leaf * 0.9f);
                    ell(s, top, vec3(-1.1f, 4.0f, -0.7f) * sc, vec3(2.8f, 2.3f, 2.8f) * sc, leaf * 1.08f);
                }
                break;
            }
            case P_PINE:
            case P_SNOWPINE: {
                vec3 trunk(0.35f, 0.24f, 0.15f);
                vec3 leaf = mixc(vec3(0.12f, 0.33f, 0.18f), vec3(0.18f, 0.4f, 0.2f), var);
                seg(s, MESH_CYL, root, vec3(0, -0.3f, 0), vec3(0, 2.0f * sc, 0), 0.4f * sc, trunk);
                mat4 top = root * rotateZ(sway);
                for (int k = 0; k < (far ? 2 : 3); k++) {
                    float y = (1.6f + k * 1.7f) * sc, w = (3.4f - k * 0.9f) * sc;
                    push(s, MESH_CONE, top * translate(vec3(0, y + 1.3f * sc, 0)) * scale(vec3(w, 2.8f * sc, w)), leaf);
                    if (p.type == P_SNOWPINE)
                        push(s, MESH_CONE, top * translate(vec3(0, y + 1.75f * sc, 0)) * scale(vec3(w * 0.72f, 1.9f * sc, w * 0.72f)),
                             vec3(0.92f, 0.95f, 1.0f));
                }
                break;
            }
            case P_PALM: {
                vec3 trunk(0.55f, 0.42f, 0.28f);
                vec3 prev(0, -0.2f, 0);
                float lean = 0.25f + var * 0.3f;
                for (int k = 1; k <= 4; k++) {
                    vec3 nx(std::sin(k * 0.35f) * lean * k * 0.6f, k * 1.5f * sc, 0);
                    seg(s, MESH_CYL, root, prev, nx, (0.38f - k * 0.04f) * sc, trunk * (k % 2 ? 1.0f : 0.9f));
                    prev = nx;
                }
                for (int k = 0; k < 7; k++) {
                    float a = k * TAU / 7 + sway * 5;
                    vec3 dir(std::sin(a), 0, std::cos(a));
                    vec3 tip = prev + dir * 3.0f * sc + vec3(0, -1.2f * sc, 0);
                    vec3 mid = prev + dir * 1.6f * sc + vec3(0, 0.3f * sc, 0);
                    seg(s, MESH_CUBE, root, prev, mid, 0.5f * sc, vec3(0.25f, 0.55f, 0.2f));
                    seg(s, MESH_CUBE, root, mid, tip, 0.4f * sc, vec3(0.28f, 0.58f, 0.22f));
                }
                ell(s, root, prev + vec3(0.2f, -0.3f, 0), vec3(0.4f, 0.4f, 0.4f) * sc, vec3(0.4f, 0.3f, 0.15f));
                break;
            }
            case P_JUNGLETREE: {
                vec3 trunk(0.38f, 0.3f, 0.2f);
                seg(s, MESH_CYL, root, vec3(0, -0.3f, 0), vec3(0, 7.5f * sc, 0), 0.7f * sc, trunk);
                mat4 top = root * rotateZ(sway);
                vec3 leaf = mixc(vec3(0.12f, 0.45f, 0.16f), vec3(0.2f, 0.52f, 0.12f), var);
                ell(s, top, vec3(0, 8.2f * sc, 0), vec3(6.5f, 2.2f, 6.5f) * sc, leaf);
                ell(s, top, vec3(0.5f, 9.3f * sc, 0.3f), vec3(4.2f, 1.8f, 4.2f) * sc, leaf * 1.12f);
                if (!far) {
                    seg(s, MESH_CYL, root, vec3(0, 5.5f * sc, 0), vec3(1.8f, 7.6f, 0.4f) * sc, 0.25f * sc, trunk);
                    seg(s, MESH_CYL, root, vec3(1.5f, 7.8f, 0.3f) * sc, vec3(1.7f, 4.5f, 0.4f) * sc, 0.06f * sc, vec3(0.2f, 0.4f, 0.15f));
                }
                break;
            }
            case P_DEADTREE: {
                vec3 c(0.2f, 0.17f, 0.16f);
                seg(s, MESH_CYL, root, vec3(0, -0.3f, 0), vec3(0.2f, 4.5f * sc, 0), 0.4f * sc, c);
                seg(s, MESH_CYL, root, vec3(0, 2.5f * sc, 0), vec3(1.5f, 4.0f, 0.3f) * sc, 0.18f * sc, c);
                seg(s, MESH_CYL, root, vec3(0.1f, 3.3f * sc, 0), vec3(-1.3f, 5.0f, -0.4f) * sc, 0.14f * sc, c);
                break;
            }
            case P_ROCK:
            case P_METALROCK: {
                vec3 c = mixc(vec3(0.45f, 0.44f, 0.42f), vec3(0.55f, 0.53f, 0.5f), var);
                ell(s, root, vec3(0, 0.3f * sc, 0), vec3(2.0f, 1.3f, 1.7f) * sc, c);
                ell(s, root, vec3(0.5f, 0.2f, 0.4f) * sc, vec3(1.2f, 0.9f, 1.1f) * sc, c * 0.92f);
                if (p.type == P_METALROCK) {
                    box(s, root, vec3(0.2f, 0.9f, 0.1f) * sc, vec3(0.45f, 0.45f, 0.45f) * sc, vec3(0.75f, 0.78f, 0.85f), 0.05f, 0.5f, 0.4f);
                    box(s, root, vec3(-0.5f, 0.7f, 0.3f) * sc, vec3(0.35f, 0.35f, 0.35f) * sc, vec3(0.8f, 0.6f, 0.4f), 0.05f, 1.1f, 0.2f);
                }
                break;
            }
            case P_BOULDER: {
                vec3 c = mixc(vec3(0.5f, 0.48f, 0.45f), vec3(0.42f, 0.4f, 0.38f), var);
                ell(s, root, vec3(0, 0.8f * sc, 0), vec3(4.6f, 3.4f, 4.0f) * sc, c);
                ell(s, root, vec3(1.3f, 0.4f, 1.0f) * sc, vec3(2.4f, 1.8f, 2.2f) * sc, c * 0.9f);
                break;
            }
            case P_CRYSTAL: {
                vec3 c(0.45f, 0.9f, 1.0f);
                for (int k = 0; k < 4; k++) {
                    float a = k * 1.7f + var * 3;
                    vec3 base(std::sin(a) * 0.4f * sc, 0, std::cos(a) * 0.4f * sc);
                    vec3 tip = base + vec3(std::sin(a) * 0.5f, (1.6f - k * 0.25f) * 1.4f, std::cos(a) * 0.5f) * sc;
                    seg(s, MESH_CONE, root, base - vec3(0, 0.3f, 0), tip, (0.5f - k * 0.06f) * sc, c * (0.8f + k * 0.08f), 0.7f);
                }
                break;
            }
            case P_EMBERBUSH:
            case P_DREAMBUSH: {
                vec3 leaf = p.type == P_EMBERBUSH ? vec3(0.22f, 0.45f, 0.18f) : vec3(0.25f, 0.3f, 0.35f);
                vec3 berry = p.type == P_EMBERBUSH ? vec3(0.95f, 0.15f, 0.1f) : vec3(0.65f, 0.3f, 0.95f);
                ell(s, root, vec3(0, 0.45f, 0), vec3(1.6f, 1.1f, 1.6f) * sc, leaf);
                if (!far)
                    for (int k = 0; k < 6; k++) {
                        float a = k * 1.05f + var;
                        ell(s, root, vec3(std::sin(a) * 0.65f, 0.55f + (k % 3) * 0.15f, std::cos(a) * 0.65f) * sc, vec3(0.18f, 0.18f, 0.18f), berry,
                            p.type == P_DREAMBUSH ? 0.5f : 0.15f);
                    }
                break;
            }
            case P_SPIRITHERB:
                for (int k = 0; k < 3; k++) {
                    float a = k * 2.1f + var * 2;
                    vec3 b(std::sin(a) * 0.2f, 0, std::cos(a) * 0.2f);
                    seg(s, MESH_CONE, root, b, b + vec3(std::sin(a) * 0.2f, 0.8f, std::cos(a) * 0.2f), 0.18f, vec3(0.4f, 0.8f, 1.0f), 0.9f);
                }
                break;
            case P_FERN:
                for (int k = 0; k < 5; k++) {
                    float a = k * TAU / 5 + var;
                    vec3 tip(std::sin(a) * 0.9f * sc, 0.6f * sc, std::cos(a) * 0.9f * sc);
                    seg(s, MESH_CUBE, root, vec3(0, 0, 0), tip, 0.25f * sc, vec3(0.25f, 0.5f + var * 0.1f, 0.2f));
                }
                break;
            case P_BONEPILE: {
                vec3 bone(0.88f, 0.85f, 0.76f);
                seg(s, MESH_CYL, root, vec3(-0.6f, 0.1f, 0), vec3(0.7f, 0.2f, 0.3f), 0.15f, bone);
                seg(s, MESH_CYL, root, vec3(0, 0.1f, -0.6f), vec3(-0.3f, 0.25f, 0.6f), 0.13f, bone);
                ell(s, root, vec3(0.2f, 0.3f, 0.1f), vec3(0.45f, 0.4f, 0.5f), bone);
                ell(s, root, vec3(0.3f, 0.32f, 0.3f), vec3(0.1f, 0.1f, 0.1f), vec3(0.05f, 0.05f, 0.05f));
                break;
            }
            default: break;
        }
    }

    // Totems around the rim of the Hollow.
    for (int k = 0; k < 14; k++) {
        float a = k * TAU / 14;
        vec3 pos(std::sin(a) * (HOLLOW_RIM + 8), 0, std::cos(a) * (HOLLOW_RIM + 8));
        pos.y = terrain_.heightAt(pos.x, pos.z);
        if (!sphereVisible(pos + vec3(0, 3, 0), 5)) continue;
        mat4 root = translate(pos) * rotateY(a + PI);
        seg(s, MESH_CYL, root, vec3(0, -0.5f, 0), vec3(0, 5.5f, 0), 0.35f, vec3(0.25f, 0.18f, 0.15f));
        ell(s, root, vec3(0, 5.8f, 0), vec3(0.8f, 0.9f, 0.9f), vec3(0.85f, 0.82f, 0.72f));
        float glow = s.ubo.params.z;
        ell(s, root, vec3(0.18f, 5.9f, 0.38f), vec3(0.16f, 0.12f, 0.1f), vec3(1.0f, 0.15f, 0.05f), 0.3f + glow * 2.0f);
        ell(s, root, vec3(-0.18f, 5.9f, 0.38f), vec3(0.16f, 0.12f, 0.1f), vec3(1.0f, 0.15f, 0.05f), 0.3f + glow * 2.0f);
        seg(s, MESH_CONE, root, vec3(0.3f, 6.1f, 0), vec3(0.9f, 7.0f, -0.2f), 0.2f, vec3(0.85f, 0.82f, 0.72f));
        seg(s, MESH_CONE, root, vec3(-0.3f, 6.1f, 0), vec3(-0.9f, 7.0f, -0.2f), 0.2f, vec3(0.85f, 0.82f, 0.72f));
    }
    // The pit's glowing heart.
    float glow = s.ubo.params.z;
    if (glow > 0.02f && sphereVisible(vec3(0, -30, 0), 30)) {
        // Burning cracks radiating from the centre of the pit floor.
        Rng cr(31337);
        for (int k = 0; k < 22; k++) {
            float a = cr.range(0, TAU), r0 = cr.range(0, 6), r1 = r0 + cr.range(6, 18);
            vec3 a0(std::sin(a) * r0, 0, std::cos(a) * r0), a1(std::sin(a + cr.range(-0.3f, 0.3f)) * r1, 0, std::cos(a) * r1);
            a0.y = terrain_.heightAt(a0.x, a0.z) + 0.05f;
            a1.y = terrain_.heightAt(a1.x, a1.z) + 0.05f;
            seg(s, MESH_CUBE, mat4::identity(), a0, a1, cr.range(0.25f, 0.6f), vec3(1.0f, 0.2f, 0.05f), glow * 1.8f);
        }
        vec3 core(0, terrain_.heightAt(0, 0) + 0.2f, 0);
        ell(s, mat4::identity(), core, vec3(7, 0.6f, 7), vec3(1.0f, 0.3f, 0.08f), glow * 2.2f);
    }
}

// ---------------------------------------------------------------------------
void Game::drawCreature(FrameScene& s, const Creature& c) {
    const Species& sp = SPECIES[c.species];
    float gs = growthScale(c);
    float sz = sp.size * gs;
    float h = creatureHeight(c);
    float rad = creatureRadius(c);
    vec3 center = c.pos + vec3(0, h * 0.5f, 0);
    float dist = length(center - camPos_);
    if (dist > 500.0f || !sphereVisible(center, std::max(rad, h) * 2.0f + sp.tailL * sz)) return;
    bool lod = dist > 160.0f;

    vec3 c1 = sp.c1, c2 = sp.c2, c3 = sp.c3;
    if (c.hurt > 0) {
        c1 = mixc(c1, vec3(1, 0.35f, 0.3f), c.hurt * 0.5f);
        c2 = mixc(c2, vec3(1, 0.35f, 0.3f), c.hurt * 0.5f);
    }
    if (c.state == CS_DEAD) {
        c1 = c1 * 0.55f;
        c2 = c2 * 0.55f;
    }
    float glowK = c.state == CS_DEAD ? 0.0f : sp.glow;
    bool asleep = c.state == CS_UNCONSCIOUS || c.state == CS_DEAD;
    float eyeGlow = sp.glowEyes && !asleep ? 1.2f : 0.0f;
    vec3 eyeCol = sp.glowEyes ? c3 : vec3(0.04f, 0.03f, 0.03f);
    float breathe = std::sin(c.anim * 0.7f) * 0.02f;

    mat4 root = translate(c.pos) * rotateY(c.yaw) * rotateX(c.pitch) * rotateZ(c.roll) * scale(vec3(sz, sz, sz));
    float moveAmt = std::min(c.moveAmt, 1.6f);
    float ph = c.anim;

    auto drawWings = [&](vec3 shoulderBase, float spanIn, float chord, bool feathered) {
        float f;
        bool open = c.flying || c.state == CS_SCRIPTED;
        if (open) f = std::sin(c.flap * 1.0f) * 0.75f + 0.1f;
        else f = 0.35f + breathe;  // folded: lying back along the body
        for (int side = -1; side <= 1; side += 2) {
            float span = spanIn;
            vec3 sh(shoulderBase.x * side, shoulderBase.y, shoulderBase.z);
            float fold = open ? 0.0f : 1.3f;
            if (!open) span *= 0.62f;
            mat4 w = root * translate(sh) * rotateY(side * fold) * rotateZ(side * f);
            vec3 wc = feathered ? c2 : c2;
            box(s, w, vec3(side * span * 0.25f, 0, -chord * 0.25f), vec3(span * 0.5f, 0.06f, chord), wc);
            box(s, w, vec3(side * span * 0.25f, 0.02f, chord * 0.22f), vec3(span * 0.5f, 0.12f, 0.12f), c1);
            mat4 w2 = w * translate(vec3(side * span * 0.5f, 0, 0)) * rotateZ(side * (open ? f * 0.5f : -0.25f));
            box(s, w2, vec3(side * span * 0.22f, 0, -chord * 0.2f), vec3(span * 0.45f, 0.05f, chord * 0.75f), wc * 0.95f,
                sp.glow > 0.8f ? glowK * 0.3f : 0.0f);
            box(s, w2, vec3(side * span * 0.22f, 0.02f, chord * 0.15f), vec3(span * 0.45f, 0.1f, 0.1f), c1);
            if (feathered && !lod)
                for (int k = 0; k < 3; k++)
                    box(s, w2, vec3(side * (span * 0.42f + k * 0.05f), 0, -chord * (0.3f + k * 0.18f)), vec3(span * 0.18f, 0.04f, chord * 0.3f), wc * 1.03f);
        }
    };

    auto drawHead = [&](vec3 hc, float hs, float yawOff, bool beak) {
        mat4 hr = root * translate(hc) * rotateY(yawOff);
        float jaw = std::max(c.attackAnim, c.breathTime > 0 ? 1.0f : 0.0f);
        ell(s, hr, vec3(0, 0, 0), vec3(hs * 0.95f, hs * 0.85f, hs * 1.1f), c1);
        if (beak) {
            seg(s, MESH_CONE, hr, vec3(0, hs * 0.05f, hs * 0.4f), vec3(0, -hs * 0.3f, hs * 1.05f), hs * 0.38f, c3);
        } else {
            box(s, hr, vec3(0, -hs * 0.05f, hs * 0.65f), vec3(hs * 0.62f, hs * 0.45f, hs * 0.85f), c1 * 0.95f);
            box(s, hr, vec3(0, -hs * 0.28f - jaw * hs * 0.15f, hs * 0.55f), vec3(hs * 0.55f, hs * 0.16f, hs * 0.75f), c2, 0, 0, jaw * 0.4f);
            if (c.breathTime > 0 && sp.breath)
                ell(s, hr, vec3(0, -hs * 0.15f, hs * 1.1f), vec3(hs * 0.5f, hs * 0.5f, hs * 0.5f), sp.breathColor, 2.0f);
            if (sp.plan == BP_QUAD && (sp.diet == D_CARNIVORE) && !lod) {
                seg(s, MESH_CONE, hr, vec3(hs * 0.18f, -hs * 0.2f, hs * 0.9f), vec3(hs * 0.18f, -hs * 0.45f, hs * 0.95f), hs * 0.1f, vec3(0.95f, 0.93f, 0.85f));
                seg(s, MESH_CONE, hr, vec3(-hs * 0.18f, -hs * 0.2f, hs * 0.9f), vec3(-hs * 0.18f, -hs * 0.45f, hs * 0.95f), hs * 0.1f, vec3(0.95f, 0.93f, 0.85f));
            }
        }
        float eyeH = asleep ? 0.04f : 0.2f;
        ell(s, hr, vec3(hs * 0.38f, hs * 0.18f, hs * 0.38f), vec3(hs * 0.2f, hs * eyeH, hs * 0.2f), eyeCol, eyeGlow);
        ell(s, hr, vec3(-hs * 0.38f, hs * 0.18f, hs * 0.38f), vec3(hs * 0.2f, hs * eyeH, hs * 0.2f), eyeCol, eyeGlow);
        if (lod) return;
        if (sp.horns == 1) {
            seg(s, MESH_CONE, hr, vec3(0, hs * 0.35f, hs * 0.45f), vec3(0, hs * 1.4f, hs * 1.2f), hs * 0.2f, c3, glowK);
        } else if (sp.horns > 1 && sp.plan != BP_SERPENT) {
            for (int k = 0; k < sp.horns; k++) {
                float side = (k % 2) ? 1.0f : -1.0f;
                float row = (float)(k / 2);
                seg(s, MESH_CONE, hr, vec3(side * hs * (0.3f + row * 0.1f), hs * 0.35f, -hs * (0.1f + row * 0.3f)),
                    vec3(side * hs * (0.55f + row * 0.15f), hs * (1.1f - row * 0.2f), -hs * (0.8f + row * 0.3f)), hs * 0.22f,
                    c.species == S_CHIMERA ? vec3(0.85f, 0.8f, 0.7f) : mixc(c2, vec3(0.9f, 0.85f, 0.75f), 0.5f));
            }
        }
        if (sp.antlers) {
            for (int side = -1; side <= 1; side += 2) {
                vec3 b(side * hs * 0.3f, hs * 0.4f, -hs * 0.1f);
                vec3 t = b + vec3(side * hs * 0.5f, hs * 1.2f, -hs * 0.3f);
                seg(s, MESH_CYL, hr, b, t, hs * 0.1f, c3);
                seg(s, MESH_CYL, hr, b + (t - b) * 0.5f, b + (t - b) * 0.5f + vec3(side * hs * 0.4f, hs * 0.3f, hs * 0.2f), hs * 0.08f, c3);
            }
        }
        if (sp.tusks) {
            for (int side = -1; side <= 1; side += 2)
                seg(s, MESH_CONE, hr, vec3(side * hs * 0.3f, -hs * 0.2f, hs * 0.8f), vec3(side * hs * 0.45f, hs * 0.3f, hs * 1.7f), hs * 0.2f, c3);
        }
        if (sp.mane) {  // ears
            for (int side = -1; side <= 1; side += 2)
                seg(s, MESH_CONE, hr, vec3(side * hs * 0.25f, hs * 0.5f, -hs * 0.2f), vec3(side * hs * 0.3f, hs * 0.95f, -hs * 0.3f), hs * 0.2f, c1);
        }
    };

    if (sp.plan == BP_QUAD) {
        float L = sp.bodyL, W = sp.bodyW, H = sp.bodyH, legL = sp.legL, legT = sp.legT;
        float bodyY = legL + H * 0.45f + breathe * H;
        ell(s, root, vec3(0, bodyY, 0), vec3(W, H, L), c1);
        if (!lod) ell(s, root, vec3(0, bodyY - H * 0.12f, L * 0.18f), vec3(W * 0.82f, H * 0.78f, L * 0.55f), mixc(c1, c2, 0.4f));
        if (sp.shell) {
            ell(s, root, vec3(0, bodyY + H * 0.28f, 0), vec3(W * 1.35f, H * 1.25f, L * 1.15f), c2);
            if (!lod)
                for (int k = 0; k < 5; k++) {
                    float a = k * 1.3f;
                    ell(s, root, vec3(std::sin(a) * W * 0.3f, bodyY + H * 0.85f, std::cos(a) * L * 0.3f), vec3(W * 0.35f, H * 0.3f, W * 0.35f), c3);
                }
            ell(s, root, vec3(W * 0.1f, bodyY + H * 1.0f, -L * 0.1f), vec3(0.3f, 0.3f, 0.3f), vec3(0.9f, 0.2f, 0.15f), 0.2f);
        }
        // Legs.
        for (int k = 0; k < 4; k++) {
            float side = (k % 2) ? 1.0f : -1.0f;
            float front = (k < 2) ? 1.0f : -1.0f;
            float off = ((k == 0 || k == 3) ? 0.0f : PI);
            float swing = std::sin(ph + off) * 0.6f * moveAmt;
            if (c.flying) swing = front > 0 ? -0.8f : 0.9f;
            vec3 hip(side * W * 0.32f, bodyY - H * 0.1f, front * L * 0.3f);
            vec3 knee = hip + vec3(0, -legL * 0.5f * std::cos(swing), legL * 0.5f * std::sin(swing));
            float s2 = swing * 0.4f + (c.flying ? 0.6f * front : 0.0f);
            vec3 foot = knee + vec3(0, -legL * 0.52f * std::cos(s2), legL * 0.52f * std::sin(s2));
            seg(s, MESH_CYL, root, hip + vec3(0, H * 0.1f, 0), knee, legT * 1.35f, c1);
            seg(s, MESH_CYL, root, knee, foot, legT, c1 * 0.88f);
            if (!lod) ell(s, root, foot + vec3(0, legT * 0.3f, legT * 0.3f), vec3(legT * 1.5f, legT * 0.8f, legT * 2.0f), vec3(0.18f, 0.15f, 0.13f));
        }
        // Necks and heads.
        int heads = sp.heads;
        for (int hI = 0; hI < heads; hI++) {
            float spread = heads > 1 ? (hI - (heads - 1) * 0.5f) / ((heads - 1) * 0.5f) : 0.0f;
            float yawOff = spread * (heads > 3 ? 0.6f : 0.4f) + (heads > 1 ? std::sin(ph * 0.4f + hI * 1.7f) * 0.15f : 0.0f);
            float na = sp.neckAngle + std::sin(ph * 0.5f + hI) * 0.05f - c.attackAnim * 0.5f;
            if (asleep) na = -0.1f;
            vec3 base(spread * W * 0.35f, bodyY + H * 0.25f, L * 0.42f);
            vec3 dir(std::sin(yawOff) * std::cos(na), std::sin(na), std::cos(yawOff) * std::cos(na));
            float nl = sp.neckL + 0.001f;
            vec3 neckEnd = base + dir * nl;
            if (heads > 3) {  // hydra necks bend
                vec3 mid = base + dir * nl * 0.5f + vec3(std::sin(ph * 0.8f + hI) * 0.2f, 0.2f, 0);
                seg(s, MESH_CYL, root, base, mid, sp.headS * 0.55f, c1);
                seg(s, MESH_CYL, root, mid, neckEnd, sp.headS * 0.48f, c1);
            } else {
                seg(s, MESH_CYL, root, base, neckEnd, sp.headS * 0.75f, c1);
            }
            if (sp.mane && !lod)
                for (int m = 0; m < 4; m++) {
                    vec3 p = base + dir * nl * (m / 3.5f) + vec3(0, sp.headS * 0.25f, -sp.headS * 0.1f);
                    seg(s, MESH_CONE, root, p, p + vec3(0, sp.headS * 0.55f, -sp.headS * 0.45f), sp.headS * 0.35f, c2);
                }
            vec3 hc = neckEnd + vec3(std::sin(yawOff), 0, std::cos(yawOff)) * sp.headS * 0.35f;
            drawHead(hc, sp.headS, yawOff, sp.beak);
        }
        // Back spines / fins.
        if (sp.fins && !lod)
            for (int k = 0; k < 5; k++) {
                float z = L * (0.35f - k * 0.18f);
                vec3 b(0, bodyY + H * 0.42f, z);
                seg(s, MESH_CONE, root, b, b + vec3(0, H * 0.45f, -H * 0.2f), H * 0.28f, c.species == S_SALAMANDER || c.species == S_KELPIE ? c3 : c2,
                    c.species == S_SALAMANDER ? glowK : (c.species == S_KELPIE ? 0.4f : 0.0f));
            }
        // Tails.
        int tails = sp.tails;
        for (int t = 0; t < tails; t++) {
            float fan = tails > 1 ? (t - (tails - 1) * 0.5f) * 0.45f : 0.0f;
            vec3 prev(0, bodyY + (sp.stinger ? H * 0.2f : 0.0f), -L * 0.46f);
            int N = lod ? 3 : 6;
            float segLen = sp.tailL / N;
            for (int k = 0; k < N; k++) {
                float sway = std::sin(ph * 0.7f - k * 0.5f + t) * 0.25f * (1.0f + moveAmt);
                vec3 dir;
                if (sp.stinger) dir = normalize(vec3(sway * 0.5f, 0.35f + k * 0.35f, -1.0f + k * 0.38f));
                else if (tails > 1) dir = normalize(vec3(std::sin(fan) + sway * 0.4f, 0.55f - k * 0.05f, -std::cos(fan)));
                else dir = normalize(vec3(sway, -0.25f + (c.flying ? 0.2f : 0.0f), -1.0f));
                vec3 next = prev + dir * segLen;
                float th = W * 0.32f * (1.0f - (float)k / N * 0.75f);
                if (tails > 1) th = W * 0.45f * (1.0f - std::fabs(k - N * 0.4f) / N);
                vec3 col = (tails > 1 && k >= N - 2) ? c2 : c1;
                seg(s, tails > 1 ? MESH_SPHERE : MESH_CYL, root, prev, next, tails > 1 ? th * 1.6f : th, col,
                    (tails > 1 && k == N - 1) ? glowK * 0.5f : 0.0f);
                prev = next;
            }
            if (sp.stinger) seg(s, MESH_CONE, root, prev, prev + vec3(0, -sp.headS * 0.3f, sp.headS * 0.9f), sp.headS * 0.35f, c3, glowK);
            else if (c.species == S_CHIMERA) drawHead(prev + vec3(0, 0.1f, -0.2f), sp.headS * 0.6f, PI, false);
            else if (tails > 1) ell(s, root, prev, vec3(W * 0.35f, W * 0.35f, W * 0.35f), c3, glowK);
            else if (sp.wings || sp.fins) seg(s, MESH_CONE, root, prev, prev + normalize(prev - vec3(0, bodyY, 0)) * sp.headS * 0.8f, sp.headS * 0.6f, c2);
            else if (sp.mane) ell(s, root, prev, vec3(W * 0.25f, W * 0.25f, sp.tailL * 0.4f), c2);
        }
        if (sp.wings) {
            bool feathered = sp.beak || c.species == S_PEGASUS;
            drawWings(vec3(W * 0.38f, bodyY + H * 0.35f, L * 0.18f), L * (feathered ? 1.0f : 1.15f), L * 0.5f, feathered);
        }
        if (c.hasSaddle) {
            box(s, root, vec3(0, bodyY + H * 0.5f, L * 0.05f), vec3(W * 0.75f, 0.14f / sz + 0.05f, L * 0.35f), vec3(0.35f, 0.2f, 0.1f));
        }
    } else if (sp.plan == BP_BIRD) {
        float L = sp.bodyL, W = sp.bodyW, H = sp.bodyH, legL = sp.legL;
        float bodyY = legL + H * 0.5f + breathe;
        box(s, root, vec3(0, bodyY, 0), vec3(W, H, L), c1, glowK * 0.25f, 0, -0.25f);
        ell(s, root, vec3(0, bodyY, 0), vec3(W * 1.1f, H * 1.05f, L * 1.05f), c1, glowK * 0.3f);
        for (int side = -1; side <= 1; side += 2) {
            float swing = std::sin(ph + (side > 0 ? PI : 0)) * 0.5f * moveAmt;
            if (c.flying) swing = 0.9f;
            vec3 hip(side * W * 0.25f, bodyY - H * 0.3f, 0);
            vec3 foot = hip + vec3(0, -legL * std::cos(swing), -legL * std::sin(swing));
            seg(s, MESH_CYL, root, hip, foot, sp.legT, c3);
            if (!lod)
                for (int k = -1; k <= 1; k++)
                    seg(s, MESH_CONE, root, foot, foot + vec3(k * 0.1f, -0.02f, 0.2f), 0.06f, vec3(0.15f, 0.13f, 0.12f));
        }
        float na = sp.neckAngle - c.attackAnim * 0.4f;
        if (asleep) na = -0.2f;
        vec3 base(0, bodyY + H * 0.3f, L * 0.4f);
        vec3 dir(0, std::sin(na), std::cos(na));
        vec3 ne = base + dir * sp.neckL;
        seg(s, MESH_CYL, root, base, ne, sp.headS * 0.7f, c1);
        drawHead(ne + vec3(0, 0, sp.headS * 0.3f), sp.headS, 0, true);
        if (!lod && (c.species == S_PHOENIX || c.species == S_THUNDERBIRD))
            for (int k = 0; k < 3; k++)
                seg(s, MESH_CONE, root, ne + vec3(0, sp.headS * 0.5f, -k * sp.headS * 0.3f),
                    ne + vec3(0, sp.headS * (1.3f - k * 0.2f), -sp.headS * (0.6f + k * 0.3f)), sp.headS * 0.25f, c3, glowK);
        for (int k = -1; k <= 1; k++) {
            vec3 tb(0, bodyY - H * 0.1f, -L * 0.45f);
            float sway = std::sin(ph * 0.5f) * 0.1f;
            seg(s, MESH_CONE, root, tb, tb + vec3(k * 0.35f + sway, -0.15f, -sp.tailL), 0.3f, k == 0 ? c3 : c2, glowK * 0.8f);
        }
        drawWings(vec3(W * 0.4f, bodyY + H * 0.3f, L * 0.1f), L * 1.6f, L * 0.55f, true);
    } else {  // serpent
        float L = sp.bodyL, W = sp.bodyW, H = sp.bodyH;
        int N = lod ? 8 : 16;
        float segLen = L / N;
        vec3 headPos;
        for (int k = 0; k < N; k++) {
            float t = (float)k / N;
            float wave = std::sin(ph * 1.2f - k * 0.55f) * W * 0.9f * std::max(0.3f, moveAmt);
            float y = H * 0.45f * (1.0f - t * 0.5f);
            if (k < 3 && !asleep) y += (3 - k) * H * 0.35f;
            vec3 p(wave, y, L * 0.4f - k * segLen);
            if (k == 0) headPos = p;
            float r = W * (1.0f - t * 0.75f);
            ell(s, root, p, vec3(r * 1.15f, r * 0.95f, segLen * 1.7f), (k % 2) ? c1 : c1 * 0.85f);
            if (!lod && k % 2 == 0) ell(s, root, p + vec3(0, -r * 0.25f, 0), vec3(r * 1.0f, r * 0.6f, segLen * 1.4f), c2);
        }
        vec3 hc = headPos + vec3(0, H * 0.15f, W * 0.8f);
        drawHead(hc, W * 1.0f, 0, false);
        if (!lod)
            for (int k = 0; k < sp.horns; k++) {
                float a = (k - (sp.horns - 1) * 0.5f) * 0.35f;
                vec3 b = hc + vec3(std::sin(a) * W * 0.4f, W * 0.35f, -W * 0.2f);
                seg(s, MESH_CONE, root, b, b + vec3(std::sin(a) * W * 0.3f, W * 0.8f, -W * 0.4f), W * 0.18f, c3, glowK * 0.6f);
            }
    }

    // Taming progress halo over knocked-out creatures.
    if (c.state == CS_UNCONSCIOUS && !c.tamed) {
        float t = totalTime_ * 2.0f;
        for (int k = 0; k < 3; k++) {
            float a = t + k * TAU / 3;
            vec3 p = c.pos + vec3(std::sin(a) * rad * 0.6f, h + 0.6f + std::sin(t * 2 + k) * 0.15f, std::cos(a) * rad * 0.6f);
            push(s, MESH_SPHERE, translate(p) * scale(vec3(0.18f, 0.18f, 0.18f)), vec3(0.95f, 0.9f, 0.5f), 1.2f);
        }
    }
}

// ---------------------------------------------------------------------------
void Game::drawHumanoid(FrameScene& s, vec3 pos, float yaw, float sc, vec3 skin, vec3 cloth, vec3 mask, float walk,
                        float swing, Item held, bool infected, int cls, float glow, float sit) {
    if (sit > 0.5f) pos.y -= 0.85f * sc;  // seated: pelvis rests on the saddle
    if (!sphereVisible(pos + vec3(0, 1.0f * sc, 0), 1.5f * sc)) return;
    if (length(pos - camPos_) > 320.0f) return;
    float hunch = infected && cls == IC_STALKER ? 0.35f : 0.0f;
    mat4 root = translate(pos) * rotateY(yaw) * scale(vec3(sc, sc, sc));
    mat4 torso = root * translate(vec3(0, 0.95f, 0)) * rotateX(hunch) * translate(vec3(0, -0.95f, 0));
    float legSwing = std::sin(walk) * 0.6f;
    // Legs.
    for (int side = -1; side <= 1; side += 2) {
        vec3 hip(side * 0.13f, 0.95f, 0);
        vec3 knee, foot;
        if (sit > 0.5f) {
            knee = hip + vec3(side * 0.25f, -0.05f, 0.4f);
            foot = knee + vec3(side * 0.05f, -0.5f, 0.05f);
        } else {
            float a = legSwing * side;
            knee = hip + vec3(0, -0.45f * std::cos(a), 0.45f * std::sin(a));
            float b = a * 0.5f - std::max(0.0f, -a) * 0.6f;
            foot = knee + vec3(0, -0.47f * std::cos(b), 0.47f * std::sin(b));
        }
        seg(s, MESH_CYL, root, hip, knee, 0.17f, cloth * 0.8f);
        seg(s, MESH_CYL, root, knee, foot, 0.14f, infected ? skin * 0.8f : cloth * 0.6f);
        box(s, root, foot + vec3(0, 0.04f, 0.06f), vec3(0.14f, 0.08f, 0.26f), infected ? skin * 0.6f : vec3(0.25f, 0.17f, 0.1f));
    }
    // Body.
    box(s, torso, vec3(0, 0.98f, 0), vec3(0.44f, 0.22f, 0.26f), cloth * 0.75f);
    box(s, torso, vec3(0, 1.32f, 0), vec3(0.5f, 0.6f, 0.28f), infected ? skin : cloth);
    if (infected) {
        box(s, torso, vec3(0, 1.08f, 0), vec3(0.52f, 0.3f, 0.3f), cloth);  // loincloth/wrap
        // Glowing veins.
        seg(s, MESH_CUBE, torso, vec3(0.08f, 1.15f, 0.145f), vec3(-0.1f, 1.5f, 0.145f), 0.025f, vec3(0.8f, 0.05f, 0.1f), 0.8f);
    }
    if (infected && (cls == IC_BRUTE || cls == IC_CHIEFTAIN)) {
        for (int side = -1; side <= 1; side += 2) {
            ell(s, torso, vec3(side * 0.32f, 1.62f, 0), vec3(0.3f, 0.2f, 0.3f), mask * 0.9f);
            seg(s, MESH_CONE, torso, vec3(side * 0.35f, 1.68f, 0), vec3(side * 0.55f, 1.95f, -0.05f), 0.1f, vec3(0.88f, 0.85f, 0.75f));
        }
    }
    if (infected && cls == IC_CHIEFTAIN)  // cape
        box(s, torso, vec3(0, 1.1f, -0.2f), vec3(0.6f, 1.0f, 0.05f), cloth * 0.9f, 0, 0, 0.1f);
    // Head.
    vec3 headC(0, 1.84f, 0);
    ell(s, torso, headC, vec3(0.3f, 0.34f, 0.3f), skin);
    if (infected) {
        box(s, torso, headC + vec3(0, 0.02f, 0.13f), vec3(0.3f, 0.36f, 0.08f), mask);
        ell(s, torso, headC + vec3(0.07f, 0.05f, 0.18f), vec3(0.07f, 0.05f, 0.04f), vec3(1.0f, 0.12f, 0.05f), 2.5f + glow);
        ell(s, torso, headC + vec3(-0.07f, 0.05f, 0.18f), vec3(0.07f, 0.05f, 0.04f), vec3(1.0f, 0.12f, 0.05f), 2.5f + glow);
        if (cls == IC_CHIEFTAIN)
            for (int side = -1; side <= 1; side += 2) {
                vec3 b = headC + vec3(side * 0.12f, 0.15f, 0);
                seg(s, MESH_CYL, torso, b, b + vec3(side * 0.35f, 0.45f, -0.1f), 0.05f, vec3(0.85f, 0.8f, 0.65f));
                seg(s, MESH_CYL, torso, b + vec3(side * 0.18f, 0.22f, -0.05f), b + vec3(side * 0.2f, 0.55f, 0.1f), 0.04f, vec3(0.85f, 0.8f, 0.65f));
            }
        if (cls == IC_SHAMAN)
            for (int k = 0; k < 3; k++)
                seg(s, MESH_CONE, torso, headC + vec3((k - 1) * 0.1f, 0.15f, -0.05f), headC + vec3((k - 1) * 0.2f, 0.6f, -0.2f), 0.08f,
                    k == 1 ? vec3(0.8f, 0.1f, 0.15f) : vec3(0.1f, 0.1f, 0.1f));
    } else {
        ell(s, torso, headC + vec3(0, 0.1f, -0.03f), vec3(0.32f, 0.22f, 0.32f), mask);  // hair
        ell(s, torso, headC + vec3(0.07f, 0.03f, 0.14f), vec3(0.05f, 0.05f, 0.03f), vec3(0.1f, 0.1f, 0.12f));
        ell(s, torso, headC + vec3(-0.07f, 0.03f, 0.14f), vec3(0.05f, 0.05f, 0.03f), vec3(0.1f, 0.1f, 0.12f));
    }
    // Arms.
    vec3 handR;
    for (int side = -1; side <= 1; side += 2) {
        vec3 sh(side * 0.33f, 1.58f, 0);
        float a = -std::sin(walk) * 0.5f * side;
        vec3 elbow, hand;
        if (side == -1 && swing > 0) {  // right arm (x negative is the character's right when facing +z)
            float lift = std::sin(std::min(swing, 1.0f) * PI) * 2.2f;
            elbow = sh + vec3(0, -0.3f * std::cos(lift), 0.3f * std::sin(lift));
            hand = elbow + vec3(0, -0.3f * std::cos(lift * 0.8f), 0.32f * std::sin(lift * 0.8f));
        } else if (sit > 0.5f) {
            elbow = sh + vec3(0, -0.28f, 0.15f);
            hand = elbow + vec3(-side * 0.1f, 0.0f, 0.3f);
        } else if (infected && swing <= 0) {  // arms forward, menacing
            elbow = sh + vec3(side * 0.05f, -0.25f, 0.18f + a * 0.2f);
            hand = elbow + vec3(0, -0.15f, 0.28f);
        } else {
            elbow = sh + vec3(side * 0.04f, -0.3f * std::cos(a), 0.3f * std::sin(a));
            hand = elbow + vec3(0, -0.3f, 0.08f);
        }
        seg(s, MESH_CYL, torso, sh, elbow, 0.13f, infected ? skin : cloth);
        seg(s, MESH_CYL, torso, elbow, hand, 0.11f, skin);
        ell(s, torso, hand, vec3(0.11f, 0.11f, 0.11f), skin);
        if (side == -1) handR = hand;
    }
    // Held item.
    vec3 wood(0.5f, 0.35f, 0.2f), stone(0.55f, 0.55f, 0.55f);
    vec3 up = handR + vec3(0, 0.1f, 0.35f) + (swing > 0 ? vec3(0, 0.25f, 0.2f) : vec3(0, 0.35f, 0));
    if (infected) {
        if (cls == IC_SHAMAN) {
            seg(s, MESH_CYL, torso, handR - vec3(0, 0.6f, 0), handR + vec3(0, 0.9f, 0.1f), 0.06f, vec3(0.2f, 0.12f, 0.1f));
            ell(s, torso, handR + vec3(0, 1.0f, 0.1f), vec3(0.2f, 0.2f, 0.2f), vec3(1.0f, 0.1f, 0.3f), 2.0f);
        } else if (cls == IC_STALKER) {
            seg(s, MESH_CONE, torso, handR, handR + vec3(0, -0.05f, 0.45f), 0.08f, vec3(0.8f, 0.78f, 0.7f));
        } else {
            seg(s, MESH_CYL, torso, handR - vec3(0, 0.1f, 0.05f), up + vec3(0, 0.2f, 0), 0.08f, vec3(0.3f, 0.2f, 0.12f));
            ell(s, torso, up + vec3(0, 0.25f, 0), vec3(0.22f, 0.3f, 0.22f) * (cls == IC_BRUTE ? 1.5f : 1.0f), vec3(0.85f, 0.82f, 0.72f));
        }
        return;
    }
    switch (held) {
        case I_STONEPICK:
            seg(s, MESH_CYL, torso, handR - vec3(0, 0.1f, 0.05f), up, 0.06f, wood);
            seg(s, MESH_CONE, torso, up + vec3(0.02f, 0, 0), up + vec3(0.3f, -0.1f, 0), 0.1f, stone);
            seg(s, MESH_CONE, torso, up - vec3(0.02f, 0, 0), up + vec3(-0.3f, -0.1f, 0), 0.1f, stone);
            break;
        case I_STONEHATCHET:
            seg(s, MESH_CYL, torso, handR - vec3(0, 0.1f, 0.05f), up, 0.06f, wood);
            box(s, torso, up + vec3(0.1f, -0.05f, 0), vec3(0.22f, 0.18f, 0.06f), stone);
            break;
        case I_SPEAR:
            seg(s, MESH_CYL, torso, handR - vec3(0, 0.6f, 0.2f), handR + vec3(0, 0.9f, 0.5f), 0.05f, wood);
            seg(s, MESH_CONE, torso, handR + vec3(0, 0.9f, 0.5f), handR + vec3(0, 1.2f, 0.62f), 0.09f, stone);
            break;
        case I_CLUB:
            seg(s, MESH_CYL, torso, handR - vec3(0, 0.1f, 0.05f), up, 0.07f, wood);
            ell(s, torso, up, vec3(0.2f, 0.28f, 0.2f), wood * 0.85f);
            break;
        case I_BOW:
            seg(s, MESH_CYL, torso, handR + vec3(0, -0.55f, 0.15f), handR + vec3(0, 0.55f, 0.15f), 0.05f, wood);
            seg(s, MESH_CYL, torso, handR + vec3(0, -0.5f, 0.05f), handR + vec3(0, 0.5f, 0.05f), 0.015f, vec3(0.9f, 0.9f, 0.85f));
            break;
        case I_TORCH:
            seg(s, MESH_CYL, torso, handR - vec3(0, 0.1f, 0), handR + vec3(0, 0.55f, 0.1f), 0.06f, wood);
            ell(s, torso, handR + vec3(0, 0.65f, 0.12f), vec3(0.2f, 0.3f, 0.2f), vec3(1.0f, 0.55f, 0.15f), 2.0f);
            break;
        case I_BLADE:
            seg(s, MESH_CYL, torso, handR - vec3(0, 0.05f, 0), handR + vec3(0, 0.15f, 0.05f), 0.06f, wood);
            seg(s, MESH_CUBE, torso, handR + vec3(0, 0.15f, 0.05f), up + vec3(0, 0.45f, 0.1f), 0.08f, vec3(0.8f, 0.82f, 0.9f), 0.1f);
            break;
        default: break;
    }
}

void Game::drawInfected(FrameScene& s, const Infected& e) {
    const InfectedInfo& info = INFECTED[e.cls];
    vec3 skin = info.skin, cloth = info.cloth;
    if (e.hurt > 0) skin = mixc(skin, vec3(1, 0.4f, 0.4f), e.hurt * 0.6f);
    if (e.burn > 0) skin = mixc(skin, vec3(0.1f, 0.08f, 0.06f), std::min(1.0f, e.burn * 0.25f));
    if (e.state == IS_DEAD) {
        if (!sphereVisible(e.pos, 2.0f * info.scale)) return;
        mat4 root = translate(e.pos + vec3(0, 0.2f, 0)) * rotateY(e.yaw) * rotateX(-1.45f) * translate(vec3(0, -0.2f, 0));
        box(s, root * scale(vec3(info.scale, info.scale, info.scale)), vec3(0, 1.1f, 0), vec3(0.5f, 1.4f, 0.3f), skin * 0.5f);
        ell(s, root * scale(vec3(info.scale, info.scale, info.scale)), vec3(0, 1.9f, 0.05f), vec3(0.3f, 0.36f, 0.3f), info.mask * 0.8f);
        return;
    }
    drawHumanoid(s, e.pos, e.yaw, info.scale, skin, cloth, info.mask, e.anim, e.swing, I_NONE, true, e.cls,
                 e.state == IS_CHASE ? 1.5f : 0.0f, 0.0f);
}

void Game::drawStructures(FrameScene& s) {
    float flick = std::sin(totalTime_ * 11.0f) * 0.1f;
    for (const auto& st : structures_) {
        if (!st.alive || !sphereVisible(st.pos + vec3(0, 1, 0), 3)) continue;
        mat4 root = translate(st.pos) * rotateY(st.yaw);
        switch (st.type) {
            case I_CAMPFIRE:
                for (int k = 0; k < 8; k++) {
                    float a = k * TAU / 8;
                    ell(s, root, vec3(std::sin(a) * 0.75f, 0.1f, std::cos(a) * 0.75f), vec3(0.35f, 0.25f, 0.35f), vec3(0.45f, 0.43f, 0.4f));
                }
                seg(s, MESH_CYL, root, vec3(-0.5f, 0.12f, -0.2f), vec3(0.5f, 0.2f, 0.2f), 0.16f, vec3(0.35f, 0.22f, 0.12f));
                seg(s, MESH_CYL, root, vec3(-0.3f, 0.2f, 0.45f), vec3(0.3f, 0.12f, -0.45f), 0.16f, vec3(0.35f, 0.22f, 0.12f));
                push(s, MESH_CONE, root * translate(vec3(0, 0.55f, 0)) * scale(vec3(0.6f, 0.9f + flick, 0.6f)), vec3(1.0f, 0.45f, 0.1f), 2.5f);
                push(s, MESH_CONE, root * translate(vec3(0.1f, 0.6f, 0.05f)) * scale(vec3(0.35f, 0.7f - flick, 0.35f)), vec3(1.0f, 0.8f, 0.3f), 3.0f);
                break;
            case I_CAULDRON:
                for (int k = 0; k < 3; k++) {
                    float a = k * TAU / 3;
                    seg(s, MESH_CYL, root, vec3(std::sin(a) * 0.6f, 0, std::cos(a) * 0.6f), vec3(std::sin(a) * 0.4f, 0.6f, std::cos(a) * 0.4f), 0.1f,
                        vec3(0.2f, 0.2f, 0.22f));
                }
                ell(s, root, vec3(0, 0.75f, 0), vec3(1.3f, 1.0f, 1.3f), vec3(0.18f, 0.18f, 0.2f));
                ell(s, root, vec3(0, 1.18f, 0), vec3(1.0f, 0.12f, 1.0f), vec3(0.3f, 0.95f, 0.5f), 1.2f + flick * 3);
                push(s, MESH_CONE, root * translate(vec3(0, 0.15f, 0)) * scale(vec3(0.4f, 0.4f, 0.4f)), vec3(1.0f, 0.5f, 0.1f), 2.0f);
                break;
            case I_WOODWALL:
            case I_SPIKEWALL: {
                vec3 wood(0.5f, 0.35f, 0.2f);
                float dmg = saturate(st.hp / 900.0f);
                wood = mixc(vec3(0.25f, 0.18f, 0.12f), wood, dmg);
                for (int k = 0; k < 5; k++)
                    seg(s, MESH_CYL, root, vec3(-1.6f + k * 0.8f, -0.2f, 0), vec3(-1.6f + k * 0.8f, 3.0f + (k % 2) * 0.2f, 0), 0.42f, wood * (k % 2 ? 1.0f : 0.9f));
                box(s, root, vec3(0, 2.2f, 0.2f), vec3(4.0f, 0.25f, 0.12f), wood * 0.8f);
                box(s, root, vec3(0, 0.8f, 0.2f), vec3(4.0f, 0.25f, 0.12f), wood * 0.8f);
                for (int k = 0; k < 5; k++)
                    push(s, MESH_CONE, root * translate(vec3(-1.6f + k * 0.8f, 3.35f, 0)) * scale(vec3(0.42f, 0.5f, 0.42f)), wood * 0.9f);
                if (st.type == I_SPIKEWALL)
                    for (int k = 0; k < 8; k++) {
                        vec3 b(-1.75f + k * 0.5f, 0.6f + (k % 3) * 0.6f, 0.2f);
                        seg(s, MESH_CONE, root, b, b + vec3(0, 0.3f, 1.2f), 0.14f, vec3(0.65f, 0.55f, 0.4f));
                        seg(s, MESH_CONE, root, b - vec3(0, 0, 0.4f), b + vec3(0, 0.3f, -1.4f), 0.14f, vec3(0.65f, 0.55f, 0.4f));
                    }
                break;
            }
            default: break;
        }
    }
    // Placement preview.
    if (mode_ == GM_PLAY && panel_ == PANEL_NONE && !player_.dead && player_.riding < 0) {
        Item it = selectedItem();
        if (it != I_NONE && ITEMS[it].kind == IK_PLACEABLE) {
            vec3 pos = player_.pos + dirFromYaw(player_.yaw) * 3.5f;
            pos.y = terrain_.heightAt(pos.x, pos.z);
            bool ok = pos.y >= 0.2f;
            vec3 col = ok ? vec3(0.3f, 1.0f, 0.4f) : vec3(1.0f, 0.3f, 0.3f);
            mat4 root = translate(pos) * rotateY(player_.yaw + player_.placeYaw);
            if (it == I_WOODWALL || it == I_SPIKEWALL) box(s, root, vec3(0, 1.5f, 0), vec3(4.0f, 3.0f, 0.3f), col, 0.6f);
            else ell(s, root, vec3(0, 0.4f, 0), vec3(1.6f, 0.8f, 1.6f), col, 0.6f);
        }
    }
}

void Game::drawEggs(FrameScene& s) {
    for (const auto& n : nests_) {
        if (!sphereVisible(n.pos, 3)) continue;
        mat4 root = translate(n.pos);
        for (int k = 0; k < 10; k++) {
            float a = k * TAU / 10;
            vec3 p(std::sin(a) * 1.1f, 0.2f, std::cos(a) * 1.1f);
            seg(s, MESH_CYL, root, p - vec3(std::cos(a), 0, -std::sin(a)) * 0.5f, p + vec3(std::cos(a), 0.1f, -std::sin(a)) * 0.5f, 0.12f,
                vec3(0.35f, 0.25f, 0.15f));
        }
    }
    for (const auto& e : eggs_) {
        if (!e.alive || !sphereVisible(e.pos, 2)) continue;
        const Species& sp = SPECIES[e.species];
        float sz = 0.45f + sp.size * 0.12f;
        vec3 base = e.pos + vec3(0, sz * 0.6f, 0);
        float wobble = e.state == EGG_INCUBATING && e.progress > 0.7f ? std::sin(totalTime_ * 12) * 0.12f * (e.progress - 0.7f) * 3 : 0.0f;
        mat4 root = translate(base) * rotateZ(wobble);
        float glow = e.state == EGG_INCUBATING ? 0.2f + e.progress * 0.8f : 0.1f;
        ell(s, root, vec3(0, 0, 0), vec3(sz, sz * 1.3f, sz), sp.c1, glow * 0.5f);
        for (int k = 0; k < 5; k++) {
            float a = k * 1.9f;
            ell(s, root, vec3(std::sin(a) * sz * 0.45f, (k - 2) * sz * 0.2f, std::cos(a) * sz * 0.45f), vec3(sz * 0.2f, sz * 0.2f, sz * 0.2f), sp.c3,
                glow);
        }
        if (e.state == EGG_NEST && e.nest >= 0)
            push(s, MESH_CONE, translate(e.pos + vec3(0, 3.0f + std::sin(totalTime_ * 2) * 0.2f, 0)) * rotateX(PI) * scale(vec3(0.4f, 0.6f, 0.4f)),
                 vec3(1.0f, 0.85f, 0.3f), 1.5f);
    }
}

void Game::drawEffects(FrameScene& s) {
    for (const auto& p : particles_) {
        float t = p.life / p.maxLife;
        float sz = p.size * (0.4f + 0.6f * t);
        if (!sphereVisible(p.pos, sz)) continue;
        push(s, MESH_CUBE, translate(p.pos) * rotateY(p.life * 5) * scale(vec3(sz, sz, sz)), p.color, p.emissive * t);
    }
    for (const auto& p : projectiles_) {
        if (!sphereVisible(p.pos, 1)) continue;
        switch (p.kind) {
            case PJ_ARROW:
            case PJ_DART: {
                vec3 d = normalize(p.vel);
                seg(s, MESH_CYL, mat4::identity(), p.pos - d * 0.4f, p.pos + d * 0.4f, 0.04f, vec3(0.6f, 0.45f, 0.3f));
                seg(s, MESH_CONE, mat4::identity(), p.pos + d * 0.35f, p.pos + d * 0.55f, 0.08f, p.color, p.kind == PJ_DART ? 0.8f : 0.0f);
                break;
            }
            case PJ_BOLT:
                push(s, MESH_SPHERE, translate(p.pos) * scale(vec3(0.35f, 0.35f, 0.35f)), p.color, 2.5f);
                break;
            case PJ_BREATH:
                push(s, MESH_SPHERE, translate(p.pos) * scale(vec3(0.7f, 0.7f, 0.7f) * (0.5f + (0.85f - p.life))), p.color, 2.5f);
                break;
        }
    }
}

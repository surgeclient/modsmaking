// Scene building: skinned + animated creatures and people, LOD props, structures,
// particles and HDR lighting setup.
#include "game.h"
#include <algorithm>

namespace {

inline void push(FrameScene& s, int m, const mat4& M, vec3 col, float emissive = 0, float gloss = 0.2f, float pattern = 0) {
    s.inst[m].push_back({M, vec4(col, emissive), vec4(gloss, pattern, 0, 0)});
}
inline void ell(FrameScene& s, const mat4& root, vec3 c, vec3 size, vec3 col, float e = 0, float gloss = 0.2f) {
    push(s, MESH_SPHERE, root * translate(c) * scale(size), col, e, gloss);
}
inline void box(FrameScene& s, const mat4& root, vec3 c, vec3 size, vec3 col, float e = 0, float yaw = 0, float pitch = 0, float roll = 0) {
    push(s, MESH_CUBE, root * translate(c) * rotateY(yaw) * rotateX(pitch) * rotateZ(roll) * scale(size), col, e);
}
inline void seg(FrameScene& s, int m, const mat4& root, vec3 a, vec3 b, float thick, vec3 col, float e = 0, float gloss = 0.2f) {
    vec3 d = b - a;
    float len = length(d);
    if (len < 1e-4f) return;
    vec3 y = d / len;
    vec3 ref = std::fabs(y.y) < 0.95f ? vec3(0, 1, 0) : vec3(1, 0, 0);
    vec3 x = normalize(cross(ref, y));
    vec3 z = cross(x, y);
    push(s, m, root * basis(x * thick, y * len, z * thick, (a + b) * 0.5f), col, e, gloss);
}
inline void glow(FrameScene& s, vec3 p, float size, vec3 col, float alpha) { s.particlesAdd.push_back({vec4(p, size), vec4(col, alpha)}); }
inline vec3 mixc(vec3 a, vec3 b, float t) { return lerp(a, b, t); }
inline float hashf(uint32_t x) {
    x ^= x >> 16;
    x *= 0x7feb352d;
    x ^= x >> 15;
    x *= 0x846ca68b;
    x ^= x >> 16;
    return (x & 0xFFFFFF) / 16777216.0f;
}

mat4 shadowMatrix(vec3 center, vec3 keyDir, float R) {
    vec3 up = std::fabs(keyDir.y) > 0.95f ? vec3(0, 0, 1) : vec3(0, 1, 0);
    mat4 lv = lookAt(center + keyDir * 500.0f, center, up);
    vec3 lc = lv.transformPoint(center);
    float texel = 2 * R / 2048.0f;
    vec3 snap(std::round(lc.x / texel) * texel - lc.x, std::round(lc.y / texel) * texel - lc.y, 0);
    lv = translate(snap) * lv;
    return ortho(-R, R, -R, R, 1.0f, 1200.0f) * lv;
}

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

    vec3 sunColor = lerp(vec3(1.0f, 0.42f, 0.18f) * 2.4f, vec3(1.0f, 0.93f, 0.84f) * 3.4f, smoothstep(0.0f, 0.35f, sun.y)) * dayF;
    vec3 keyDir = sun;
    float shadowStrength = dayF;
    if (sun.y < 0.02f) {
        keyDir = moon;
        sunColor = lerp(vec3(0.3f, 0.38f, 0.7f), vec3(0.55f, 0.25f, 0.3f), hollow * 0.35f) * 0.42f * night;
        shadowStrength = 0.7f * night;
    }
    vec3 zenithDay(0.16f, 0.38f, 0.85f), zenithNight(0.004f, 0.008f, 0.025f), zenithSunset(0.28f, 0.25f, 0.48f);
    vec3 horizDay(0.6f, 0.75f, 0.93f), horizNight(0.014f, 0.018f, 0.04f), horizSunset(1.0f, 0.5f, 0.28f);
    vec3 zenith = lerp(lerp(zenithNight, zenithDay, dayF), zenithSunset, sunset * 0.6f);
    vec3 horizon = lerp(lerp(horizNight, horizDay, dayF), horizSunset, sunset * 0.85f);
    horizon += vec3(0.035f, 0.004f, 0.006f) * hollow;

    u.sunDir = vec4(keyDir, shadowStrength);
    u.sunColor = vec4(sunColor, 1);
    u.skyColor = vec4(zenith * 1.05f + vec3(0.015f, 0.02f, 0.035f) * night, 1);
    u.groundColor = vec4(vec3(0.2f, 0.17f, 0.12f) * dayF + vec3(0.01f, 0.009f, 0.012f), 1);
    u.fogColor = vec4(horizon, lerpf(0.0011f, 0.0024f, night));
    u.moonDir = vec4(moon, 1);
    u.skySunDir = vec4(sun, 1);
    u.hollowPos = vec4(0, -30, 0, HOLLOW_RIM - 6.0f);
    u.camPos = vec4(camPos, 1);
    u.camRight = vec4(view.m[0], view.m[4], view.m[8], 0);
    u.camUp = vec4(view.m[1], view.m[5], view.m[9], 0);
    u.playerPos = vec4(mode_ == GM_PLAY ? player_.pos : vec3(0, -1000, 0), 1);
    u.post = vec4(lerpf(0.78f, 1.6f, night), 0.05f, 1.06f, 0.3f);
    u.world = vec4(Terrain::HALF, 0, 1.0f + 0.4f * std::sin(totalTime_ * 0.07f), 0);

    mat4 vp = proj * view;
    u.viewProj = vp;
    u.invViewProj = inverse(vp);
    vec3 fwd = normalize(camTarget - camPos);
    vec3 focus = mode_ == GM_PLAY ? player_.pos : camPos + fwd * 15.0f;
    u.lightViewProj[0] = shadowMatrix(focus + fwd * 8.0f, keyDir, 26.0f);
    u.lightViewProj[1] = shadowMatrix(camPos + fwd * 110.0f, keyDir, 150.0f);

    struct PL {
        vec3 pos;
        float radius;
        vec3 color;
        float intensity;
        float d;
    };
    std::vector<PL> lights;
    float flick = 0.85f + 0.15f * std::sin(totalTime_ * 13.0f) * std::sin(totalTime_ * 7.3f);
    for (const auto& st : structures_)
        if (st.alive && st.type == I_CAMPFIRE) lights.push_back({st.pos + vec3(0, 1.2f, 0), 20.0f, vec3(1.0f, 0.5f, 0.18f), 4.0f * flick, 0});
    if (mode_ == GM_PLAY && !player_.dead && selectedItem() == I_TORCH && player_.riding < 0)
        lights.push_back({player_.pos + vec3(0, 2.0f, 0) + dirFromYaw(player_.yaw) * 0.5f, 15.0f, vec3(1.0f, 0.55f, 0.22f), 3.2f * flick, 0});
    for (const auto& c : creatures_) {
        if (!c.alive || c.state == CS_DEAD) continue;
        const Species& sp = SPECIES[c.species];
        if (sp.glow >= 1.0f || c.species == S_FIRE_DRAGON)
            lights.push_back({c.pos + vec3(0, creatureHeight(c), 0), 6.0f + sp.size * 5.0f, sp.c3, 1.4f + (c.breathTime > 0 ? 3.0f : 0.0f), 0});
    }
    for (const auto& e : infected_)
        if (e.alive && e.state != IS_DEAD && e.cls == IC_SHAMAN) lights.push_back({e.pos + vec3(0, 2.2f, 0), 7.0f, vec3(1.0f, 0.1f, 0.25f), 2.0f, 0});
    for (const auto& p : projectiles_)
        if (p.kind == PJ_BREATH || p.kind == PJ_BOLT) {
            lights.push_back({p.pos, 9.0f, p.color, 0.8f, 0});
            if (lights.size() > 80) break;
        }
    if (hollow > 0.05f) lights.push_back({vec3(0, -14, 0), 95.0f, vec3(1.0f, 0.12f, 0.04f), 3.0f * hollow, 0});
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
    s.reset(renderer_->meshCount());
    float aspect = (float)std::max(1, renderer_->width()) / (float)std::max(1, renderer_->height());
    view_ = lookAt(camPos_, camTarget_, vec3(0, 1, 0));
    proj_ = perspective(fov_, aspect, 0.25f, 3000.0f);
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
        bool riding = player_.riding >= 0;
        float yaw = riding ? creatures_[player_.riding].yaw : player_.yaw;
        float moveAmt = riding ? 0.0f : saturate(lengthXZ(player_.vel) / 4.6f);
        drawHumanoid(s, models_.player, player_.pos, yaw, 1.0f, vec3(1, 1, 1), player_.anim, moveAmt, player_.swing, held, false, 0, 0,
                     riding ? 1.0f : 0.0f, player_.crouch ? 0.35f : 0.0f);
    }
    if (mode_ == GM_TRAILER && trailerGriffin_ >= 0 && trailerTime_ > 17.5f && trailerTime_ < 24.5f) {
        const Creature& g = creatures_[trailerGriffin_];
        drawHumanoid(s, models_.player, creatureSeat(g), g.yaw, 1.0f, vec3(1, 1, 1), 0, 0, 0, I_SPEAR, false, 0, 0, 1.0f, 0);
    }
    drawEffects(s);
}

// ---------------------------------------------------------------------------
void Game::drawProps(FrameScene& s) {
    for (const auto& p : props_.props) {
        if (p.respawn > 0) continue;
        const auto& variants = models_.props[p.type];
        if (variants.empty()) continue;
        bool small = p.type >= P_EMBERBUSH;
        bool tree = p.type <= P_SNOWPINE;
        float maxD = small ? 130.0f : (tree ? 480.0f : 320.0f);
        vec3 d = p.pos - camPos_;
        float dist2 = d.x * d.x + d.z * d.z;
        if (dist2 > maxD * maxD) continue;
        float sc = p.scale;
        float bound = small ? 1.6f : (tree ? 7.0f : 3.0f) * sc;
        if (!sphereVisible(p.pos + vec3(0, bound * 0.5f, 0), bound)) continue;
        const PropModel& pm = variants[p.seed % variants.size()];
        float lodDist = small ? 35.0f : 75.0f;
        int mesh = dist2 > lodDist * lodDist ? pm.lod1 : pm.lod0;
        float h = hashf(p.seed);
        vec3 tint(0.92f + 0.16f * h, 0.92f + 0.16f * hashf(p.seed * 7u), 0.92f + 0.12f * hashf(p.seed * 13u));
        float gloss = p.type == P_CRYSTAL ? 0.85f : (p.type == P_METALROCK ? 0.45f : (pm.pattern == 5 ? 0.12f : 0.08f));
        float glowMul = (p.type == P_SPIRITHERB || p.type == P_DREAMBUSH || p.type == P_CRYSTAL) ? 1.0f + s.ubo.params.y * 1.5f : 1.0f;
        Instance inst{translate(p.pos - vec3(0, 0.05f, 0)) * rotateY(p.yaw) * scale(vec3(sc, sc, sc)), vec4(tint, glowMul),
                      vec4(gloss, (float)pm.pattern, pm.wind, 0)};
        bool castShadow = dist2 < 150.0f * 150.0f;
        (castShadow ? s.inst : s.instFar)[mesh].push_back(inst);
    }

    // Totems around the rim of the Hollow.
    float glowAmt = s.ubo.params.z;
    for (int k = 0; k < 14; k++) {
        float a = k * TAU / 14;
        vec3 pos(std::sin(a) * (HOLLOW_RIM + 8), 0, std::cos(a) * (HOLLOW_RIM + 8));
        pos.y = terrain_.heightAt(pos.x, pos.z);
        if (!sphereVisible(pos + vec3(0, 3, 0), 5)) continue;
        mat4 root = translate(pos) * rotateY(a + PI);
        seg(s, MESH_CYL, root, vec3(0, -0.5f, 0), vec3(0, 5.5f, 0), 0.35f, vec3(0.25f, 0.18f, 0.15f));
        seg(s, MESH_CYL, root, vec3(-0.9f, 4.2f, 0), vec3(0.9f, 4.4f, 0), 0.15f, vec3(0.25f, 0.18f, 0.15f));
        ell(s, root, vec3(0, 5.8f, 0), vec3(0.8f, 0.9f, 0.9f), vec3(0.85f, 0.82f, 0.72f));
        for (int e = -1; e <= 1; e += 2) {
            ell(s, root, vec3(e * 0.18f, 5.9f, 0.38f), vec3(0.16f, 0.12f, 0.1f), vec3(1.0f, 0.15f, 0.05f), 0.2f + glowAmt * 2.5f);
            seg(s, MESH_CONE, root, vec3(e * 0.3f, 6.1f, 0), vec3(e * 0.9f, 7.0f, -0.2f), 0.2f, vec3(0.85f, 0.82f, 0.72f));
        }
        if (glowAmt > 0.1f) glow(s, pos + rotateY(a + PI).transformDir(vec3(0, 5.9f, 0.45f)), 0.6f, vec3(1.0f, 0.15f, 0.05f), glowAmt * 0.7f);
    }
    if (glowAmt > 0.02f && sphereVisible(vec3(0, -30, 0), 30)) {
        Rng cr(31337);
        for (int k = 0; k < 22; k++) {
            float a = cr.range(0, TAU), rr0 = cr.range(0, 6), rr1 = rr0 + cr.range(6, 18);
            vec3 a0(std::sin(a) * rr0, 0, std::cos(a) * rr0), a1(std::sin(a + cr.range(-0.3f, 0.3f)) * rr1, 0, std::cos(a) * rr1);
            a0.y = terrain_.heightAt(a0.x, a0.z) + 0.05f;
            a1.y = terrain_.heightAt(a1.x, a1.z) + 0.05f;
            seg(s, MESH_CUBE, mat4::identity(), a0, a1, cr.range(0.25f, 0.6f), vec3(1.0f, 0.2f, 0.05f), glowAmt * 2.0f);
        }
        vec3 core(0, terrain_.heightAt(0, 0) + 0.2f, 0);
        ell(s, mat4::identity(), core, vec3(7, 0.6f, 7), vec3(1.0f, 0.3f, 0.08f), glowAmt * 2.5f);
        glow(s, core + vec3(0, 2, 0), 9.0f, vec3(1.0f, 0.2f, 0.05f), glowAmt * 0.35f);
    }
}

// ---------------------------------------------------------------------------
void Game::animateCreature(const Creature& c, const Rig& rig, std::vector<mat4>& local) const {
    const Species& sp = SPECIES[c.species];
    local.assign(rig.bones.size(), mat4::identity());
    float ph = c.anim;
    float mv = std::min(c.moveAmt, 1.6f);
    bool asleep = c.state == CS_UNCONSCIOUS || c.state == CS_DEAD;
    float breathe = std::sin(totalTime_ * 1.6f + c.anim) * 0.02f;
    bool open = c.flying || c.state == CS_SCRIPTED;
    float jawOpen = std::max(c.attackAnim, c.breathTime > 0 ? 1.0f : 0.0f);

    if (sp.plan == BP_SERPENT) {
        float amp = 0.28f * std::max(mv, asleep ? 0.0f : 0.25f);
        for (int k = 0; k < rig.spineN; k++) local[rig.spine[k]] = rotateY(std::sin(ph * 1.2f - k * 0.6f) * amp * (k == 0 ? 0.5f : 1.0f));
        local[rig.spine[0]] = local[rig.spine[0]] * rotateX(c.attackAnim * 0.35f + (asleep ? 0.6f : std::sin(ph * 0.5f) * 0.05f));
        local[rig.head[0]] = rotateX(-c.attackAnim * 0.3f) * rotateY(std::sin(totalTime_ * 0.7f + c.anim) * 0.2f);
        local[rig.jaw[0]] = rotateX(jawOpen * 0.6f);
        return;
    }

    local[rig.body] = rotateZ(std::sin(ph) * 0.03f * mv) * rotateX(std::sin(ph * 2.0f) * 0.025f * mv + breathe);
    for (int k = 0; k < rig.legs; k++) {
        bool front = rig.legs == 2 ? true : k < 2;
        float off = rig.legs == 2 ? (k == 0 ? 0.0f : PI) : ((k == 0 || k == 3) ? 0.0f : PI);
        float swing = std::sin(ph + off) * 0.55f * mv;
        float lift = std::max(0.0f, std::cos(ph + off)) * 0.8f * mv;
        mat4 up = rotateX(-swing), lo = rotateX(front ? lift : -lift * 0.6f);
        if (c.flying) {
            up = rotateX(front ? 0.9f : 1.1f);
            lo = rotateX(front ? -1.3f : 0.3f);
        }
        if (asleep) {
            up = rotateX(front ? -0.4f : 0.5f);
            lo = rotateX(front ? 1.1f : -1.0f);
        }
        local[rig.legUp[k]] = up;
        local[rig.legLo[k]] = lo;
    }
    for (int h = 0; h < rig.heads; h++) {
        float look = std::sin(totalTime_ * 0.31f + h * 1.7f + c.anim * 0.1f) * 0.2f;
        if (rig.heads > 3) look += std::sin(totalTime_ * 0.9f + h * 1.3f) * 0.25f;
        float bob = sp.plan == BP_BIRD ? std::sin(ph * 2.0f) * 0.12f * mv : std::sin(ph * 2.0f) * 0.04f * mv;
        float droop = asleep ? 0.9f : 0.0f;
        local[rig.neck0[h]] = rotateY(look * 0.5f) * rotateX(c.attackAnim * 0.5f + droop + bob);
        local[rig.neck1[h]] = rotateX(c.attackAnim * 0.25f + droop * 0.4f);
        local[rig.head[h]] = rotateY(look * 0.5f) * rotateX(-c.attackAnim * 0.3f - droop * 0.3f);
        local[rig.jaw[h]] = rotateX(jawOpen * 0.55f);
    }
    for (int t = 0; t < rig.tails; t++)
        for (int k = 0; k < rig.tailSegs; k++) {
            float sway = std::sin(ph * 0.7f - k * 0.6f + t * 0.8f + totalTime_ * 0.8f) * 0.12f * (1.0f + mv);
            if (rig.tails > 1) sway *= 1.5f;
            float pitch = asleep ? 0.06f : (c.flying ? -0.04f : 0.02f * std::sin(ph + k));
            if (sp.stinger && k >= 2) pitch -= c.attackAnim * 0.35f;
            local[rig.tail[t][k]] = rotateY(sway) * rotateX(pitch);
        }
    if (rig.wings)
        for (int wi = 0; wi < 2; wi++) {
            float side = wi == 0 ? -1.0f : 1.0f;
            if (open && !asleep) {
                float f = std::sin(c.flap) * 0.75f + 0.1f;
                local[rig.wingIn[wi]] = rotateZ(side * f);
                local[rig.wingOut[wi]] = rotateZ(side * std::sin(c.flap - 0.7f) * 0.45f);
            } else {
                local[rig.wingIn[wi]] = rotateY(side * 1.35f) * rotateZ(side * (0.45f + breathe));
                local[rig.wingOut[wi]] = rotateY(side * 0.45f) * rotateZ(side * -0.35f);
            }
        }
}

void Game::drawCreature(FrameScene& s, const Creature& c) {
    const Species& sp = SPECIES[c.species];
    const Rig& rig = models_.creature[c.species];
    float gs = growthScale(c);
    float sz = sp.size * gs;
    float h = creatureHeight(c);
    float rad = creatureRadius(c);
    vec3 center = c.pos + vec3(0, h * 0.5f, 0);
    float dist = length(center - camPos_);
    if (dist > 550.0f || !sphereVisible(center, std::max(rad, h) * 2.0f + sp.tailL * sz + (sp.wings ? sp.bodyL * sz : 0.0f))) return;

    animateCreature(c, rig, localScratch_);
    poseRig(rig, localScratch_, boneScratch_);
    uint32_t offset = (uint32_t)s.bones.size();
    s.bones.insert(s.bones.end(), boneScratch_.begin(), boneScratch_.end());

    vec3 tint(1, 1, 1);
    if (c.hurt > 0) tint = mixc(tint, vec3(1.6f, 0.6f, 0.55f), c.hurt * 0.5f);
    if (c.state == CS_DEAD) tint = vec3(0.45f, 0.42f, 0.42f);
    bool asleep = c.state == CS_UNCONSCIOUS || c.state == CS_DEAD;
    mat4 model = translate(c.pos) * rotateY(c.yaw) * rotateX(c.pitch) * rotateZ(c.roll) * scale(vec3(sz, sz, sz));
    s.skinned[rig.mesh].push_back({model, vec4(tint, asleep ? 0.1f : 1.0f), vec4(rig.gloss, (float)rig.pattern, 0, (float)offset)});

    if (c.hasSaddle) {
        mat4 bodyM = model * boneScratch_[rig.body];
        float L = sp.bodyL, W = sp.bodyW, H = sp.bodyH;
        box(s, bodyM, vec3(0, rig.seatY - H * 0.04f, L * 0.05f), vec3(W * 0.7f, H * 0.12f, L * 0.34f), vec3(0.32f, 0.18f, 0.09f));
        box(s, bodyM, vec3(0, rig.seatY + H * 0.06f, L * 0.18f), vec3(W * 0.3f, H * 0.14f, L * 0.06f), vec3(0.28f, 0.16f, 0.08f));
        for (int sd = -1; sd <= 1; sd += 2)
            seg(s, MESH_CYL, bodyM, vec3(sd * W * 0.36f, rig.seatY - H * 0.05f, L * 0.05f), vec3(sd * W * 0.4f, rig.seatY - H * 0.55f, L * 0.05f), 0.03f / sz,
                vec3(0.2f, 0.12f, 0.06f));
    }
    if (c.state == CS_UNCONSCIOUS && !c.tamed) {
        float t = totalTime_ * 2.0f;
        for (int k = 0; k < 3; k++) {
            float a = t + k * TAU / 3;
            vec3 p = c.pos + vec3(std::sin(a) * rad * 0.6f, h + 0.6f + std::sin(t * 2 + k) * 0.15f, std::cos(a) * rad * 0.6f);
            glow(s, p, 0.25f, vec3(1.0f, 0.9f, 0.45f), 0.9f);
        }
    }
    if (c.breathTime > 0 && sp.breath) glow(s, creatureHead(c), sz * 0.8f, sp.breathColor, 0.8f);
    if (sp.glow >= 1.0f && !asleep && (c.species == S_PHOENIX || c.species == S_SALAMANDER)) glow(s, center, rad * 2.2f, sp.c3, 0.12f);
}

void Game::drawHumanoid(FrameScene& s, const Rig& rig, vec3 pos, float yaw, float sc, vec3 tint, float walk, float moveAmt, float swing,
                        Item held, bool infected, int cls, float glowAmt, float sit, float hunch) {
    if (sit > 0.5f) pos.y -= 0.85f * sc;
    if (!sphereVisible(pos + vec3(0, 1.0f * sc, 0), 1.6f * sc)) return;
    if (length(pos - camPos_) > 350.0f) return;
    std::vector<mat4>& local = localScratch_;
    local.assign(rig.bones.size(), mat4::identity());
    float legSwing = std::sin(walk) * 0.6f * moveAmt;
    for (int sd = 0; sd < 2; sd++) {
        float dir = sd == 0 ? 1.0f : -1.0f;
        float spread = sd == 0 ? -1.0f : 1.0f;
        if (sit > 0.5f) {
            local[rig.thigh[sd]] = rotateX(-1.45f) * rotateZ(spread * 0.3f);
            local[rig.shin[sd]] = rotateX(1.45f);
        } else {
            local[rig.thigh[sd]] = rotateX(-legSwing * dir - hunch * 0.6f);
            float bend = std::max(0.0f, -std::sin(walk + (sd == 0 ? 0.0f : PI))) * 0.9f * moveAmt + 0.05f + hunch * 1.0f;
            local[rig.shin[sd]] = rotateX(bend);
        }
        float armA = std::sin(walk) * 0.45f * moveAmt * -dir;
        mat4 up, lo;
        if (sd == 0 && swing > 0) {
            float lift = std::sin(std::min(swing, 1.0f) * PI) * 2.3f;
            up = rotateX(-lift);
            lo = rotateX(-0.25f - lift * 0.25f);
        } else if (sit > 0.5f) {
            up = rotateX(-0.7f);
            lo = rotateX(-0.6f);
        } else if (infected) {
            up = rotateX(-0.85f + armA * 0.3f) * rotateZ(spread * 0.15f);
            lo = rotateX(-0.45f);
        } else {
            up = rotateX(armA) * rotateZ(spread * 0.07f);
            lo = rotateX(-0.15f - (held != I_NONE && sd == 0 ? 0.6f : 0.0f));
        }
        local[rig.armUp[sd]] = up;
        local[rig.armLo[sd]] = lo;
    }
    local[rig.torso] = rotateX(hunch) * rotateY(swing > 0 ? -std::sin(std::min(swing, 1.0f) * PI) * 0.3f : std::sin(walk) * 0.08f * moveAmt);
    local[rig.headB] = rotateX(-hunch * 0.6f);
    poseRig(rig, local, boneScratch_);
    uint32_t offset = (uint32_t)s.bones.size();
    s.bones.insert(s.bones.end(), boneScratch_.begin(), boneScratch_.end());
    float bob = std::fabs(std::sin(walk)) * 0.04f * moveAmt;
    mat4 model = translate(pos + vec3(0, bob, 0)) * rotateY(yaw) * scale(vec3(sc, sc, sc));
    s.skinned[rig.mesh].push_back({model, vec4(tint, 1.0f + glowAmt), vec4(rig.gloss, (float)rig.pattern, 0, (float)offset)});

    mat4 hand = model * boneScratch_[rig.armLo[0]] * translate(rig.handRest[0]);
    vec3 wood(0.45f, 0.31f, 0.18f), stone(0.5f, 0.5f, 0.5f);
    vec3 a(0, 0.02f, -0.06f), b(0, 0.12f, 0.42f);
    if (infected) {
        if (cls == IC_SHAMAN) {
            seg(s, MESH_CYL, hand, vec3(0, -0.6f, 0.05f), vec3(0, 1.0f, 0.1f), 0.05f, vec3(0.2f, 0.12f, 0.1f));
            ell(s, hand, vec3(0, 1.1f, 0.1f), vec3(0.2f, 0.2f, 0.2f), vec3(1.0f, 0.1f, 0.3f), 2.5f);
            glow(s, hand.transformPoint(vec3(0, 1.1f, 0.1f)), 0.6f, vec3(1.0f, 0.1f, 0.3f), 0.6f);
        } else if (cls != IC_STALKER) {
            seg(s, MESH_CYL, hand, a, b, 0.07f, vec3(0.3f, 0.2f, 0.12f));
            ell(s, hand, b + vec3(0, 0.04f, 0.08f), vec3(0.2f, 0.26f, 0.2f) * (cls == IC_BRUTE ? 1.5f : 1.0f), vec3(0.85f, 0.82f, 0.72f));
            seg(s, MESH_CONE, hand, b + vec3(0.06f, 0.08f, 0.08f), b + vec3(0.2f, 0.18f, 0.12f), 0.06f, vec3(0.88f, 0.86f, 0.78f));
        }
        return;
    }
    switch (held) {
        case I_STONEPICK:
            seg(s, MESH_CYL, hand, a, b, 0.045f, wood);
            seg(s, MESH_CONE, hand, b, b + vec3(0.25f, -0.08f, 0.02f), 0.09f, stone, 0, 0.3f);
            seg(s, MESH_CONE, hand, b, b + vec3(-0.25f, -0.08f, 0.02f), 0.09f, stone, 0, 0.3f);
            break;
        case I_STONEHATCHET:
            seg(s, MESH_CYL, hand, a, b, 0.045f, wood);
            box(s, hand, b + vec3(0.09f, -0.02f, 0), vec3(0.2f, 0.16f, 0.05f), stone, 0, 0, 0.3f);
            break;
        case I_SPEAR:
            seg(s, MESH_CYL, hand, vec3(0, -0.05f, -0.6f), vec3(0, 0.2f, 1.0f), 0.04f, wood);
            seg(s, MESH_CONE, hand, vec3(0, 0.2f, 1.0f), vec3(0, 0.25f, 1.3f), 0.08f, stone, 0, 0.4f);
            break;
        case I_CLUB:
            seg(s, MESH_CYL, hand, a, b, 0.055f, wood);
            ell(s, hand, b + vec3(0, 0.03f, 0.06f), vec3(0.16f, 0.16f, 0.26f), wood * 0.85f);
            break;
        case I_BOW:
            seg(s, MESH_CYL, hand, vec3(0, -0.5f, 0.12f), vec3(0, 0.0f, 0.2f), 0.035f, wood);
            seg(s, MESH_CYL, hand, vec3(0, 0.0f, 0.2f), vec3(0, 0.5f, 0.12f), 0.035f, wood);
            seg(s, MESH_CYL, hand, vec3(0, -0.5f, 0.12f), vec3(0, 0.5f, 0.12f), 0.008f, vec3(0.9f, 0.9f, 0.85f));
            break;
        case I_TORCH:
            seg(s, MESH_CYL, hand, a, b, 0.045f, wood);
            glow(s, hand.transformPoint(b + vec3(0, 0.1f, 0.03f)), 0.35f, vec3(1.0f, 0.55f, 0.15f), 1.0f);
            break;
        case I_BLADE:
            seg(s, MESH_CYL, hand, vec3(0, -0.03f, -0.08f), vec3(0, 0.0f, 0.06f), 0.05f, wood);
            box(s, hand, vec3(0, 0.0f, 0.07f), vec3(0.18f, 0.03f, 0.03f), vec3(0.6f, 0.6f, 0.65f));
            seg(s, MESH_CUBE, hand, vec3(0, 0.01f, 0.08f), vec3(0, 0.12f, 0.95f), 0.06f, vec3(0.8f, 0.82f, 0.9f), 0, 0.9f);
            break;
        default: break;
    }
}

void Game::drawInfected(FrameScene& s, const Infected& e) {
    const InfectedInfo& info = INFECTED[e.cls];
    const Rig& rig = models_.infected[e.cls];
    vec3 tint(1, 1, 1);
    if (e.hurt > 0) tint = mixc(tint, vec3(1.8f, 0.7f, 0.7f), e.hurt * 0.6f);
    if (e.burn > 0) tint = mixc(tint, vec3(0.15f, 0.12f, 0.1f), std::min(1.0f, e.burn * 0.25f));
    if (e.state == IS_DEAD) {
        if (!sphereVisible(e.pos, 2.0f * info.scale)) return;
        localScratch_.assign(rig.bones.size(), mat4::identity());
        poseRig(rig, localScratch_, boneScratch_);
        uint32_t offset = (uint32_t)s.bones.size();
        s.bones.insert(s.bones.end(), boneScratch_.begin(), boneScratch_.end());
        float sc = info.scale;
        mat4 model = translate(e.pos + vec3(0, 0.15f * sc, 0)) * rotateY(e.yaw) * rotateX(-1.5f) * translate(vec3(0, -0.15f, -0.1f)) * scale(vec3(sc, sc, sc));
        s.skinned[rig.mesh].push_back({model, vec4(tint * 0.6f, 0.0f), vec4(rig.gloss, (float)rig.pattern, 0, (float)offset)});
        return;
    }
    float moveAmt = e.state == IS_EMERGE ? 0.6f : (e.state == IS_CHASE ? 1.0f : 0.6f);
    float hunch = e.cls == IC_STALKER ? 0.45f : (e.cls == IC_BRUTE ? 0.15f : 0.05f);
    drawHumanoid(s, rig, e.pos, e.yaw, info.scale, tint, e.anim, moveAmt, e.swing, I_NONE, true, e.cls, e.state == IS_CHASE ? 1.5f : 0.0f, 0, hunch);
}

void Game::drawStructures(FrameScene& s) {
    float flick = std::sin(totalTime_ * 11.0f) * 0.1f;
    for (const auto& st : structures_) {
        if (!st.alive || !sphereVisible(st.pos + vec3(0, 1, 0), 3)) continue;
        mat4 root = translate(st.pos) * rotateY(st.yaw);
        switch (st.type) {
            case I_CAMPFIRE:
                for (int k = 0; k < 9; k++) {
                    float a = k * TAU / 9;
                    ell(s, root, vec3(std::sin(a) * 0.75f, 0.1f, std::cos(a) * 0.75f), vec3(0.38f, 0.26f, 0.34f), vec3(0.42f, 0.4f, 0.38f));
                }
                seg(s, MESH_CYL, root, vec3(-0.55f, 0.12f, -0.2f), vec3(0.5f, 0.3f, 0.2f), 0.15f, vec3(0.3f, 0.2f, 0.11f));
                seg(s, MESH_CYL, root, vec3(-0.3f, 0.3f, 0.5f), vec3(0.3f, 0.12f, -0.5f), 0.15f, vec3(0.3f, 0.2f, 0.11f));
                seg(s, MESH_CYL, root, vec3(0.2f, 0.12f, 0.5f), vec3(-0.1f, 0.4f, -0.4f), 0.13f, vec3(0.28f, 0.18f, 0.1f));
                ell(s, root, vec3(0, 0.15f, 0), vec3(0.7f, 0.15f, 0.7f), vec3(1.0f, 0.35f, 0.08f), 2.0f + flick * 5);
                glow(s, st.pos + vec3(0, 0.6f, 0), 1.6f, vec3(1.0f, 0.45f, 0.12f), 0.55f + flick);
                break;
            case I_CAULDRON:
                for (int k = 0; k < 3; k++) {
                    float a = k * TAU / 3;
                    seg(s, MESH_CYL, root, vec3(std::sin(a) * 0.6f, 0, std::cos(a) * 0.6f), vec3(std::sin(a) * 0.4f, 0.6f, std::cos(a) * 0.4f), 0.09f,
                        vec3(0.2f, 0.2f, 0.22f), 0, 0.6f);
                }
                ell(s, root, vec3(0, 0.75f, 0), vec3(1.3f, 1.0f, 1.3f), vec3(0.15f, 0.15f, 0.17f), 0, 0.7f);
                ell(s, root, vec3(0, 1.18f, 0), vec3(1.0f, 0.12f, 1.0f), vec3(0.3f, 0.95f, 0.5f), 1.4f + flick * 3);
                glow(s, st.pos + vec3(0, 1.4f, 0), 1.0f, vec3(0.3f, 1.0f, 0.5f), 0.4f);
                ell(s, root, vec3(0, 0.12f, 0), vec3(0.5f, 0.2f, 0.5f), vec3(1.0f, 0.4f, 0.1f), 2.0f);
                break;
            case I_WOODWALL:
            case I_SPIKEWALL: {
                vec3 wood(0.45f, 0.31f, 0.18f);
                float dmg = saturate(st.hp / 900.0f);
                wood = mixc(vec3(0.2f, 0.14f, 0.1f), wood, dmg);
                for (int k = 0; k < 6; k++)
                    seg(s, MESH_CYL, root, vec3(-1.75f + k * 0.7f, -0.3f, 0), vec3(-1.75f + k * 0.7f, 3.0f + (k % 2) * 0.25f, 0), 0.4f, wood * (k % 2 ? 1.0f : 0.88f));
                box(s, root, vec3(0, 2.2f, 0.22f), vec3(4.2f, 0.22f, 0.12f), wood * 0.75f);
                box(s, root, vec3(0, 0.8f, 0.22f), vec3(4.2f, 0.22f, 0.12f), wood * 0.75f);
                for (int k = 0; k < 6; k++)
                    push(s, MESH_CONE, root * translate(vec3(-1.75f + k * 0.7f, 3.35f + (k % 2) * 0.25f, 0)) * scale(vec3(0.4f, 0.5f, 0.4f)), wood * 0.9f);
                if (st.type == I_SPIKEWALL)
                    for (int k = 0; k < 8; k++) {
                        vec3 b(-1.75f + k * 0.5f, 0.6f + (k % 3) * 0.6f, 0.2f);
                        seg(s, MESH_CONE, root, b, b + vec3(0, 0.3f, 1.2f), 0.13f, vec3(0.6f, 0.5f, 0.36f));
                        seg(s, MESH_CONE, root, b - vec3(0, 0, 0.4f), b + vec3(0, 0.3f, -1.4f), 0.13f, vec3(0.6f, 0.5f, 0.36f));
                    }
                break;
            }
            default: break;
        }
    }
    if (mode_ == GM_PLAY && panel_ == PANEL_NONE && !player_.dead && player_.riding < 0) {
        Item it = selectedItem();
        if (it != I_NONE && ITEMS[it].kind == IK_PLACEABLE) {
            vec3 pos = player_.pos + dirFromYaw(player_.yaw) * 3.5f;
            pos.y = terrain_.heightAt(pos.x, pos.z);
            bool ok = !terrain_.isOcean(pos.x, pos.z, -0.2f);
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
        for (int k = 0; k < 14; k++) {
            float a = k * TAU / 14;
            vec3 p(std::sin(a) * 1.1f, 0.2f + (k % 2) * 0.12f, std::cos(a) * 1.1f);
            seg(s, MESH_CYL, root, p - vec3(std::cos(a), 0, -std::sin(a)) * 0.55f, p + vec3(std::cos(a), 0.1f, -std::sin(a)) * 0.55f, 0.1f,
                vec3(0.33f, 0.23f, 0.14f));
        }
        ell(s, root, vec3(0, 0.08f, 0), vec3(1.8f, 0.2f, 1.8f), vec3(0.4f, 0.32f, 0.2f));
    }
    for (const auto& e : eggs_) {
        if (!e.alive || !sphereVisible(e.pos, 2)) continue;
        const Species& sp = SPECIES[e.species];
        float sz = 0.6f + sp.size * 0.15f;
        float wobble = e.state == EGG_INCUBATING && e.progress > 0.7f ? std::sin(totalTime_ * 12) * 0.12f * (e.progress - 0.7f) * 3 : 0.0f;
        mat4 model = translate(e.pos) * rotateZ(wobble) * scale(vec3(sz, sz, sz));
        float g = e.state == EGG_INCUBATING ? e.progress : 0.0f;
        s.inst[models_.eggMesh].push_back({model, vec4(mixc(sp.c1, vec3(1, 1, 1), 0.2f), 1.0f), vec4(0.6f, 0, 0, 0)});
        if (g > 0.05f || sp.glow > 0.5f) glow(s, e.pos + vec3(0, 0.7f * sz, 0), 0.9f * sz, sp.c3, 0.15f + g * 0.4f);
        if (e.state == EGG_NEST && e.nest >= 0) glow(s, e.pos + vec3(0, 2.6f + std::sin(totalTime_ * 2) * 0.2f, 0), 0.35f, vec3(1.0f, 0.85f, 0.3f), 0.9f);
    }
}

void Game::drawEffects(FrameScene& s) {
    for (const auto& p : particles_) {
        float t = p.life / p.maxLife;
        if (!sphereVisible(p.pos, p.size * 2)) continue;
        if (p.emissive > 0.25f) {
            float size = p.size * (0.5f + 0.8f * t);
            s.particlesAdd.push_back({vec4(p.pos, size), vec4(p.color * (0.6f + p.emissive), t * 0.9f)});
        } else {
            float size = p.size * (1.6f - 0.6f * t) * (p.gravity < 0 ? 2.2f - t : 1.0f);
            s.particlesAlpha.push_back({vec4(p.pos, size), vec4(p.color, t * 0.85f)});
        }
    }
    for (const auto& p : projectiles_) {
        if (!sphereVisible(p.pos, 1)) continue;
        switch (p.kind) {
            case PJ_ARROW:
            case PJ_DART: {
                vec3 d = normalize(p.vel);
                seg(s, MESH_CYL, mat4::identity(), p.pos - d * 0.45f, p.pos + d * 0.4f, 0.035f, vec3(0.55f, 0.4f, 0.25f));
                seg(s, MESH_CONE, mat4::identity(), p.pos + d * 0.38f, p.pos + d * 0.55f, 0.07f, p.color, p.kind == PJ_DART ? 1.0f : 0.0f, 0.5f);
                if (p.kind == PJ_DART) glow(s, p.pos, 0.2f, p.color, 0.6f);
                break;
            }
            case PJ_BOLT:
                glow(s, p.pos, 0.6f, p.color, 1.0f);
                glow(s, p.pos, 0.25f, vec3(1, 0.8f, 0.9f), 1.0f);
                break;
            case PJ_BREATH:
                glow(s, p.pos, 0.7f + (0.85f - p.life) * 1.2f, p.color, 0.8f);
                break;
        }
    }
}

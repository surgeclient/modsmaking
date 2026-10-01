// The boot trailer: a ~45 second scripted cinematic rendered live with the game engine.
#include "game.h"
#include <GLFW/glfw3.h>

namespace {

struct Shot {
    float start, end;
    float timeOfDay;
    const char* caption;
    const char* caption2;
    vec3 captionColor;
};

// Shot list. Camera paths are computed in updateTrailer() because several of them
// follow actors that move.
const Shot SHOTS[] = {
    {0.0f, 6.0f, 0.262f, "SOMEWHERE BEYOND THE EDGE OF EVERY MAP...", nullptr, {1.0f, 0.92f, 0.8f}},
    {6.0f, 12.0f, 0.34f, "AN ISLAND WHERE LEGENDS STILL BREATHE", nullptr, {1.0f, 0.95f, 0.85f}},
    {12.0f, 18.0f, 0.43f, "DRAGONS RULE ITS SKIES", nullptr, {1.0f, 0.7f, 0.4f}},
    {18.0f, 24.0f, 0.52f, "HUNT THEM.  TAME THEM.  RIDE THEM.", nullptr, {0.85f, 0.95f, 1.0f}},
    {24.0f, 29.5f, 0.735f, "BUT WHEN THE SUN GOES DOWN...", nullptr, {1.0f, 0.75f, 0.5f}},
    {29.5f, 38.0f, 0.84f, "THE HOLLOW OPENS", "AND THE TRIBE KILLS EVERYTHING IT SEES", {1.0f, 0.2f, 0.15f}},
    {38.0f, 46.0f, 0.84f, nullptr, nullptr, {1, 1, 1}},
};
const int SHOT_COUNT = sizeof(SHOTS) / sizeof(SHOTS[0]);
const float TRAILER_LENGTH = 46.0f;

float ease(float t) {
    t = saturate(t);
    return t * t * (3 - 2 * t);
}

vec3 meadowSpot;
vec3 forestSpot;
std::vector<int> herd;

}  // namespace

void Game::startTrailer() {
    mode_ = GM_TRAILER;
    trailerTime_ = opt_.trailerAt;
    trailerSpawnedTribe_ = false;
    setCursorCaptured(true);

    // Pick deterministic, good-looking locations.
    Rng r(99);
    meadowSpot = terrain_.randomLand(r, B_MEADOW, 4.0f);
    forestSpot = terrain_.randomLand(r, B_FOREST, 4.0f);

    trailerDragon_ = spawnCreature(S_FIRE_DRAGON, vec3(330, 120, 40));
    creatures_[trailerDragon_].state = CS_SCRIPTED;
    creatures_[trailerDragon_].flying = true;
    creatures_[trailerDragon_].level = 99;
    trailerGriffin_ = spawnCreature(S_GRIFFIN, forestSpot + vec3(0, 40, 0));
    creatures_[trailerGriffin_].state = CS_SCRIPTED;
    creatures_[trailerGriffin_].flying = true;
    creatures_[trailerGriffin_].hasSaddle = true;
    herd.clear();
    for (int k = 0; k < 6; k++) {
        int species = k % 3 == 0 ? S_UNICORN : S_PEGASUS;
        int idx = spawnCreature(species, meadowSpot, k == 5);
        creatures_[idx].state = CS_SCRIPTED;
        herd.push_back(idx);
    }
    trailerPegasus_ = herd[0];
}

void Game::endTrailer() {
    for (int idx : {trailerDragon_, trailerGriffin_})
        if (idx >= 0 && idx < (int)creatures_.size()) creatures_[idx].alive = false;
    for (int idx : herd)
        if (idx < (int)creatures_.size()) creatures_[idx].alive = false;
    herd.clear();
    trailerDragon_ = trailerGriffin_ = trailerPegasus_ = -1;
    for (auto& e : infected_)
        if (e.trailer) e.alive = false;
    for (auto& b : bands_) b.active = false;
    mode_ = GM_TITLE;
    titleTime_ = 0;
    setCursorCaptured(false);
}

void Game::updateTrailer(float dt) {
    trailerTime_ += dt;
    float t = trailerTime_;
    if (keyPressed(GLFW_KEY_ENTER) || keyPressed(GLFW_KEY_ESCAPE) || keyPressed(GLFW_KEY_SPACE) || t >= TRAILER_LENGTH) {
        endTrailer();
        return;
    }

    int shot = 0;
    for (int i = 0; i < SHOT_COUNT; i++)
        if (t >= SHOTS[i].start) shot = i;
    const Shot& S = SHOTS[shot];
    float k = (t - S.start) / (S.end - S.start);
    time_ = S.timeOfDay + k * 0.012f;

    // Fade between shots.
    float edge = std::min(t - S.start, S.end - t);
    fade_ = 1.0f - saturate(edge / 0.45f);
    if (shot == 0) fade_ = std::max(fade_, 1.0f - saturate(t / 1.5f));
    if (shot == SHOT_COUNT - 1) fade_ = 1.0f;

    // Scripted actors.
    if (trailerDragon_ >= 0) {
        Creature& d = creatures_[trailerDragon_];
        float a = t * 0.32f;
        vec3 volcano(330, 0, 40);
        d.pos = volcano + vec3(std::cos(a) * 110.0f, 105.0f + std::sin(t * 0.8f) * 6.0f, std::sin(a) * 110.0f);
        d.yaw = std::atan2(-std::sin(a), std::cos(a));  // tangent of the circle
        d.pitch = -0.1f;
        d.roll = -0.25f;
        d.breathTime = (shot == 2 && k > 0.55f && k < 0.9f) ? 0.5f : 0.0f;
        if (d.breathTime > 0 && rng_.chance(0.8f)) {
            vec3 head = creatureHead(d);
            Projectile p;
            p.kind = PJ_BREATH;
            p.pos = head;
            p.vel = normalize(dirFromYaw(d.yaw) + vec3(rng_.range(-0.1f, 0.1f), -0.35f, rng_.range(-0.1f, 0.1f))) * 28.0f;
            p.life = 1.0f;
            p.dmg = 0;
            p.owner = {TK_CREATURE, trailerDragon_};
            p.color = SPECIES[S_FIRE_DRAGON].breathColor;
            projectiles_.push_back(p);
        }
    }
    if (trailerGriffin_ >= 0) {
        Creature& g = creatures_[trailerGriffin_];
        float gt = std::max(0.0f, t - 18.0f);
        vec3 start = forestSpot + vec3(-90, 0, 0);
        g.pos = start + vec3(gt * 26.0f, 0, std::sin(gt * 0.5f) * 10.0f);
        g.pos.y = std::max(terrain_.heightAt(g.pos.x, g.pos.z), 0.0f) + 26.0f + std::sin(gt * 1.3f) * 3.0f;
        g.yaw = PI * 0.5f + std::cos(gt * 0.5f) * 0.18f;
        g.pitch = std::sin(gt * 1.3f) * -0.1f;
        g.flying = true;
    }
    for (int n = 0; n < (int)herd.size(); n++) {
        Creature& c = creatures_[herd[n]];
        float ht = std::max(0.0f, t - 6.0f);
        vec3 p = meadowSpot + vec3(-45.0f + ht * 12.5f + (n % 3) * 3.0f, 0, (n - 2.5f) * 4.5f + std::sin(ht + n) * 0.8f);
        p.y = terrain_.heightAt(p.x, p.z);
        c.pos = p;
        c.yaw = PI * 0.5f;
        c.moveAmt = 1.5f;
        c.flying = false;
    }

    // Tribe emerges at the start of the Hollow shot.
    if (shot == 5 && !trailerSpawnedTribe_) {
        trailerSpawnedTribe_ = true;
        forcedSpawnAngle_ = std::atan2(11.0f, 19.0f) + 0.35f;
        spawnBand(3, true);
        spawnBand(3, true);
        forcedSpawnAngle_ = -100.0f;
        for (auto& b : bands_)
            if (b.active) b.waypoint = normalize(vec3(11.0f + rng_.range(-4, 4), 0, 19.0f)) * 70.0f;
        shake_ = 0.5f;
    }
    for (int i = 0; i < (int)infected_.size(); i++)
        if (infected_[i].alive && infected_[i].trailer) updateInfected(i, dt);

    updateCreatures(dt);
    updateProjectiles(dt);
    updateAmbientFX(dt);
    updateParticles(dt);
    // Embers rising from the pit.
    if (shot >= 5 && rng_.chance(dt * 60)) {
        vec3 p(rng_.range(-20, 20), -31, rng_.range(-20, 20));
        spawnParticles(p, 1, vec3(1.0f, 0.25f, 0.08f), 2.0f, 4.0f, 0.14f, 1.5f, -3.0f);
    }

    // Camera.
    switch (shot) {
        case 0: {
            camPos_ = lerp(vec3(420, 20, 720), vec3(260, 34, 470), ease(k));
            camTarget_ = vec3(0, 25, 0);
            break;
        }
        case 1: {
            vec3 herdC = meadowSpot + vec3(-45.0f + (t - 6.0f) * 12.5f, 2, 0);
            camPos_ = lerp(meadowSpot + vec3(-40, 5, 28), meadowSpot + vec3(20, 4, 24), ease(k));
            camPos_.y = std::max(camPos_.y, terrain_.heightAt(camPos_.x, camPos_.z) + 2.5f);
            camTarget_ = herdC;
            break;
        }
        case 2: {
            const Creature& d = creatures_[trailerDragon_];
            vec3 side(std::cos(d.yaw), 0, -std::sin(d.yaw));
            camPos_ = d.pos - dirFromYaw(d.yaw) * (22.0f - k * 6.0f) + side * 14.0f + vec3(0, 6.0f, 0);
            camTarget_ = d.pos + dirFromYaw(d.yaw) * 6.0f;
            break;
        }
        case 3: {
            const Creature& g = creatures_[trailerGriffin_];
            camPos_ = g.pos + vec3(-6.0f + k * 10.0f, 3.0f, 12.0f - k * 3.0f);
            camTarget_ = g.pos + vec3(3, 0, 0);
            break;
        }
        case 4: {
            camPos_ = lerp(vec3(10, 80, 300), vec3(0, 55, 130), ease(k));
            camTarget_ = vec3(0, -5, 0);
            break;
        }
        case 5: {
            // Hover over the rim and look down into the pit as the tribe climbs out.
            // Crouched on the pit floor as the tribe crawls out and marches at the camera.
            camPos_ = lerp(vec3(11, 0, 19), vec3(8, 0, 14), ease(k));
            camPos_.y = terrain_.heightAt(camPos_.x, camPos_.z) + 1.3f;
            camTarget_ = lerp(vec3(2, -29.0f, 4), vec3(0, -27.5f, 2), ease(k));
            break;
        }
        default: break;
    }
    if (shake_ > 0) {
        shake_ = std::max(0.0f, shake_ - dt);
        camPos_ += vec3(rng_.range(-1, 1), rng_.range(-1, 1), rng_.range(-1, 1)) * shake_ * 0.4f;
    }
}

void Game::drawTrailerUI(FrameScene& s) {
    float W = (float)renderer_->width(), H = (float)renderer_->height();
    float ui = std::max(1.0f, H / 720.0f);
    float t = trailerTime_;
    int shot = 0;
    for (int i = 0; i < SHOT_COUNT; i++)
        if (t >= SHOTS[i].start) shot = i;
    const Shot& S = SHOTS[shot];

    // Cinematic letterbox.
    float bar = H * 0.11f;
    s.ui.push_back({vec4(0, 0, W, bar), vec4(0, 0, 0, 1)});
    s.ui.push_back({vec4(0, H - bar, W, bar), vec4(0, 0, 0, 1)});
    if (fade_ > 0) s.ui.push_back({vec4(0, 0, W, H), vec4(0, 0, 0, fade_)});

    auto drawText = [&](const std::string& str, float y, float px, vec4 col) { uiTextCentered(s, W * 0.5f, y, str, px, col); };

    float local = t - S.start;
    float dur = S.end - S.start;
    if (S.caption) {
        float a = saturate((local - 0.6f) / 0.8f) * saturate((dur - local - 0.4f) / 0.6f);
        drawText(S.caption, H - bar + bar * 0.32f, 3 * ui, vec4(S.captionColor, a));
    }
    if (S.caption2) {
        float a = saturate((local - 3.5f) / 0.8f) * saturate((dur - local - 0.4f) / 0.6f);
        drawText(S.caption2, H - bar + bar * 0.32f + 34 * ui, 2 * ui, vec4(1.0f, 0.6f, 0.55f, a));
    }
    if (shot == SHOT_COUNT - 1) {
        float a = saturate((local - 0.5f) / 1.2f);
        drawText("MYTHBOUND", H * 0.36f, 13 * ui, vec4(1.0f, 0.82f, 0.45f, a));
        drawText("SURVIVE THE NIGHT", H * 0.36f + 120 * ui, 3 * ui, vec4(1.0f, 0.35f, 0.3f, saturate((local - 2.0f) / 1.0f)));
        drawText("21 MYTHICAL CREATURES  -  ONE ISLAND  -  NO MERCY", H * 0.36f + 170 * ui, 2 * ui,
                 vec4(0.9f, 0.9f, 0.95f, saturate((local - 3.0f) / 1.0f)));
    }
    uiTextCentered(s, W - 90 * ui, bar * 0.4f, "ENTER TO SKIP", 1.2f * ui, vec4(0.6f, 0.6f, 0.6f, 0.7f));
}

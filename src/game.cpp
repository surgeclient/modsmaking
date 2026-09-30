#include "game.h"
#include <GLFW/glfw3.h>
#include <cstdio>

// ---------------------------------------------------------------------------
// Setup
// ---------------------------------------------------------------------------
bool Game::init(GLFWwindow* window, Renderer* renderer, const Options& opt) {
    window_ = window;
    renderer_ = renderer;
    opt_ = opt;

    std::printf("Generating island...\n");
    terrain_.generate();
    props_.generate(terrain_);
    std::vector<Vertex> v;
    std::vector<uint32_t> idx;
    terrain_.buildMesh(v, idx);
    renderer_->uploadTerrain(v, idx);
    std::printf("Island ready: %zu props\n", props_.props.size());

    resetWorld();
    if (opt_.skipTrailer) startPlay();
    else startTrailer();
    return true;
}

void Game::resetWorld() {
    creatures_.clear();
    eggs_.clear();
    nests_.clear();
    infected_.clear();
    bands_.clear();
    structures_.clear();
    projectiles_.clear();
    particles_.clear();
    for (auto& p : props_.props) {
        p.respawn = 0;
        p.hp = p.type <= P_SNOWPINE ? 120.0f : (p.type == P_BOULDER ? 250.0f : 60.0f);
    }
    spawnInitialLife();
    time_ = 0.3f;
    day_ = 1;
    night_ = 0;
    nightActive_ = false;
}

int Game::spawnCreature(int species, vec3 pos, bool baby) {
    const Species& sp = SPECIES[species];
    Creature c;
    c.species = species;
    c.pos = pos;
    c.home = pos;
    c.yaw = rng_.range(-PI, PI);
    c.level = baby ? rng_.irange(1, 10) : rng_.irange(1, 30);
    float lv = (float)c.level;
    c.maxHp = sp.hp * (1.0f + lv * 0.04f);
    c.hp = c.maxHp;
    c.dmg = sp.dmg * (1.0f + lv * 0.025f);
    c.speed = sp.speed;
    c.maxTorpor = (100.0f + sp.hp * 0.12f) * (0.7f + 0.3f * sp.torporRes) * (1.0f + lv * 0.015f);
    c.maxStamina = 100.0f + sp.hp * 0.05f;
    c.stamina = c.maxStamina;
    c.baby = baby;
    c.growth = baby ? 0.3f : 1.0f;
    c.name = sp.name;
    c.moveTarget = pos;
    c.thinkTimer = rng_.range(0, 1);
    c.anim = rng_.range(0, 10);
    // Reuse dead slots so indices stay small.
    for (int i = 0; i < (int)creatures_.size(); i++)
        if (!creatures_[i].alive) {
            creatures_[i] = c;
            return i;
        }
    creatures_.push_back(c);
    return (int)creatures_.size() - 1;
}

void Game::spawnInitialLife() {
    for (int s = 0; s < S_COUNT; s++) {
        const Species& sp = SPECIES[s];
        if (sp.tame == TM_EGG) {
            for (int n = 0; n < 2; n++) {
                Nest nest{s, terrain_.randomLand(rng_, sp.home[n % 3], 4.0f)};
                Egg e;
                e.species = s;
                e.pos = nest.pos;
                e.nest = (int)nests_.size();
                eggs_.push_back(e);
                nest.egg = (int)eggs_.size() - 1;
                nests_.push_back(nest);
            }
        }
        for (int n = 0; n < sp.spawnCount; n++) {
            vec3 p;
            if (sp.tame == TM_EGG && n < (int)nests_.size() && n < 2) {
                // Guards circle their nest.
                const Nest* nest = nullptr;
                int k = 0;
                for (auto& ns : nests_)
                    if (ns.species == s && k++ == n) nest = &ns;
                p = nest ? nest->pos + vec3(rng_.range(-12, 12), 0, rng_.range(-12, 12)) : terrain_.randomLand(rng_, sp.home[0]);
            } else {
                p = terrain_.randomLand(rng_, sp.home[n % 3], sp.swimmer ? 0.3f : 1.5f);
            }
            p.y = terrain_.heightAt(p.x, p.z);
            int adult = spawnCreature(s, p, false);
            if (sp.tame == TM_BABY && n % 2 == 0) {
                vec3 bp = p + vec3(rng_.range(-3, 3), 0, rng_.range(-3, 3));
                bp.y = terrain_.heightAt(bp.x, bp.z);
                int b = spawnCreature(s, bp, true);
                creatures_[b].parent = adult;
            }
        }
    }
}

void Game::startPlay() {
    mode_ = GM_PLAY;
    paused_ = false;
    panel_ = PANEL_NONE;
    player_ = Player{};
    vec3 start = opt_.hasPos ? vec3(opt_.posX, 0, opt_.posZ) : vec3(-20, 0, 430);
    // Walk inland until we are on dry sand.
    for (int i = 0; i < 200 && terrain_.heightAt(start.x, start.z) < 1.2f; i++) start.z -= 2.0f;
    start.y = terrain_.heightAt(start.x, start.z);
    player_.pos = start;
    player_.yaw = opt_.hasPos ? opt_.yaw : PI;
    player_.pitch = opt_.pitch;
    time_ = opt_.startTime;
    nightActive_ = false;
    if (time_ >= 0.77f || time_ < 0.22f) {
        nightActive_ = true;
        night_ = 1;
        bandsToSpawn_ = 3;
        tribeSpawnTimer_ = 0;
    }
    player_.inv[I_EMBERBERRY] = 6;
    if (opt_.demo) {
        for (Item it : {I_STONEPICK, I_STONEHATCHET, I_CLUB, I_SPEAR, I_BOW, I_TORCH, I_CAMPFIRE, I_CAULDRON})
            player_.inv[it] = 1;
        player_.inv[I_ARROW] = 30;
        player_.inv[I_SLEEPDART] = 20;
        player_.inv[I_WOODWALL] = 6;
        player_.inv[I_SADDLE] = 2;
        player_.inv[I_MYTHBAIT] = 6;
        player_.inv[I_WILDBAIT] = 10;
        player_.inv[I_RAWMEAT] = 20;
        player_.inv[I_WOOD] = 80;
        player_.inv[I_STONE] = 60;
        player_.inv[I_THATCH] = 60;
        player_.inv[I_FIBER] = 60;
        player_.inv[I_FLINT] = 40;
        player_.inv[I_CRYSTAL] = 6;
        player_.inv[I_SPIRITHERB] = 10;
        player_.inv[I_DREAMBERRY] = 30;
        player_.inv[I_EMBERBERRY] = 40;
        vec3 gp = player_.pos + vec3(4, 0, -3);
        gp.y = terrain_.heightAt(gp.x, gp.z);
        int g = spawnCreature(S_GRIFFIN, gp, false);
        creatures_[g].tamed = true;
        creatures_[g].hasSaddle = true;
        creatures_[g].state = CS_FOLLOW;
        creatures_[g].name = "Skyclaw";
        vec3 hp = player_.pos + vec3(-4, 0, -4);
        hp.y = terrain_.heightAt(hp.x, hp.z);
        int h = spawnCreature(S_SALAMANDER, hp, false);
        creatures_[h].tamed = true;
        creatures_[h].state = CS_FOLLOW;
        creatures_[h].name = "Ember";
    }
    setCursorCaptured(true);
    if (opt_.uiTest == "craft") panel_ = PANEL_CRAFT;
    if (opt_.uiTest == "help") panel_ = PANEL_HELP;
    if (opt_.uiTest == "tame") {
        vec3 gp = player_.pos + dirFromYaw(player_.yaw) * 5.0f;
        gp.y = terrain_.heightAt(gp.x, gp.z);
        int g = spawnCreature(S_MANTICORE, gp);
        creatures_[g].state = CS_UNCONSCIOUS;
        creatures_[g].torpor = creatures_[g].maxTorpor * 0.8f;
        creatures_[g].food[I_MYTHBAIT] = 2;
        creatures_[g].food[I_DREAMBERRY] = 10;
        creatures_[g].tameProgress = 70;
        creatures_[g].tameEff = 0.9f;
        panel_ = PANEL_TAME;
        panelTarget_ = g;
    }
    if (opt_.uiTest == "zoo") {
        // Line every species up on a meadow for a group photo.
        for (auto& c : creatures_) c.alive = false;
        vec3 origin(-160, 0, 60);
        for (int sId = 0; sId < S_COUNT; sId++) {
            int row = sId / 7, col = sId % 7;
            vec3 p = origin + vec3((col - 3) * 13.0f, 0, -row * 15.0f);
            p.y = terrain_.heightAt(p.x, p.z);
            int c = spawnCreature(sId, p);
            creatures_[c].state = CS_SCRIPTED;
            creatures_[c].yaw = 0.35f;
            creatures_[c].level = 1;
        }
        for (auto& pr : props_.props)
            if (distXZ(pr.pos, origin + vec3(0, 0, -10)) < 75) pr.respawn = 1e9f;
        player_.pos = origin + vec3(0, 0, 40);
        player_.pos.y = terrain_.heightAt(player_.pos.x, player_.pos.z);
        panel_ = PANEL_NONE;
    }
    if (opt_.uiTest == "ride") {
        for (auto& c : creatures_)
            if (c.alive && c.tamed && c.species == S_GRIFFIN) {
                int idx = (int)(&c - &creatures_[0]);
                c.pos = player_.pos + vec3(0, 45, -40);
                c.flying = true;
                c.state = CS_RIDDEN;
                player_.riding = idx;
                player_.yaw = 0.3f;
                player_.pitch = -0.25f;
            }
    }
    if (opt_.uiTest == "brew") {
        brew_ = Brew{};
        brew_.recipe = RECIPE_COUNT - 2;
        brew_.stage = 1;
        brew_.hits = 1;
        brew_.speed = 0.0f;
        brew_.needle = 0.45f;
        brew_.zoneStart = 0.55f;
        brew_.zoneWidth = 0.16f;
        panel_ = PANEL_BREW;
    }
    addMessage("You wash up on an unknown island...", vec3(1.0f, 0.9f, 0.7f));
    addMessage("Punch trees & rocks (LMB) for wood, thatch & stone. TAB to craft.", vec3(0.8f, 0.9f, 1.0f));
    addMessage("When night falls, the tribe climbs out of the Hollow. Run or fight.", vec3(1.0f, 0.5f, 0.4f));
}

void Game::addMessage(const std::string& s, vec3 color) {
    messages_.push_back({s, color, 7.0f});
    if (messages_.size() > 7) messages_.erase(messages_.begin());
    std::printf("[msg] %s\n", s.c_str());
}

// ---------------------------------------------------------------------------
// Input
// ---------------------------------------------------------------------------
void Game::pollInput() {
    std::copy(keys_, keys_ + 400, prevKeys_);
    std::copy(mouse_, mouse_ + 8, prevMouse_);
    for (int k = 32; k < 400; k++) keys_[k] = glfwGetKey(window_, k) == GLFW_PRESS;
    for (int b = 0; b < 8; b++) mouse_[b] = glfwGetMouseButton(window_, b) == GLFW_PRESS;
    double x, y;
    glfwGetCursorPos(window_, &x, &y);
    if (firstMouse_) {
        mouseX_ = x;
        mouseY_ = y;
        firstMouse_ = false;
    }
    mouseDX_ = x - mouseX_;
    mouseDY_ = y - mouseY_;
    mouseX_ = x;
    mouseY_ = y;
    if (!cursorCaptured_) mouseDX_ = mouseDY_ = 0;
    scroll_ = scrollAccum_;
    scrollAccum_ = 0;
    if (glfwWindowShouldClose(window_)) quit_ = true;
}

bool Game::keyDown(int k) const { return k >= 0 && k < 400 && keys_[k]; }
bool Game::keyPressed(int k) const { return k >= 0 && k < 400 && keys_[k] && !prevKeys_[k]; }
bool Game::mouseDown(int b) const { return mouse_[b]; }
bool Game::mousePressed(int b) const { return mouse_[b] && !prevMouse_[b]; }

void Game::setCursorCaptured(bool c) {
    cursorCaptured_ = c;
    glfwSetInputMode(window_, GLFW_CURSOR, c ? GLFW_CURSOR_DISABLED : GLFW_CURSOR_NORMAL);
    firstMouse_ = true;
}

// ---------------------------------------------------------------------------
// Frame
// ---------------------------------------------------------------------------
void Game::frame(float dt) {
    dt = std::min(dt, 0.05f);
    pollInput();
    totalTime_ += dt;

    if (keyPressed(GLFW_KEY_F12)) {
        char name[64];
        std::snprintf(name, sizeof(name), "mythbound_%d.ppm", (int)totalTime_);
        renderer_->requestScreenshot(name);
    }

    switch (mode_) {
        case GM_TRAILER:
            updateTrailer(dt);
            break;
        case GM_TITLE:
            titleTime_ += dt;
            time_ = 0.705f;
            updateCreatures(dt);
            updateParticles(dt);
            {
                float a = titleTime_ * 0.04f;
                camTarget_ = vec3(0, 20, 0);
                camPos_ = vec3(std::sin(a) * 520.0f, 150.0f, std::cos(a) * 520.0f);
            }
            if (keyPressed(GLFW_KEY_ENTER) || keyPressed(GLFW_KEY_KP_ENTER)) {
                resetWorld();
                startPlay();
            }
            if (keyPressed(GLFW_KEY_T)) startTrailer();
            if (keyPressed(GLFW_KEY_ESCAPE)) quit_ = true;
            break;
        case GM_PLAY:
            if (keyPressed(GLFW_KEY_ESCAPE)) {
                if (panel_ != PANEL_NONE && panel_ != PANEL_BREW) panel_ = PANEL_NONE;
                else if (panel_ == PANEL_NONE) {
                    paused_ = !paused_;
                    setCursorCaptured(!paused_);
                }
            }
            if (paused_) {
                if (keyPressed(GLFW_KEY_Q)) quit_ = true;
                if (keyPressed(GLFW_KEY_T)) {
                    paused_ = false;
                    startTrailer();
                }
            } else {
                updatePlay(dt);
            }
            break;
    }

    FrameScene scene;
    buildScene(scene);
    buildUI(scene);

    if (opt_.captureAfter >= 0 && !captured_ && totalTime_ >= opt_.captureAfter) {
        renderer_->requestScreenshot(opt_.capturePath.empty() ? "capture.ppm" : opt_.capturePath);
        captured_ = true;
    }
    renderer_->render(scene);
    if (opt_.quitAfter > 0 && totalTime_ >= opt_.quitAfter) quit_ = true;
}

void Game::updateTime(float dt) {
    bool night = time_ >= 0.75f || time_ < 0.25f;
    float dayLength = night ? 270.0f : 420.0f;  // seconds per half-cycle: nights are shorter
    float prev = time_;
    time_ += dt * 0.5f / dayLength;
    if (time_ >= 1.0f) time_ -= 1.0f;

    auto crossed = [&](float t) { return prev < t && time_ >= t; };
    if (crossed(0.25f)) day_++;
    if (crossed(0.77f)) {
        night_++;
        nightActive_ = true;
        bandsToSpawn_ = std::min(2 + night_, 9);
        tribeSpawnTimer_ = 0;
        addMessage("NIGHT " + std::to_string(night_) + " - THE HOLLOW AWAKENS", vec3(1.0f, 0.25f, 0.2f));
        addMessage("War drums echo from the centre of the island...", vec3(1.0f, 0.55f, 0.45f));
        shake_ = 0.6f;
    }
    if (crossed(0.2f)) {
        for (auto& b : bands_) b.returning = true;
        addMessage("The tribe retreats towards the Hollow before sunrise.", vec3(1.0f, 0.8f, 0.5f));
    }
    if (crossed(0.26f)) {
        nightActive_ = false;
        addMessage("Dawn breaks. Any of the tribe left outside will burn.", vec3(1.0f, 0.9f, 0.6f));
    }
}

void Game::updatePlay(float dt) {
    playTime_ += dt;
    updateTime(dt);
    if (panel_ == PANEL_NONE || panel_ == PANEL_TAME) updatePlayer(dt);
    updatePanels(dt);
    updateCreatures(dt);
    updateEggsAndNests(dt);
    updateTribe(dt);
    updateProjectiles(dt);
    updateParticles(dt);
    respawnWildlife(dt);

    // Resource respawn.
    for (auto& p : props_.props)
        if (p.respawn > 0) {
            p.respawn -= dt;
            if (p.respawn <= 0) p.hp = p.type <= P_SNOWPINE ? 120.0f : (p.type == P_BOULDER ? 250.0f : 60.0f);
        }

    for (auto& m : messages_) m.time -= dt;
    while (!messages_.empty() && messages_.front().time <= 0) messages_.erase(messages_.begin());

    // Camera.
    if (panel_ == PANEL_NONE) camDist_ = clampf(camDist_ - (float)scroll_ * 0.8f, 2.5f, 22.0f);
    vec3 focus = player_.pos + vec3(0, 1.6f, 0);
    float dist = camDist_;
    if (player_.riding >= 0) {
        const Creature& c = creatures_[player_.riding];
        focus = creatureSeat(c) + vec3(0, 1.2f, 0);
        dist = camDist_ + creatureRadius(c) * 2.2f;
    }
    vec3 fwd(std::sin(player_.yaw) * std::cos(player_.pitch), std::sin(player_.pitch), std::cos(player_.yaw) * std::cos(player_.pitch));
    vec3 right = normalize(cross(fwd, vec3(0, 1, 0)));
    camPos_ = focus - fwd * dist + right * 0.7f;
    float ground = terrain_.heightAt(camPos_.x, camPos_.z);
    camPos_.y = std::max(camPos_.y, ground + 0.6f);
    camTarget_ = focus + fwd * 12.0f + right * 0.7f;
    if (opt_.uiTest == "zoo") {
        camPos_ = vec3(-160, 0, 60) + vec3(0, 0, 42);
        camPos_.y = terrain_.heightAt(camPos_.x, camPos_.z) + 15.0f;
        camTarget_ = vec3(-160, terrain_.heightAt(-160, 46) + 2.0f, 46);
    }
    if (shake_ > 0) {
        shake_ = std::max(0.0f, shake_ - dt);
        camPos_ += vec3(rng_.range(-1, 1), rng_.range(-1, 1), rng_.range(-1, 1)) * shake_ * 0.35f;
    }
}


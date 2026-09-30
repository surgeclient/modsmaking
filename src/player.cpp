// Player: survival stats, movement, gathering, combat, riding, carrying eggs/babies, building.
#include "game.h"
#include <GLFW/glfw3.h>

std::vector<Item> Game::hotbarItems() const {
    std::vector<Item> list;
    for (int it = 1; it < I_COUNT && list.size() < 8; it++)
        if (player_.inv[it] > 0 && (ITEMS[it].kind == IK_TOOL || ITEMS[it].kind == IK_PLACEABLE)) list.push_back((Item)it);
    return list;
}

Item Game::selectedItem() const {
    auto list = hotbarItems();
    return player_.hotSel < (int)list.size() ? list[player_.hotSel] : I_NONE;
}

bool Game::canCraft(const Recipe& r) const {
    for (auto& in : r.in)
        if (in.item != I_NONE && player_.inv[in.item] < in.count) return false;
    return true;
}

void Game::craft(const Recipe& r, int multiplier) {
    for (auto& in : r.in)
        if (in.item != I_NONE) player_.inv[in.item] -= in.count;
    player_.inv[r.out] += r.outCount * multiplier;
}

static vec3 cameraForward(float yaw, float pitch) {
    return {std::sin(yaw) * std::cos(pitch), std::sin(pitch), std::cos(yaw) * std::cos(pitch)};
}

void Game::updatePlayer(float dt) {
    Player& p = player_;
    if (p.dead) {
        p.respawnTimer -= dt;
        if (p.respawnTimer <= 0) {
            vec3 start(rng_.range(-120, 80), 0, 440);
            for (int i = 0; i < 200 && terrain_.heightAt(start.x, start.z) < 1.2f; i++) start.z -= 2.0f;
            start.y = terrain_.heightAt(start.x, start.z);
            p.pos = start;
            p.vel = {};
            p.hp = p.maxHp;
            p.food = std::max(p.food, 60.0f);
            p.water = std::max(p.water, 60.0f);
            p.stamina = 100;
            p.dead = false;
            addMessage("You wake up on the beach...", vec3(1.0f, 0.9f, 0.7f));
        }
        return;
    }
    p.hurtFlash = std::max(0.0f, p.hurtFlash - dt * 1.5f);
    p.attackCd -= dt;
    p.swing = std::max(0.0f, p.swing - dt * 3.0f);

    if (panel_ == PANEL_NONE) {
        p.yaw -= (float)mouseDX_ * 0.0025f;
        p.pitch = clampf(p.pitch - (float)mouseDY_ * 0.0025f, -1.2f, 0.95f);
    }
    if (panel_ == PANEL_NONE)
        for (int k = 0; k < 8; k++)
            if (keyPressed(GLFW_KEY_1 + k)) p.hotSel = k;
    if (keyPressed(GLFW_KEY_TAB)) {
        panel_ = PANEL_CRAFT;
        return;
    }
    if (keyPressed(GLFW_KEY_F1)) {
        panel_ = PANEL_HELP;
        return;
    }

    findInteraction();
    if (keyPressed(GLFW_KEY_E)) playerInteract();
    if (keyPressed(GLFW_KEY_Q)) dropCarried();
    if (keyPressed(GLFW_KEY_X)) {
        Item best = I_NONE;
        for (Item it : {I_COOKEDMEAT, I_EMBERBERRY, I_RAWMEAT, I_DREAMBERRY})
            if (p.inv[it] > 0) {
                best = it;
                break;
            }
        if (best == I_NONE) addMessage("You have nothing to eat.", vec3(1.0f, 0.7f, 0.5f));
        else {
            p.inv[best]--;
            p.food = std::min(100.0f, p.food + ITEMS[best].food);
            if (best == I_EMBERBERRY) p.water = std::min(100.0f, p.water + 2);
            if (best == I_DREAMBERRY) p.stamina = std::max(0.0f, p.stamina - 30);
            addMessage(std::string("Ate ") + ITEMS[best].name, vec3(0.8f, 1.0f, 0.8f));
        }
    }
    Item sel = selectedItem();
    if (keyPressed(GLFW_KEY_R)) {
        if (sel == I_BOW) {
            p.ammoDarts = !p.ammoDarts;
            addMessage(p.ammoDarts ? "Bow loaded with SLEEP DARTS" : "Bow loaded with ARROWS", vec3(0.8f, 0.9f, 1.0f));
        } else {
            p.placeYaw += PI * 0.25f;
        }
    }

    // Mount / dismount.
    if (keyPressed(GLFW_KEY_F)) {
        if (p.riding >= 0) {
            Creature& c = creatures_[p.riding];
            c.state = c.stayMode ? CS_STAY : CS_FOLLOW;
            vec3 side(std::cos(c.yaw), 0, -std::sin(c.yaw));
            p.pos = c.pos + side * (creatureRadius(c) + 1.0f);
            p.pos.y = std::max(terrain_.heightAt(p.pos.x, p.pos.z), c.pos.y);
            p.vel = {};
            p.riding = -1;
        } else {
            int best = -1;
            float bestD = 1e9f;
            for (int i = 0; i < (int)creatures_.size(); i++) {
                const Creature& c = creatures_[i];
                if (!c.alive || !c.tamed || c.state == CS_DEAD || c.state == CS_UNCONSCIOUS) continue;
                float d = distXZ(c.pos, p.pos) - creatureRadius(c);
                if (d < 3.5f && d < bestD) {
                    bestD = d;
                    best = i;
                }
            }
            if (best >= 0) {
                Creature& c = creatures_[best];
                const Species& sp = SPECIES[c.species];
                if (!sp.rideable) addMessage(std::string(sp.name) + " is too small to ride - it fights beside you instead.", vec3(1.0f, 0.8f, 0.5f));
                else if (c.baby) addMessage("It's still a baby - let it grow up first.", vec3(1.0f, 0.8f, 0.5f));
                else if (p.carryBaby >= 0 || p.carryEgg >= 0) addMessage("Your hands are full.", vec3(1.0f, 0.8f, 0.5f));
                else {
                    if (!c.hasSaddle && p.inv[I_SADDLE] > 0) {
                        p.inv[I_SADDLE]--;
                        c.hasSaddle = true;
                        addMessage("You strap a saddle onto " + c.name + ".", vec3(0.8f, 1.0f, 0.8f));
                    }
                    if (!c.hasSaddle) addMessage("You need a Saddle to ride (Hide, Fiber, Bone).", vec3(1.0f, 0.7f, 0.5f));
                    else {
                        p.riding = best;
                        c.state = CS_RIDDEN;
                        addMessage("Riding " + c.name + ".  LMB bite" + std::string(sp.breath ? "  RMB breath" : "") +
                                       (sp.flyer ? "  SPACE fly up  CTRL down" : "") + "  F dismount",
                                   vec3(0.8f, 0.95f, 1.0f));
                    }
                }
            }
        }
    }

    if (p.riding >= 0) {
        updateRiding(dt);
        return;
    }

    // --- Movement ---
    vec3 fwd = dirFromYaw(p.yaw);
    vec3 right = normalize(cross(fwd, vec3(0, 1, 0)));
    vec3 wish;
    if (keyDown(GLFW_KEY_W)) wish += fwd;
    if (keyDown(GLFW_KEY_S)) wish -= fwd;
    if (keyDown(GLFW_KEY_D)) wish += right;
    if (keyDown(GLFW_KEY_A)) wish -= right;
    bool moving = lengthXZ(wish) > 0.1f;
    if (moving) wish = normalize(wish);
    p.crouch = keyDown(GLFW_KEY_LEFT_CONTROL) || keyDown(GLFW_KEY_C);
    bool sprint = keyDown(GLFW_KEY_LEFT_SHIFT) && p.stamina > 1 && moving && !p.crouch && !p.swimming;
    float speed = p.swimming ? 3.2f : (p.crouch ? 2.2f : (sprint ? 8.2f : 4.6f));
    if (p.carryBaby >= 0 || p.carryEgg >= 0) speed *= 0.85f;
    p.vel.x = wish.x * speed;
    p.vel.z = wish.z * speed;
    p.vel.y -= 22.0f * dt;
    if (keyPressed(GLFW_KEY_SPACE) && p.onGround && p.stamina >= 8) {
        p.vel.y = 7.0f;
        p.stamina -= 8;
        p.onGround = false;
    }
    vec3 np = p.pos + p.vel * dt;
    float ground = terrain_.heightAt(np.x, np.z);
    // Cliffs: can't walk up anything too steep.
    if (ground - p.pos.y > 1.1f && !p.swimming) {
        np.x = p.pos.x;
        np.z = p.pos.z;
        ground = terrain_.heightAt(np.x, np.z);
    }
    p.swimming = terrain_.isOcean(np.x, np.z, 1.3f);
    if (p.swimming) {
        np.y = lerpf(p.pos.y, -1.15f, saturate(dt * 5));
        p.vel.y = 0;
        p.onGround = false;
    } else if (np.y <= ground) {
        np.y = ground;
        p.vel.y = 0;
        p.onGround = true;
    } else {
        p.onGround = np.y - ground < 0.05f;
    }
    pushOutOfProps(np, 0.4f);
    pushOutOfStructures(np, 0.4f);
    for (const auto& c : creatures_) {
        if (!c.alive || c.state == CS_CARRIED || c.state == CS_DEAD || c.baby) continue;
        float r = creatureRadius(c) * 0.75f + 0.4f;
        vec3 d = np - c.pos;
        d.y = 0;
        float dd = lengthXZ(d);
        if (dd < r && dd > 1e-4f && std::fabs(np.y - c.pos.y) < creatureHeight(c)) np += d * ((r - dd) / dd);
    }
    if (lengthXZ(np) > Terrain::HALF - 20) np = p.pos;
    p.pos = np;
    p.anim += dt * (moving ? speed * 1.4f : 0.0f);

    // --- Stats ---
    if (sprint) p.stamina = std::max(0.0f, p.stamina - 14 * dt);
    else if (p.swimming && moving) p.stamina = std::max(0.0f, p.stamina - 3 * dt);
    else p.stamina = std::min(100.0f, p.stamina + 12 * dt);
    p.food = std::max(0.0f, p.food - dt * (sprint ? 0.15f : 0.085f));
    p.water = std::max(0.0f, p.water - dt * (sprint ? 0.2f : 0.13f));
    if (p.food <= 0 || p.water <= 0) damagePlayer(1.5f * dt, {}, p.food <= 0 ? "starvation" : "thirst");
    else if (p.food > 35 && p.water > 35) p.hp = std::min(p.maxHp, p.hp + 0.6f * dt);
    if (p.swimming && p.stamina <= 0) damagePlayer(5 * dt, {}, "exhaustion");
    // Frostspire cold bites at night unless near a fire.
    if (terrain_.biomeAt(p.pos.x, p.pos.z) == B_SNOW && (time_ > 0.75f || time_ < 0.25f) && !nearCampfire(p.pos, 10) && sel != I_TORCH)
        damagePlayer(1.0f * dt, {}, "the cold");

    // --- Actions ---
    if (panel_ == PANEL_NONE && cursorCaptured_) {
        if (ITEMS[sel].kind == IK_PLACEABLE && sel != I_NONE) {
            if (mousePressed(GLFW_MOUSE_BUTTON_LEFT)) placeStructure();
        } else if (mouseDown(GLFW_MOUSE_BUTTON_LEFT) && p.attackCd <= 0) {
            if (sel != I_BOW || mousePressed(GLFW_MOUSE_BUTTON_LEFT)) playerAttack();
        }
    }
}

void Game::updateRiding(float dt) {
    Player& p = player_;
    Creature& c = creatures_[p.riding];
    const Species& sp = SPECIES[c.species];
    if (!c.alive || c.state == CS_DEAD) {
        p.riding = -1;
        return;
    }
    vec3 fwd = dirFromYaw(p.yaw);
    vec3 right = normalize(cross(fwd, vec3(0, 1, 0)));
    vec3 wish;
    if (keyDown(GLFW_KEY_W)) wish += fwd;
    if (keyDown(GLFW_KEY_S)) wish -= fwd;
    if (keyDown(GLFW_KEY_D)) wish += right;
    if (keyDown(GLFW_KEY_A)) wish -= right;
    bool moving = lengthXZ(wish) > 0.1f;
    bool sprint = keyDown(GLFW_KEY_LEFT_SHIFT) && c.stamina > 1 && moving;
    float speed = c.speed * (sprint ? 1.6f : 1.0f) * (c.flying ? 1.35f : 1.0f);
    if (sprint) c.stamina = std::max(0.0f, c.stamina - 10 * dt);
    else c.stamina = std::min(c.maxStamina, c.stamina + 8 * dt);

    if (sp.flyer) {
        if (keyDown(GLFW_KEY_SPACE)) c.flying = true;
    }
    float ground = terrain_.heightAt(c.pos.x, c.pos.z);
    vec3 np = c.pos;
    if (moving) {
        wish = normalize(wish);
        c.yaw = approachAngle(c.yaw, std::atan2(wish.x, wish.z), 4.0f * dt);
        np += dirFromYaw(c.yaw) * (speed * dt * std::max(0.2f, dot(dirFromYaw(c.yaw), wish)));
    }
    float ng = terrain_.heightAt(np.x, np.z);
    if (c.flying) {
        float vy = 0;
        if (keyDown(GLFW_KEY_SPACE)) vy = 9;
        if (keyDown(GLFW_KEY_LEFT_CONTROL) || keyDown(GLFW_KEY_C)) vy = -11;
        np.y = c.pos.y + vy * dt;
        np.y = std::min(np.y, std::max(ng, 0.0f) + 160.0f);
        c.pitch = lerpf(c.pitch, clampf(-vy * 0.05f, -0.45f, 0.45f), dt * 4);
        if (np.y <= std::max(ng, -0.2f) + 0.3f) {
            np.y = std::max(ng, -0.2f);
            if (vy <= 0) c.flying = false;
        }
        c.stamina = std::max(0.0f, c.stamina - 1.5f * dt);
        if (c.stamina <= 0) np.y -= 6 * dt;  // exhausted flyers sink
    } else {
        if (ng - c.pos.y > 2.0f + sp.size) np = vec3(c.pos.x, c.pos.y, c.pos.z);  // too steep
        ng = terrain_.heightAt(np.x, np.z);
        float body = creatureHeight(c);
        np.y = terrain_.isOcean(np.x, np.z, 0.3f) ? std::max(ng, -body * 0.55f) : ng;
        c.pitch = lerpf(c.pitch, 0, dt * 4);
    }
    pushOutOfStructures(np, creatureRadius(c) * 0.8f);
    if (lengthXZ(np) > Terrain::HALF - 20) np = c.pos;
    c.moveAmt = lerpf(c.moveAmt, moving ? (sprint ? 1.6f : 1.0f) : 0.0f, saturate(dt * 6));
    c.pos = np;
    (void)ground;
    p.pos = creatureSeat(c);

    // Attacks.
    if (panel_ == PANEL_NONE && mousePressed(GLFW_MOUSE_BUTTON_LEFT) && c.attackCd <= 0) {
        c.attackCd = 0.9f;
        c.attackAnim = 1;
        vec3 cf = dirFromYaw(c.yaw);
        float reach = creatureRadius(c) + 3.0f;
        for (int k = 0; k < (int)infected_.size(); k++) {
            const Infected& e = infected_[k];
            if (!e.alive || e.state == IS_DEAD) continue;
            vec3 d = e.pos - c.pos;
            if (lengthXZ(d) < reach && dot(normalize(vec3(d.x, 0, d.z)), cf) > 0.3f) damageInfected(k, c.dmg, {TK_CREATURE, p.riding});
        }
        for (int k = 0; k < (int)creatures_.size(); k++) {
            if (k == p.riding) continue;
            const Creature& o = creatures_[k];
            if (!o.alive || o.tamed || o.state == CS_DEAD) continue;
            vec3 d = o.pos - c.pos;
            if (lengthXZ(d) - creatureRadius(o) < reach && dot(normalize(vec3(d.x, 0, d.z)), cf) > 0.3f)
                damageCreature(k, c.dmg, 0, {TK_CREATURE, p.riding});
        }
        // Big mounts harvest trees and rocks in one bite.
        props_.query(c.pos, reach + 2, scratch_);
        std::vector<int> hits = scratch_;
        for (int idx : hits) {
            Prop& pr = props_.props[idx];
            if (pr.respawn > 0) continue;
            vec3 d = pr.pos - c.pos;
            if (lengthXZ(d) < reach + 1 && dot(normalize(vec3(d.x, 0, d.z)), cf) > 0.3f) {
                bool tree = pr.type <= P_SNOWPINE;
                bool rock = pr.type >= P_ROCK && pr.type <= P_CRYSTAL;
                if (tree) {
                    p.inv[I_WOOD] += 4;
                    p.inv[I_THATCH] += 3;
                } else if (rock) {
                    p.inv[pr.type == P_METALROCK ? I_METAL : (pr.type == P_CRYSTAL ? I_CRYSTAL : I_STONE)] += 3;
                    p.inv[I_FLINT] += 1;
                } else if (pr.type == P_EMBERBUSH) p.inv[I_EMBERBERRY] += 4;
                else if (pr.type == P_DREAMBUSH) p.inv[I_DREAMBERRY] += 4;
                else p.inv[I_FIBER] += 3;
                pr.hp -= 40;
                if (pr.hp <= 0) pr.respawn = 200;
                spawnParticles(pr.pos + vec3(0, 1.0f, 0), 5, tree ? vec3(0.5f, 0.35f, 0.2f) : vec3(0.55f, 0.55f, 0.55f), 3, 0.5f, 0.15f, 0);
            }
        }
    }
    if (panel_ == PANEL_NONE && sp.breath && mouseDown(GLFW_MOUSE_BUTTON_RIGHT) && c.breathCd <= 0) {
        vec3 dir = normalize(camTarget_ - creatureHead(c));
        creatureBreath(p.riding, dir);
        c.breathCd = 1.2f;
    }
}

void Game::playerAttack() {
    Player& p = player_;
    Item it = selectedItem();
    vec3 eye = p.pos + vec3(0, 1.6f, 0);
    vec3 camF = cameraForward(p.yaw, p.pitch);

    if (it == I_BOW) {
        Item ammo = p.ammoDarts ? I_SLEEPDART : I_ARROW;
        if (p.inv[ammo] <= 0) {
            addMessage(std::string("Out of ") + ITEMS[ammo].name + "s! (R switches ammo)", vec3(1.0f, 0.7f, 0.5f));
            p.attackCd = 0.4f;
            return;
        }
        p.inv[ammo]--;
        Projectile pr;
        pr.kind = p.ammoDarts ? PJ_DART : PJ_ARROW;
        vec3 aim = camPos_ + normalize(camTarget_ - camPos_) * 80.0f;
        pr.pos = eye + camF * 0.5f;
        pr.vel = normalize(aim - pr.pos) * 60.0f;
        pr.life = 3.0f;
        pr.dmg = ITEMS[ammo].damage;
        pr.torpor = ITEMS[ammo].torpor;
        pr.owner = {TK_PLAYER, 0};
        pr.gravity = true;
        pr.color = ITEMS[ammo].color;
        projectiles_.push_back(pr);
        p.attackCd = 0.75f;
        p.swing = 0.6f;
        return;
    }

    float dmg = it == I_NONE ? 7.0f : ITEMS[it].damage;
    float torpor = it == I_NONE ? 3.0f : ITEMS[it].torpor;
    float reach = it == I_SPEAR ? 3.6f : 2.8f;
    p.attackCd = it == I_CLUB ? 0.8f : (it == I_SPEAR ? 0.7f : 0.55f);
    p.swing = 1.0f;
    vec3 fwd = dirFromYaw(p.yaw);

    // 1. Living targets.
    TargetRef best;
    float bestD = 1e9f;
    for (int k = 0; k < (int)creatures_.size(); k++) {
        const Creature& c = creatures_[k];
        if (!c.alive || c.state == CS_DEAD || c.state == CS_CARRIED || c.tamed) continue;
        vec3 d = c.pos - p.pos;
        float dist = lengthXZ(d) - creatureRadius(c);
        if (dist < reach && dist < bestD && (lengthXZ(d) < creatureRadius(c) + 0.5f || dot(normalize(vec3(d.x, 0, d.z)), fwd) > 0.4f)) {
            bestD = dist;
            best = {TK_CREATURE, k};
        }
    }
    for (int k = 0; k < (int)infected_.size(); k++) {
        const Infected& e = infected_[k];
        if (!e.alive || e.state == IS_DEAD) continue;
        vec3 d = e.pos - p.pos;
        float dist = lengthXZ(d) - 0.4f;
        if (dist < reach && dist < bestD && dot(normalize(vec3(d.x, 0, d.z)), fwd) > 0.4f) {
            bestD = dist;
            best = {TK_INFECTED, k};
        }
    }
    if (best.kind != TK_NONE) {
        damageTarget(best, dmg, torpor, {TK_PLAYER, 0});
        return;
    }

    // 2. Corpses.
    for (int k = 0; k < (int)creatures_.size(); k++) {
        Creature& c = creatures_[k];
        if (!c.alive || c.state != CS_DEAD || c.corpseHits <= 0) continue;
        if (distXZ(c.pos, p.pos) - creatureRadius(c) > reach) continue;
        c.corpseHits--;
        bool blade = it == I_STONEHATCHET || it == I_BLADE;
        p.inv[I_RAWMEAT] += blade ? 3 : 2;
        p.inv[I_HIDE] += blade ? 2 : 1;
        if (rng_.chance(0.3f)) p.inv[I_BONE] += 1;
        spawnParticles(c.pos + vec3(0, 0.5f, 0), 6, vec3(0.5f, 0.05f, 0.05f), 2.5f, 0.5f, 0.12f, 0);
        if (c.corpseHits <= 0) c.deathTimer = std::min(c.deathTimer, 1.0f);
        return;
    }
    for (int k = 0; k < (int)infected_.size(); k++) {
        Infected& e = infected_[k];
        if (!e.alive || e.state != IS_DEAD || e.corpseHits <= 0) continue;
        if (distXZ(e.pos, p.pos) > reach + 0.5f) continue;
        if (e.cls == IC_CHIEFTAIN && e.corpseHits == 8) {
            p.inv[I_HOLLOWHEART] += 1;
            addMessage("You tear out a pulsing HOLLOW HEART.", vec3(1.0f, 0.3f, 0.3f));
        }
        e.corpseHits--;
        p.inv[I_BONE] += rng_.irange(1, 2);
        p.inv[I_FIBER] += 1;
        if (rng_.chance(0.3f)) p.inv[I_HIDE] += 1;
        if (e.corpseHits <= 0) e.deathTimer = std::min(e.deathTimer, 1.0f);
        return;
    }

    // 3. Gather resources.
    props_.query(p.pos, reach + 3, scratch_);
    int bestProp = -1;
    float bestPD = 1e9f;
    for (int idx : scratch_) {
        const Prop& pr = props_.props[idx];
        if (pr.respawn > 0) continue;
        vec3 d = pr.pos - p.pos;
        float dist = lengthXZ(d) - std::max(PropField::collisionRadius(pr), 0.6f);
        if (dist < reach && dist < bestPD && (lengthXZ(d) < 1.0f || dot(normalize(vec3(d.x, 0, d.z)), fwd) > 0.3f)) {
            bestPD = dist;
            bestProp = idx;
        }
    }
    if (bestProp < 0) return;
    Prop& pr = props_.props[bestProp];
    bool pick = it == I_STONEPICK, hatchet = it == I_STONEHATCHET || it == I_BLADE;
    auto give = [&](Item i, int n) { if (n > 0) p.inv[i] += n; };
    vec3 chip(0.5f, 0.5f, 0.5f);
    switch (pr.type) {
        case P_OAK: case P_PINE: case P_PALM: case P_JUNGLETREE: case P_DEADTREE: case P_SNOWPINE:
            give(I_WOOD, hatchet ? rng_.irange(3, 5) : 1);
            give(I_THATCH, hatchet ? 1 : rng_.irange(2, 3));
            chip = vec3(0.5f, 0.35f, 0.2f);
            break;
        case P_ROCK: case P_BOULDER:
            give(I_STONE, pick ? rng_.irange(1, 2) : rng_.irange(2, 3));
            give(I_FLINT, pick ? rng_.irange(2, 3) : (rng_.chance(0.5f) ? 1 : 0));
            break;
        case P_METALROCK:
            if (pick) give(I_METAL, rng_.irange(1, 2));
            else if (rng_.chance(0.02f)) addMessage("You need a Stone Pick to mine metal.", vec3(1.0f, 0.8f, 0.5f));
            give(I_STONE, 1);
            chip = vec3(0.65f, 0.65f, 0.75f);
            break;
        case P_CRYSTAL:
            give(I_CRYSTAL, pick ? rng_.irange(1, 2) : (rng_.chance(0.35f) ? 1 : 0));
            chip = vec3(0.5f, 0.95f, 1.0f);
            break;
        case P_EMBERBUSH:
            give(I_EMBERBERRY, rng_.irange(2, 4));
            give(I_FIBER, rng_.irange(1, 2));
            chip = vec3(0.9f, 0.2f, 0.15f);
            break;
        case P_DREAMBUSH:
            give(I_DREAMBERRY, rng_.irange(2, 3));
            give(I_FIBER, rng_.irange(1, 2));
            chip = vec3(0.6f, 0.3f, 0.85f);
            break;
        case P_SPIRITHERB:
            give(I_SPIRITHERB, rng_.irange(1, 2));
            chip = vec3(0.4f, 0.8f, 1.0f);
            break;
        case P_FERN:
            give(I_FIBER, rng_.irange(2, 3));
            chip = vec3(0.3f, 0.6f, 0.25f);
            break;
        case P_BONEPILE:
            give(I_BONE, rng_.irange(1, 3));
            chip = vec3(0.9f, 0.88f, 0.8f);
            break;
        default: break;
    }
    pr.hp -= (pick || hatchet) ? 30.0f : 20.0f;
    if (pr.type >= P_EMBERBUSH) pr.hp -= 20.0f;
    if (pr.hp <= 0) pr.respawn = rng_.range(150, 260);
    spawnParticles(pr.pos + vec3(0, 1.0f, 0) + (p.pos - pr.pos) * 0.2f, 5, chip, 3, 0.5f, 0.12f, pr.type == P_CRYSTAL || pr.type == P_SPIRITHERB ? 0.8f : 0.0f);
}

void Game::findInteraction() {
    iaKind_ = IA_NONE;
    iaIdx_ = -1;
    iaPrompt_.clear();
    const Player& p = player_;
    if (p.riding >= 0) return;
    vec3 fwd = dirFromYaw(p.yaw);
    float bestScore = 1e9f;
    auto consider = [&](InteractKind k, int idx, vec3 pos, float radius, const std::string& prompt) {
        vec3 d = pos - p.pos;
        float dist = lengthXZ(d) - radius;
        if (dist > 3.0f) return;
        float facing = lengthXZ(d) > 0.5f ? dot(normalize(vec3(d.x, 0, d.z)), fwd) : 1.0f;
        if (facing < 0.2f && dist > 0.8f) return;
        float score = dist - facing * 1.5f;
        if (score < bestScore) {
            bestScore = score;
            iaKind_ = k;
            iaIdx_ = idx;
            iaPrompt_ = prompt;
        }
    };
    for (int i = 0; i < (int)creatures_.size(); i++) {
        const Creature& c = creatures_[i];
        if (!c.alive || c.state == CS_DEAD || c.state == CS_CARRIED) continue;
        const Species& sp = SPECIES[c.species];
        float r = creatureRadius(c);
        if (c.state == CS_UNCONSCIOUS && !c.tamed) {
            if (sp.tame == TM_KNOCKOUT) consider(IA_CREATURE, i, c.pos, r, "E  Feed & tame the " + std::string(sp.name));
        } else if (c.tamed) {
            std::string s = "E  " + std::string(c.stayMode ? "Follow me" : "Stay here") + "   (" + c.name + ")";
            if (sp.rideable && !c.baby) s += "   F  Ride";
            consider(IA_CREATURE, i, c.pos, r, s);
        } else if (c.baby && sp.tame == TM_BABY) {
            if (c.readyToBond) consider(IA_CREATURE, i, c.pos, r, "E  Feed the baby " + std::string(sp.name) + " to bond with it");
            else if (p.carryBaby < 0 && p.carryEgg < 0) consider(IA_CREATURE, i, c.pos, r, "E  Grab the baby " + std::string(sp.name));
        }
    }
    for (int i = 0; i < (int)eggs_.size(); i++) {
        const Egg& e = eggs_[i];
        if (!e.alive || e.state == EGG_CARRIED) continue;
        const char* name = SPECIES[e.species].name;
        bool dragonKin = e.species == S_FIRE_DRAGON || e.species == S_FROST_WYVERN || e.species == S_STORM_DRAKE;
        if (e.state == EGG_INCUBATING) {
            std::string s = std::string(name) + " egg  " + std::to_string((int)(e.progress * 100)) + "%";
            if (dragonKin && !e.fed) s += "   E  Offer Dragon Bait";
            else if (!nearCampfire(e.pos, 7)) s += "   (needs a campfire!)";
            consider(IA_EGG, i, e.pos, 0.5f, s);
        } else if (p.carryBaby < 0 && p.carryEgg < 0) {
            consider(IA_EGG, i, e.pos, 0.5f, "E  Steal the " + std::string(name) + " egg");
        }
    }
    for (int i = 0; i < (int)structures_.size(); i++) {
        const Structure& s = structures_[i];
        if (!s.alive) continue;
        if (s.type == I_CAMPFIRE) consider(IA_STRUCTURE, i, s.pos, 0.8f, "E  Cook all raw meat");
        if (s.type == I_CAULDRON) consider(IA_STRUCTURE, i, s.pos, 0.8f, "E  Brew taming bait");
    }
    if (iaKind_ == IA_NONE) {
        vec3 ahead = p.pos + fwd * 1.5f;
        if (terrain_.isOcean(ahead.x, ahead.z, 0.2f) || p.swimming) {
            iaKind_ = IA_WATER;
            iaPrompt_ = "E  Drink";
        }
    }
    if (p.carryEgg >= 0 || p.carryBaby >= 0) {
        std::string s = nearCampfire(p.pos, 7.0f) ? "Q  Set it down by the fire" : "Q  Drop   (bring it to a CAMPFIRE)";
        if (iaPrompt_.empty()) iaPrompt_ = s;
        else iaPrompt_ = s + "     " + iaPrompt_;
    }
}

void Game::playerInteract() {
    Player& p = player_;
    switch (iaKind_) {
        case IA_WATER:
            p.water = 100;
            addMessage("You drink deeply.", vec3(0.6f, 0.8f, 1.0f));
            break;
        case IA_STRUCTURE: {
            Structure& s = structures_[iaIdx_];
            if (s.type == I_CAMPFIRE) {
                int n = p.inv[I_RAWMEAT];
                if (n == 0) addMessage("You have no raw meat to cook.", vec3(1.0f, 0.8f, 0.6f));
                else {
                    p.inv[I_RAWMEAT] = 0;
                    p.inv[I_COOKEDMEAT] += n;
                    addMessage("Cooked " + std::to_string(n) + " meat.", vec3(1.0f, 0.8f, 0.5f));
                    spawnParticles(s.pos + vec3(0, 0.8f, 0), 20, vec3(1.0f, 0.6f, 0.2f), 2, 1.0f, 0.12f, 1.0f, -2.0f);
                }
            } else if (s.type == I_CAULDRON) {
                panel_ = PANEL_CAULDRON;
                panelTarget_ = iaIdx_;
                craftSel_ = 0;
            }
            break;
        }
        case IA_EGG: {
            Egg& e = eggs_[iaIdx_];
            bool dragonKin = e.species == S_FIRE_DRAGON || e.species == S_FROST_WYVERN || e.species == S_STORM_DRAKE;
            if (e.state == EGG_INCUBATING && dragonKin && !e.fed) {
                if (p.inv[I_DRAGONBAIT] > 0) {
                    p.inv[I_DRAGONBAIT]--;
                    e.fed = true;
                    addMessage("The egg drinks in the Dragon Bait and begins to glow.", vec3(1.0f, 0.6f, 0.3f));
                } else {
                    addMessage("Dragon-kin eggs need DRAGON BAIT (brew it in a Cauldron).", vec3(1.0f, 0.7f, 0.4f));
                }
                break;
            }
            if (p.carryEgg >= 0 || p.carryBaby >= 0) break;
            e.state = EGG_CARRIED;
            if (e.nest >= 0 && nests_[e.nest].egg == iaIdx_) nests_[e.nest].timer = 0;
            p.carryEgg = iaIdx_;
            aggroFamily(e.species, e.pos, 120.0f);
            addMessage(std::string("You stole a ") + SPECIES[e.species].name + " egg! Get it to a CAMPFIRE before its parents catch you!",
                       vec3(1.0f, 0.75f, 0.3f));
            shake_ = 0.3f;
            break;
        }
        case IA_CREATURE: {
            Creature& c = creatures_[iaIdx_];
            const Species& sp = SPECIES[c.species];
            if (c.state == CS_UNCONSCIOUS && !c.tamed) {
                panel_ = PANEL_TAME;
                panelTarget_ = iaIdx_;
            } else if (c.tamed) {
                c.stayMode = !c.stayMode;
                c.state = c.stayMode ? CS_STAY : CS_FOLLOW;
                addMessage(c.name + (c.stayMode ? " will stay here." : " is following you."), vec3(0.8f, 0.95f, 1.0f));
            } else if (c.baby && c.readyToBond) {
                Item food = I_NONE;
                int need = 0;
                if (sp.diet == D_CARNIVORE) {
                    if (p.inv[I_COOKEDMEAT] >= 1) food = I_COOKEDMEAT, need = 1;
                    else if (p.inv[I_RAWMEAT] >= 2) food = I_RAWMEAT, need = 2;
                } else if (p.inv[I_EMBERBERRY] >= 5) food = I_EMBERBERRY, need = 5;
                if (food == I_NONE) {
                    addMessage(std::string("The baby wants ") + (sp.diet == D_CARNIVORE ? "meat (2 raw or 1 cooked)." : "5 Emberberries."),
                               vec3(1.0f, 0.8f, 0.5f));
                } else {
                    p.inv[food] -= need;
                    c.readyToBond = false;
                    c.parent = -1;
                    tameCreature(iaIdx_);
                }
            } else if (c.baby && p.carryBaby < 0 && p.carryEgg < 0) {
                c.state = CS_CARRIED;
                p.carryBaby = iaIdx_;
                aggroFamily(c.species, c.pos, 80.0f);
                addMessage(std::string("You grabbed a baby ") + sp.name + "! Its family is furious - run to your CAMPFIRE!",
                           vec3(1.0f, 0.75f, 0.3f));
                shake_ = 0.3f;
            }
            break;
        }
        default: break;
    }
}

void Game::dropCarried() {
    Player& p = player_;
    vec3 at = p.pos + dirFromYaw(p.yaw) * 1.2f;
    at.y = terrain_.heightAt(at.x, at.z);
    if (p.carryEgg >= 0) {
        Egg& e = eggs_[p.carryEgg];
        e.pos = at;
        e.nest = -1;
        if (nearCampfire(at, 7.0f)) {
            e.state = EGG_INCUBATING;
            bool dragonKin = e.species == S_FIRE_DRAGON || e.species == S_FROST_WYVERN || e.species == S_STORM_DRAKE;
            if (dragonKin && !e.fed && p.inv[I_DRAGONBAIT] > 0) {
                p.inv[I_DRAGONBAIT]--;
                e.fed = true;
            }
            if (dragonKin && !e.fed) addMessage("The egg is warm but dormant... it needs DRAGON BAIT.", vec3(1.0f, 0.7f, 0.4f));
            else addMessage("The egg begins to warm by the fire. Keep the camp safe!", vec3(1.0f, 0.85f, 0.5f));
        } else {
            e.state = EGG_NEST;
        }
        p.carryEgg = -1;
    }
    if (p.carryBaby >= 0) {
        Creature& c = creatures_[p.carryBaby];
        c.pos = at;
        if (nearCampfire(at, 15.0f)) {
            c.state = CS_STAY;
            c.readyToBond = true;
            addMessage("The baby sniffs the fire... feed it (E) to win its trust!", vec3(1.0f, 0.85f, 0.5f));
        } else {
            c.state = CS_WANDER;
        }
        p.carryBaby = -1;
    }
}

void Game::placeStructure() {
    Player& p = player_;
    Item it = selectedItem();
    if (p.inv[it] <= 0) return;
    vec3 pos = p.pos + dirFromYaw(p.yaw) * 3.5f;
    pos.y = terrain_.heightAt(pos.x, pos.z);
    if (terrain_.isOcean(pos.x, pos.z, -0.2f)) {
        addMessage("Can't build in water.", vec3(1.0f, 0.7f, 0.5f));
        return;
    }
    Structure s;
    s.type = it;
    s.pos = pos;
    s.yaw = p.yaw + p.placeYaw;
    s.hp = it == I_SPIKEWALL ? 700.0f : (it == I_WOODWALL ? 900.0f : 400.0f);
    structures_.push_back(s);
    p.inv[it]--;
    spawnParticles(pos + vec3(0, 0.5f, 0), 12, vec3(0.6f, 0.5f, 0.35f), 2.5f, 0.6f, 0.15f, 0);
    if (it == I_CAMPFIRE && p.inv[I_CAMPFIRE] == 0)
        addMessage("Campfire lit! Cook meat, hatch eggs and bond with babies here.", vec3(1.0f, 0.8f, 0.5f));
}

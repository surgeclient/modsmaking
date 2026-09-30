// Mythical creature AI: wandering, fighting, fleeing, knockouts, taming, eggs & babies.
#include "game.h"

float Game::creatureRadius(const Creature& c) const {
    const Species& sp = SPECIES[c.species];
    float s = sp.size * growthScale(c);
    if (sp.plan == BP_SERPENT) return sp.bodyW * s * 1.6f;
    if (sp.plan == BP_BIRD) return std::max(sp.bodyL, sp.bodyW) * 0.6f * s;
    return std::max(sp.bodyL * 0.55f, sp.bodyW * 0.6f) * s;
}

float Game::creatureHeight(const Creature& c) const {
    const Species& sp = SPECIES[c.species];
    float s = sp.size * growthScale(c);
    if (sp.plan == BP_SERPENT) return sp.bodyH * s * 1.4f;
    return (sp.legL + sp.bodyH) * s;
}

vec3 Game::creatureSeat(const Creature& c) const {
    const Species& sp = SPECIES[c.species];
    float s = sp.size * growthScale(c);
    float y = sp.plan == BP_SERPENT ? sp.bodyH * s * 1.1f : (sp.legL + sp.bodyH * 0.95f) * s;
    return c.pos + vec3(0, y, 0) + dirFromYaw(c.yaw) * (0.05f * sp.bodyL * s);
}

vec3 Game::creatureHead(const Creature& c) const {
    const Species& sp = SPECIES[c.species];
    float s = sp.size * growthScale(c);
    vec3 f = dirFromYaw(c.yaw);
    if (sp.plan == BP_SERPENT) return c.pos + f * (sp.bodyL * 0.1f * s) + vec3(0, sp.bodyH * s * 1.6f, 0);
    float forward = sp.bodyL * 0.5f + sp.neckL * std::cos(sp.neckAngle) + sp.headS * 0.6f;
    float up = sp.legL + sp.bodyH * 0.7f + sp.neckL * std::sin(sp.neckAngle);
    return c.pos + f * (forward * s) + vec3(0, up * s, 0);
}

float Game::foodValue(int species, Item it) const {
    const Species& sp = SPECIES[species];
    if (sp.rarity != R_COMMON) return it == I_MYTHBAIT ? 60.0f : 0.0f;
    switch (it) {
        case I_MYTHBAIT: return 70.0f;
        case I_WILDBAIT: return 50.0f;
        case I_COOKEDMEAT: return sp.diet == D_CARNIVORE ? 16.0f : 0.0f;
        case I_RAWMEAT: return sp.diet == D_CARNIVORE ? 11.0f : 0.0f;
        case I_EMBERBERRY: return sp.diet == D_HERBIVORE ? 7.0f : 0.0f;
        default: return 0.0f;
    }
}

void Game::updateCreatures(float dt) {
    for (int i = 0; i < (int)creatures_.size(); i++)
        if (creatures_[i].alive) updateCreature(i, dt);
}

void Game::moveCreature(Creature& c, vec3 target, float speed, float dt, bool face) {
    const Species& sp = SPECIES[c.species];
    vec3 d = target - c.pos;
    d.y = 0;
    float dist = lengthXZ(d);
    float spd = 0;
    if (dist > 0.4f) {
        float desired = std::atan2(d.x, d.z);
        float turnRate = (c.flying ? 2.2f : 3.5f) * (sp.size > 2.0f ? 0.7f : 1.0f);
        if (face) c.yaw = approachAngle(c.yaw, desired, turnRate * dt);
        float diff = std::fabs(wrapAngle(desired - c.yaw));
        spd = speed * std::max(0.25f, std::cos(std::min(diff, 1.3f)));
        spd = std::min(spd, dist / std::max(dt, 1e-3f));
    }
    vec3 np = c.pos + dirFromYaw(c.yaw) * (spd * dt);
    float ground = terrain_.heightAt(np.x, np.z);
    // Wild land creatures refuse deep water.
    if (!c.flying && !sp.swimmer && !c.tamed && terrain_.isOcean(np.x, np.z, 1.5f) && c.state == CS_WANDER) {
        c.moveTarget = c.home;
        return;
    }
    if (lengthXZ(np) > Terrain::HALF - 30) {
        c.moveTarget = vec3(0, 0, 0);
        return;
    }
    if (c.flying) {
        float desiredY = std::max(ground, 0.0f) + c.altitude;
        float vy = clampf((desiredY - c.pos.y) * 1.5f, -8.0f, 8.0f);
        np.y = c.pos.y + vy * dt;
        np.y = std::max(np.y, ground + 0.5f);
        c.pitch = lerpf(c.pitch, clampf(-vy * 0.06f, -0.5f, 0.5f), dt * 3);
    } else {
        float body = creatureHeight(c);
        np.y = terrain_.isOcean(np.x, np.z, 0.3f) ? std::max(ground, -body * 0.55f) : ground;
        c.pitch = lerpf(c.pitch, 0, dt * 4);
    }
    pushOutOfStructures(np, creatureRadius(c) * 0.8f);
    c.vel = (np - c.pos) / std::max(dt, 1e-4f);
    c.pos = np;
    c.moveAmt = lerpf(c.moveAmt, saturate(spd / std::max(c.speed * 0.5f, 0.1f)), saturate(dt * 6));
}

void Game::aggroFamily(int species, vec3 pos, float radius) {
    for (auto& c : creatures_) {
        if (!c.alive || c.tamed || c.baby || c.species != species) continue;
        if (c.state == CS_UNCONSCIOUS || c.state == CS_DEAD) continue;
        if (distXZ(c.pos, pos) > radius) continue;
        c.state = CS_ATTACK;
        c.target = {TK_PLAYER, 0};
        c.stateTimer = 45;
    }
}

void Game::creatureThink(int i) {
    Creature& c = creatures_[i];
    const Species& sp = SPECIES[c.species];
    if (c.state == CS_UNCONSCIOUS || c.state == CS_DEAD || c.state == CS_RIDDEN || c.state == CS_CARRIED || c.state == CS_SCRIPTED) return;

    // Find the nearest visible member of the tribe.
    int nearInf = -1;
    float nearInfD = 24.0f;
    for (int k = 0; k < (int)infected_.size(); k++) {
        const Infected& e = infected_[k];
        if (!e.alive || e.state == IS_DEAD || e.state == IS_EMERGE) continue;
        float d = distXZ(e.pos, c.pos);
        if (d < nearInfD) {
            nearInfD = d;
            nearInf = k;
        }
    }

    if (c.tamed) {
        if (c.state == CS_ATTACK) {
            if (!targetAlive(c.target) || distXZ(targetPos(c.target), c.pos) > 60) c.state = c.stayMode ? CS_STAY : CS_FOLLOW;
            return;
        }
        if (c.baby && c.growth < 0.6f) return;  // little ones don't fight
        TargetRef la = player_.lastAttacker;
        if (!c.stayMode && la.kind != TK_NONE && totalTime_ - player_.lastAttackerTime < 10 && targetAlive(la) &&
            !(la.kind == TK_CREATURE && creatures_[la.idx].tamed) && distXZ(targetPos(la), c.pos) < 40) {
            c.state = CS_ATTACK;
            c.target = la;
            return;
        }
        if (nearInf >= 0 && nearInfD < (c.stayMode ? 14.0f : 20.0f)) {
            c.state = CS_ATTACK;
            c.target = {TK_INFECTED, nearInf};
        }
        return;
    }

    // Wild.
    if (c.state == CS_ATTACK) {
        if (!targetAlive(c.target) || distXZ(targetPos(c.target), c.pos) > 80 || c.stateTimer <= 0) {
            c.state = CS_WANDER;
            c.target = {};
        }
        return;
    }
    if (c.state == CS_FLEE) return;

    if (nearInf >= 0) {
        if (sp.temper == T_PASSIVE || c.baby || c.hp < c.maxHp * 0.3f) {
            c.state = CS_FLEE;
            c.moveTarget = c.pos + normalize(c.pos - infected_[nearInf].pos) * 40.0f;
            c.stateTimer = 6;
        } else if (nearInfD < 16.0f) {
            c.state = CS_ATTACK;
            c.target = {TK_INFECTED, nearInf};
            c.stateTimer = 30;
        }
        return;
    }

    if (sp.temper == T_AGGRESSIVE && !c.baby) {
        float range = 22.0f * (player_.crouch ? 0.55f : 1.0f);
        if (!player_.dead && distXZ(player_.pos, c.pos) < range) {
            c.state = CS_ATTACK;
            c.target = {TK_PLAYER, 0};
            c.stateTimer = 30;
            return;
        }
        for (int k = 0; k < (int)creatures_.size(); k++) {
            const Creature& o = creatures_[k];
            if (!o.alive || !o.tamed || o.state == CS_DEAD) continue;
            if (distXZ(o.pos, c.pos) < 16.0f) {
                c.state = CS_ATTACK;
                c.target = {TK_CREATURE, k};
                c.stateTimer = 30;
                return;
            }
        }
    }
}

void Game::creatureBreath(int i, vec3 dir) {
    Creature& c = creatures_[i];
    const Species& sp = SPECIES[c.species];
    vec3 head = creatureHead(c);
    int count = sp.size > 1.5f ? 12 : 6;
    for (int k = 0; k < count; k++) {
        Projectile p;
        p.kind = PJ_BREATH;
        p.pos = head;
        vec3 jitter(rng_.range(-0.12f, 0.12f), rng_.range(-0.08f, 0.08f), rng_.range(-0.12f, 0.12f));
        p.vel = normalize(dir + jitter) * rng_.range(22.0f, 30.0f);
        p.life = 0.85f;
        p.dmg = c.dmg * (sp.size > 1.5f ? 0.12f : 0.3f);
        p.torpor = 0;
        p.owner = {TK_CREATURE, i};
        p.color = sp.breathColor;
        projectiles_.push_back(p);
    }
    c.breathTime = 0.9f;
}

void Game::creatureAttack(int i, float dt) {
    Creature& c = creatures_[i];
    const Species& sp = SPECIES[c.species];
    if (!targetAlive(c.target)) {
        c.state = c.tamed ? (c.stayMode ? CS_STAY : CS_FOLLOW) : CS_WANDER;
        return;
    }
    vec3 tp = targetPos(c.target);
    float dist = distXZ(tp, c.pos);
    float reach = creatureRadius(c) + targetRadius(c.target) + 1.4f;
    vec3 toT = tp - c.pos;
    if (sp.breath && dist < 24 && dist > reach * 0.8f && c.breathCd <= 0 && !c.baby) {
        vec3 head = creatureHead(c);
        creatureBreath(i, normalize(tp + vec3(0, 0.8f, 0) - head));
        c.breathCd = 4.5f + rng_.range(0, 2);
    }
    if (c.flying) c.altitude = std::max(1.5f, c.altitude - dt * 6);
    if (dist > reach * 0.9f) {
        moveCreature(c, tp, c.speed, dt);
    } else {
        c.yaw = approachAngle(c.yaw, std::atan2(toT.x, toT.z), 5 * dt);
        c.moveAmt = lerpf(c.moveAmt, 0, dt * 5);
        if (c.attackCd <= 0) {
            damageTarget(c.target, c.dmg, 0, {TK_CREATURE, i});
            c.attackCd = 1.3f;
            c.attackAnim = 1;
        }
    }
    if (!c.tamed) c.stateTimer -= dt;
}

void Game::tameCreature(int i) {
    Creature& c = creatures_[i];
    const Species& sp = SPECIES[c.species];
    int bonus = (int)(c.level * 0.5f * c.tameEff);
    c.level += bonus;
    c.tamed = true;
    c.state = CS_FOLLOW;
    c.stayMode = false;
    c.torpor = 0;
    c.roll = 0;
    c.maxHp *= 1.0f + bonus * 0.03f;
    c.hp = c.maxHp;
    for (auto& f : c.food) f = 0;
    player_.tames++;
    addMessage("TAMED! Level " + std::to_string(c.level) + " " + sp.name + " (+" + std::to_string(bonus) + " bonus levels)", vec3(0.4f, 1.0f, 0.5f));
    if (sp.rideable && !c.baby) addMessage("Press F to ride it (needs a Saddle). E toggles follow / stay.", vec3(0.8f, 0.95f, 1.0f));
    spawnParticles(c.pos + vec3(0, creatureHeight(c), 0), 50, vec3(1.0f, 0.9f, 0.4f), 6, 1.4f, 0.25f, 1.0f, -1.0f);
    if (panel_ == PANEL_TAME && panelTarget_ == i) panel_ = PANEL_NONE;
}

void Game::updateCreature(int i, float dt) {
    Creature& c = creatures_[i];
    const Species& sp = SPECIES[c.species];
    c.attackCd -= dt;
    c.breathCd -= dt;
    c.breathTime -= dt;
    c.hurt = std::max(0.0f, c.hurt - dt * 3);
    c.attackAnim = std::max(0.0f, c.attackAnim - dt * 2.5f);
    c.anim += dt * (1.5f + c.moveAmt * (5.0f + c.speed * 0.35f)) / std::max(0.6f, sp.size * growthScale(c));
    c.flap += dt * (c.flying ? 5.5f : 1.0f);

    if (c.state == CS_DEAD) {
        c.deathTimer -= dt;
        c.roll = lerpf(c.roll, 1.45f, saturate(dt * 3));
        float g = terrain_.heightAt(c.pos.x, c.pos.z);
        if (c.pos.y > g) c.pos.y = std::max(g, c.pos.y - 15 * dt);
        if (c.deathTimer <= 0) c.alive = false;
        return;
    }
    if (c.state == CS_SCRIPTED) return;
    if (c.state == CS_CARRIED) {
        vec3 f = dirFromYaw(player_.yaw);
        c.pos = player_.pos + vec3(0, 1.9f, 0) + f * 0.35f;
        c.yaw = player_.yaw;
        c.moveAmt = 0.2f;
        return;
    }

    if (c.tamed && c.baby) {
        c.growth += dt / 240.0f;
        if (c.growth >= 1.0f) {
            c.growth = 1.0f;
            c.baby = false;
            addMessage("Your " + std::string(sp.name) + " has grown up!", vec3(0.5f, 1.0f, 0.6f));
        }
    }
    if (c.tamed && c.hp < c.maxHp && c.state != CS_ATTACK) c.hp = std::min(c.maxHp, c.hp + c.maxHp * 0.01f * dt);

    // --- Unconscious: taming happens here ---
    if (c.state == CS_UNCONSCIOUS) {
        c.roll = lerpf(c.roll, 1.4f, saturate(dt * 2.5f));
        c.moveAmt = 0;
        c.flying = false;
        float g = terrain_.heightAt(c.pos.x, c.pos.z);
        c.pos.y = std::max(terrain_.isOcean(c.pos.x, c.pos.z, 0.3f) ? -creatureHeight(c) * 0.4f : g, c.pos.y - 18 * dt);
        c.torpor -= c.maxTorpor / 110.0f * dt;
        if (!c.tamed && c.torpor < c.maxTorpor * 0.55f && c.food[I_DREAMBERRY] > 0) {
            c.food[I_DREAMBERRY]--;
            c.torpor = std::min(c.maxTorpor, c.torpor + c.maxTorpor * 0.1f);
        }
        c.eatTimer -= dt;
        if (!c.tamed && c.eatTimer <= 0) {
            c.eatTimer = 2.0f;
            Item best = I_NONE;
            float bestV = 0;
            for (Item it : {I_MYTHBAIT, I_WILDBAIT, I_COOKEDMEAT, I_RAWMEAT, I_EMBERBERRY}) {
                float v = foodValue(c.species, it);
                if (c.food[it] > 0 && v > bestV) {
                    bestV = v;
                    best = it;
                }
            }
            if (best != I_NONE) {
                c.food[best]--;
                c.tameProgress += bestV;
                spawnParticles(creatureHead(c), 6, ITEMS[best].color, 1.5f, 0.6f, 0.12f, 0.3f);
                if (c.tameProgress >= sp.tameDifficulty) tameCreature(i);
            }
        }
        if (c.state == CS_UNCONSCIOUS && c.torpor <= 0) {
            c.torpor = 0;
            c.roll = 0;
            if (c.tamed) {
                c.state = c.stayMode ? CS_STAY : CS_FOLLOW;
            } else {
                c.tameEff *= 0.85f;
                addMessage("The " + std::string(sp.name) + " woke up!", vec3(1.0f, 0.6f, 0.3f));
                if (sp.temper == T_PASSIVE) {
                    c.state = CS_FLEE;
                    c.moveTarget = c.pos + normalize(c.pos - player_.pos) * 40.0f;
                    c.stateTimer = 8;
                } else {
                    c.state = CS_ATTACK;
                    c.target = {TK_PLAYER, 0};
                    c.stateTimer = 30;
                }
                if (panel_ == PANEL_TAME && panelTarget_ == i) panel_ = PANEL_NONE;
            }
        }
        return;
    }
    c.torpor = std::max(0.0f, c.torpor - c.maxTorpor * 0.04f * dt);
    c.roll = lerpf(c.roll, 0, saturate(dt * 4));

    c.thinkTimer -= dt;
    if (c.thinkTimer <= 0) {
        creatureThink(i);
        c.thinkTimer = 0.35f + rng_.range(0, 0.3f);
    }

    switch (c.state) {
        case CS_WANDER: {
            // Babies stay close to a parent.
            if (c.baby && c.parent >= 0 && creatures_[c.parent].alive && creatures_[c.parent].state != CS_DEAD) {
                const Creature& p = creatures_[c.parent];
                if (distXZ(p.pos, c.pos) > 5.0f) {
                    moveCreature(c, p.pos, c.speed * 0.6f, dt);
                    break;
                }
            }
            c.stateTimer -= dt;
            float d = distXZ(c.moveTarget, c.pos);
            if (d < 2.0f || c.stateTimer <= 0) {
                if (c.stateTimer <= 0) {
                    float r = sp.flyer ? 90.0f : 35.0f;
                    vec3 t = c.home + vec3(rng_.range(-r, r), 0, rng_.range(-r, r));
                    float h = terrain_.heightAt(t.x, t.z);
                    if ((h > (sp.swimmer ? -6.0f : 1.0f) && lengthXZ(t) > HOLLOW_RIM + 10) || sp.flyer) c.moveTarget = t;
                    c.stateTimer = rng_.range(6, 14);
                    if (sp.flyer && !c.baby) {
                        c.flying = rng_.chance(0.45f);
                        c.altitude = rng_.range(18, 45);
                    }
                } else {
                    c.moveTarget = c.pos;  // idle a moment
                }
            }
            float spd = c.speed * (c.flying ? 0.75f : 0.3f);
            if (c.flying && !sp.flyer) c.flying = false;
            if (c.flying && c.stateTimer < 1.5f) c.altitude = 0;  // come in to land
            if (c.flying && c.altitude <= 0 && c.pos.y - terrain_.heightAt(c.pos.x, c.pos.z) < 1.0f) c.flying = false;
            moveCreature(c, c.moveTarget, spd, dt);
            break;
        }
        case CS_FLEE:
            c.stateTimer -= dt;
            moveCreature(c, c.moveTarget, c.speed * 1.1f, dt);
            if (c.stateTimer <= 0) c.state = CS_WANDER;
            break;
        case CS_ATTACK:
            creatureAttack(i, dt);
            break;
        case CS_FOLLOW: {
            vec3 goal = player_.riding >= 0 ? creatures_[player_.riding].pos : player_.pos;
            // Spread followers out a little so they don't stack up.
            vec3 off = vec3(std::sin(i * 2.4f), 0, std::cos(i * 2.4f)) * (3.0f + creatureRadius(c));
            goal = goal + off;
            float d = distXZ(goal, c.pos);
            bool airborne = player_.riding >= 0 && creatures_[player_.riding].flying;
            if (sp.flyer && !c.baby && (airborne || d > 60)) {
                c.flying = true;
                c.altitude = airborne ? std::max(3.0f, creatures_[player_.riding].pos.y - terrain_.heightAt(c.pos.x, c.pos.z)) : 20.0f;
            } else if (c.flying && d < 8) {
                c.altitude = 0;
                if (c.pos.y - terrain_.heightAt(c.pos.x, c.pos.z) < 1.0f) c.flying = false;
            }
            if (d > 2.5f) moveCreature(c, goal, c.speed * (d > 18 ? 1.3f : 0.75f), dt);
            else c.moveAmt = lerpf(c.moveAmt, 0, dt * 4);
            if (d > 250) {  // hopelessly lost: catch up
                c.pos = goal;
                c.pos.y = terrain_.heightAt(goal.x, goal.z);
            }
            break;
        }
        case CS_STAY:
            c.moveAmt = lerpf(c.moveAmt, 0, dt * 4);
            if (c.flying) {
                c.altitude = 0;
                moveCreature(c, c.pos, 0, dt);
                if (c.pos.y - terrain_.heightAt(c.pos.x, c.pos.z) < 1.0f) c.flying = false;
            }
            break;
        case CS_RIDDEN:
            break;
        default:
            break;
    }
}

// ---------------------------------------------------------------------------
void Game::updateEggsAndNests(float dt) {
    for (int n = 0; n < (int)nests_.size(); n++) {
        Nest& nest = nests_[n];
        bool hasEgg = nest.egg >= 0 && eggs_[nest.egg].alive && eggs_[nest.egg].state == EGG_NEST && eggs_[nest.egg].nest == n;
        if (hasEgg) continue;
        nest.timer += dt;
        if (nest.timer > 150.0f) {
            nest.timer = 0;
            Egg e;
            e.species = nest.species;
            e.pos = nest.pos;
            e.nest = n;
            eggs_.push_back(e);
            nest.egg = (int)eggs_.size() - 1;
        }
    }
    for (int i = 0; i < (int)eggs_.size(); i++) {
        Egg& e = eggs_[i];
        if (!e.alive) continue;
        if (e.state == EGG_CARRIED) {
            e.pos = player_.pos + vec3(0, 1.1f, 0) + dirFromYaw(player_.yaw) * 0.6f;
        } else if (e.state == EGG_INCUBATING) {
            e.pos.y = terrain_.heightAt(e.pos.x, e.pos.z);
            bool dragonKin = e.species == S_FIRE_DRAGON || e.species == S_FROST_WYVERN || e.species == S_STORM_DRAKE;
            if (nearCampfire(e.pos, 7.0f) && (e.fed || !dragonKin)) {
                e.progress += dt / 45.0f;
                if (rng_.chance(dt * 4)) spawnParticles(e.pos + vec3(0, 0.5f, 0), 1, vec3(1.0f, 0.8f, 0.4f), 0.5f, 1.0f, 0.08f, 1.0f, -0.5f);
            }
            if (e.progress >= 1.0f) {
                e.alive = false;
                int b = spawnCreature(e.species, e.pos, true);
                Creature& c = creatures_[b];
                c.tamed = true;
                c.state = CS_FOLLOW;
                c.level = rng_.irange(5, 20);
                player_.tames++;
                spawnParticles(e.pos + vec3(0, 0.6f, 0), 40, SPECIES[e.species].c3, 4, 1.2f, 0.18f, 1.0f);
                addMessage("A baby " + std::string(SPECIES[e.species].name) + " hatched and imprinted on you!", vec3(1.0f, 0.85f, 0.4f));
            }
        }
    }
}

void Game::respawnWildlife(float dt) {
    respawnTimer_ -= dt;
    if (respawnTimer_ > 0) return;
    respawnTimer_ = 12.0f;
    int counts[S_COUNT] = {}, babies[S_COUNT] = {};
    for (auto& c : creatures_)
        if (c.alive && !c.tamed && c.state != CS_DEAD) (c.baby ? babies : counts)[c.species]++;
    for (int s = 0; s < S_COUNT; s++) {
        const Species& sp = SPECIES[s];
        if (counts[s] >= sp.spawnCount) continue;
        vec3 p = terrain_.randomLand(rng_, sp.home[rng_.irange(0, 2)], sp.swimmer ? 0.3f : 1.5f);
        if (distXZ(p, player_.pos) < 150.0f) continue;
        int a = spawnCreature(s, p, false);
        if (sp.tame == TM_BABY && babies[s] < sp.spawnCount / 2) {
            int b = spawnCreature(s, p + vec3(2, 0, 2), true);
            creatures_[b].parent = a;
        }
        return;  // one per tick keeps it cheap
    }
}

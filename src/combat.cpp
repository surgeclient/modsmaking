// Damage, knockouts, projectiles, particles and collision helpers.
#include "game.h"

static bool friendlyToPlayer(const std::vector<Creature>& cs, TargetRef t) {
    return t.kind == TK_PLAYER || t.kind == TK_STRUCTURE || (t.kind == TK_CREATURE && t.idx >= 0 && cs[t.idx].tamed);
}

bool Game::targetAlive(TargetRef t) const {
    switch (t.kind) {
        case TK_PLAYER: return !player_.dead;
        case TK_CREATURE: {
            const Creature& c = creatures_[t.idx];
            return c.alive && c.state != CS_DEAD && c.state != CS_CARRIED;
        }
        case TK_INFECTED: {
            const Infected& e = infected_[t.idx];
            return e.alive && e.state != IS_DEAD;
        }
        case TK_STRUCTURE: return structures_[t.idx].alive;
        default: return false;
    }
}

vec3 Game::targetPos(TargetRef t) const {
    switch (t.kind) {
        case TK_PLAYER: return player_.riding >= 0 ? creatures_[player_.riding].pos : player_.pos;
        case TK_CREATURE: return creatures_[t.idx].pos;
        case TK_INFECTED: return infected_[t.idx].pos;
        case TK_STRUCTURE: return structures_[t.idx].pos;
        default: return {};
    }
}

float Game::targetRadius(TargetRef t) const {
    switch (t.kind) {
        case TK_PLAYER: return player_.riding >= 0 ? creatureRadius(creatures_[player_.riding]) : 0.5f;
        case TK_CREATURE: return creatureRadius(creatures_[t.idx]);
        case TK_INFECTED: return 0.5f * INFECTED[infected_[t.idx].cls].scale;
        case TK_STRUCTURE: return 1.0f;
        default: return 0.5f;
    }
}

void Game::damageTarget(TargetRef t, float dmg, float torpor, TargetRef attacker) {
    switch (t.kind) {
        case TK_PLAYER: {
            std::string cause = "the wilds";
            if (attacker.kind == TK_CREATURE) cause = std::string("a ") + SPECIES[creatures_[attacker.idx].species].name;
            if (attacker.kind == TK_INFECTED) cause = INFECTED[infected_[attacker.idx].cls].name;
            damagePlayer(dmg, attacker, cause);
            break;
        }
        case TK_CREATURE: damageCreature(t.idx, dmg, torpor, attacker); break;
        case TK_INFECTED: damageInfected(t.idx, dmg, attacker); break;
        case TK_STRUCTURE: {
            Structure& s = structures_[t.idx];
            s.hp -= dmg;
            if (s.hp <= 0 && s.alive) {
                s.alive = false;
                addMessage(std::string("Your ") + ITEMS[s.type].name + " was destroyed!", vec3(1.0f, 0.5f, 0.4f));
            }
            break;
        }
        default: break;
    }
}

void Game::damageCreature(int i, float dmg, float torpor, TargetRef attacker) {
    Creature& c = creatures_[i];
    const Species& sp = SPECIES[c.species];
    if (!c.alive || c.state == CS_DEAD || c.state == CS_CARRIED || c.state == CS_SCRIPTED) return;
    bool byPlayerSide = friendlyToPlayer(creatures_, attacker);
    if (c.tamed && byPlayerSide) return;  // no friendly fire

    c.hp -= dmg;
    c.hurt = 1;
    vec3 center = c.pos + vec3(0, creatureHeight(c) * 0.6f, 0);
    vec3 bloodCol = sp.glow > 0.9f ? sp.c3 : vec3(0.55f, 0.05f, 0.05f);
    spawnParticles(center, 6 + (int)(dmg * 0.2f), bloodCol, 3.5f, 0.6f, 0.12f, sp.glow > 0.9f ? 0.8f : 0.0f);

    if (c.state == CS_UNCONSCIOUS && !c.tamed && dmg > 1.0f) c.tameEff = std::max(0.3f, c.tameEff - 0.05f);

    if (!c.tamed && torpor > 0 && c.state != CS_UNCONSCIOUS) {
        c.torpor += torpor;
        if (c.torpor >= c.maxTorpor && c.hp > 0) {
            c.torpor = c.maxTorpor;
            c.state = CS_UNCONSCIOUS;
            c.eatTimer = 2;
            c.flying = false;
            if (sp.tame == TM_KNOCKOUT)
                addMessage("Knocked out a Lv " + std::to_string(c.level) + " " + sp.name + "! Press E to feed it.", vec3(0.6f, 0.85f, 1.0f));
            else if (sp.tame == TM_EGG)
                addMessage(std::string(sp.name) + " can't be tamed this way - steal an EGG from its nest.", vec3(1.0f, 0.8f, 0.5f));
            else
                addMessage(std::string(sp.name) + " can't be tamed this way - grab a BABY instead.", vec3(1.0f, 0.8f, 0.5f));
        }
    }

    if (c.hp <= 0) {
        c.hp = 0;
        c.state = CS_DEAD;
        c.deathTimer = 90.0f;
        c.corpseHits = (int)(4 + sp.size * 4);
        c.flying = false;
        if (c.tamed) addMessage("Your " + c.name + " has died!", vec3(1.0f, 0.3f, 0.3f));
        if (player_.riding == i) player_.riding = -1;
        if (panel_ == PANEL_TAME && panelTarget_ == i) panel_ = PANEL_NONE;
        return;
    }

    // Reaction.
    if (!c.tamed && c.state != CS_UNCONSCIOUS && attacker.kind != TK_NONE && attacker.kind != TK_STRUCTURE) {
        if (sp.temper == T_PASSIVE || c.baby) {
            c.state = CS_FLEE;
            c.moveTarget = c.pos + normalize(c.pos - targetPos(attacker)) * 45.0f;
            c.stateTimer = 8;
        } else {
            c.state = CS_ATTACK;
            c.target = attacker;
            c.stateTimer = 30;
        }
        if (c.baby && c.parent >= 0) {
            Creature& p = creatures_[c.parent];
            if (p.alive && p.state != CS_DEAD && p.state != CS_UNCONSCIOUS && !p.tamed) {
                p.state = CS_ATTACK;
                p.target = attacker;
                p.stateTimer = 30;
            }
        }
    } else if (c.tamed && c.state != CS_RIDDEN && c.state != CS_ATTACK && attacker.kind != TK_NONE && targetAlive(attacker)) {
        c.state = CS_ATTACK;
        c.target = attacker;
    }
}

void Game::damageInfected(int i, float dmg, TargetRef attacker) {
    Infected& e = infected_[i];
    if (!e.alive || e.state == IS_DEAD) return;
    if (attacker.kind == TK_INFECTED) return;
    e.hp -= dmg;
    e.hurt = 1;
    spawnParticles(e.pos + vec3(0, 1.2f * INFECTED[e.cls].scale, 0), 5 + (int)(dmg * 0.2f), vec3(0.15f, 0.02f, 0.05f), 3.0f, 0.6f, 0.12f, 0.0f);
    if (e.hp <= 0) {
        e.state = IS_DEAD;
        e.deathTimer = 45.0f;
        e.corpseHits = e.cls == IC_CHIEFTAIN ? 8 : 4;
        if (friendlyToPlayer(creatures_, attacker)) player_.kills++;
        if (e.cls == IC_CHIEFTAIN) addMessage("A Hollow Chieftain has fallen! Harvest its corpse for a Hollow Heart.", vec3(1.0f, 0.85f, 0.3f));
        return;
    }
    if (attacker.kind != TK_NONE && attacker.kind != TK_STRUCTURE && e.state != IS_EMERGE && targetAlive(attacker)) {
        if (!(e.state == IS_CHASE && targetAlive(e.target))) {
            e.state = IS_CHASE;
            e.target = attacker;
            e.lastKnown = targetPos(attacker);
            e.lastSeen = totalTime_;
        }
    }
}

void Game::damagePlayer(float dmg, TargetRef attacker, const std::string& cause) {
    if (player_.dead) return;
    if (player_.riding >= 0) {
        // The mount takes most of the hit.
        int r = player_.riding;
        damageCreature(r, dmg * 0.8f, 0, attacker);
        dmg *= 0.2f;
    }
    player_.hp -= dmg;
    player_.hurtFlash = 1.0f;
    if (attacker.kind != TK_NONE) {
        player_.lastAttacker = attacker;
        player_.lastAttackerTime = totalTime_;
    }
    spawnParticles(player_.pos + vec3(0, 1.2f, 0), 6, vec3(0.6f, 0.05f, 0.05f), 3, 0.5f, 0.1f, 0);
    shake_ = std::max(shake_, 0.2f);
    if (player_.hp <= 0) playerDie(cause);
}

void Game::playerDie(const std::string& cause) {
    player_.hp = 0;
    player_.dead = true;
    player_.respawnTimer = 6.0f;
    addMessage("YOU DIED - killed by " + cause, vec3(1.0f, 0.2f, 0.2f));
    if (player_.riding >= 0) {
        creatures_[player_.riding].state = CS_STAY;
        creatures_[player_.riding].stayMode = true;
        player_.riding = -1;
    }
    dropCarried();
    // Lose half of every raw resource and food.
    for (int it = 1; it < I_COUNT; it++)
        if (ITEMS[it].kind == IK_RESOURCE || ITEMS[it].kind == IK_FOOD) player_.inv[it] /= 2;
    panel_ = PANEL_NONE;
}

// ---------------------------------------------------------------------------
void Game::spawnParticles(vec3 pos, int count, vec3 color, float speed, float life, float size, float emissive, float gravity) {
    for (int i = 0; i < count && particles_.size() < 4000; i++) {
        Particle p;
        p.pos = pos;
        p.vel = normalize(vec3(rng_.range(-1, 1), rng_.range(-0.2f, 1.2f), rng_.range(-1, 1))) * (speed * rng_.range(0.3f, 1.0f));
        p.color = color * rng_.range(0.8f, 1.1f);
        p.life = p.maxLife = life * rng_.range(0.6f, 1.2f);
        p.size = size * rng_.range(0.7f, 1.3f);
        p.emissive = emissive;
        p.gravity = gravity;
        particles_.push_back(p);
    }
}

void Game::updateParticles(float dt) {
    for (size_t i = 0; i < particles_.size();) {
        Particle& p = particles_[i];
        p.life -= dt;
        if (p.life <= 0) {
            p = particles_.back();
            particles_.pop_back();
            continue;
        }
        p.vel.y -= p.gravity * dt;
        p.vel *= (1.0f - 1.5f * dt);
        p.pos += p.vel * dt;
        i++;
    }
}

void Game::updateProjectiles(float dt) {
    for (int pi = 0; pi < (int)projectiles_.size(); pi++) {
        Projectile& p = projectiles_[pi];
        if (!p.alive) continue;
        p.life -= dt;
        if (p.life <= 0) {
            p.alive = false;
            continue;
        }
        if (p.gravity) p.vel.y -= 7.0f * dt;
        vec3 np = p.pos + p.vel * dt;
        if (p.kind == PJ_BREATH || p.kind == PJ_BOLT) {
            if (rng_.chance(0.6f)) spawnParticles(np, 1, p.color, 0.8f, 0.35f, p.kind == PJ_BREATH ? 0.35f : 0.18f, 1.2f, -1.0f);
        }
        if (np.y < terrain_.heightAt(np.x, np.z)) {
            p.alive = false;
            spawnParticles(np, 4, p.kind == PJ_ARROW || p.kind == PJ_DART ? vec3(0.45f, 0.4f, 0.3f) : p.color, 2, 0.4f, 0.1f,
                           p.kind >= PJ_BOLT ? 1.0f : 0.0f);
            continue;
        }
        bool ownerFriendly = friendlyToPlayer(creatures_, p.owner);
        TargetRef hit;
        // Creatures.
        for (int k = 0; k < (int)creatures_.size() && hit.kind == TK_NONE; k++) {
            const Creature& c = creatures_[k];
            if (!c.alive || c.state == CS_DEAD || c.state == CS_CARRIED) continue;
            if (p.owner.kind == TK_CREATURE && p.owner.idx == k) continue;
            if (ownerFriendly && c.tamed) continue;
            if (p.owner.kind == TK_CREATURE && creatures_[p.owner.idx].species == c.species && !c.tamed) continue;
            float r = creatureRadius(c) * 0.9f + 0.3f;
            vec3 center = c.pos + vec3(0, creatureHeight(c) * 0.55f, 0);
            if (length2(np - center) < r * r) hit = {TK_CREATURE, k};
        }
        // Tribe.
        if (hit.kind == TK_NONE && p.owner.kind != TK_INFECTED)
            for (int k = 0; k < (int)infected_.size(); k++) {
                const Infected& e = infected_[k];
                if (!e.alive || e.state == IS_DEAD) continue;
                float s = INFECTED[e.cls].scale;
                vec3 center = e.pos + vec3(0, 1.1f * s, 0);
                if (std::fabs(np.y - center.y) < 1.1f * s && distXZ(np, center) < 0.6f * s + 0.2f) {
                    hit = {TK_INFECTED, k};
                    break;
                }
            }
        // Player.
        if (hit.kind == TK_NONE && !ownerFriendly && !player_.dead && player_.riding < 0) {
            vec3 center = player_.pos + vec3(0, 0.9f, 0);
            if (std::fabs(np.y - center.y) < 1.0f && distXZ(np, center) < 0.6f) hit = {TK_PLAYER, 0};
        }
        if (hit.kind != TK_NONE) {
            damageTarget(hit, p.dmg, p.torpor, p.owner);
            p.alive = false;
            if (p.kind == PJ_BOLT) spawnParticles(np, 10, p.color, 3, 0.5f, 0.15f, 1.0f);
            continue;
        }
        p.pos = np;
    }
    // Compact.
    size_t w = 0;
    for (size_t r = 0; r < projectiles_.size(); r++)
        if (projectiles_[r].alive) projectiles_[w++] = projectiles_[r];
    projectiles_.resize(w);
}

// ---------------------------------------------------------------------------
void Game::pushOutOfStructures(vec3& p, float radius, int* hitStructure) {
    for (int i = 0; i < (int)structures_.size(); i++) {
        const Structure& s = structures_[i];
        if (!s.alive) continue;
        vec3 d = p - s.pos;
        if (std::fabs(d.x) > 6 || std::fabs(d.z) > 6) continue;
        if (s.type == I_WOODWALL || s.type == I_SPIKEWALL) {
            vec3 right(std::cos(s.yaw), 0, -std::sin(s.yaw));
            vec3 fwd = dirFromYaw(s.yaw);
            float lx = dot(d, right), lz = dot(d, fwd);
            float hx = 2.0f + radius, hz = 0.3f + radius;
            if (std::fabs(lx) < hx && std::fabs(lz) < hz && p.y < s.pos.y + 3.2f) {
                float px = hx - std::fabs(lx), pz = hz - std::fabs(lz);
                if (pz < px) p += fwd * (lz >= 0 ? pz : -pz);
                else p += right * (lx >= 0 ? px : -px);
                if (hitStructure) *hitStructure = i;
            }
        } else {
            float r = 0.9f + radius;
            float dd = lengthXZ(d);
            if (dd < r && dd > 1e-4f) {
                p += vec3(d.x, 0, d.z) * ((r - dd) / dd);
                if (hitStructure) *hitStructure = i;
            }
        }
    }
}

void Game::pushOutOfProps(vec3& p, float radius) {
    props_.query(p, radius + 3.0f, scratch_);
    for (int idx : scratch_) {
        const Prop& pr = props_.props[idx];
        if (pr.respawn > 0) continue;
        float r = PropField::collisionRadius(pr);
        if (r <= 0) continue;
        vec3 d = p - pr.pos;
        d.y = 0;
        float dd = lengthXZ(d);
        float rr = r + radius;
        if (dd < rr && dd > 1e-4f) p += d * ((rr - dd) / dd);
    }
}

bool Game::nearCampfire(vec3 p, float radius) const {
    for (const auto& s : structures_)
        if (s.alive && s.type == I_CAMPFIRE && distXZ(s.pos, p) < radius) return true;
    return false;
}

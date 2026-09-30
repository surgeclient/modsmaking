// The Infected Tribe: war bands crawl out of the Hollow at night, roam the island in
// formation and kill anything they SEE. They never know where you are unless they spot
// you - hide, crouch, swim or outrun them. At sunrise they flee back underground or burn.
#include "game.h"

vec3 Game::formationSlot(const Band& b, int slot) const {
    if (b.leader < 0 || slot == 0) return b.waypoint;
    const Infected& l = infected_[b.leader];
    int row = (slot + 1) / 2;
    float side = (slot % 2) ? 1.0f : -1.0f;
    vec3 local(side * 2.4f * row, 0, -2.6f * row);
    float c = std::cos(l.yaw), s = std::sin(l.yaw);
    vec3 world(local.x * c + local.z * s, 0, -local.x * s + local.z * c);
    return l.pos + world;
}

int Game::spawnInfected(int cls, vec3 pos, int band, int slot, bool trailer) {
    const InfectedInfo& info = INFECTED[cls];
    Infected e;
    e.cls = cls;
    e.pos = pos;
    e.band = band;
    e.slot = slot;
    float nightScale = 1.0f + std::max(0, night_ - 1) * 0.1f;  // every night they grow stronger
    e.maxHp = info.hp * nightScale;
    e.hp = e.maxHp;
    e.dmg = info.dmg * nightScale;
    e.speed = info.speed;
    e.state = IS_EMERGE;
    e.stateTimer = 2.5f + slot * 0.5f;
    e.yaw = std::atan2(pos.x, pos.z);  // face outwards from the pit
    e.thinkTimer = rng_.range(0, 0.3f);
    e.anim = rng_.range(0, 5);
    e.trailer = trailer;
    for (int i = 0; i < (int)infected_.size(); i++)
        if (!infected_[i].alive) {
            infected_[i] = e;
            return i;
        }
    infected_.push_back(e);
    return (int)infected_.size() - 1;
}

void Game::spawnBand(int nightNum, bool trailer) {
    Band b;
    b.active = true;
    b.waypoint = terrain_.randomLand(rng_, B_COUNT, 2.0f);
    int bandIdx = -1;
    for (int i = 0; i < (int)bands_.size(); i++)
        if (!bands_[i].active) bandIdx = i;
    if (bandIdx < 0) {
        bands_.push_back(b);
        bandIdx = (int)bands_.size() - 1;
    } else {
        bands_[bandIdx] = b;
    }

    int size = 3 + std::min(nightNum, 5) + rng_.irange(0, 1);
    float ang = forcedSpawnAngle_ > -50.0f ? forcedSpawnAngle_ + rng_.range(-0.5f, 0.5f) : rng_.range(0, TAU);
    for (int s = 0; s < size; s++) {
        int cls;
        if (s == 0) cls = (nightNum >= 2 && rng_.chance(0.5f)) || trailer ? IC_CHIEFTAIN : IC_WARRIOR;
        else {
            float r = rng_.f();
            if (r < 0.45f) cls = IC_WARRIOR;
            else if (r < 0.65f) cls = IC_STALKER;
            else if (r < 0.82f) cls = IC_SHAMAN;
            else cls = nightNum >= 2 || trailer ? IC_BRUTE : IC_WARRIOR;
        }
        float a = ang + rng_.range(-0.6f, 0.6f);
        float r = trailer ? rng_.range(3.0f, 10.0f) : rng_.range(4.0f, HOLLOW_PIT - 6.0f);
        vec3 p(std::sin(a) * r, 0, std::cos(a) * r);
        p.y = terrain_.heightAt(p.x, p.z) - 2.5f;
        int idx = spawnInfected(cls, p, bandIdx, s, trailer);
        if (s == 0) bands_[bandIdx].leader = idx;
    }
}

bool Game::infectedSees(const Infected& e, vec3 p, float range) const {
    vec3 eye = e.pos + vec3(0, 1.7f * INFECTED[e.cls].scale, 0);
    vec3 d = p - eye;
    float dist = length(d);
    if (dist > range) return false;
    if (dist > 9.0f) {
        vec3 f = dirFromYaw(e.yaw);
        float cosA = dot(normalize(vec3(d.x, 0, d.z)), f);
        if (cosA < 0.26f) return false;  // ~150 degree field of view
    }
    return terrain_.lineOfSight(eye, p);
}

void Game::updateTribe(float dt) {
    bool nightWindow = time_ >= 0.77f || time_ < 0.19f;
    if (nightActive_ && bandsToSpawn_ > 0 && nightWindow) {
        tribeSpawnTimer_ -= dt;
        if (tribeSpawnTimer_ <= 0) {
            spawnBand(night_);
            bandsToSpawn_--;
            tribeSpawnTimer_ = 14.0f;
            shake_ = std::max(shake_, 0.25f);
        }
    }
    for (int i = 0; i < (int)infected_.size(); i++)
        if (infected_[i].alive) updateInfected(i, dt);

    // Keep every band led by someone.
    for (int b = 0; b < (int)bands_.size(); b++) {
        Band& band = bands_[b];
        if (!band.active) continue;
        bool leaderOk = band.leader >= 0 && infected_[band.leader].alive && infected_[band.leader].state != IS_DEAD &&
                        infected_[band.leader].band == b;
        if (leaderOk) continue;
        band.leader = -1;
        for (int i = 0; i < (int)infected_.size(); i++) {
            Infected& e = infected_[i];
            if (e.alive && e.state != IS_DEAD && e.band == b) {
                band.leader = i;
                e.slot = 0;
                break;
            }
        }
        if (band.leader < 0) band.active = false;
    }
}

static void moveInfected(Infected& e, vec3 target, float speed, float dt, const Terrain& t) {
    vec3 d = target - e.pos;
    d.y = 0;
    float dist = lengthXZ(d);
    if (dist < 0.3f) {
        e.anim += dt * 0.5f;
        return;
    }
    e.yaw = approachAngle(e.yaw, std::atan2(d.x, d.z), 6.0f * dt);
    float step = std::min(speed * dt, dist);
    vec3 np = e.pos + dirFromYaw(e.yaw) * step;
    float g = t.heightAt(np.x, np.z);
    if (t.isOcean(np.x, np.z, 1.0f)) return;  // the tribe cannot swim
    np.y = g;
    e.pos = np;
    e.anim += dt * speed * 1.6f;
}

void Game::updateInfected(int i, float dt) {
    Infected& e = infected_[i];
    const InfectedInfo& info = INFECTED[e.cls];
    e.attackCd -= dt;
    e.hurt = std::max(0.0f, e.hurt - dt * 3);
    e.swing = std::max(0.0f, e.swing - dt * 2.2f);

    if (e.state == IS_DEAD) {
        e.deathTimer -= dt;
        if (e.deathTimer <= 0) e.alive = false;
        return;
    }

    // Sunlight burns them.
    if (!e.trailer && time_ >= 0.26f && time_ < 0.75f && e.state != IS_EMERGE) {
        e.burn += dt;
        e.hp -= e.maxHp * 0.22f * dt;
        if (rng_.chance(dt * 25)) spawnParticles(e.pos + vec3(0, 1.2f, 0), 2, vec3(1.0f, 0.5f, 0.1f), 2.0f, 0.8f, 0.2f, 1.0f, -3.0f);
        if (e.hp <= 0) {
            e.state = IS_DEAD;
            e.deathTimer = 4.0f;
            spawnParticles(e.pos + vec3(0, 1.0f, 0), 30, vec3(0.2f, 0.2f, 0.2f), 3.0f, 2.0f, 0.4f, 0.0f, -1.5f);
            return;
        }
    }

    if (e.state == IS_EMERGE) {
        e.stateTimer -= dt;
        float g = terrain_.heightAt(e.pos.x, e.pos.z);
        e.pos.y = g - 2.5f * saturate(e.stateTimer / 2.5f);
        e.anim += dt * 3;
        if (rng_.chance(dt * 12))
            spawnParticles(vec3(e.pos.x, g + 0.2f, e.pos.z), 3, vec3(0.5f, 0.05f, 0.05f), 2.0f, 1.2f, 0.25f, 0.6f, -1.0f);
        if (e.stateTimer <= 0) e.state = IS_FORMATION;
        return;
    }
    if (e.band < 0 || e.band >= (int)bands_.size()) {
        e.alive = false;
        return;
    }
    Band& b = bands_[e.band];
    if (e.trailer) {
        // Trailer extras just march out of the pit.
        if (b.leader == i) moveInfected(e, b.waypoint, e.speed * 0.5f, dt, terrain_);
        else moveInfected(e, formationSlot(b, e.slot), e.speed * 0.6f, dt, terrain_);
        return;
    }

    // --- Perception ---
    e.thinkTimer -= dt;
    bool canSeeTarget = false;
    if (e.thinkTimer <= 0) {
        e.thinkTimer = 0.25f + rng_.range(0, 0.15f);
        if (e.state == IS_CHASE && targetAlive(e.target)) {
            vec3 tp = targetPos(e.target) + vec3(0, 1.0f, 0);
            if (infectedSees(e, tp, info.sight * 1.7f) || distXZ(tp, e.pos) < 6) {
                e.lastKnown = tp;
                e.lastSeen = totalTime_;
                canSeeTarget = true;
            }
        } else if (e.state == IS_FORMATION && !b.returning) {
            TargetRef best;
            float bestD = 1e9f;
            if (!player_.dead) {
                float range = info.sight;
                if (player_.crouch) range *= 0.55f;
                bool lit = selectedItem() == I_TORCH || nearCampfire(player_.pos, 12.0f);
                if (!lit) range *= 0.75f;
                if (player_.riding >= 0) range *= 1.25f;
                vec3 pp = player_.pos + vec3(0, player_.crouch ? 0.8f : 1.5f, 0);
                if (infectedSees(e, pp, range)) {
                    best = {TK_PLAYER, 0};
                    bestD = distXZ(pp, e.pos);
                }
            }
            for (int k = 0; k < (int)creatures_.size(); k++) {
                const Creature& c = creatures_[k];
                if (!c.alive || c.state == CS_DEAD || c.state == CS_CARRIED || c.state == CS_SCRIPTED) continue;
                float d = distXZ(c.pos, e.pos);
                float range = info.sight * 0.85f + creatureRadius(c) * 3.0f;
                if (d >= bestD || d > range) continue;
                if (infectedSees(e, c.pos + vec3(0, creatureHeight(c) * 0.6f, 0), range)) {
                    best = {TK_CREATURE, k};
                    bestD = d;
                }
            }
            if (best.kind != TK_NONE) {
                e.state = IS_CHASE;
                e.target = best;
                e.lastKnown = targetPos(best);
                e.lastSeen = totalTime_;
                e.swing = 0.6f;
                // War cry: the rest of the band joins in.
                for (auto& o : infected_) {
                    if (!o.alive || o.band != e.band || o.state != IS_FORMATION) continue;
                    if (distXZ(o.pos, e.pos) > 40) continue;
                    o.state = IS_CHASE;
                    o.target = best;
                    o.lastKnown = e.lastKnown;
                    o.lastSeen = totalTime_;
                }
                if (best.kind == TK_PLAYER) {
                    addMessage("A war band has SPOTTED you! Run, hide, or fight!", vec3(1.0f, 0.3f, 0.25f));
                    shake_ = std::max(shake_, 0.3f);
                }
            }
        }
    }

    switch (e.state) {
        case IS_FORMATION: {
            float spd = e.speed * 0.5f;
            if (b.leader == i) {
                if (b.returning) b.waypoint = vec3(0, 0, 0);
                float d = distXZ(e.pos, b.waypoint);
                if (b.returning && lengthXZ(e.pos) < HOLLOW_PIT) {
                    e.state = IS_RETURN;
                    break;
                }
                if (d < 6.0f) {
                    b.waitTimer -= dt;
                    if (b.waitTimer <= 0) {
                        b.waypoint = terrain_.randomLand(rng_, B_COUNT, 2.0f);
                        b.waitTimer = rng_.range(3, 8);
                    }
                } else {
                    moveInfected(e, b.waypoint, spd, dt, terrain_);
                }
            } else {
                if (b.returning && lengthXZ(e.pos) < HOLLOW_PIT) {
                    e.state = IS_RETURN;
                    break;
                }
                vec3 slot = formationSlot(b, e.slot);
                float d = distXZ(slot, e.pos);
                moveInfected(e, slot, d > 8 ? e.speed : spd * 1.1f, dt, terrain_);
            }
            break;
        }
        case IS_CHASE: {
            if (!targetAlive(e.target) || b.returning) {
                e.state = IS_FORMATION;
                e.target = {};
                break;
            }
            vec3 tp = targetPos(e.target);
            float dist = distXZ(tp, e.pos);
            if (dist > info.sight * 1.8f) {
                e.state = IS_FORMATION;
                break;
            }
            bool fresh = totalTime_ - e.lastSeen < 5.0f;
            vec3 goal = fresh ? tp : e.lastKnown;
            if (!fresh && distXZ(e.lastKnown, e.pos) < 3.0f) {
                e.state = IS_FORMATION;  // lost the trail
                e.target = {};
                break;
            }
            float reach = info.attackRange + targetRadius(e.target);
            if (info.ranged) {
                if (dist < 9.0f) moveInfected(e, e.pos + normalize(e.pos - tp) * 5.0f, e.speed, dt, terrain_);
                else if (dist > 22.0f || !fresh) moveInfected(e, goal, e.speed, dt, terrain_);
                vec3 d = tp - e.pos;
                e.yaw = approachAngle(e.yaw, std::atan2(d.x, d.z), 6 * dt);
                if (fresh && dist < reach && e.attackCd <= 0 && (canSeeTarget || dist < 30)) {
                    Projectile p;
                    p.kind = PJ_BOLT;
                    p.pos = e.pos + vec3(0, 2.0f * info.scale, 0);
                    vec3 aim = tp + vec3(0, 1.0f, 0) - p.pos;
                    p.vel = normalize(aim) * 24.0f;
                    p.life = 3.0f;
                    p.dmg = e.dmg;
                    p.torpor = 0;
                    p.owner = {TK_INFECTED, i};
                    p.color = vec3(0.9f, 0.1f, 0.3f);
                    projectiles_.push_back(p);
                    e.attackCd = info.attackCooldown;
                    e.swing = 1;
                }
            } else if (dist > reach) {
                vec3 before = e.pos;
                moveInfected(e, goal, e.speed, dt, terrain_);
                int hit = -1;
                pushOutOfStructures(e.pos, 0.5f * info.scale, &hit);
                if (hit >= 0) {
                    Structure& st = structures_[hit];
                    if (st.type == I_SPIKEWALL) damageInfected(i, 25.0f * dt, {TK_STRUCTURE, hit});
                    if (e.attackCd <= 0) {  // smash through whatever is in the way
                        st.hp -= e.dmg * 1.5f;
                        e.attackCd = info.attackCooldown;
                        e.swing = 1;
                        spawnParticles(e.pos + dirFromYaw(e.yaw) + vec3(0, 1.2f, 0), 6, vec3(0.5f, 0.35f, 0.2f), 3, 0.6f, 0.15f, 0);
                        if (st.hp <= 0) {
                            st.alive = false;
                            addMessage(std::string("The tribe smashed a ") + ITEMS[st.type].name + "!", vec3(1.0f, 0.5f, 0.4f));
                        }
                    }
                }
                (void)before;
            } else {
                vec3 d = tp - e.pos;
                e.yaw = approachAngle(e.yaw, std::atan2(d.x, d.z), 8 * dt);
                if (e.attackCd <= 0) {
                    damageTarget(e.target, e.dmg, 0, {TK_INFECTED, i});
                    e.attackCd = info.attackCooldown;
                    e.swing = 1;
                }
            }
            break;
        }
        case IS_RETURN: {
            float d = lengthXZ(e.pos);
            if (d > 6.0f) moveInfected(e, vec3(0, 0, 0), e.speed * 0.8f, dt, terrain_);
            e.pos.y -= dt * 1.5f;  // sink into the pit
            if (e.pos.y < terrain_.heightAt(e.pos.x, e.pos.z) - 3.0f) e.alive = false;
            break;
        }
        default:
            break;
    }

    // Chieftains drag more of the tribe out of the ground when wounded.
    if (e.cls == IC_CHIEFTAIN && !e.summoned && e.hp < e.maxHp * 0.5f) {
        e.summoned = true;
        addMessage("The Chieftain howls - the ground splits open!", vec3(1.0f, 0.3f, 0.2f));
        shake_ = std::max(shake_, 0.5f);
        vec3 base = e.pos;
        int band = e.band;
        for (int k = 0; k < 2; k++) {  // note: spawning may reallocate, so 'e' is not used below
            vec3 p = base + vec3(rng_.range(-4, 4), 0, rng_.range(-4, 4));
            p.y = terrain_.heightAt(p.x, p.z) - 2.5f;
            int n = spawnInfected(IC_WARRIOR, p, band, 7 + k, false);
            infected_[n].stateTimer = 1.5f;
        }
    }
}

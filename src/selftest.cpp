// Automated gameplay self-test: drives the real simulation code with scripted inputs and
// checks the core loops (knockout taming, bait rules, eggs, babies, the tribe, dawn).
// Run with:  mythbound --selftest
#include "game.h"
#include <cstdio>

bool Game::runSelfTest() {
    int failures = 0;
    auto check = [&](bool ok, const char* what) {
        std::printf("  [%s] %s\n", ok ? "PASS" : "FAIL", what);
        if (!ok) failures++;
    };
    auto sim = [&](float seconds) {
        const float dt = 1.0f / 30.0f;
        for (float t = 0; t < seconds; t += dt) {
            updateCreatures(dt);
            updateEggsAndNests(dt);
            updateTribe(dt);
            updateProjectiles(dt);
            updateParticles(dt);
            totalTime_ += dt;
        }
    };
    auto placeNear = [&](vec3 base, float dx, float dz) {
        vec3 p = base + vec3(dx, 0, dz);
        p.y = terrain_.heightAt(p.x, p.z);
        return p;
    };

    std::printf("MYTHBOUND self-test\n");
    resetWorld();
    startPlay();
    // Clear wildlife around the test area so nothing interferes.
    vec3 base = terrain_.randomLand(rng_, B_MEADOW, 3.0f);
    for (auto& c : creatures_)
        if (distXZ(c.pos, base) < 120) c.alive = false;
    player_.pos = base;
    time_ = 0.5f;
    nightActive_ = false;

    // 1. Knockout taming of a common creature with raw meat.
    {
        int h = spawnCreature(S_HIPPOGRIFF, placeNear(base, 5, 0));
        int hits = 0;
        while (creatures_[h].state != CS_UNCONSCIOUS && hits < 200) {
            damageCreature(h, ITEMS[I_CLUB].damage, ITEMS[I_CLUB].torpor, {TK_PLAYER, 0});
            hits++;
        }
        check(creatures_[h].state == CS_UNCONSCIOUS, "club knocks out a Hippogriff");
        std::printf("      (took %d club hits, hp left %.0f/%.0f)\n", hits, creatures_[h].hp, creatures_[h].maxHp);
        creatures_[h].food[I_RAWMEAT] = 20;
        creatures_[h].food[I_DREAMBERRY] = 10;
        float t = 0;
        while (!creatures_[h].tamed && t < 120) {
            sim(1);
            t += 1;
        }
        check(creatures_[h].tamed, "Hippogriff tames by eating raw meat");
        std::printf("      (tamed after %.0f s, level %d)\n", t, creatures_[h].level);
        check(creatures_[h].state == CS_FOLLOW, "tamed creature follows the player");
    }

    // 2. Rare creatures refuse normal food, accept Mythic Bait.
    {
        int g = spawnCreature(S_GRIFFIN, placeNear(base, -6, 4));
        creatures_[g].torpor = creatures_[g].maxTorpor;
        damageCreature(g, 1, creatures_[g].maxTorpor, {TK_PLAYER, 0});
        check(creatures_[g].state == CS_UNCONSCIOUS, "sleep darts knock out a Griffin");
        creatures_[g].food[I_RAWMEAT] = 20;
        creatures_[g].food[I_DREAMBERRY] = 20;
        sim(15);
        check(creatures_[g].tameProgress == 0 && !creatures_[g].tamed, "Griffin refuses raw meat (rare creature)");
        creatures_[g].food[I_MYTHBAIT] = 4;
        float t = 0;
        while (!creatures_[g].tamed && t < 60) {
            sim(1);
            t += 1;
        }
        check(creatures_[g].tamed, "Griffin tames with Mythic Bait");
    }

    // 3. Egg taming: dragon egg by a campfire with Dragon Bait.
    {
        Structure fire;
        fire.type = I_CAMPFIRE;
        fire.pos = placeNear(base, 0, 8);
        structures_.push_back(fire);
        Egg e;
        e.species = S_FIRE_DRAGON;
        e.pos = placeNear(base, 30, 30);
        eggs_.push_back(e);
        int ei = (int)eggs_.size() - 1;
        eggs_[ei].state = EGG_CARRIED;
        player_.carryEgg = ei;
        player_.pos = placeNear(base, 0, 6);
        player_.yaw = 0;  // facing +z towards the fire
        player_.inv[I_DRAGONBAIT] = 0;
        dropCarried();
        check(eggs_[ei].state == EGG_INCUBATING, "egg dropped by the campfire starts incubating");
        sim(10);
        check(eggs_[ei].progress == 0, "dragon egg stays dormant without Dragon Bait");
        eggs_[ei].fed = true;
        int before = 0;
        for (auto& c : creatures_)
            if (c.alive && c.tamed && c.species == S_FIRE_DRAGON) before++;
        sim(50);
        int after = 0;
        for (auto& c : creatures_)
            if (c.alive && c.tamed && c.species == S_FIRE_DRAGON && c.baby) after++;
        check(!eggs_[ei].alive && after > before, "Fire Dragon egg hatches into a tamed baby");
    }

    // 4. Baby taming: grab a unicorn foal, parents get angry, bond at the campfire.
    {
        int parent = spawnCreature(S_UNICORN, placeNear(base, 20, -10));
        int baby = spawnCreature(S_UNICORN, placeNear(base, 22, -10), true);
        creatures_[baby].parent = parent;
        player_.pos = placeNear(base, 21, -12);
        iaKind_ = IA_CREATURE;
        iaIdx_ = baby;
        playerInteract();
        check(player_.carryBaby == baby && creatures_[baby].state == CS_CARRIED, "player picks up the baby unicorn");
        check(creatures_[parent].state == CS_ATTACK && creatures_[parent].target.kind == TK_PLAYER, "the parent charges the player");
        creatures_[parent].alive = false;  // escaped
        player_.pos = placeNear(base, 0, 5);
        player_.yaw = 0;
        dropCarried();
        check(creatures_[baby].readyToBond, "baby dropped at camp is ready to bond");
        player_.inv[I_EMBERBERRY] = 10;
        iaKind_ = IA_CREATURE;
        iaIdx_ = baby;
        playerInteract();
        check(creatures_[baby].tamed, "feeding the baby tames it");
    }

    // 5. The tribe: spawns at night, roams, only hunts what it sees.
    {
        for (auto& c : creatures_)
            if (!c.tamed) c.alive = false;  // keep the test clean
        for (auto& c : creatures_)
            if (c.tamed) {
                c.stayMode = true;
                c.state = CS_STAY;
            }
        time_ = 0.8f;
        nightActive_ = true;
        night_ = 1;
        // Hide the player on the far side of the island behind terrain.
        player_.pos = placeNear(vec3(-380, 0, -120), 0, 0);
        player_.crouch = true;
        spawnBand(1);
        spawnBand(1);
        int count = 0;
        for (auto& e : infected_)
            if (e.alive) count++;
        check(count >= 8, "war bands climb out of the Hollow");
        sim(8);
        int emerged = 0, chasingPlayer = 0;
        for (auto& e : infected_)
            if (e.alive && e.state != IS_EMERGE) emerged++;
        check(emerged == count, "all members finish emerging");
        sim(20);
        for (auto& e : infected_)
            if (e.alive && e.state == IS_CHASE && e.target.kind == TK_PLAYER) chasingPlayer++;
        check(chasingPlayer == 0, "the tribe does NOT hunt a player it cannot see");
        float moved = 0;
        for (auto& e : infected_)
            if (e.alive) moved = std::max(moved, lengthXZ(e.pos));
        check(moved > HOLLOW_RIM, "war bands roam out across the island");

        // Now stand right in front of a band leader.
        int leader = bands_[0].leader;
        Infected& L = infected_[leader];
        player_.crouch = false;
        player_.pos = L.pos + dirFromYaw(L.yaw) * 10.0f;
        player_.pos.y = terrain_.heightAt(player_.pos.x, player_.pos.z);
        sim(1.0f);
        int hunters = 0;
        for (auto& e : infected_)
            if (e.alive && e.band == 0 && e.state == IS_CHASE && e.target.kind == TK_PLAYER) hunters++;
        check(hunters >= 2, "spotting the player alerts the whole war band");
        float hpBefore = player_.hp;
        sim(4);
        check(player_.hp < hpBefore || player_.dead, "the tribe attacks the player");

        // A wild creature in plain sight gets hunted too.
        player_.pos = placeNear(vec3(-380, 0, -120), 0, 0);
        player_.dead = false;
        player_.hp = 100;
        for (auto& e : infected_) {
            e.state = e.state == IS_DEAD ? IS_DEAD : IS_FORMATION;
            e.target = {};
        }
        int band1 = bands_[1].leader;
        vec3 front = infected_[band1].pos + dirFromYaw(infected_[band1].yaw) * 12.0f;
        int prey = spawnCreature(S_MOSSBACK, placeNear(front, 0, 0));
        sim(1.0f);
        bool hunted = false;
        for (auto& e : infected_)
            if (e.alive && e.state == IS_CHASE && e.target.kind == TK_CREATURE && e.target.idx == prey) hunted = true;
        check(hunted, "the tribe hunts wild creatures it sees");

        // Dawn: stragglers burn.
        time_ = 0.3f;
        nightActive_ = false;
        sim(8);
        int alive = 0;
        for (auto& e : infected_)
            if (e.alive && e.state != IS_DEAD) alive++;
        check(alive == 0, "sunlight burns the tribe at dawn");
    }

    // 6. Crafting.
    {
        for (auto& v : player_.inv) v = 0;
        player_.inv[I_WOOD] = 10;
        player_.inv[I_FIBER] = 10;
        player_.inv[I_STONE] = 5;
        const Recipe* club = nullptr;
        for (int i = 0; i < RECIPE_COUNT; i++)
            if (RECIPES[i].out == I_CLUB) club = &RECIPES[i];
        check(club && canCraft(*club), "can craft a knockout club from gathered resources");
        if (club) craft(*club, 1);
        check(player_.inv[I_CLUB] == 1 && player_.inv[I_WOOD] == 4, "crafting consumes ingredients");
    }

    // 7. Soak: a full sunset -> night -> sunrise cycle through the real frame update.
    {
        resetWorld();
        startPlay();
        time_ = 0.7f;
        int maxTribe = 0;
        const float dt = 1.0f / 20.0f;
        for (int step = 0; step < 20 * 60 * 9 && !(time_ > 0.3f && time_ < 0.5f); step++) {
            updatePlay(dt);
            totalTime_ += dt;
            int n = 0;
            for (auto& e : infected_)
                if (e.alive && e.state != IS_DEAD) n++;
            maxTribe = std::max(maxTribe, n);
        }
        int left = 0;
        for (auto& e : infected_)
            if (e.alive && e.state != IS_DEAD) left++;
        std::printf("      (peak tribe size %d, %zu creatures, night %d)\n", maxTribe, creatures_.size(), night_);
        check(maxTribe >= 10 && night_ == 1, "a full night spawns the tribe");
        check(left == 0, "the island is clear again after sunrise");
    }

    std::printf("Species: %d   Items: %d   Recipes: %d\n", (int)S_COUNT, (int)I_COUNT - 1, RECIPE_COUNT);
    std::printf(failures == 0 ? "ALL TESTS PASSED\n" : "%d TEST(S) FAILED\n", failures);
    return failures == 0;
}

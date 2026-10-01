#pragma once
#include "renderer.h"
#include "world.h"
#include "data.h"
#include "models.h"
#include <string>
#include <vector>

struct GLFWwindow;

enum GameMode { GM_TRAILER, GM_TITLE, GM_PLAY };

enum TargetKind { TK_NONE, TK_PLAYER, TK_CREATURE, TK_INFECTED, TK_STRUCTURE };
struct TargetRef {
    TargetKind kind = TK_NONE;
    int idx = -1;
    bool operator==(const TargetRef& o) const { return kind == o.kind && idx == o.idx; }
};

enum CState { CS_WANDER, CS_FLEE, CS_ATTACK, CS_UNCONSCIOUS, CS_FOLLOW, CS_STAY, CS_RIDDEN, CS_CARRIED, CS_DEAD, CS_SCRIPTED };

struct Creature {
    int species = 0;
    bool alive = true;
    vec3 pos, vel;
    float yaw = 0, pitch = 0, roll = 0;
    float hp = 100, maxHp = 100, torpor = 0, maxTorpor = 100, stamina = 100, maxStamina = 100;
    int level = 1;
    float dmg = 10, speed = 5;
    CState state = CS_WANDER;
    float stateTimer = 0, thinkTimer = 0;
    vec3 moveTarget, home;
    TargetRef target;
    float attackCd = 0, breathCd = 0;
    bool flying = false;
    float altitude = 0;  // desired height above ground while flying
    // taming
    bool tamed = false, hasSaddle = false, stayMode = false;
    float tameProgress = 0, tameEff = 1, eatTimer = 0;
    int food[I_COUNT] = {};
    std::string name;
    // family / growth
    bool baby = false;
    float growth = 1;
    int parent = -1;
    bool readyToBond = false;  // baby dropped at camp, waiting for food
    // death / corpse
    float deathTimer = 0;
    int corpseHits = 0;
    // animation
    float anim = 0, flap = 0, moveAmt = 0, attackAnim = 0, hurt = 0, breathTime = 0;
};

enum EggState { EGG_NEST, EGG_CARRIED, EGG_INCUBATING };
struct Egg {
    int species = 0;
    vec3 pos;
    int nest = -1;
    EggState state = EGG_NEST;
    float progress = 0;
    bool alive = true;
    bool fed = false;  // dragon-kin eggs need Dragon Bait
};

struct Nest {
    int species;
    vec3 pos;
    float timer = 0;
    int egg = -1;
};

enum IState { IS_EMERGE, IS_FORMATION, IS_CHASE, IS_RETURN, IS_DEAD };
struct Infected {
    int cls = IC_WARRIOR;
    bool alive = true;
    vec3 pos;
    float yaw = 0;
    float hp = 100, maxHp = 100, dmg = 10, speed = 5;
    int band = -1;
    int slot = 0;  // formation slot
    IState state = IS_EMERGE;
    float stateTimer = 0, thinkTimer = 0;
    TargetRef target;
    vec3 lastKnown;
    float lastSeen = 0;
    float attackCd = 0, anim = 0, swing = 0, hurt = 0, burn = 0;
    float deathTimer = 0;
    int corpseHits = 0;
    bool summoned = false;
    bool trailer = false;
};

struct Band {
    bool active = false;
    int leader = -1;
    vec3 waypoint;
    float waitTimer = 0;
    bool returning = false;
};

struct Structure {
    Item type;
    vec3 pos;
    float yaw = 0;
    float hp = 500;
    bool alive = true;
};

enum ProjKind { PJ_ARROW, PJ_DART, PJ_BOLT, PJ_BREATH };
struct Projectile {
    ProjKind kind;
    vec3 pos, vel;
    float life, dmg, torpor;
    TargetRef owner;
    vec3 color;
    bool alive = true;
    bool gravity = false;
};

struct Particle {
    vec3 pos, vel, color;
    float life, maxLife, size, emissive, gravity;
};

struct Message {
    std::string text;
    vec3 color;
    float time;
};

struct Player {
    vec3 pos, vel;
    float yaw = PI, pitch = -0.15f;
    bool onGround = false, swimming = false, crouch = false;
    float hp = 100, maxHp = 100, stamina = 100, food = 100, water = 100;
    int inv[I_COUNT] = {};
    int hotSel = 0;
    float attackCd = 0, swing = 0, hurtFlash = 0;
    int riding = -1;
    int carryEgg = -1, carryBaby = -1;
    bool dead = false;
    float respawnTimer = 0;
    bool ammoDarts = false;
    float placeYaw = 0;
    TargetRef lastAttacker;
    float lastAttackerTime = 0;
    float anim = 0;
    int kills = 0, tames = 0;
};

enum Panel { PANEL_NONE, PANEL_CRAFT, PANEL_TAME, PANEL_CAULDRON, PANEL_BREW, PANEL_HELP };

struct Brew {
    int recipe = 0;
    int stage = 0;
    int hits = 0;
    float needle = 0, dir = 1, speed = 1;
    float zoneStart = 0.4f, zoneWidth = 0.2f;
    float resultTimer = 0;
    bool done = false;
    int structure = -1;
};

struct Options {
    bool skipTrailer = false;
    bool validation = false;
    float startTime = 0.3f;
    std::string capturePath;
    float captureAfter = -1;
    float trailerAt = 0;
    bool autoNight = false;
    bool demo = false;      // start with tools, resources and a tamed griffin
    bool hasPos = false;    // --pos x z : start position
    float posX = 0, posZ = 0, yaw = PI, pitch = -0.15f;
    float quitAfter = -1;   // seconds, for automated captures
    bool selfTest = false;
    std::string uiTest;     // craft | tame | brew | help : open a panel at start (for screenshots)
};

// Text helper shared by the HUD and the trailer (ui.cpp).
void uiTextCentered(FrameScene& s, float cx, float y, const std::string& t, float px, vec4 col);

class Game {
public:
    bool init(GLFWwindow* window, Renderer* renderer, const Options& opt);
    void frame(float dt);
    bool wantsQuit() const { return quit_; }
    void onScroll(double dy) { scrollAccum_ += dy; }
    bool runSelfTest();  // selftest.cpp

private:
    // --- setup (game.cpp) ---
    void resetWorld();
    void startPlay();
    void spawnInitialLife();
    int spawnCreature(int species, vec3 pos, bool baby = false);
    void addMessage(const std::string& s, vec3 color = vec3(1, 1, 1));

    // --- input (game.cpp) ---
    void pollInput();
    bool keyDown(int k) const;
    bool keyPressed(int k) const;
    bool mouseDown(int b) const;
    bool mousePressed(int b) const;
    void setCursorCaptured(bool c);

    // --- simulation ---
    void updatePlay(float dt);
    void updateTime(float dt);
    void updatePlayer(float dt);            // player.cpp
    void updateRiding(float dt);            // player.cpp
    void playerAttack();                    // player.cpp
    void playerInteract();                  // player.cpp
    void findInteraction();                 // player.cpp
    void placeStructure();                  // player.cpp
    void dropCarried();                     // player.cpp
    void playerDie(const std::string& cause);
    void updatePanels(float dt);            // ui.cpp
    std::vector<Item> hotbarItems() const;  // player.cpp
    Item selectedItem() const;
    bool canCraft(const Recipe& r) const;
    void craft(const Recipe& r, int multiplier);

    void updateCreatures(float dt);  // creatures.cpp
    void updateCreature(int i, float dt);
    void creatureThink(int i);
    void moveCreature(Creature& c, vec3 target, float speed, float dt, bool face = true);
    void creatureAttack(int i, float dt);
    void creatureBreath(int i, vec3 dir);
    void tameCreature(int i);
    void updateEggsAndNests(float dt);
    void respawnWildlife(float dt);
    float creatureRadius(const Creature& c) const;
    float growthScale(const Creature& c) const { return c.baby ? 0.3f + 0.7f * c.growth : 1.0f; }
    vec3 creatureHead(const Creature& c) const;
    float foodValue(int species, Item it) const;
    float creatureHeight(const Creature& c) const;
    vec3 creatureSeat(const Creature& c) const;
    void aggroFamily(int species, vec3 pos, float radius);

    void updateTribe(float dt);  // tribe.cpp
    void spawnBand(int nightNum, bool trailer = false);
    int spawnInfected(int cls, vec3 pos, int band, int slot, bool trailer);
    void updateInfected(int i, float dt);
    bool infectedSees(const Infected& e, vec3 p, float range) const;
    vec3 formationSlot(const Band& b, int slot) const;

    void updateProjectiles(float dt);  // combat.cpp
    void updateParticles(float dt);
    void spawnParticles(vec3 pos, int count, vec3 color, float speed, float life, float size, float emissive, float gravity = 4.0f);
    void damageCreature(int i, float dmg, float torpor, TargetRef attacker);
    void damageInfected(int i, float dmg, TargetRef attacker);
    void damagePlayer(float dmg, TargetRef attacker, const std::string& cause);
    void damageTarget(TargetRef t, float dmg, float torpor, TargetRef attacker);
    bool targetAlive(TargetRef t) const;
    vec3 targetPos(TargetRef t) const;
    float targetRadius(TargetRef t) const;
    void pushOutOfStructures(vec3& p, float radius, int* hitStructure = nullptr);
    void pushOutOfProps(vec3& p, float radius);
    bool nearCampfire(vec3 p, float radius) const;

    // --- presentation ---
    void buildScene(FrameScene& s);  // draw.cpp
    void setupLighting(FrameScene& s, vec3 camPos, vec3 camTarget, const mat4& view, const mat4& proj);
    void drawProps(FrameScene& s);
    void drawCreature(FrameScene& s, const Creature& c);
    void drawInfected(FrameScene& s, const Infected& e);
    void drawHumanoid(FrameScene& s, const Rig& rig, vec3 pos, float yaw, float scale, vec3 tint, float walk, float moveAmt, float swing,
                      Item held, bool infected, int cls, float glow, float sit, float hunch);
    void animateCreature(const Creature& c, const Rig& rig, std::vector<mat4>& local) const;
    void updateAmbientFX(float dt);
    void drawStructures(FrameScene& s);
    void drawEggs(FrameScene& s);
    void drawEffects(FrameScene& s);
    bool sphereVisible(vec3 c, float r) const;

    void buildUI(FrameScene& s);  // ui.cpp
    void drawHUD(FrameScene& s);
    void drawPanels(FrameScene& s);
    void drawTitle(FrameScene& s);

    void updateTrailer(float dt);  // trailer.cpp
    void startTrailer();
    void endTrailer();
    void drawTrailerUI(FrameScene& s);

    // --- state ---
    GLFWwindow* window_ = nullptr;
    Renderer* renderer_ = nullptr;
    Options opt_;
    GameMode mode_ = GM_TRAILER;
    bool quit_ = false, paused_ = false, cursorCaptured_ = false;

    Terrain terrain_;
    PropField props_;
    std::vector<Creature> creatures_;
    std::vector<Egg> eggs_;
    std::vector<Nest> nests_;
    std::vector<Infected> infected_;
    std::vector<Band> bands_;
    std::vector<Structure> structures_;
    std::vector<Projectile> projectiles_;
    std::vector<Particle> particles_;
    std::vector<Message> messages_;
    Player player_;
    Rng rng_{777};

    float time_ = 0.3f;       // time of day, 0 = midnight, 0.5 = noon
    int day_ = 1;
    int night_ = 0;
    bool nightActive_ = false;
    float tribeSpawnTimer_ = 0;
    int bandsToSpawn_ = 0;
    float totalTime_ = 0, playTime_ = 0;
    float respawnTimer_ = 0;

    // camera
    vec3 camPos_, camTarget_;
    mat4 view_, proj_, viewProj_;
    vec4 frustum_[6];
    float camDist_ = 6.0f;
    float fov_ = 1.1f;
    float shake_ = 0;

    // interaction
    enum InteractKind { IA_NONE, IA_CREATURE, IA_EGG, IA_STRUCTURE, IA_WATER };
    InteractKind iaKind_ = IA_NONE;
    int iaIdx_ = -1;
    std::string iaPrompt_;
    Panel panel_ = PANEL_NONE;
    int panelTarget_ = -1;
    int craftSel_ = 0;
    Brew brew_;

    // input
    bool keys_[400] = {}, prevKeys_[400] = {};
    bool mouse_[8] = {}, prevMouse_[8] = {};
    double mouseX_ = 0, mouseY_ = 0, mouseDX_ = 0, mouseDY_ = 0;
    bool firstMouse_ = true;
    double scrollAccum_ = 0, scroll_ = 0;

    // trailer
    float trailerTime_ = 0;
    int trailerDragon_ = -1, trailerGriffin_ = -1, trailerPegasus_ = -1;
    bool trailerSpawnedTribe_ = false;
    float forcedSpawnAngle_ = -100.0f;  // trailer: make the tribe emerge facing the camera
    float titleTime_ = 0;
    float fade_ = 0;

    // models & animation scratch
    ModelLibrary models_;
    std::vector<mat4> localScratch_, boneScratch_;
    float fxTimer_ = 0;

    // captures
    bool captured_ = false;
    std::vector<int> scratch_;
};

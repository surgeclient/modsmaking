// Static game data: items, recipes, creature species and tribe classes.
#pragma once
#include "math.h"
#include "world.h"

// ---------------------------------------------------------------- Items ----
enum Item {
    I_NONE,
    I_WOOD, I_THATCH, I_STONE, I_FLINT, I_FIBER, I_EMBERBERRY, I_DREAMBERRY, I_SPIRITHERB,
    I_CRYSTAL, I_METAL, I_BONE, I_HIDE, I_RAWMEAT, I_COOKEDMEAT, I_HOLLOWHEART,
    I_STONEPICK, I_STONEHATCHET, I_SPEAR, I_CLUB, I_BOW, I_ARROW, I_SLEEPDART, I_TORCH, I_BLADE,
    I_WILDBAIT, I_MYTHBAIT, I_DRAGONBAIT,
    I_CAMPFIRE, I_CAULDRON, I_WOODWALL, I_SPIKEWALL, I_SADDLE,
    I_COUNT
};

enum ItemKind { IK_RESOURCE, IK_FOOD, IK_TOOL, IK_AMMO, IK_BAIT, IK_PLACEABLE, IK_GEAR };

struct ItemInfo {
    const char* name;
    ItemKind kind;
    vec3 color;
    float damage;   // melee / projectile damage
    float torpor;   // knockout power
    float food;     // hunger restored when eaten
};
extern const ItemInfo ITEMS[I_COUNT];

enum Station { ST_HAND, ST_CAULDRON };

struct Recipe {
    Item out;
    int outCount;
    Station station;
    struct { Item item; int count; } in[4];
    const char* hint;
};
extern const Recipe RECIPES[];
extern const int RECIPE_COUNT;

// -------------------------------------------------------------- Species ----
enum BodyPlan { BP_QUAD, BP_BIRD, BP_SERPENT };
enum TameMethod { TM_KNOCKOUT, TM_EGG, TM_BABY };
enum Diet { D_CARNIVORE, D_HERBIVORE };
enum Temper { T_PASSIVE, T_NEUTRAL, T_AGGRESSIVE };
enum Rarity { R_COMMON, R_RARE, R_LEGENDARY };

struct Species {
    const char* name;
    const char* lore;
    BodyPlan plan;
    float size;
    vec3 c1, c2, c3;   // body, secondary (wings/mane/belly), accent
    float glow;        // emissive strength of accents
    // proportions (metres at size 1)
    float bodyL, bodyW, bodyH, legL, legT, neckL, neckAngle, headS, tailL;
    int heads, tails, horns;
    bool wings, mane, shell, beak, stinger, antlers, tusks, fins, glowEyes;
    TameMethod tame;
    Diet diet;
    Temper temper;
    Rarity rarity;
    bool flyer, swimmer, rideable;
    float hp, dmg, speed, torporRes;
    int spawnCount;  // wild population target
    Biome home[3];
    float tameDifficulty;  // food points needed
    bool breath;           // ranged breath/elemental attack
    vec3 breathColor;
};

enum SpeciesId {
    S_FIRE_DRAGON, S_FROST_WYVERN, S_STORM_DRAKE, S_GRIFFIN, S_HIPPOGRIFF, S_PEGASUS, S_UNICORN, S_PHOENIX,
    S_THUNDERBIRD, S_MANTICORE, S_BASILISK, S_KELPIE, S_MOSSBACK, S_JACKALOPE, S_SALAMANDER, S_CERBERUS,
    S_CHIMERA, S_BEHEMOTH, S_KITSUNE, S_HYDRA, S_ROC,
    S_COUNT
};
extern const Species SPECIES[S_COUNT];

// ---------------------------------------------------------------- Tribe ----
enum InfectedClass { IC_WARRIOR, IC_BRUTE, IC_STALKER, IC_SHAMAN, IC_CHIEFTAIN, IC_COUNT };

struct InfectedInfo {
    const char* name;
    float hp, dmg, speed, sight, scale, attackRange, attackCooldown;
    vec3 skin, cloth, mask;
    bool ranged;
};
extern const InfectedInfo INFECTED[IC_COUNT];

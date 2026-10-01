// Model library: skeletons + generated meshes for every creature, the player, the tribe
// and all props. Built once at start-up from the procedural modeller (meshgen).
#pragma once
#include "meshgen.h"
#include "data.h"
#include "world.h"

struct Bone {
    int parent;
    vec3 pivot;
};

struct Rig {
    std::vector<Bone> bones;
    int mesh = -1;
    float gloss = 0.2f;
    int pattern = 0;  // surface detail: 1 scales, 2 fur, 3 feathers, 4 foliage, 5 rock, 6 skin
    // Creature bones (-1 when absent).
    int body = 0;
    int heads = 0, neck0[5] = {}, neck1[5] = {}, head[5] = {}, jaw[5] = {};
    int legs = 0, legUp[4] = {}, legLo[4] = {};
    int tails = 0, tailSegs = 0, tail[9][6] = {};
    bool wings = false;
    int wingIn[2] = {}, wingOut[2] = {};
    int spineN = 0, spine[16] = {};
    // Humanoid bones.
    int pelvis = -1, torso = -1, headB = -1, armUp[2] = {}, armLo[2] = {}, thigh[2] = {}, shin[2] = {};
    vec3 handRest[2];
    float seatY = 0;  // rest-pose rider height
};

struct PropModel {
    int lod0 = -1, lod1 = -1;
    int pattern = 4;
    float wind = 1.0f;
};

class ModelLibrary {
public:
    void build(Renderer& r);
    Rig creature[S_COUNT];
    Rig player;
    Rig infected[IC_COUNT];
    std::vector<PropModel> props[P_COUNT];
    int eggMesh = -1;
};

// Bone matrices (model space, rest-relative) from per-bone local rotations.
void poseRig(const Rig& rig, const std::vector<mat4>& local, std::vector<mat4>& out);

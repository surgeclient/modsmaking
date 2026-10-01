// Procedural mesh generation: signed-distance-field modelling + surface nets.
// Creatures, people, trees and rocks are described as smooth-blended shapes
// (round cones, ellipsoids, boxes) and turned into organic meshes at load time,
// with baked ambient occlusion, colour blending and skinning weights.
#pragma once
#include "renderer.h"
#include <functional>
#include <vector>

struct Prim {
    enum Type { CAPSULE, ELLIPSOID, BOX };
    Type type = CAPSULE;
    vec3 a, b;                       // capsule endpoints; ellipsoid / box centre in a
    float ra = 0.1f, rb = 0.1f;      // capsule radii at a and b
    vec3 radii{0.1f, 0.1f, 0.1f};    // ellipsoid radii / box half extents
    vec3 ax{1, 0, 0}, ay{0, 1, 0}, az{0, 0, 1};  // orientation of ellipsoid / box
    float blend = 0.08f;             // smooth-union radius
    float disp = 0;                  // noise displacement amplitude for this shape
    float dispFreq = 1.5f;
    int bone = 0;
    vec3 color{1, 1, 1};
    float emissive = 0;
    bool subtract = false;
};

struct MeshData {
    std::vector<Vertex> v;
    std::vector<uint32_t> idx;
};

struct SdfModel {
    std::vector<Prim> prims;
    int seed = 1;
    // Optional hooks: recolour a surface point, and wind sway weight for foliage.
    std::function<vec3(vec3 p, vec3 n, vec3 c, float ao)> colorFn;
    std::function<float(vec3 p)> swayFn;
    float colorSharpness = 1.0f;  // higher = crisper colour borders between shapes
};

// Convenience constructors.
Prim capsule(vec3 a, vec3 b, float ra, float rb, vec3 color, int bone = 0, float blend = 0.08f);
Prim ellipsoid(vec3 c, vec3 radii, vec3 color, int bone = 0, float blend = 0.08f);
Prim ellipsoidRot(vec3 c, vec3 radii, float yaw, float pitch, float roll, vec3 color, int bone = 0, float blend = 0.08f);
Prim roundBox(vec3 c, vec3 halfExt, float round, vec3 color, int bone = 0, float blend = 0.05f);

// Builds a smooth mesh from the model and appends it to out.
void buildSdf(const SdfModel& m, float voxel, MeshData& out);

// Explicit geometry for crisp thin parts (horns, claws, wings, leaves, eyes).
void addSphere(MeshData& m, vec3 c, vec3 radii, vec3 color, int bone, float emissive = 0, int segs = 10);
void addCone(MeshData& m, vec3 base, vec3 tip, float r, vec3 color, int bone, float emissive = 0, int segs = 8, float sway = 0);
void addCylinder(MeshData& m, vec3 a, vec3 b, float ra, float rb, vec3 color, int bone, int segs = 8, float sway = 0);
// Double-sided flat polygon (convex fan), e.g. wing membranes and leaves.
void addPolygon(MeshData& m, const std::vector<vec3>& pts, vec3 color, int bone, float emissive = 0, float sway = 0, float thickness = 0.01f);
// Hexagonal crystal prism with a pointed tip.
void addCrystal(MeshData& m, vec3 base, vec3 dir, float r, float len, vec3 color, float emissive);

float noise3(vec3 p, int seed = 0);
float fbm3(vec3 p, int octaves, int seed = 0);

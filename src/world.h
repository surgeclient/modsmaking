#pragma once
#include "math.h"
#include "renderer.h"
#include <vector>

enum Biome { B_OCEAN, B_BEACH, B_MEADOW, B_FOREST, B_JUNGLE, B_HIGHLANDS, B_SNOW, B_ASHLANDS, B_CORRUPT, B_COUNT };
extern const char* BIOME_NAMES[B_COUNT];

constexpr float HOLLOW_RIM = 52.0f;   // radius of the crater rim around the island centre
constexpr float HOLLOW_PIT = 26.0f;   // radius of the pit the tribe crawls out of

enum PropType {
    P_OAK, P_PINE, P_PALM, P_JUNGLETREE, P_DEADTREE, P_SNOWPINE,
    P_ROCK, P_BOULDER, P_METALROCK, P_CRYSTAL,
    P_EMBERBUSH, P_DREAMBUSH, P_SPIRITHERB, P_BONEPILE, P_FERN,
    P_COUNT
};

struct Prop {
    PropType type;
    vec3 pos;
    float scale;
    float yaw;
    float hp;
    float respawn;  // > 0 while depleted
    uint32_t seed;
};

class Terrain {
public:
    static constexpr int N = 512;          // cells per side
    static constexpr float HALF = 640.0f;  // world spans [-HALF, HALF]
    static constexpr float CELL = 2 * HALF / N;

    void generate();
    float heightAt(float x, float z) const;
    // True where the ground lies under the sea (the Hollow's pit is dry despite being deep).
    bool isOcean(float x, float z, float depth = 0.0f) const { return heightAt(x, z) < -depth && x * x + z * z > HOLLOW_RIM * HOLLOW_RIM; }
    vec3 normalAt(float x, float z) const;
    Biome biomeAt(float x, float z) const;
    void buildMesh(std::vector<Vertex>& v, std::vector<uint32_t>& idx) const;
    // Height samples and grass colour/density (RGBA8) for the GPU grass, water and terrain shaders.
    void buildMaps(int& n, std::vector<float>& heights, std::vector<uint32_t>& grass) const;
    // Terrain line-of-sight test (true when nothing blocks the segment).
    bool lineOfSight(vec3 a, vec3 b) const;
    // Random dry-land point in a biome (or any biome when b == B_COUNT).
    vec3 randomLand(Rng& rng, Biome b = B_COUNT, float minHeight = 1.5f) const;

private:
    float rawHeight(float x, float z) const;
    std::vector<float> h_;
};

class PropField {
public:
    void generate(const Terrain& t);
    std::vector<Prop> props;
    // Spatial hash lookup: indices of props whose cell overlaps the circle.
    void query(vec3 p, float radius, std::vector<int>& out) const;
    static float collisionRadius(const Prop& p);

private:
    static constexpr float GCELL = 16.0f;
    static constexpr int GN = (int)(2 * Terrain::HALF / GCELL);
    std::vector<std::vector<int>> grid_;
    void insert(int idx);
};

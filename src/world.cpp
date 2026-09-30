#include "world.h"
#include "noise.h"

const char* BIOME_NAMES[B_COUNT] = {"Ocean", "Sunward Beach", "Whisper Meadows", "Elderwood", "Emerald Jungle",
                                    "Stormcrag Highlands", "Frostspire Peaks", "Cinder Wastes", "The Hollow"};

static const vec3 VOLCANO(330, 0, 40);

float Terrain::rawHeight(float x, float z) const {
    float r = std::sqrt(x * x + z * z);
    // Coastline radius wobbles with noise so the island is not a circle.
    float R = 470.0f + fbm(x * 0.0032f, z * 0.0032f, 4, 11) * 120.0f;
    float mask = smoothstep(R, R - 110.0f, r);

    float hills = fbm(x * 0.0085f, z * 0.0085f, 5, 3);
    float land = 9.0f + hills * 11.0f + fbm(x * 0.03f, z * 0.03f, 3, 5) * 1.5f;

    // Northern mountains (negative z).
    float north = smoothstep(-40.0f, -300.0f, z) * mask;
    land += ridged(x * 0.0065f, z * 0.0065f, 5, 7) * 80.0f * north;

    // Eastern volcano.
    float dv = std::sqrt((x - VOLCANO.x) * (x - VOLCANO.x) + (z - VOLCANO.z) * (z - VOLCANO.z));
    float volcano = std::max(0.0f, 78.0f - dv * 0.62f);
    volcano -= std::max(0.0f, 22.0f - dv) * 1.6f;  // crater
    land += volcano * mask;

    // Southern lowlands flatten into jungle and beaches.
    land -= smoothstep(120.0f, 380.0f, z) * 4.0f;

    float h = lerpf(-26.0f, land, mask);

    // The Hollow: a raised rim around a pit in the middle of the island.
    float rim = 17.0f * std::exp(-((r - HOLLOW_RIM) * (r - HOLLOW_RIM)) / (2.0f * 13.0f * 13.0f));
    h += rim;
    float pit = smoothstep(HOLLOW_RIM - 8.0f, HOLLOW_PIT - 6.0f, r);
    h = lerpf(h, -34.0f + fbm(x * 0.05f, z * 0.05f, 2, 9) * 3.0f, pit);
    return h;
}

void Terrain::generate() {
    h_.resize((N + 1) * (N + 1));
    for (int z = 0; z <= N; z++)
        for (int x = 0; x <= N; x++) h_[z * (N + 1) + x] = rawHeight(-HALF + x * CELL, -HALF + z * CELL);
}

// Exact interpolation over the same triangles the mesh uses.
float Terrain::heightAt(float x, float z) const {
    float fx = (x + HALF) / CELL, fz = (z + HALF) / CELL;
    if (fx < 0 || fz < 0 || fx >= N || fz >= N) return -26.0f;
    int ix = (int)fx, iz = (int)fz;
    float tx = fx - ix, tz = fz - iz;
    float h00 = h_[iz * (N + 1) + ix], h10 = h_[iz * (N + 1) + ix + 1];
    float h01 = h_[(iz + 1) * (N + 1) + ix], h11 = h_[(iz + 1) * (N + 1) + ix + 1];
    // Triangles: (00, 01, 10) and (10, 01, 11)
    if (tx + tz <= 1.0f) return h00 + (h10 - h00) * tx + (h01 - h00) * tz;
    return h11 + (h01 - h11) * (1.0f - tx) + (h10 - h11) * (1.0f - tz);
}

vec3 Terrain::normalAt(float x, float z) const {
    float e = CELL * 0.5f;
    float hx = heightAt(x + e, z) - heightAt(x - e, z);
    float hz = heightAt(x, z + e) - heightAt(x, z - e);
    return normalize(vec3(-hx, 2 * e, -hz));
}

Biome Terrain::biomeAt(float x, float z) const {
    float h = heightAt(x, z);
    float r = std::sqrt(x * x + z * z);
    if (r < HOLLOW_RIM + 22.0f + fbm(x * 0.02f, z * 0.02f, 2, 4) * 10.0f) return B_CORRUPT;
    if (h < 0.0f) return B_OCEAN;
    if (h < 2.6f) return B_BEACH;
    float dv = std::sqrt((x - VOLCANO.x) * (x - VOLCANO.x) + (z - VOLCANO.z) * (z - VOLCANO.z));
    if (dv < 150.0f + fbm(x * 0.01f, z * 0.01f, 2, 8) * 40.0f) return B_ASHLANDS;
    if (h > 58.0f) return B_SNOW;
    if (h > 34.0f || (z < -180.0f && h > 24.0f)) return B_HIGHLANDS;
    if (z > 140.0f) return B_JUNGLE;
    float f = fbm(x * 0.006f, z * 0.006f, 3, 21);
    return (f > 0.05f || x < -250.0f) ? B_FOREST : B_MEADOW;
}

static vec3 biomeColor(Biome b, float h, float slope, float n) {
    vec3 c;
    switch (b) {
        case B_OCEAN: c = vec3(0.62f, 0.56f, 0.40f); break;
        case B_BEACH: c = vec3(0.90f, 0.82f, 0.60f); break;
        case B_MEADOW: c = vec3(0.45f, 0.66f, 0.26f); break;
        case B_FOREST: c = vec3(0.27f, 0.47f, 0.20f); break;
        case B_JUNGLE: c = vec3(0.22f, 0.52f, 0.20f); break;
        case B_HIGHLANDS: c = vec3(0.50f, 0.52f, 0.36f); break;
        case B_SNOW: c = vec3(0.93f, 0.95f, 0.98f); break;
        case B_ASHLANDS: c = vec3(0.24f, 0.21f, 0.20f); break;
        case B_CORRUPT: c = vec3(0.20f, 0.13f, 0.18f); break;
        default: c = vec3(0.5f, 0.5f, 0.5f);
    }
    c = c * (0.9f + n * 0.2f);
    vec3 rock = b == B_ASHLANDS ? vec3(0.16f, 0.14f, 0.14f) : (b == B_CORRUPT ? vec3(0.13f, 0.08f, 0.1f) : vec3(0.47f, 0.45f, 0.43f));
    if (b != B_SNOW) c = lerp(c, rock, smoothstep(0.55f, 0.8f, slope));
    else c = lerp(c, vec3(0.55f, 0.58f, 0.62f), smoothstep(0.7f, 0.9f, slope));
    if (h < 0.5f && b != B_CORRUPT) c = lerp(c, vec3(0.55f, 0.5f, 0.38f), 0.6f);  // wet sand
    return c;
}

void Terrain::buildMesh(std::vector<Vertex>& v, std::vector<uint32_t>& idx) const {
    v.reserve((N + 1) * (N + 1));
    for (int z = 0; z <= N; z++)
        for (int x = 0; x <= N; x++) {
            float wx = -HALF + x * CELL, wz = -HALF + z * CELL;
            float h = h_[z * (N + 1) + x];
            vec3 n = normalAt(wx, wz);
            float slope = 1.0f - n.y;
            slope = saturate(slope * 3.2f);
            float noise = fbm(wx * 0.08f, wz * 0.08f, 2, 99);
            Biome b = biomeAt(wx, wz);
            vec3 c = biomeColor(b, h, slope, noise);
            // Glowing red veins in the corrupted ground around the Hollow.
            if (b == B_CORRUPT) {
                float vein = std::fabs(fbm(wx * 0.05f, wz * 0.05f, 3, 55));
                c = lerp(c, vec3(0.5f, 0.08f, 0.07f), smoothstep(0.08f, 0.0f, vein) * 0.7f);
            }
            if (b == B_ASHLANDS) {
                float lava = std::fabs(fbm(wx * 0.03f, wz * 0.03f, 3, 66));
                c = lerp(c, vec3(0.9f, 0.3f, 0.05f), smoothstep(0.05f, 0.0f, lava) * 0.85f);
            }
            v.push_back({{wx, h, wz}, n, c});
        }
    idx.reserve(N * N * 6);
    for (int z = 0; z < N; z++)
        for (int x = 0; x < N; x++) {
            uint32_t a = z * (N + 1) + x, b = a + N + 1;
            idx.insert(idx.end(), {a, b, a + 1, a + 1, b, b + 1});
        }
}

bool Terrain::lineOfSight(vec3 a, vec3 b) const {
    vec3 d = b - a;
    float len = length(d);
    int steps = std::max(2, (int)(len / 3.0f));
    for (int i = 1; i < steps; i++) {
        vec3 p = a + d * ((float)i / steps);
        if (heightAt(p.x, p.z) > p.y) return false;
    }
    return true;
}

vec3 Terrain::randomLand(Rng& rng, Biome b, float minHeight) const {
    for (int attempt = 0; attempt < 400; attempt++) {
        float x = rng.range(-HALF * 0.8f, HALF * 0.8f), z = rng.range(-HALF * 0.8f, HALF * 0.8f);
        float h = heightAt(x, z);
        if (h < minHeight) continue;
        Biome bb = biomeAt(x, z);
        if (bb == B_CORRUPT && b != B_CORRUPT) continue;
        if (b != B_COUNT && bb != b) continue;
        return {x, h, z};
    }
    return {0, heightAt(0, 420), 420};
}

// ---------------------------------------------------------------------------
float PropField::collisionRadius(const Prop& p) {
    switch (p.type) {
        case P_OAK: case P_JUNGLETREE: return 0.6f * p.scale;
        case P_PINE: case P_SNOWPINE: case P_PALM: case P_DEADTREE: return 0.45f * p.scale;
        case P_BOULDER: return 2.2f * p.scale;
        case P_ROCK: case P_METALROCK: return 0.9f * p.scale;
        case P_CRYSTAL: return 0.8f * p.scale;
        default: return 0.0f;
    }
}

void PropField::insert(int idx) {
    const Prop& p = props[idx];
    int gx = (int)((p.pos.x + Terrain::HALF) / GCELL), gz = (int)((p.pos.z + Terrain::HALF) / GCELL);
    if (gx < 0 || gz < 0 || gx >= GN || gz >= GN) return;
    grid_[gz * GN + gx].push_back(idx);
}

void PropField::query(vec3 p, float radius, std::vector<int>& out) const {
    out.clear();
    int x0 = (int)((p.x - radius + Terrain::HALF) / GCELL), x1 = (int)((p.x + radius + Terrain::HALF) / GCELL);
    int z0 = (int)((p.z - radius + Terrain::HALF) / GCELL), z1 = (int)((p.z + radius + Terrain::HALF) / GCELL);
    x0 = std::max(x0, 0); z0 = std::max(z0, 0);
    x1 = std::min(x1, GN - 1); z1 = std::min(z1, GN - 1);
    for (int z = z0; z <= z1; z++)
        for (int x = x0; x <= x1; x++)
            for (int i : grid_[z * GN + x]) out.push_back(i);
}

void PropField::generate(const Terrain& t) {
    Rng rng(4242);
    props.clear();
    grid_.assign(GN * GN, {});
    const float step = 4.8f;
    for (float z = -Terrain::HALF + 4; z < Terrain::HALF - 4; z += step)
        for (float x = -Terrain::HALF + 4; x < Terrain::HALF - 4; x += step) {
            float px = x + rng.range(-2.5f, 2.5f), pz = z + rng.range(-2.5f, 2.5f);
            float h = t.heightAt(px, pz);
            if (h < 0.4f) continue;
            vec3 n = t.normalAt(px, pz);
            Biome b = t.biomeAt(px, pz);
            float density = fbm(px * 0.012f, pz * 0.012f, 3, 77) * 0.5f + 0.5f;
            float roll = rng.f();
            PropType type = P_COUNT;
            float sc = rng.range(0.8f, 1.3f);
            bool steep = n.y < 0.78f;
            switch (b) {
                case B_BEACH:
                    if (roll < 0.035f) type = P_PALM;
                    else if (roll < 0.045f) type = P_ROCK;
                    break;
                case B_MEADOW:
                    if (roll < 0.03f * density) type = P_OAK;
                    else if (roll < 0.05f) type = P_EMBERBUSH;
                    else if (roll < 0.058f) type = P_DREAMBUSH;
                    else if (roll < 0.07f) type = P_ROCK;
                    else if (roll < 0.075f) type = P_BOULDER;
                    else if (roll < 0.12f) type = P_FERN;
                    break;
                case B_FOREST:
                    if (roll < 0.30f * density + 0.03f) type = rng.chance(0.55f) ? P_PINE : P_OAK;
                    else if (roll < 0.24f) type = P_EMBERBUSH;
                    else if (roll < 0.26f) type = P_DREAMBUSH;
                    else if (roll < 0.275f) type = P_SPIRITHERB;
                    else if (roll < 0.29f) type = P_ROCK;
                    else if (roll < 0.34f) type = P_FERN;
                    break;
                case B_JUNGLE:
                    if (roll < 0.20f * density + 0.04f) type = rng.chance(0.7f) ? P_JUNGLETREE : P_PALM;
                    else if (roll < 0.27f) type = P_FERN;
                    else if (roll < 0.30f) type = P_EMBERBUSH;
                    else if (roll < 0.32f) type = P_DREAMBUSH;
                    else if (roll < 0.335f) type = P_SPIRITHERB;
                    break;
                case B_HIGHLANDS:
                    if (roll < 0.05f) type = P_PINE;
                    else if (roll < 0.10f) type = P_ROCK;
                    else if (roll < 0.125f) type = P_METALROCK;
                    else if (roll < 0.14f) type = P_BOULDER;
                    else if (roll < 0.15f) type = P_CRYSTAL;
                    break;
                case B_SNOW:
                    if (roll < 0.05f) type = P_SNOWPINE;
                    else if (roll < 0.075f) type = P_CRYSTAL;
                    else if (roll < 0.1f) type = P_METALROCK;
                    else if (roll < 0.12f) type = P_ROCK;
                    break;
                case B_ASHLANDS:
                    if (roll < 0.025f) type = P_DEADTREE;
                    else if (roll < 0.07f) type = P_METALROCK;
                    else if (roll < 0.10f) type = P_ROCK;
                    else if (roll < 0.11f) type = P_CRYSTAL;
                    else if (roll < 0.12f) type = P_BONEPILE;
                    break;
                case B_CORRUPT:
                    if (roll < 0.03f) type = P_DEADTREE;
                    else if (roll < 0.06f) type = P_BONEPILE;
                    else if (roll < 0.07f) type = P_ROCK;
                    break;
                default: break;
            }
            if (type == P_COUNT) continue;
            bool isTree = type <= P_SNOWPINE;
            if (steep && isTree) continue;
            if (type == P_BOULDER) sc = rng.range(1.0f, 2.0f);
            Prop p{type, {px, h, pz}, sc, rng.range(0, TAU), 0, 0, rng.next()};
            p.hp = isTree ? 120.0f : (type == P_BOULDER ? 250.0f : 60.0f);
            props.push_back(p);
        }
    for (int i = 0; i < (int)props.size(); i++) insert(i);
}

// Builds every model in the game from code: smooth skinned creatures and people, trees,
// rocks and plants. All sizes are in metres at species size 1 (instances scale them).
#include "models.h"
#include <cstdio>
#include <chrono>

namespace {

int addBone(Rig& rig, int parent, vec3 pivot) {
    rig.bones.push_back({parent, pivot});
    return (int)rig.bones.size() - 1;
}

vec3 mixc(vec3 a, vec3 b, float t) { return lerp(a, b, t); }

// Countershading, darker backs, AO tint and optional markings.
std::function<vec3(vec3, vec3, vec3, float)> creatureColoring(int seed, float markStrength, float markFreq, vec3 markTint) {
    return [=](vec3 p, vec3 n, vec3 c, float ao) {
        float belly = smoothstep(0.15f, -0.7f, n.y);
        c = lerp(c, c * 1.22f + vec3(0.05f, 0.05f, 0.04f), belly * 0.55f);
        c = c * lerpf(1.0f, 0.86f, smoothstep(0.45f, 0.95f, n.y));
        if (markStrength > 0) {
            float mk = noise3(p * markFreq, seed) * 0.7f + noise3(p * markFreq * 2.3f, seed + 5) * 0.3f;
            c = lerp(c, c * markTint, smoothstep(0.12f, 0.32f, mk) * markStrength);
        }
        return c * (0.7f + 0.3f * ao);
    };
}

// Local frame helper for heads that can be yawed.
struct Frame3 {
    vec3 o, x, y, z;
    vec3 operator()(float a, float b, float c) const { return o + x * a + y * b + z * c; }
};

void addWing(MeshData& md, SdfModel& m, Rig& rig, int wi, int side, vec3 S, float span, float chord, vec3 boneCol, vec3 skinCol, bool feathered,
             float emissive, float armR) {
    vec3 E = S + vec3(side * span * 0.5f, span * 0.04f, -chord * 0.05f);
    vec3 T = E + vec3(side * span * 0.5f, -span * 0.02f, -chord * 0.22f);
    int win = addBone(rig, rig.body, S);
    int wout = addBone(rig, win, E);
    rig.wingIn[wi] = win;
    rig.wingOut[wi] = wout;
    Prim arm = capsule(S, E, armR, armR * 0.65f, boneCol, win, armR * 1.5f);
    m.prims.push_back(arm);
    addCylinder(md, E, T, armR * 0.6f, armR * 0.25f, boneCol, wout, 6);
    addSphere(md, E, vec3(armR * 0.7f, armR * 0.7f, armR * 0.7f), boneCol, wout, 0, 6);
    addPolygon(md, {S, E, E + vec3(0, 0, -chord * 0.95f), S + vec3(0, 0, -chord * 0.7f)}, skinCol, win, emissive * 0.5f);
    if (!feathered) {
        std::vector<vec3> outer = {E, T, T + vec3(-side * span * 0.12f, 0, -chord * 0.35f), E + vec3(side * span * 0.32f, 0, -chord * 0.8f),
                                   E + vec3(side * span * 0.08f, 0, -chord * 0.98f), E + vec3(0, 0, -chord * 0.95f)};
        addPolygon(md, outer, skinCol * 0.95f, wout, emissive);
        // Finger bones along the membrane.
        addCylinder(md, E, outer[2], armR * 0.3f, armR * 0.12f, boneCol, wout, 5);
        addCylinder(md, E, outer[3], armR * 0.3f, armR * 0.12f, boneCol, wout, 5);
        addCylinder(md, E, outer[4], armR * 0.3f, armR * 0.12f, boneCol, wout, 5);
    } else {
        addPolygon(md, {E, T, E + vec3(side * span * 0.3f, 0, -chord * 0.85f), E + vec3(0, 0, -chord * 0.95f)}, skinCol, wout, emissive);
        // Primary feathers fanning out from the wing tip.
        for (int f = 0; f < 7; f++) {
            float t = 0.15f + f * 0.13f;
            vec3 root = lerp(E, T, t) + vec3(0, 0.005f * f, -chord * 0.25f);
            float back = 1.0f - t * 0.6f;
            vec3 dir = normalize(vec3(side * (0.25f + t * 0.9f), -0.02f, -back));
            float len = chord * (0.55f + t * 0.35f);
            vec3 lat = normalize(cross(vec3(0, 1, 0), dir));
            float fw = chord * 0.09f;
            vec3 tip = root + dir * len;
            vec3 col = mixc(skinCol, boneCol, f % 2 ? 0.0f : 0.15f) * (0.9f + 0.03f * f);
            addPolygon(md, {root - lat * fw, root + lat * fw, tip + lat * fw * 0.6f, tip, tip - lat * fw * 0.6f}, col, wout,
                       emissive * (0.6f + t), 0, 0.008f);
        }
        // Secondary feathers along the inner trailing edge.
        for (int f = 0; f < 5; f++) {
            float t = f / 4.0f;
            vec3 root = lerp(S, E, t) + vec3(0, -0.004f, -chord * 0.55f);
            vec3 dir = normalize(vec3(side * 0.12f, -0.02f, -1.0f));
            vec3 lat = normalize(cross(vec3(0, 1, 0), dir));
            float fw = chord * 0.1f;
            vec3 tip = root + dir * chord * 0.5f;
            addPolygon(md, {root - lat * fw, root + lat * fw, tip + lat * fw * 0.6f, tip, tip - lat * fw * 0.6f}, skinCol * 0.93f, win, emissive * 0.4f, 0,
                       0.008f);
        }
    }
}

void buildQuad(int sId, Rig& rig, MeshData& md) {
    const Species& sp = SPECIES[sId];
    float L = sp.bodyL, W = sp.bodyW, H = sp.bodyH, legL = sp.legL, legT = sp.legT, hs = sp.headS;
    float vox = std::max(L, W * 1.5f) / 30.0f;
    float minR = vox * 1.3f;
    float bodyY = legL + H * 0.45f;
    vec3 c1 = sp.c1, c2 = sp.c2, c3 = sp.c3;
    bool hooves = sp.diet == D_HERBIVORE && sId != S_MOSSBACK && sId != S_JACKALOPE;
    bool carnivore = sp.diet == D_CARNIVORE;
    SdfModel m;
    m.seed = 100 + sId;
    rig.bones.clear();
    int body = addBone(rig, -1, vec3(0, bodyY, 0));
    rig.body = body;

    // Torso.
    m.prims.push_back(ellipsoid({0, bodyY, 0}, {W * 0.5f, H * 0.5f, L * 0.5f}, c1, body, H * 0.2f));
    m.prims.push_back(ellipsoid({0, bodyY + H * 0.06f, L * 0.26f}, {W * 0.5f, H * 0.54f, L * 0.27f}, c1, body, H * 0.25f));
    m.prims.push_back(ellipsoid({0, bodyY + H * 0.03f, -L * 0.28f}, {W * 0.47f, H * 0.5f, L * 0.26f}, c1, body, H * 0.25f));
    if (sp.shell) {
        Prim shell = ellipsoid({0, bodyY + H * 0.3f, 0}, {W * 0.7f, H * 0.65f, L * 0.6f}, c2, body, H * 0.1f);
        shell.disp = H * 0.05f;
        shell.dispFreq = 4.0f / H;
        m.prims.push_back(shell);
        for (int k = 0; k < 6; k++) {
            float a = k * 1.1f;
            m.prims.push_back(ellipsoid({std::sin(a) * W * 0.32f, bodyY + H * 0.88f, std::cos(a) * L * 0.32f}, {W * 0.2f, H * 0.18f, W * 0.2f}, c3, body,
                                        H * 0.15f));
        }
    }

    // Legs.
    rig.legs = 4;
    for (int k = 0; k < 4; k++) {
        float side = (k % 2) ? 1.0f : -1.0f;
        float front = (k < 2) ? 1.0f : -1.0f;
        vec3 hip(side * W * 0.33f, bodyY - H * 0.12f, front * L * 0.3f);
        vec3 knee = hip + vec3(0, -legL * 0.5f, front > 0 ? legL * 0.03f : -legL * 0.1f);
        vec3 foot(hip.x, legT * 0.35f, hip.z + (front > 0 ? legL * 0.02f : -legL * 0.02f));
        int up = addBone(rig, body, hip);
        int lo = addBone(rig, up, knee);
        rig.legUp[k] = up;
        rig.legLo[k] = lo;
        float lt = legT * 1.25f;
        m.prims.push_back(ellipsoid(hip + vec3(0, -legL * 0.08f, front > 0 ? 0 : -legT * 0.2f), {lt * 1.15f, legL * 0.3f, lt * 1.45f}, c1, up, H * 0.25f));
        m.prims.push_back(capsule(hip, knee, std::max(lt * 0.8f, minR), std::max(lt * 0.5f, minR), c1, up, lt * 0.45f));
        m.prims.push_back(capsule(knee, foot, std::max(lt * 0.5f, minR), std::max(lt * 0.4f, minR), c1 * 0.93f, lo, lt * 0.3f));
        vec3 footCol = hooves ? vec3(0.13f, 0.11f, 0.1f) : c1 * 0.8f;
        m.prims.push_back(ellipsoid(foot + vec3(0, 0, legT * 0.22f), {legT * 0.62f, legT * 0.42f, legT * 0.85f}, footCol, lo, legT * 0.2f));
        if (carnivore)
            for (int c = -1; c <= 1; c++)
                addCone(md, foot + vec3(c * legT * 0.3f, legT * 0.05f, legT * 0.85f), foot + vec3(c * legT * 0.35f, -legT * 0.3f, legT * 1.35f), legT * 0.13f,
                        vec3(0.8f, 0.77f, 0.7f), lo, 0, 6);
    }

    // Necks and heads.
    rig.heads = sp.heads;
    for (int h = 0; h < sp.heads; h++) {
        float spread = sp.heads > 1 ? (h - (sp.heads - 1) * 0.5f) / ((sp.heads - 1) * 0.5f) : 0.0f;
        float yawOff = spread * (sp.heads > 3 ? 0.55f : 0.4f);
        float na = sp.neckAngle;
        vec3 base(spread * W * 0.33f, bodyY + H * 0.25f, L * 0.42f);
        vec3 dir(std::sin(yawOff) * std::cos(na), std::sin(na), std::cos(yawOff) * std::cos(na));
        float nl = std::max(sp.neckL, 0.05f);
        vec3 mid = base + dir * nl * 0.5f, end = base + dir * nl;
        int n0 = addBone(rig, body, base), n1 = addBone(rig, n0, mid), hd = addBone(rig, n1, end);
        rig.neck0[h] = n0;
        rig.neck1[h] = n1;
        rig.head[h] = hd;
        float nr = sp.heads > 3 ? 0.36f : 0.5f;
        vec3 headCol = sp.beak ? c2 : c1;  // eagle-fronted beasts get a white feathered head
        m.prims.push_back(capsule(base, mid, hs * nr, hs * nr * 0.86f, sp.beak ? mixc(c1, c2, 0.6f) : c1, n0, hs * 0.3f));
        m.prims.push_back(capsule(mid, end, hs * nr * 0.86f, hs * nr * 0.74f, headCol, n1, hs * 0.2f));
        Frame3 F{end + vec3(std::sin(yawOff), 0, std::cos(yawOff)) * hs * 0.35f + vec3(0, hs * 0.05f, 0), vec3(std::cos(yawOff), 0, -std::sin(yawOff)),
                 vec3(0, 1, 0), vec3(std::sin(yawOff), 0, std::cos(yawOff))};
        vec3 hc = F.o;
        m.prims.push_back(ellipsoidRot(hc, {hs * 0.5f, hs * 0.44f, hs * 0.58f}, yawOff, 0, 0, headCol, hd, hs * 0.15f));
        int jw = addBone(rig, hd, F(0, -hs * 0.2f, 0));
        rig.jaw[h] = jw;
        if (!sp.beak) {
            m.prims.push_back(ellipsoidRot(F(0, -hs * 0.05f, hs * 0.62f), {hs * 0.32f, hs * 0.26f, hs * 0.46f}, yawOff, 0, 0, c1, hd, hs * 0.15f));
            m.prims.push_back(capsule(F(0, -hs * 0.24f, hs * 0.1f), F(0, -hs * 0.3f, hs * 0.85f), hs * 0.17f, hs * 0.1f, mixc(c1, c2, 0.25f), jw, hs * 0.08f));
            if (carnivore) {
                m.prims.push_back(ellipsoidRot(F(0, hs * 0.18f, hs * 0.28f), {hs * 0.42f, hs * 0.13f, hs * 0.25f}, yawOff, 0, 0, c1, hd, hs * 0.12f));
                for (int s = -1; s <= 1; s += 2)
                    addCone(md, F(s * hs * 0.17f, -hs * 0.16f, hs * 0.85f), F(s * hs * 0.17f, -hs * 0.38f, hs * 0.88f), hs * 0.055f, vec3(0.95f, 0.93f, 0.85f),
                            hd, 0, 6);
            }
        } else {
            addCone(md, F(0, hs * 0.06f, hs * 0.38f), F(0, -hs * 0.24f, hs * 1.15f), hs * 0.24f, c3, hd, 0, 10);
            addCone(md, F(0, -hs * 0.14f, hs * 0.38f), F(0, -hs * 0.24f, hs * 0.92f), hs * 0.13f, c3 * 0.85f, jw, 0, 8);
        }
        float eyeE = sp.glowEyes ? 1.6f : 0.0f;
        vec3 eyeCol = sp.glowEyes ? c3 : vec3(0.03f, 0.025f, 0.02f);
        for (int s = -1; s <= 1; s += 2) addSphere(md, F(s * hs * 0.34f, hs * 0.15f, hs * (sp.beak ? 0.28f : 0.38f)), vec3(hs * 0.1f, hs * 0.1f, hs * 0.1f), eyeCol, hd, eyeE, 8);
        if (sp.horns == 1) {
            addCone(md, F(0, hs * 0.32f, hs * 0.36f), F(0, hs * 1.4f, hs * 1.05f), hs * 0.13f, c3, hd, sp.glow, 10);
        } else if (sp.horns > 1) {
            vec3 hornCol = sId == S_CHIMERA ? vec3(0.85f, 0.8f, 0.7f) : mixc(c2, vec3(0.9f, 0.85f, 0.75f), 0.5f);
            for (int k = 0; k < sp.horns; k++) {
                float s = (k % 2) ? 1.0f : -1.0f;
                float row = (float)(k / 2);
                vec3 b = F(s * hs * (0.28f + row * 0.1f), hs * 0.32f, -hs * (0.08f + row * 0.28f));
                vec3 m1 = F(s * hs * (0.45f + row * 0.12f), hs * (0.75f - row * 0.15f), -hs * (0.45f + row * 0.2f));
                vec3 t = F(s * hs * (0.5f + row * 0.15f), hs * (1.05f - row * 0.2f), -hs * (0.95f + row * 0.25f));
                addCylinder(md, b, m1, hs * 0.14f, hs * 0.1f, hornCol, hd, 7);
                addCone(md, m1, t, hs * 0.1f, hornCol * 1.05f, hd, 0, 7);
            }
        }
        if (sp.antlers)
            for (int s = -1; s <= 1; s += 2) {
                vec3 b = F(s * hs * 0.28f, hs * 0.38f, -hs * 0.05f), t = F(s * hs * 0.8f, hs * 1.5f, -hs * 0.4f);
                addCylinder(md, b, t, hs * 0.07f, hs * 0.04f, c3, hd, 5);
                vec3 mid2 = lerp(b, t, 0.5f);
                addCylinder(md, mid2, mid2 + vec3(0, hs * 0.5f, hs * 0.3f), hs * 0.05f, hs * 0.03f, c3, hd, 5);
                addCylinder(md, lerp(b, t, 0.75f), lerp(b, t, 0.75f) + vec3(s * hs * 0.4f, hs * 0.3f, 0), hs * 0.04f, hs * 0.02f, c3, hd, 5);
            }
        if (sp.tusks)
            for (int s = -1; s <= 1; s += 2)
                addCone(md, F(s * hs * 0.26f, -hs * 0.18f, hs * 0.8f), F(s * hs * 0.48f, hs * 0.3f, hs * 1.65f), hs * 0.13f, c3, hd, 0, 8);
        bool ears = sp.mane || sId == S_KITSUNE || sId == S_JACKALOPE;
        if (ears) {
            float earLen = sId == S_JACKALOPE ? 1.9f : (sId == S_KITSUNE ? 1.3f : 0.9f);
            for (int s = -1; s <= 1; s += 2)
                addCone(md, F(s * hs * 0.24f, hs * 0.35f, -hs * 0.1f), F(s * hs * 0.32f, hs * (0.35f + 0.5f * earLen), -hs * 0.28f), hs * 0.14f, c1, hd, 0, 8);
        }
        if (sp.mane) {
            vec3 upv(0, 1, 0);
            m.prims.push_back(capsule(base + upv * hs * 0.3f - dir * hs * 0.1f, mid + upv * hs * 0.33f, hs * 0.24f, hs * 0.2f, c2, n0, hs * 0.18f));
            m.prims.push_back(capsule(mid + upv * hs * 0.33f, end + upv * hs * 0.3f - dir * hs * 0.1f, hs * 0.2f, hs * 0.14f, c2, n1, hs * 0.15f));
            if (carnivore)
                for (int k = 0; k < 6; k++) {
                    float t = k / 5.0f;
                    vec3 p = lerp(base, end, t) + upv * hs * 0.35f;
                    addCone(md, p, p + vec3(std::sin(k * 2.1f) * hs * 0.3f, hs * 0.45f, -hs * 0.35f), hs * 0.16f, c2, t < 0.5f ? n0 : n1, 0, 6);
                }
        }
    }

    // Tails.
    rig.tails = sp.tails;
    rig.tailSegs = 5;
    const int N = 5;
    for (int t = 0; t < sp.tails; t++) {
        float fan = sp.tails > 1 ? (t - (sp.tails - 1) * 0.5f) * 0.42f : 0.0f;
        vec3 prev(0, bodyY + (sp.stinger ? H * 0.2f : 0.0f), -L * 0.46f);
        int parent = body;
        float segLen = sp.tailL / N;
        vec3 dir;
        for (int k = 0; k < N; k++) {
            if (sp.stinger) dir = normalize(vec3(0, 0.35f + k * 0.35f, -1.0f + k * 0.38f));
            else if (sp.tails > 1) dir = normalize(vec3(std::sin(fan), 0.5f - k * 0.08f, -std::cos(fan)));
            else dir = normalize(vec3(0, -0.28f + (sp.wings || sp.fins ? 0.12f : 0.0f), -1.0f));
            vec3 next = prev + dir * segLen;
            int b = addBone(rig, parent, prev);
            rig.tail[t][k] = b;
            parent = b;
            float r0 = W * 0.17f * (1.0f - (float)k / N * 0.75f), r1 = W * 0.17f * (1.0f - (float)(k + 1) / N * 0.75f);
            vec3 col = c1;
            if (sp.tails > 1) {
                r0 = W * 0.24f * (0.55f + std::sin((k + 0.5f) / N * PI) * 0.6f);
                r1 = W * 0.24f * (0.55f + std::sin((k + 1.5f) / N * PI) * 0.6f);
                if (k >= N - 2) col = c2;
            }
            if (sp.mane && sp.diet == D_HERBIVORE) col = c2;
            m.prims.push_back(capsule(prev, next, std::max(r0, minR), std::max(r1, minR), col, b, W * 0.1f));
            if (sp.fins && (sp.wings || sId == S_STORM_DRAKE || sId == S_HYDRA) && k < N - 1)
                addCone(md, (prev + next) * 0.5f + vec3(0, r0 * 0.8f, 0), (prev + next) * 0.5f + vec3(0, r0 * 0.8f + W * 0.22f, -W * 0.1f), W * 0.08f, c2, b, 0, 6);
            prev = next;
        }
        int last = rig.tail[t][N - 1];
        if (sp.stinger) {
            addCone(md, prev, prev + vec3(0, -hs * 0.3f, hs * 0.9f), hs * 0.18f, c3, last, sp.glow, 8);
        } else if (sId == S_CHIMERA) {
            m.prims.push_back(ellipsoid(prev + dir * W * 0.12f, {W * 0.14f, W * 0.11f, W * 0.2f}, c3, last, W * 0.06f));
            for (int s = -1; s <= 1; s += 2) addSphere(md, prev + dir * W * 0.2f + vec3(s * W * 0.08f, W * 0.06f, 0), vec3(W * 0.03f, W * 0.03f, W * 0.03f), vec3(1, 0.8f, 0.1f), last, 1.0f, 6);
        } else if (sp.tails > 1) {
            addSphere(md, prev, vec3(W * 0.13f, W * 0.13f, W * 0.13f), c3, last, 2.0f, 8);
        } else if (sp.wings || sp.fins) {
            addPolygon(md, {prev, prev + vec3(W * 0.32f, 0, -W * 0.35f), prev + vec3(0, 0, -W * 0.85f), prev + vec3(-W * 0.32f, 0, -W * 0.35f)}, c2, last, 0, 0, W * 0.04f);
        } else if (sp.mane) {
            m.prims.push_back(capsule(prev, prev + vec3(0, -sp.tailL * 0.55f, -sp.tailL * 0.3f), W * 0.15f, W * 0.07f, c2, last, W * 0.1f));
        }
    }

    // Spines / fins along the back.
    if (sp.fins) {
        bool flame = sId == S_SALAMANDER;
        for (int k = 0; k < 6; k++) {
            float z = L * (0.38f - k * 0.15f);
            vec3 b(0, bodyY + H * 0.45f, z);
            float hgt = H * (flame ? 0.55f : 0.45f) * (1.0f - std::fabs(k - 2.0f) * 0.12f);
            addCone(md, b, b + vec3(0, hgt, -H * 0.18f), H * 0.2f, flame || sId == S_KELPIE ? c3 : c2, body,
                    flame ? 1.6f : (sId == S_KELPIE ? 0.4f : 0.0f), 7);
        }
    }

    // Wings.
    if (sp.wings) {
        bool feathered = sp.beak || sId == S_PEGASUS;
        rig.wings = true;
        float span = L * (feathered ? 1.05f : 1.2f), chord = L * 0.55f;
        for (int wi = 0; wi < 2; wi++) {
            int side = wi == 0 ? -1 : 1;
            vec3 S(side * W * 0.36f, bodyY + H * 0.38f, L * 0.15f);
            addWing(md, m, rig, wi, side, S, span, chord, feathered ? c2 : c1, c2, feathered, sp.glow > 0.7f ? sp.glow * 0.3f : 0.0f, W * 0.09f);
        }
    }
    rig.seatY = bodyY + H * (sp.shell ? 0.9f : 0.5f);

    float markStrength = 0, markFreq = 3.0f / L;
    vec3 markTint(0.7f, 0.7f, 0.7f);
    if (sId == S_SALAMANDER) markStrength = 0.8f, markTint = c2 / std::max(0.01f, c1.x);
    if (sId == S_HYDRA || sId == S_BEHEMOTH || sId == S_JACKALOPE) markStrength = 0.35f;
    if (sId == S_FIRE_DRAGON || sId == S_STORM_DRAKE) markStrength = 0.3f, markTint = vec3(0.6f, 0.55f, 0.6f);
    m.colorFn = creatureColoring(m.seed, markStrength, markFreq, markTint);
    buildSdf(m, vox, md);
}

void buildBird(int sId, Rig& rig, MeshData& md) {
    const Species& sp = SPECIES[sId];
    float L = sp.bodyL, W = sp.bodyW, H = sp.bodyH, legL = sp.legL, hs = sp.headS;
    float vox = std::max(L, W * 1.5f) / 30.0f;
    float bodyY = legL + H * 0.5f;
    vec3 c1 = sp.c1, c2 = sp.c2, c3 = sp.c3;
    float bodyGlow = sId == S_PHOENIX ? 0.35f : 0.0f;
    SdfModel m;
    m.seed = 300 + sId;
    int body = addBone(rig, -1, vec3(0, bodyY, 0));
    rig.body = body;
    Prim torso = ellipsoidRot({0, bodyY, 0}, {W * 0.55f, H * 0.55f, L * 0.55f}, 0, -0.25f, 0, c1, body, H * 0.2f);
    torso.emissive = bodyGlow;
    m.prims.push_back(torso);
    Prim chest = ellipsoid({0, bodyY + H * 0.08f, L * 0.25f}, {W * 0.48f, H * 0.5f, L * 0.32f}, mixc(c1, c2, 0.35f), body, H * 0.25f);
    chest.emissive = bodyGlow;
    m.prims.push_back(chest);
    m.prims.push_back(ellipsoid({0, bodyY, -L * 0.42f}, {W * 0.3f, H * 0.25f, L * 0.25f}, c1, body, H * 0.2f));
    rig.legs = 2;
    for (int k = 0; k < 2; k++) {
        float side = k == 0 ? -1.0f : 1.0f;
        vec3 hip(side * W * 0.25f, bodyY - H * 0.3f, L * 0.05f), knee(side * W * 0.27f, legL * 0.5f, -L * 0.04f), foot(side * W * 0.28f, 0.04f, L * 0.03f);
        int up = addBone(rig, body, hip), lo = addBone(rig, up, knee);
        rig.legUp[k] = up;
        rig.legLo[k] = lo;
        m.prims.push_back(ellipsoid(hip, {W * 0.2f, H * 0.28f, W * 0.22f}, c1, up, H * 0.15f));
        m.prims.push_back(capsule(hip, knee, std::max(sp.legT * 0.6f, vox * 1.3f), std::max(sp.legT * 0.45f, vox * 1.2f), c3, up, sp.legT * 0.3f));
        m.prims.push_back(capsule(knee, foot, std::max(sp.legT * 0.45f, vox * 1.2f), std::max(sp.legT * 0.4f, vox * 1.2f), c3, lo, sp.legT * 0.3f));
        for (int c = -1; c <= 1; c++)
            addCone(md, foot, foot + vec3(c * L * 0.08f, -0.02f, L * 0.16f), sp.legT * 0.25f, vec3(0.12f, 0.1f, 0.08f), lo, 0, 6);
        addCone(md, foot, foot + vec3(0, -0.02f, -L * 0.1f), sp.legT * 0.22f, vec3(0.12f, 0.1f, 0.08f), lo, 0, 6);
    }
    rig.heads = 1;
    float na = sp.neckAngle;
    vec3 base(0, bodyY + H * 0.35f, L * 0.38f), dir(0, std::sin(na), std::cos(na));
    vec3 mid = base + dir * sp.neckL * 0.5f, end = base + dir * sp.neckL;
    int n0 = addBone(rig, body, base), n1 = addBone(rig, n0, mid), hd = addBone(rig, n1, end);
    rig.neck0[0] = n0;
    rig.neck1[0] = n1;
    rig.head[0] = hd;
    m.prims.push_back(capsule(base, mid, hs * 0.5f, hs * 0.43f, c1, n0, hs * 0.3f));
    m.prims.push_back(capsule(mid, end, hs * 0.43f, hs * 0.38f, c1, n1, hs * 0.2f));
    vec3 hc = end + vec3(0, hs * 0.05f, hs * 0.25f);
    Prim headP = ellipsoid(hc, {hs * 0.5f, hs * 0.46f, hs * 0.56f}, c1, hd, hs * 0.15f);
    headP.emissive = bodyGlow;
    m.prims.push_back(headP);
    int jw = addBone(rig, hd, hc + vec3(0, -hs * 0.1f, hs * 0.3f));
    rig.jaw[0] = jw;
    addCone(md, hc + vec3(0, hs * 0.08f, hs * 0.36f), hc + vec3(0, -hs * 0.32f, hs * 1.2f), hs * 0.22f, c3, hd, 0, 10);
    addCone(md, hc + vec3(0, -hs * 0.12f, hs * 0.36f), hc + vec3(0, -hs * 0.2f, hs * 0.9f), hs * 0.12f, c3 * 0.85f, jw, 0, 8);
    float eyeE = sp.glowEyes ? 1.6f : 0.0f;
    for (int s = -1; s <= 1; s += 2)
        addSphere(md, hc + vec3(s * hs * 0.32f, hs * 0.12f, hs * 0.28f), vec3(hs * 0.1f, hs * 0.1f, hs * 0.1f), sp.glowEyes ? c3 : vec3(0.03f, 0.03f, 0.02f), hd,
                  eyeE, 8);
    if (sId == S_PHOENIX || sId == S_THUNDERBIRD)
        for (int k = 0; k < 4; k++)
            addCone(md, hc + vec3(0, hs * 0.35f, -k * hs * 0.18f), hc + vec3(0, hs * (1.2f - k * 0.15f), -hs * (0.55f + k * 0.3f)), hs * 0.14f, c3, hd, sp.glow, 6);
    // Tail feathers.
    rig.tails = 1;
    rig.tailSegs = 1;
    int tb = addBone(rig, body, vec3(0, bodyY, -L * 0.5f));
    rig.tail[0][0] = tb;
    for (int k = -2; k <= 2; k++) {
        float a = k * 0.22f;
        vec3 root(0, bodyY - H * 0.05f, -L * 0.5f);
        vec3 d = normalize(vec3(std::sin(a), -0.15f, -std::cos(a)));
        vec3 lat = normalize(cross(vec3(0, 1, 0), d));
        float len = sp.tailL * (1.0f - std::fabs((float)k) * 0.12f);
        float fw = L * 0.08f;
        vec3 tip = root + d * len;
        vec3 col = (k % 2) ? c2 : c3;
        addPolygon(md, {root - lat * fw * 0.6f, root + lat * fw * 0.6f, tip + lat * fw, tip + d * fw, tip - lat * fw}, col, tb,
                   sId == S_PHOENIX ? 1.2f : (sId == S_THUNDERBIRD ? 0.3f : 0.0f), 0, 0.01f);
    }
    rig.wings = true;
    float span = L * 1.6f, chord = L * 0.55f;
    for (int wi = 0; wi < 2; wi++) {
        int side = wi == 0 ? -1 : 1;
        vec3 S(side * W * 0.42f, bodyY + H * 0.3f, L * 0.12f);
        addWing(md, m, rig, wi, side, S, span, chord, c1, c2, true, sId == S_PHOENIX ? 0.9f : (sId == S_THUNDERBIRD ? 0.25f : 0.0f), W * 0.1f);
    }
    rig.seatY = bodyY + H * 0.55f;
    m.colorFn = creatureColoring(m.seed, 0, 1, vec3(1, 1, 1));
    buildSdf(m, vox, md);
}

void buildSerpent(int sId, Rig& rig, MeshData& md) {
    const Species& sp = SPECIES[sId];
    float L = sp.bodyL, W = sp.bodyW, H = sp.bodyH;
    float vox = W / 9.0f;
    vec3 c1 = sp.c1, c2 = sp.c2, c3 = sp.c3;
    SdfModel m;
    m.seed = 500 + sId;
    const int N = 12;
    float seg = L / N;
    vec3 pts[N + 1];
    for (int k = 0; k <= N; k++) {
        float t = (float)k / N;
        float y = H * 0.5f * (1.0f - t * 0.45f) + (k < 3 ? (3 - k) * H * 0.32f : 0.0f);
        pts[k] = vec3(0, y, L * 0.42f - k * seg);
    }
    rig.spineN = N;
    for (int k = 0; k < N; k++) rig.spine[k] = addBone(rig, k == 0 ? -1 : rig.spine[k - 1], pts[k]);
    rig.body = rig.spine[N / 2];
    for (int k = 0; k < N; k++) {
        float t0 = (float)k / N, t1 = (float)(k + 1) / N;
        m.prims.push_back(capsule(pts[k], pts[k + 1], W * 0.55f * (1.0f - t0 * 0.78f), W * 0.55f * (1.0f - t1 * 0.78f), (k % 2) ? c1 : c1 * 0.92f,
                                  rig.spine[k], W * 0.25f));
    }
    rig.heads = 1;
    int hd = addBone(rig, rig.spine[0], pts[0] + vec3(0, 0, W * 0.3f));
    rig.head[0] = hd;
    rig.neck0[0] = rig.neck1[0] = rig.spine[0];
    vec3 hc = pts[0] + vec3(0, H * 0.12f, W * 0.85f);
    m.prims.push_back(ellipsoid(hc, {W * 0.55f, W * 0.4f, W * 0.75f}, c1, hd, W * 0.2f));
    m.prims.push_back(ellipsoid(hc + vec3(0, W * 0.1f, -W * 0.2f), {W * 0.62f, W * 0.3f, W * 0.45f}, c1, hd, W * 0.2f));  // hood
    int jw = addBone(rig, hd, hc + vec3(0, -W * 0.15f, -W * 0.1f));
    rig.jaw[0] = jw;
    m.prims.push_back(capsule(hc + vec3(0, -W * 0.18f, -W * 0.1f), hc + vec3(0, -W * 0.22f, W * 0.6f), W * 0.2f, W * 0.12f, c2, jw, W * 0.08f));
    for (int s = -1; s <= 1; s += 2) {
        addSphere(md, hc + vec3(s * W * 0.33f, W * 0.15f, W * 0.35f), vec3(W * 0.11f, W * 0.09f, W * 0.11f), c3, hd, 2.0f, 8);
        addCone(md, hc + vec3(s * W * 0.15f, -W * 0.12f, W * 0.55f), hc + vec3(s * W * 0.15f, -W * 0.42f, W * 0.6f), W * 0.06f, vec3(0.95f, 0.93f, 0.85f), hd, 0, 6);
    }
    for (int k = 0; k < sp.horns; k++) {
        float a = (k - (sp.horns - 1) * 0.5f) * 0.38f;
        vec3 b = hc + vec3(std::sin(a) * W * 0.4f, W * 0.3f, -W * 0.25f);
        addCone(md, b, b + vec3(std::sin(a) * W * 0.35f, W * 0.85f, -W * 0.45f), W * 0.12f, c3, hd, sp.glow * 0.5f, 7);
    }
    rig.seatY = H * 0.5f + W * 0.55f;
    m.colorFn = [seed = m.seed, c2](vec3 p, vec3 n, vec3 c, float ao) {
        float belly = smoothstep(0.0f, -0.6f, n.y);
        c = lerp(c, c2, belly * 0.85f);
        float band = noise3(p * vec3(1.5f, 1.5f, 3.0f), seed);
        c = lerp(c, c * 0.65f, smoothstep(0.15f, 0.3f, band) * (1.0f - belly) * 0.6f);
        return c * (0.7f + 0.3f * ao);
    };
    buildSdf(m, vox, md);
}

// Player and tribe members share one humanoid rig.
void buildHumanoid(int kind, Rig& rig, MeshData& md) {
    // kind: -1 player, otherwise InfectedClass
    bool inf = kind >= 0;
    const InfectedInfo* info = inf ? &INFECTED[kind] : nullptr;
    vec3 skin = inf ? info->skin : vec3(0.86f, 0.66f, 0.52f);
    vec3 shirt = inf ? info->skin : vec3(0.42f, 0.3f, 0.19f);
    vec3 pants = inf ? info->skin * 0.9f : vec3(0.3f, 0.24f, 0.17f);
    vec3 boots = inf ? info->skin * 0.7f : vec3(0.2f, 0.13f, 0.08f);
    vec3 cloth = inf ? info->cloth : vec3(0.5f, 0.38f, 0.22f);
    vec3 hair = vec3(0.22f, 0.13f, 0.07f);
    float bulk = inf && (kind == IC_BRUTE || kind == IC_CHIEFTAIN) ? 1.3f : (inf && kind == IC_STALKER ? 0.82f : 1.0f);
    SdfModel m;
    m.seed = 700 + kind;
    int pelvis = addBone(rig, -1, {0, 0.95f, 0});
    int torso = addBone(rig, pelvis, {0, 1.05f, 0});
    int head = addBone(rig, torso, {0, 1.62f, 0});
    rig.pelvis = pelvis;
    rig.torso = torso;
    rig.headB = head;
    rig.body = pelvis;
    for (int s = 0; s < 2; s++) {
        float x = s == 0 ? -1.0f : 1.0f;  // 0 = right side (x negative)
        rig.armUp[s] = addBone(rig, torso, {x * 0.31f, 1.53f, 0});
        rig.armLo[s] = addBone(rig, rig.armUp[s], {x * 0.34f, 1.25f, 0});
        rig.handRest[s] = vec3(x * 0.36f, 0.98f, 0.03f);
    }
    for (int s = 0; s < 2; s++) {
        float x = s == 0 ? -1.0f : 1.0f;
        rig.thigh[s] = addBone(rig, pelvis, {x * 0.115f, 0.93f, 0});
        rig.shin[s] = addBone(rig, rig.thigh[s], {x * 0.115f, 0.5f, 0.02f});
    }
    auto P = [&](Prim p) { m.prims.push_back(p); };
    P(ellipsoid({0, 1.36f, 0}, vec3(0.2f, 0.25f, 0.125f) * vec3(bulk, 1, bulk), shirt, torso, 0.06f));
    P(ellipsoid({0, 1.49f, -0.005f}, vec3(0.25f * bulk, 0.09f, 0.115f * bulk), shirt, torso, 0.06f));
    P(ellipsoid({0, 1.13f, 0}, vec3(0.165f, 0.17f, 0.115f) * vec3(bulk, 1, bulk), shirt, torso, 0.06f));
    P(ellipsoid({0, 0.96f, 0}, vec3(0.185f * bulk, 0.12f, 0.125f), pants, pelvis, 0.05f));
    P(capsule({0, 1.56f, 0}, {0, 1.7f, 0.01f}, 0.055f * bulk, 0.05f, skin, head, 0.03f));
    P(ellipsoid({0, 1.83f, 0.01f}, {0.105f, 0.13f, 0.12f}, skin, head, 0.03f));
    P(ellipsoid({0, 1.755f, 0.045f}, {0.08f, 0.055f, 0.075f}, skin, head, 0.03f));
    P(capsule({0, 1.83f, 0.115f}, {0, 1.795f, 0.14f}, 0.02f, 0.016f, skin, head, 0.015f));
    if (!inf) {
        P(ellipsoid({0, 1.88f, -0.015f}, {0.114f, 0.105f, 0.125f}, hair, head, 0.02f));
        P(ellipsoid({0, 1.0f, 0}, {0.195f, 0.035f, 0.135f}, vec3(0.25f, 0.15f, 0.08f), pelvis, 0.01f));
        P(ellipsoid({0, 1.25f, 0.05f}, {0.17f, 0.24f, 0.09f}, cloth, torso, 0.03f));  // tunic front
    } else {
        P(ellipsoid({0, 0.9f, 0}, {0.2f * bulk, 0.16f, 0.14f * bulk}, cloth, pelvis, 0.03f));  // loincloth
        Prim mask = roundBox({0, 1.84f, 0.1f}, {0.1f, 0.13f, 0.03f}, 0.02f, info->mask, head, 0.01f);
        P(mask);
    }
    for (int s = 0; s < 2; s++) {
        float x = s == 0 ? -1.0f : 1.0f;
        vec3 sh(x * 0.31f, 1.53f, 0), el(x * 0.34f, 1.25f, 0), wr(x * 0.36f, 1.02f, 0.02f);
        P(capsule(sh, el, 0.062f * bulk, 0.05f * bulk, inf ? skin : shirt, rig.armUp[s], 0.03f));
        P(capsule(el, wr, 0.048f * bulk, 0.038f * bulk, skin, rig.armLo[s], 0.025f));
        P(ellipsoid(rig.handRest[s], vec3(0.04f, 0.065f, 0.032f) * bulk, skin, rig.armLo[s], 0.02f));
        vec3 hip(x * 0.115f, 0.93f, 0), kn(x * 0.115f, 0.5f, 0.02f), mid(x * 0.115f, 0.3f, 0.0f), an(x * 0.115f, 0.09f, -0.005f);
        P(capsule(hip, kn, 0.085f * bulk, 0.062f * bulk, pants, rig.thigh[s], 0.04f));
        P(capsule(kn, mid, 0.058f * bulk, 0.05f * bulk, pants, rig.shin[s], 0.03f));
        P(capsule(mid, an, 0.055f * bulk, 0.045f * bulk, boots, rig.shin[s], 0.02f));
        P(ellipsoid(vec3(x * 0.115f, 0.05f, 0.055f), vec3(0.055f, 0.045f, 0.11f) * bulk, boots, rig.shin[s], 0.03f));
    }
    if (inf) {
        for (int s = -1; s <= 1; s += 2) addSphere(md, {s * 0.045f, 1.865f, 0.135f}, {0.022f, 0.016f, 0.012f}, {1.0f, 0.12f, 0.05f}, head, 3.0f, 8);
        if (kind == IC_BRUTE || kind == IC_CHIEFTAIN)
            for (int s = -1; s <= 1; s += 2) {
                addCone(md, {s * 0.3f, 1.58f, 0}, {s * 0.48f, 1.85f, -0.04f}, 0.06f, {0.88f, 0.85f, 0.75f}, torso, 0, 7);
                addCone(md, {s * 0.24f, 1.6f, -0.05f}, {s * 0.32f, 1.82f, -0.12f}, 0.04f, {0.88f, 0.85f, 0.75f}, torso, 0, 7);
            }
        if (kind == IC_STALKER)
            for (int s = 0; s < 2; s++)
                for (int c = -1; c <= 1; c++) {
                    vec3 h = rig.handRest[s];
                    addCone(md, h + vec3(c * 0.02f, -0.04f, 0.02f), h + vec3(c * 0.03f, -0.2f, 0.08f), 0.012f, {0.85f, 0.82f, 0.72f}, rig.armLo[s], 0, 5);
                }
        if (kind == IC_SHAMAN)
            for (int k = 0; k < 5; k++) {
                float a = (k - 2) * 0.35f;
                addCone(md, {std::sin(a) * 0.08f, 1.93f, -0.03f}, {std::sin(a) * 0.25f, 2.35f - std::fabs(a) * 0.2f, -0.12f}, 0.03f,
                        k == 2 ? vec3(0.8f, 0.1f, 0.15f) : vec3(0.1f, 0.1f, 0.1f), head, 0, 5);
            }
        if (kind == IC_CHIEFTAIN) {
            for (int s = -1; s <= 1; s += 2) {
                vec3 b(s * 0.08f, 1.95f, 0);
                addCylinder(md, b, b + vec3(s * 0.25f, 0.4f, -0.08f), 0.025f, 0.015f, {0.85f, 0.8f, 0.65f}, head, 5);
                addCylinder(md, b + vec3(s * 0.12f, 0.2f, -0.04f), b + vec3(s * 0.12f, 0.48f, 0.08f), 0.02f, 0.01f, {0.85f, 0.8f, 0.65f}, head, 5);
                addCylinder(md, b + vec3(s * 0.2f, 0.33f, -0.06f), b + vec3(s * 0.42f, 0.5f, -0.04f), 0.018f, 0.01f, {0.85f, 0.8f, 0.65f}, head, 5);
            }
            addPolygon(md, {{-0.28f, 1.55f, -0.14f}, {0.28f, 1.55f, -0.14f}, {0.34f, 0.55f, -0.24f}, {-0.34f, 0.55f, -0.24f}}, info->cloth * 0.9f, torso, 0, 0, 0.02f);
            for (int k = 0; k < 7; k++) {
                float a = (k - 3) * 0.3f;
                addSphere(md, {std::sin(a) * 0.17f, 1.5f - std::cos(a) * 0.06f, 0.11f + std::cos(a) * 0.03f}, {0.025f, 0.03f, 0.02f}, {0.9f, 0.88f, 0.8f}, torso, 0, 6);
            }
        }
        m.colorFn = [seed = m.seed](vec3 p, vec3 n, vec3 c, float ao) {
            float v = std::fabs(noise3(p * 9.0f, seed));
            c = lerp(c, vec3(0.35f, 0.04f, 0.08f), smoothstep(0.06f, 0.0f, v) * 0.8f);
            return c * (0.65f + 0.35f * ao);
        };
    } else {
        m.colorFn = [](vec3, vec3, vec3 c, float ao) { return c * (0.7f + 0.3f * ao); };
    }
    m.colorSharpness = 2.0f;
    buildSdf(m, 0.017f, md);
    rig.seatY = 0;
}

// ---------------------------------------------------------------------------
// Props
// ---------------------------------------------------------------------------
vec3 jitterColor(vec3 c, Rng& r, float amt) { return c * vec3(1 + r.range(-amt, amt), 1 + r.range(-amt, amt), 1 + r.range(-amt, amt)); }

void buildTree(PropType type, int variant, float vox, MeshData& md) {
    Rng r(1000 + type * 37 + variant * 101);
    SdfModel m;
    m.seed = 900 + type * 7 + variant;
    vec3 bark(0.33f, 0.23f, 0.15f);
    switch (type) {
        case P_OAK: {
            vec3 leaf = jitterColor(vec3(0.27f, 0.47f, 0.17f), r, 0.12f);
            m.prims.push_back(capsule({0, -0.8f, 0}, {r.range(-0.2f, 0.2f), 3.0f, r.range(-0.2f, 0.2f)}, 0.45f, 0.28f, bark, 0, 0.3f));
            for (int k = 0; k < 4; k++) {
                float a = k * TAU / 4 + r.range(-0.4f, 0.4f);
                vec3 root(std::sin(a) * 0.3f, 0.1f, std::cos(a) * 0.3f);
                m.prims.push_back(capsule(root, root + vec3(std::sin(a) * 0.9f, -0.6f, std::cos(a) * 0.9f), 0.22f, 0.08f, bark, 0, 0.2f));
            }
            int blobs = 7 + variant;
            for (int k = 0; k < blobs; k++) {
                float a = r.range(0, TAU), d = r.range(0.4f, 1.9f);
                vec3 c(std::sin(a) * d, r.range(3.9f, 5.6f), std::cos(a) * d);
                float rad = r.range(1.3f, 2.0f);
                m.prims.push_back(capsule({0, 2.4f, 0}, c * 0.7f + vec3(0, 0.5f, 0), 0.2f, 0.12f, bark, 0, 0.15f));
                Prim p = ellipsoid(c, {rad, rad * 0.85f, rad}, jitterColor(leaf, r, 0.1f), 0, 0.5f);
                p.disp = 0.5f;
                p.dispFreq = 1.3f;
                m.prims.push_back(p);
            }
            break;
        }
        case P_PINE:
        case P_SNOWPINE: {
            vec3 needle = jitterColor(vec3(0.13f, 0.29f, 0.17f), r, 0.1f);
            m.prims.push_back(capsule({0, -0.6f, 0}, {0, 8.0f, 0}, 0.32f, 0.06f, bark, 0, 0.1f));
            for (int k = 0; k < 5; k++) {
                float y0 = 1.5f + k * 1.25f;
                float R = 2.4f - k * 0.4f + r.range(-0.15f, 0.15f);
                Prim tier = capsule({0, y0, 0}, {0, y0 + 2.1f, 0}, R, 0.08f, jitterColor(needle, r, 0.06f), 0, 0.1f);
                tier.disp = 0.22f;
                tier.dispFreq = 2.2f;
                m.prims.push_back(tier);
            }
            if (type == P_SNOWPINE)
                m.colorFn = [](vec3 p, vec3 n, vec3 c, float ao) {
                    float snow = smoothstep(0.35f, 0.65f, n.y + noise3(p * 2.0f, 4) * 0.25f) * smoothstep(1.0f, 1.8f, p.y);
                    return lerp(c, vec3(0.92f, 0.95f, 1.0f), snow) * (0.6f + 0.4f * ao);
                };
            break;
        }
        case P_JUNGLETREE: {
            vec3 leaf = jitterColor(vec3(0.14f, 0.44f, 0.16f), r, 0.12f);
            m.prims.push_back(capsule({0, -0.6f, 0}, {r.range(-0.4f, 0.4f), 9.0f, r.range(-0.4f, 0.4f)}, 0.55f, 0.3f, bark * 1.1f, 0, 0.3f));
            for (int k = 0; k < 5; k++) {
                float a = k * TAU / 5 + r.range(-0.3f, 0.3f);
                m.prims.push_back(capsule({0, 1.6f, 0}, {std::sin(a) * 1.8f, -0.4f, std::cos(a) * 1.8f}, 0.3f, 0.06f, bark * 1.1f, 0, 0.25f));
            }
            for (int k = 0; k < 5; k++) {
                float a = r.range(0, TAU), d = r.range(0.5f, 2.6f);
                Prim p = ellipsoid({std::sin(a) * d, r.range(8.6f, 10.2f), std::cos(a) * d}, {r.range(2.4f, 3.4f), r.range(1.0f, 1.5f), r.range(2.4f, 3.4f)},
                                   jitterColor(leaf, r, 0.1f), 0, 0.6f);
                p.disp = 0.45f;
                p.dispFreq = 1.4f;
                m.prims.push_back(p);
            }
            break;
        }
        case P_DEADTREE: {
            vec3 c(0.2f, 0.17f, 0.15f);
            m.prims.push_back(capsule({0, -0.6f, 0}, {0.3f, 4.8f, 0}, 0.38f, 0.12f, c, 0, 0.2f));
            for (int k = 0; k < 4; k++) {
                float a = r.range(0, TAU), y = r.range(2.0f, 4.0f);
                m.prims.push_back(capsule({0.1f, y, 0}, {std::sin(a) * 1.6f, y + r.range(0.6f, 1.5f), std::cos(a) * 1.6f}, 0.14f, 0.04f, c, 0, 0.1f));
            }
            break;
        }
        default: break;
    }
    if (!m.colorFn)
        m.colorFn = [](vec3 p, vec3 n, vec3 c, float ao) {
            float v = noise3(p * 0.8f, 11) * 0.15f;
            c = c * (1.0f + v) * lerpf(0.85f, 1.15f, smoothstep(-0.3f, 0.8f, n.y));
            return c * (0.45f + 0.55f * ao);
        };
    m.swayFn = [](vec3 p) { return smoothstep(1.2f, 6.0f, p.y); };
    buildSdf(m, vox, md);
}

void buildPalm(int variant, MeshData& md, bool lowDetail) {
    Rng r(2000 + variant);
    SdfModel m;
    vec3 trunk(0.5f, 0.38f, 0.25f);
    vec3 prev(0, -0.4f, 0);
    float lean = 0.3f + variant * 0.2f;
    vec3 top;
    for (int k = 1; k <= 6; k++) {
        vec3 nx(std::sin(k * 0.3f) * lean * k * 0.5f, k * 1.25f, 0);
        m.prims.push_back(capsule(prev, nx, 0.3f - k * 0.025f, 0.28f - k * 0.025f, trunk, 0, 0.05f));
        prev = nx;
    }
    top = prev;
    m.colorFn = [](vec3 p, vec3, vec3 c, float ao) { return c * (0.8f + 0.2f * std::sin(p.y * 9.0f)) * (0.6f + 0.4f * ao); };
    m.swayFn = [](vec3 p) { return smoothstep(2.0f, 7.0f, p.y) * 0.5f; };
    buildSdf(m, lowDetail ? 0.18f : 0.07f, md);
    int fronds = 9;
    for (int f = 0; f < fronds; f++) {
        float a = f * TAU / fronds + r.range(-0.2f, 0.2f);
        vec3 dir(std::sin(a), 0, std::cos(a));
        vec3 lat(dir.z, 0, -dir.x);
        vec3 p0 = top;
        int segs = lowDetail ? 3 : 6;
        for (int s = 0; s < segs; s++) {
            float t0 = (float)s / segs, t1 = (float)(s + 1) / segs;
            auto pt = [&](float t) { return top + dir * (t * 3.6f) + vec3(0, 0.6f * t - 2.2f * t * t, 0); };
            vec3 a0 = pt(t0), a1 = pt(t1);
            float w0 = 0.55f * std::sin(t0 * PI * 0.9f + 0.15f), w1 = 0.55f * std::sin(t1 * PI * 0.9f + 0.15f);
            vec3 col = vec3(0.24f, 0.5f, 0.17f) * (0.9f + 0.2f * t0);
            addPolygon(md, {a0 - lat * w0 - vec3(0, w0 * 0.3f, 0), a1 - lat * w1 - vec3(0, w1 * 0.3f, 0), a1, a0}, col, 0, 0, 0.6f + t0, 0.01f);
            addPolygon(md, {a0, a1, a1 + lat * w1 - vec3(0, w1 * 0.3f, 0), a0 + lat * w0 - vec3(0, w0 * 0.3f, 0)}, col * 0.95f, 0, 0, 0.6f + t0, 0.01f);
            p0 = a1;
        }
    }
    for (int k = 0; k < 4; k++) addSphere(md, top + vec3(std::sin(k * 1.7f) * 0.3f, -0.35f, std::cos(k * 1.7f) * 0.3f), {0.2f, 0.22f, 0.2f}, {0.35f, 0.25f, 0.12f}, 0, 0, 8);
}

void buildRock(PropType type, int variant, float vox, MeshData& md) {
    Rng r(3000 + type * 13 + variant * 7);
    SdfModel m;
    m.seed = 1200 + type * 3 + variant;
    float s = type == P_BOULDER ? 2.2f : 1.0f;
    vec3 c = jitterColor(vec3(0.46f, 0.44f, 0.41f), r, 0.06f);
    int blobs = 2 + variant % 2;
    for (int k = 0; k < blobs; k++) {
        vec3 o(r.range(-0.5f, 0.5f) * s, r.range(0.1f, 0.4f) * s, r.range(-0.5f, 0.5f) * s);
        Prim p = ellipsoidRot(o, vec3(r.range(0.8f, 1.2f), r.range(0.55f, 0.85f), r.range(0.75f, 1.1f)) * s, r.range(0, TAU), r.range(-0.3f, 0.3f), 0, c, 0, 0.3f * s);
        p.disp = 0.22f * s;
        p.dispFreq = 1.6f / s;
        m.prims.push_back(p);
    }
    bool mossy = variant % 2 == 0 && type != P_METALROCK;
    bool metal = type == P_METALROCK;
    m.colorFn = [mossy, metal, seed = m.seed](vec3 p, vec3 n, vec3 c, float ao) {
        float v = noise3(p * 2.5f, seed) * 0.5f + noise3(p * 9.0f, seed + 3) * 0.25f;
        c = c * (0.85f + v * 0.5f);
        float strata = std::sin(p.y * 7.0f + noise3(p * 1.5f, seed) * 3.0f);
        c = c * (0.93f + 0.07f * strata);
        if (mossy) c = lerp(c, vec3(0.25f, 0.4f, 0.15f), smoothstep(0.55f, 0.85f, n.y + v * 0.3f) * 0.85f);
        if (metal) {
            float ore = noise3(p * 3.0f, seed + 9);
            c = lerp(c, vec3(0.62f, 0.38f, 0.22f), smoothstep(0.2f, 0.35f, ore));
            c = lerp(c, vec3(0.75f, 0.77f, 0.82f), smoothstep(0.4f, 0.5f, noise3(p * 5.0f, seed + 4)));
        }
        return c * (0.45f + 0.55f * ao);
    };
    buildSdf(m, vox * s, md);
}

void buildPlant(PropType type, int variant, bool low, MeshData& md) {
    Rng r(4000 + type * 17 + variant);
    switch (type) {
        case P_EMBERBUSH:
        case P_DREAMBUSH: {
            SdfModel m;
            m.seed = 1500 + type + variant;
            vec3 leaf = type == P_EMBERBUSH ? vec3(0.2f, 0.42f, 0.16f) : vec3(0.22f, 0.27f, 0.33f);
            for (int k = 0; k < 6; k++) {
                float a = r.range(0, TAU), d = r.range(0.0f, 0.5f);
                Prim p = ellipsoid({std::sin(a) * d, r.range(0.35f, 0.75f), std::cos(a) * d}, vec3(r.range(0.45f, 0.65f), r.range(0.35f, 0.5f), r.range(0.45f, 0.65f)),
                                   jitterColor(leaf, r, 0.1f), 0, 0.2f);
                p.disp = 0.12f;
                p.dispFreq = 4.0f;
                m.prims.push_back(p);
            }
            m.colorFn = [](vec3 p, vec3 n, vec3 c, float ao) { return c * (0.8f + 0.4f * noise3(p * 4.0f, 7)) * (0.5f + 0.5f * ao); };
            m.swayFn = [](vec3 p) { return smoothstep(0.2f, 1.0f, p.y) * 0.4f; };
            buildSdf(m, low ? 0.12f : 0.05f, md);
            vec3 berry = type == P_EMBERBUSH ? vec3(0.95f, 0.12f, 0.08f) : vec3(0.65f, 0.3f, 0.95f);
            int n = low ? 8 : 22;
            for (int k = 0; k < n; k++) {
                float a = r.range(0, TAU), el = r.range(0.1f, 1.2f);
                vec3 d(std::cos(el) * std::sin(a), std::sin(el), std::cos(el) * std::cos(a));
                vec3 c = vec3(0, 0.55f, 0) + d * vec3(0.75f, 0.45f, 0.75f);
                addSphere(md, c, vec3(0.06f, 0.06f, 0.06f), berry, 0, type == P_DREAMBUSH ? 1.2f : 0.25f, 6);
            }
            break;
        }
        case P_SPIRITHERB:
            for (int k = 0; k < 6; k++) {
                float a = k * TAU / 6 + r.range(-0.2f, 0.2f);
                vec3 d(std::sin(a), 0, std::cos(a)), lat(d.z, 0, -d.x);
                addPolygon(md, {vec3(0, 0, 0), d * 0.25f + lat * 0.08f + vec3(0, 0.35f, 0), d * 0.45f + vec3(0, 0.55f, 0), d * 0.25f - lat * 0.08f + vec3(0, 0.35f, 0)},
                           vec3(0.3f, 0.7f, 0.9f), 0, 0.6f, 0.5f, 0.005f);
            }
            for (int k = 0; k < 3; k++)
                addSphere(md, vec3(std::sin(k * 2.1f) * 0.12f, 0.6f + k * 0.08f, std::cos(k * 2.1f) * 0.12f), vec3(0.06f, 0.06f, 0.06f), vec3(0.5f, 0.9f, 1.0f), 0, 3.0f, 6);
            break;
        case P_FERN:
            for (int f = 0; f < 7; f++) {
                float a = f * TAU / 7 + r.range(-0.3f, 0.3f);
                vec3 dir(std::sin(a), 0, std::cos(a)), lat(dir.z, 0, -dir.x);
                int segs = low ? 2 : 5;
                for (int s = 0; s < segs; s++) {
                    float t0 = (float)s / segs, t1 = (float)(s + 1) / segs;
                    auto pt = [&](float t) { return dir * (t * 1.1f) + vec3(0, 0.75f * t - 0.6f * t * t, 0); };
                    float w0 = 0.2f * std::sin(t0 * PI * 0.9f + 0.2f), w1 = 0.2f * std::sin(t1 * PI * 0.9f + 0.2f);
                    addPolygon(md, {pt(t0) - lat * w0, pt(t1) - lat * w1, pt(t1) + lat * w1, pt(t0) + lat * w0}, vec3(0.2f, 0.45f + 0.1f * t0, 0.15f), 0, 0,
                               0.8f * t0, 0.005f);
                }
            }
            break;
        case P_CRYSTAL: {
            SdfModel m;
            Prim base = ellipsoid({0, 0.1f, 0}, {0.8f, 0.4f, 0.7f}, vec3(0.35f, 0.33f, 0.36f), 0, 0.1f);
            base.disp = 0.1f;
            m.prims.push_back(base);
            buildSdf(m, low ? 0.12f : 0.06f, md);
            for (int k = 0; k < 6; k++) {
                float a = k * 1.1f + variant;
                vec3 b(std::sin(a) * 0.35f, 0.1f, std::cos(a) * 0.35f);
                vec3 d = normalize(vec3(std::sin(a) * 0.35f, 1.0f, std::cos(a) * 0.35f));
                addCrystal(md, b, d, 0.16f - k * 0.012f, 1.7f - k * 0.18f, vec3(0.4f, 0.85f, 1.0f), 0.7f);
            }
            break;
        }
        case P_BONEPILE: {
            SdfModel m;
            vec3 bone(0.86f, 0.83f, 0.74f);
            for (int k = 0; k < 5; k++) {
                float a = r.range(0, TAU);
                vec3 c(r.range(-0.5f, 0.5f), 0.12f, r.range(-0.5f, 0.5f));
                vec3 d(std::cos(a) * 0.6f, 0.05f, std::sin(a) * 0.6f);
                m.prims.push_back(capsule(c - d, c + d, 0.07f, 0.06f, bone, 0, 0.03f));
                m.prims.push_back(ellipsoid(c - d, {0.11f, 0.09f, 0.11f}, bone, 0, 0.03f));
                m.prims.push_back(ellipsoid(c + d, {0.1f, 0.08f, 0.1f}, bone, 0, 0.03f));
            }
            m.prims.push_back(ellipsoid({0.2f, 0.25f, 0.1f}, {0.22f, 0.2f, 0.26f}, bone, 0, 0.05f));
            Prim sock = ellipsoid({0.27f, 0.28f, 0.33f}, {0.06f, 0.06f, 0.06f}, bone, 0, 0.02f);
            sock.subtract = true;
            m.prims.push_back(sock);
            sock.a = vec3(0.13f, 0.28f, 0.33f);
            m.prims.push_back(sock);
            m.colorFn = [](vec3 p, vec3, vec3 c, float ao) { return c * (0.85f + 0.2f * noise3(p * 6.0f, 2)) * (0.4f + 0.6f * ao); };
            buildSdf(m, low ? 0.06f : 0.025f, md);
            break;
        }
        default: break;
    }
}

}  // namespace

void poseRig(const Rig& rig, const std::vector<mat4>& local, std::vector<mat4>& out) {
    out.resize(rig.bones.size());
    for (size_t i = 0; i < rig.bones.size(); i++) {
        const Bone& b = rig.bones[i];
        mat4 m = translate(b.pivot) * local[i] * translate(-b.pivot);
        out[i] = b.parent >= 0 ? out[b.parent] * m : m;
    }
}

void ModelLibrary::build(Renderer& r) {
    auto t0 = std::chrono::steady_clock::now();
    size_t tris = 0;
    auto upload = [&](MeshData& md) {
        tris += md.idx.size() / 3;
        return r.addMesh(md.v, md.idx);
    };
    for (int s = 0; s < S_COUNT; s++) {
        MeshData md;
        Rig& rig = creature[s];
        const Species& sp = SPECIES[s];
        if (sp.plan == BP_QUAD) buildQuad(s, rig, md);
        else if (sp.plan == BP_BIRD) buildBird(s, rig, md);
        else buildSerpent(s, rig, md);
        rig.mesh = upload(md);
        switch (s) {
            case S_FIRE_DRAGON: case S_FROST_WYVERN: case S_STORM_DRAKE: case S_BASILISK: case S_HYDRA: case S_SALAMANDER:
                rig.pattern = 1, rig.gloss = 0.55f;
                break;
            case S_PHOENIX: case S_THUNDERBIRD: case S_ROC:
                rig.pattern = 3, rig.gloss = 0.15f;
                break;
            case S_GRIFFIN: case S_MANTICORE: case S_CERBERUS: case S_CHIMERA: case S_KITSUNE: case S_JACKALOPE:
                rig.pattern = 2, rig.gloss = 0.08f;
                break;
            case S_MOSSBACK: case S_BEHEMOTH:
                rig.pattern = 5, rig.gloss = 0.1f;
                break;
            default:
                rig.pattern = 6, rig.gloss = s == S_KELPIE ? 0.6f : 0.3f;
        }
        std::printf("  modelled %-18s %6zu triangles, %2zu bones\n", sp.name, md.idx.size() / 3, rig.bones.size());
    }
    {
        MeshData md;
        buildHumanoid(-1, player, md);
        player.mesh = upload(md);
        player.pattern = 6;
        player.gloss = 0.15f;
    }
    for (int c = 0; c < IC_COUNT; c++) {
        MeshData md;
        buildHumanoid(c, infected[c], md);
        infected[c].mesh = upload(md);
        infected[c].pattern = 6;
        infected[c].gloss = 0.35f;
    }
    // Props: a few variants each, with a coarse LOD for distance.
    auto addProp = [&](PropType t, int variants, int pattern, float wind, std::function<void(int, bool, MeshData&)> fn) {
        for (int v = 0; v < variants; v++) {
            PropModel pm;
            MeshData a, b;
            fn(v, false, a);
            fn(v, true, b);
            pm.lod0 = upload(a);
            pm.lod1 = upload(b);
            pm.pattern = pattern;
            pm.wind = wind;
            props[t].push_back(pm);
        }
    };
    for (PropType t : {P_OAK, P_PINE, P_SNOWPINE, P_JUNGLETREE, P_DEADTREE})
        addProp(t, t == P_DEADTREE ? 2 : 3, 4, t == P_DEADTREE ? 0.2f : 1.0f,
                [t](int v, bool low, MeshData& md) { buildTree(t, v, low ? 0.5f : 0.17f, md); });
    addProp(P_PALM, 2, 4, 1.0f, [](int v, bool low, MeshData& md) { buildPalm(v, md, low); });
    for (PropType t : {P_ROCK, P_BOULDER, P_METALROCK})
        addProp(t, 3, 5, 0.0f, [t](int v, bool low, MeshData& md) { buildRock(t, v, low ? 0.25f : 0.085f, md); });
    for (PropType t : {P_EMBERBUSH, P_DREAMBUSH, P_SPIRITHERB, P_FERN, P_CRYSTAL, P_BONEPILE})
        addProp(t, 2, t == P_CRYSTAL || t == P_BONEPILE ? 5 : 4, t == P_CRYSTAL || t == P_BONEPILE ? 0.0f : 0.6f,
                [t](int v, bool low, MeshData& md) { buildPlant(t, v, low, md); });
    {
        SdfModel m;
        m.prims.push_back(capsule({0, 0.42f, 0}, {0, 1.0f, 0}, 0.42f, 0.3f, {1, 1, 1}, 0, 0.3f));
        m.colorFn = [](vec3 p, vec3, vec3 c, float ao) {
            float spot = noise3(p * 7.0f, 77);
            return lerp(c, c * 0.45f, smoothstep(0.25f, 0.35f, spot)) * (0.6f + 0.4f * ao);
        };
        MeshData md;
        buildSdf(m, 0.03f, md);
        eggMesh = upload(md);
    }
    double secs = std::chrono::duration<double>(std::chrono::steady_clock::now() - t0).count();
    std::printf("Model library: %d meshes, %.1fk triangles in %.1f s\n", r.meshCount(), tris / 1000.0, secs);
}

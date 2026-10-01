#version 450
#extension GL_GOOGLE_include_directive : require
#include "scene_ubo.glsl"
layout(set = 0, binding = 3) uniform sampler2D heightMap;
layout(set = 0, binding = 4) uniform sampler2D grassMap;
layout(push_constant) uniform PC { vec4 v; } pc;

layout(location = 0) in vec3 inPos;
layout(location = 0) out vec3 vWorld;
layout(location = 1) out vec3 vNormal;
layout(location = 2) out vec3 vColor;
layout(location = 3) out float vT;

uint pcg(uint v) {
    uint state = v * 747796405u + 2891336453u;
    uint word = ((state >> ((state >> 28u) + 4u)) ^ state) * 277803737u;
    return (word >> 22u) ^ word;
}
float rnd(uint n) { return float(pcg(n)) / 4294967295.0; }

void main() {
    int N = int(pc.v.y);
    const float sp = 0.4;
    int id = gl_InstanceIndex;
    ivec2 cell = ivec2(floor(S.camPos.xz / sp)) + ivec2(id % N - N / 2, id / N - N / 2);
    uint seed = uint(cell.x * 73856093) ^ uint(cell.y * 19349663);
    float r1 = rnd(seed), r2 = rnd(seed + 1u), r3 = rnd(seed + 2u), r4 = rnd(seed + 3u), r5 = rnd(seed + 4u);
    vec2 wp = (vec2(cell) + vec2(r1, r2)) * sp;
    float dist = length(wp - S.camPos.xz);
    vec2 uv = (wp + S.world.x) / (2.0 * S.world.x);
    vec4 gm = textureLod(grassMap, uv, 0.0);
    float radius = float(N) * sp * 0.5;
    float density = gm.a * (1.0 - smoothstep(radius * 0.55, radius, dist));
    vT = inPos.y;
    if (r3 > density) {
        vWorld = vec3(0.0);
        vNormal = vec3(0.0, 1.0, 0.0);
        vColor = vec3(0.0);
        gl_Position = vec4(0.0, 0.0, 2.0, 1.0);  // culled
        return;
    }
    float ground = textureLod(heightMap, uv, 0.0).r;
    float hgt = (0.25 + 0.45 * r4) * (0.45 + 0.6 * gm.a);
    float width = 0.035 + 0.035 * r5;
    float angle = r1 * 6.2831;
    vec3 side = vec3(cos(angle), 0.0, sin(angle));
    vec3 facing = vec3(-sin(angle), 0.0, cos(angle));
    float t = S.params.x;
    vec2 windDir = normalize(vec2(1.0, 0.4));
    float wind = (sin(t * 1.6 + dot(wp, windDir) * 0.35) * 0.5 + 0.5) * 0.55 + sin(t * 4.3 + wp.x * 1.7 + wp.y * 1.3) * 0.12;
    wind *= S.world.z;
    vec2 away = wp - S.playerPos.xz;
    float pd = length(away);
    float push = (1.0 - smoothstep(0.3, 1.5, pd)) * step(abs(S.playerPos.y - ground), 2.5);
    float tt = inPos.y;
    bool flower = r5 > 0.955 && gm.a > 0.55;
    if (flower) {
        hgt *= 1.25;
        if (tt > 0.9) width *= 3.2;
    }
    vec3 bend = vec3(windDir.x, 0.0, windDir.y) * wind * tt * tt * hgt;
    bend += vec3(away.x, 0.0, away.y) / max(pd, 0.01) * push * tt * hgt * 0.9;
    bend += facing * (r2 - 0.5) * 0.4 * tt * tt * hgt;
    vec3 p = vec3(wp.x, ground, wp.y) + side * inPos.x * width + vec3(0.0, tt * hgt, 0.0) + bend;
    p.y -= length(bend) * 0.35 * tt;
    vWorld = p;
    vNormal = normalize(mix(facing, vec3(0.0, 1.0, 0.0), 0.7));
    vec3 c = pow(gm.rgb, vec3(2.2)) * 0.62;
    c *= mix(0.3, 1.1, tt);
    c = mix(c, c * vec3(1.35, 1.2, 0.55), r5 * r5 * tt);
    if (flower && tt > 0.6) {
        float k = fract(r2 * 7.0);
        vec3 petal = k < 0.3 ? vec3(0.9, 0.85, 0.75) : (k < 0.55 ? vec3(0.95, 0.75, 0.1) : (k < 0.8 ? vec3(0.55, 0.3, 0.85) : vec3(0.85, 0.2, 0.25)));
        c = petal * 0.55;
    }
    vColor = c;
    gl_Position = S.viewProj * vec4(p, 1.0);
}

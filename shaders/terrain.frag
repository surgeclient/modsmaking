#version 450
#extension GL_GOOGLE_include_directive : require
#include "scene_ubo.glsl"
#include "noise.glsl"
#include "lighting.glsl"
layout(set = 0, binding = 4) uniform sampler2D grassMap;

layout(location = 0) in vec3 vWorld;
layout(location = 1) in vec3 vNormal;
layout(location = 2) in vec3 vColor;
layout(location = 3) in float vEmis;
layout(location = 4) in float vAO;
layout(location = 0) out vec4 outColor;

void main() {
    vec3 n = normalize(vNormal);
    vec2 p = vWorld.xz;
    float dist = length(S.camPos.xyz - vWorld);
    float detail = 1.0 - smoothstep(40.0, 160.0, dist);
    float slope = 1.0 - n.y;
    float n1 = fbm2(p * 0.035, 4);
    float n2 = fbm2(p * 0.31, 3);
    float n3 = vnoise(p * 2.7);
    vec2 uv = (p + S.world.x) / (2.0 * S.world.x);
    float dens = texture(grassMap, uv).a;

    vec3 base = vColor;
    float lum = dot(base, vec3(0.3, 0.59, 0.11));
    vec3 col = base * (0.72 + 0.5 * n1 + 0.18 * (n2 - 0.5));

    // Dirt and dry patches in grassy areas.
    vec3 dirt = vec3(0.13, 0.09, 0.055) * (0.8 + 0.5 * n2);
    float dirtMask = smoothstep(0.55, 0.72, n2 + (1.0 - dens) * 0.15 + n1 * 0.2) * smoothstep(0.05, 0.4, dens);
    col = mix(col, dirt, dirtMask * 0.75);
    col = mix(col, col * vec3(1.15, 1.05, 0.7), smoothstep(0.5, 0.8, n1) * dens * 0.5);

    // Layered rock on steep slopes.
    float rockMask = smoothstep(0.24, 0.42, slope + (n2 - 0.5) * 0.12);
    float strata = 0.5 + 0.5 * sin(vWorld.y * 1.7 + n1 * 7.0 + n3 * 0.6);
    vec3 rock = mix(vec3(0.085, 0.08, 0.075), vec3(0.2, 0.185, 0.165), strata) * (0.75 + 0.5 * n2);
    rock *= mix(0.35, 1.0, smoothstep(0.01, 0.06, lum));  // dark rock in the ashlands / Hollow
    col = mix(col, rock, rockMask);

    // Sand ripples and wet sand at the shoreline.
    bool sandy = base.r > base.g && base.g > base.b && lum > 0.25;
    if (sandy) {
        float ripple = sin(p.x * 2.1 + p.y * 0.9 + n2 * 5.0) * 0.5 + 0.5;
        col *= mix(1.0, 0.88 + 0.16 * ripple, detail);
    }
    float wet = 1.0 - smoothstep(0.0, 1.4, vWorld.y);
    col = mix(col, col * 0.45, wet * 0.8);

    // Snow sparkle.
    if (lum > 0.7) {
        float spark = step(0.996, hash12(floor(p * 30.0))) * detail;
        col += vec3(spark) * 3.0 * (1.0 - S.params.y);
    }

    // Micro normal detail.
    float e = 0.08;
    float hx = vnoise(p * 3.0 + vec2(e, 0.0)) - vnoise(p * 3.0 - vec2(e, 0.0));
    float hz = vnoise(p * 3.0 + vec2(0.0, e)) - vnoise(p * 3.0 - vec2(0.0, e));
    float bump = (0.35 + rockMask * 0.9) * detail;
    n = normalize(n + vec3(-hx, 0.0, -hz) * bump * 1.5);

    float ao = vAO * (0.75 + 0.25 * n2) * mix(1.0, 0.8, dens * detail);
    vec3 lit = shade(col, n, vWorld, ao, rockMask * 0.15 + wet * 0.5, 0.0);
    // Glowing lava and the Hollow's veins.
    if (vEmis > 0.01) {
        // Carve thin glowing cracks inside the lava / vein regions.
        float cr = abs(vnoise(p * 0.45) - 0.5) + abs(vnoise(p * 1.7 + 3.0) - 0.5) * 0.35;
        float crack = 1.0 - smoothstep(0.02, 0.09, cr);
        float pool = smoothstep(0.6, 0.95, vEmis);
        float k = max(crack * smoothstep(0.05, 0.4, vEmis), pool * 0.8);
        float flicker = 0.8 + 0.2 * sin(S.params.x * 2.0 + n1 * 20.0);
        lit = mix(lit, lit * 0.3, k);
        lit += vec3(1.0, 0.28, 0.04) * k * 7.0 * flicker * (0.7 + n3 * 0.6);
    }
    lit = applyFog(lit, vWorld);
    outColor = vec4(lit, dist);
}

#version 450
#extension GL_GOOGLE_include_directive : require
#include "scene_ubo.glsl"
#include "noise.glsl"
#include "lighting.glsl"

layout(location = 0) in vec3 vWorld;
layout(location = 1) in vec3 vNormal;
layout(location = 2) in vec3 vColor;
layout(location = 3) in float vEmis;
layout(location = 4) in float vAO;
layout(location = 5) in vec3 vObj;
layout(location = 6) in vec2 vMat;
layout(location = 0) out vec4 outColor;

void main() {
    vec3 n = normalize(vNormal);
    if (!gl_FrontFacing) n = -n;
    vec3 albedo = vColor;
    float gloss = vMat.x;
    int pattern = int(vMat.y + 0.5);
    float translucency = 0.0;
    float dist = length(S.camPos.xyz - vWorld);
    float detail = 1.0 - smoothstep(30.0, 90.0, dist);
    if (pattern == 1) {  // scales
        vec3 q = vObj * 9.0;
        float cell = vnoise3(q) * 0.6 + vnoise3(q * 2.3) * 0.4;
        albedo *= mix(1.0, 0.78 + 0.4 * cell, detail);
        n = normalize(n + (vec3(vnoise3(q + 3.1), vnoise3(q + 7.7), vnoise3(q + 1.3)) - 0.5) * 0.35 * detail);
    } else if (pattern == 2) {  // fur
        float f = vnoise3(vObj * vec3(30.0, 6.0, 30.0)) * 0.6 + vnoise3(vObj * 70.0) * 0.4;
        albedo *= mix(1.0, 0.75 + 0.45 * f, detail);
        translucency = 0.1;
    } else if (pattern == 3) {  // feathers
        float f = sin(vObj.z * 26.0 + vnoise3(vObj * 5.0) * 4.0) * 0.5 + 0.5;
        albedo *= mix(1.0, 0.82 + 0.25 * f, detail);
        translucency = 0.2;
    } else if (pattern == 4) {  // foliage
        float f = vnoise3(vObj * 3.0) * 0.6 + vnoise3(vObj * 11.0) * 0.4;
        albedo *= 0.7 + 0.55 * f;
        albedo.r *= 0.9 + 0.25 * vnoise3(vObj * 1.3 + 5.0);
        translucency = 0.6;
    } else if (pattern == 5) {  // rock / bark
        float f = vnoise3(vObj * 2.0) * 0.5 + vnoise3(vObj * 7.0) * 0.3 + vnoise3(vObj * 23.0) * 0.2;
        albedo *= 0.65 + 0.6 * f;
        n = normalize(n + (vec3(vnoise3(vObj * 9.0), vnoise3(vObj * 9.0 + 4.0), vnoise3(vObj * 9.0 + 9.0)) - 0.5) * 0.5 * detail);
    } else if (pattern == 6) {  // skin
        albedo *= 0.92 + 0.12 * vnoise3(vObj * 20.0);
        translucency = 0.15;
    }
    vec3 col = shade(albedo, n, vWorld, vAO, gloss, translucency);
    col += albedo * vEmis * 4.0;
    col = applyFog(col, vWorld);
    outColor = vec4(col, 1.0);
}

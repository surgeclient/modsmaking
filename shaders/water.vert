#version 450
#extension GL_GOOGLE_include_directive : require
#include "scene_ubo.glsl"

layout(location = 0) in vec3 inPos;
layout(location = 0) out vec3 vWorld;
layout(location = 1) out vec3 vNormal;

void main() {
    vec3 p = inPos;
    // Keep the ocean centred under the camera so it never ends.
    p.xz += floor(S.camPos.xz / 16.0) * 16.0;
    float t = S.params.x;
    float h = 0.0;
    vec2 grad = vec2(0.0);
    const vec3 waves[4] = vec3[](vec3(0.021, 0.013, 1.1), vec3(-0.017, 0.024, 0.9), vec3(0.042, -0.031, 1.7), vec3(-0.06, -0.047, 2.3));
    const float amps[4] = float[](0.35, 0.28, 0.12, 0.07);
    for (int i = 0; i < 4; i++) {
        float ph = dot(p.xz, waves[i].xy * 6.0) + t * waves[i].z;
        h += sin(ph) * amps[i];
        grad += cos(ph) * amps[i] * waves[i].xy * 6.0;
    }
    p.y = h;
    vWorld = p;
    vNormal = normalize(vec3(-grad.x, 1.0, -grad.y));
    gl_Position = S.viewProj * vec4(p, 1.0);
}

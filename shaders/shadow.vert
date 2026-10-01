#version 450
#extension GL_GOOGLE_include_directive : require
#include "scene_ubo.glsl"
layout(location = 0) in vec3 inPos;
layout(location = 4) in float inSway;
layout(location = 5) in vec4 m0;
layout(location = 6) in vec4 m1;
layout(location = 7) in vec4 m2;
layout(location = 8) in vec4 m3;
layout(location = 10) in vec4 iParams;
layout(location = 11) in vec2 inUV;
layout(location = 12) in float inCard;
layout(push_constant) uniform PC { vec4 v; } pc;
layout(location = 0) out vec2 vUV;
layout(location = 1) out float vCard;

void main() {
    vec4 wp = mat4(m0, m1, m2, m3) * vec4(inPos, 1.0);
    float sway = inSway * iParams.z;
    if (sway > 0.0) {
        float t = S.params.x;
        vec2 w = vec2(sin(t * 1.3 + wp.x * 0.15 + wp.z * 0.1), cos(t * 1.1 + wp.z * 0.13)) * 0.6;
        w += vec2(sin(t * 3.7 + wp.x * 0.9), cos(t * 3.1 + wp.z * 0.8)) * 0.15;
        wp.xz += w * sway * 0.3 * S.world.z;
    }
    vUV = inUV;
    vCard = inCard;
    gl_Position = S.lightViewProj[int(pc.v.x)] * wp;
}

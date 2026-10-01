#version 450
#extension GL_GOOGLE_include_directive : require
#include "scene_ubo.glsl"
layout(location = 0) in vec3 inPos;
layout(location = 3) in vec4 inSkin;
layout(location = 5) in vec4 m0;
layout(location = 6) in vec4 m1;
layout(location = 7) in vec4 m2;
layout(location = 8) in vec4 m3;
layout(location = 10) in vec4 iParams;
layout(location = 11) in vec2 inUV;
layout(location = 12) in float inCard;
layout(std430, set = 0, binding = 2) readonly buffer Bones { mat4 bones[]; };
layout(push_constant) uniform PC { vec4 v; } pc;
layout(location = 0) out vec2 vUV;
layout(location = 1) out float vCard;

void main() {
    int base = int(iParams.w + 0.5);
    mat4 B = bones[base + int(inSkin.x + 0.5)] * (1.0 - inSkin.z) + bones[base + int(inSkin.y + 0.5)] * inSkin.z;
    vUV = inUV;
    vCard = inCard;
    gl_Position = S.lightViewProj[int(pc.v.x)] * (mat4(m0, m1, m2, m3) * (B * vec4(inPos, 1.0)));
}

#version 450
#extension GL_GOOGLE_include_directive : require
#include "scene_ubo.glsl"
#include "mesh_inputs.glsl"
layout(std430, set = 0, binding = 2) readonly buffer Bones { mat4 bones[]; };

void main() {
    int base = int(iParams.w + 0.5);
    mat4 B = bones[base + int(inSkin.x + 0.5)] * (1.0 - inSkin.z) + bones[base + int(inSkin.y + 0.5)] * inSkin.z;
    mat4 M = mat4(m0, m1, m2, m3) * B;
    vec4 wp = M * vec4(inPos, 1.0);
    mat3 N = transpose(inverse(mat3(M)));
    emitOutputs(wp.xyz, normalize(N * inNormal), inPos, inSway);
    gl_Position = S.viewProj * wp;
}

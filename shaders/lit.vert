#version 450
#extension GL_GOOGLE_include_directive : require
#include "scene_ubo.glsl"

layout(location = 0) in vec3 inPos;
layout(location = 1) in vec3 inNormal;
layout(location = 2) in vec3 inColor;
layout(location = 3) in vec4 m0;
layout(location = 4) in vec4 m1;
layout(location = 5) in vec4 m2;
layout(location = 6) in vec4 m3;
layout(location = 7) in vec4 iColor;

layout(location = 0) out vec3 vWorld;
layout(location = 1) out vec3 vNormal;
layout(location = 2) out vec3 vColor;
layout(location = 3) out float vEmissive;
layout(location = 4) out vec4 vShadow;

void main() {
    mat4 M = mat4(m0, m1, m2, m3);
    vec4 wp = M * vec4(inPos, 1.0);
    mat3 N = transpose(inverse(mat3(M)));
    vWorld = wp.xyz;
    vNormal = normalize(N * inNormal);
    vColor = pow(inColor * iColor.rgb, vec3(2.2));  // sRGB authoring -> linear
    vEmissive = iColor.a;
    vShadow = S.lightViewProj * wp;
    gl_Position = S.viewProj * wp;
}

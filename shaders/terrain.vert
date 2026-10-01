#version 450
#extension GL_GOOGLE_include_directive : require
#include "scene_ubo.glsl"

layout(location = 0) in vec3 inPos;
layout(location = 1) in vec3 inNormal;
layout(location = 2) in vec4 inColor;
layout(location = 3) in vec4 inSkin;

layout(location = 0) out vec3 vWorld;
layout(location = 1) out vec3 vNormal;
layout(location = 2) out vec3 vColor;
layout(location = 3) out float vEmis;
layout(location = 4) out float vAO;

void main() {
    vWorld = inPos;
    vNormal = inNormal;
    vColor = pow(inColor.rgb, vec3(2.2));
    vEmis = inColor.a;
    vAO = inSkin.w;
    gl_Position = S.viewProj * vec4(inPos, 1.0);
}

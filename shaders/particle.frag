#version 450
#extension GL_GOOGLE_include_directive : require
#include "scene_ubo.glsl"
layout(location = 0) in vec2 vUV;
layout(location = 1) in vec4 vColor;
layout(location = 2) in vec3 vWorld;
layout(location = 0) out vec4 outColor;

void main() {
    float r = length(vUV);
    float a = clamp(1.0 - r, 0.0, 1.0);
    a = a * a;
    float dist = length(S.camPos.xyz - vWorld);
    float fog = exp(-dist * S.fogColor.w * 1.2);
    outColor = vec4(vColor.rgb, a * vColor.a * fog);
}

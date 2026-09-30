#version 450
#extension GL_GOOGLE_include_directive : require
#include "scene_ubo.glsl"

layout(location = 0) in vec3 inPos;
layout(location = 3) in vec4 m0;
layout(location = 4) in vec4 m1;
layout(location = 5) in vec4 m2;
layout(location = 6) in vec4 m3;

void main() {
    gl_Position = S.lightViewProj * mat4(m0, m1, m2, m3) * vec4(inPos, 1.0);
}

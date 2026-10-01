#version 450
#extension GL_GOOGLE_include_directive : require
#include "scene_ubo.glsl"
layout(location = 0) in vec4 posSize;
layout(location = 1) in vec4 color;
layout(location = 0) out vec2 vUV;
layout(location = 1) out vec4 vColor;
layout(location = 2) out vec3 vWorld;

const vec2 corners[6] = vec2[](vec2(-1, -1), vec2(1, -1), vec2(1, 1), vec2(-1, -1), vec2(1, 1), vec2(-1, 1));

void main() {
    vec2 c = corners[gl_VertexIndex];
    vec3 p = posSize.xyz + (S.camRight.xyz * c.x + S.camUp.xyz * c.y) * posSize.w;
    vUV = c;
    vColor = vec4(pow(color.rgb, vec3(2.2)), color.a);
    vWorld = p;
    gl_Position = S.viewProj * vec4(p, 1.0);
}

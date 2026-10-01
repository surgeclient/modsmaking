#version 450
#extension GL_GOOGLE_include_directive : require
#include "scene_ubo.glsl"
#include "lighting.glsl"

layout(location = 0) in vec3 vWorld;
layout(location = 1) in vec3 vNormal;
layout(location = 2) in vec3 vColor;
layout(location = 3) in float vT;
layout(location = 0) out vec4 outColor;

void main() {
    vec3 n = normalize(vNormal);
    vec3 l = normalize(S.sunDir.xyz);
    vec3 v = normalize(S.camPos.xyz - vWorld);
    float ndl = dot(n, l) * 0.5 + 0.5;
    float sh = shadowFactor(vWorld, vec3(0.0, 1.0, 0.0), 0.7);
    vec3 hemi = mix(S.groundColor.rgb, S.skyColor.rgb, 0.5 + 0.5 * vT);
    vec3 col = vColor * hemi * (0.5 + 0.5 * vT);
    col += vColor * S.sunColor.rgb * ndl * sh * 0.9;
    float back = pow(max(dot(v, -l), 0.0), 4.0);
    col += vColor * S.sunColor.rgb * back * sh * 0.8 * vT;
    col += pointLights(vWorld, n, vColor);
    col = applyFog(col, vWorld);
    outColor = vec4(col, 1.0);
}

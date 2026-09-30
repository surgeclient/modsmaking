#version 450
#extension GL_GOOGLE_include_directive : require
#include "scene_ubo.glsl"
#include "common_light.glsl"

layout(location = 0) in vec3 vWorld;
layout(location = 1) in vec3 vNormal;
layout(location = 0) out vec4 outColor;

void main() {
    // No ocean inside the Hollow's crater.
    if (length(vWorld.xz - S.hollowPos.xz) < S.hollowPos.w) discard;
    vec3 n = normalize(vNormal);
    vec3 v = normalize(S.camPos.xyz - vWorld);
    vec3 l = normalize(S.sunDir.xyz);
    float fres = pow(1.0 - max(dot(n, v), 0.0), 4.0);
    vec3 deep = vec3(0.01, 0.07, 0.12) * (S.skyColor.rgb * 1.5 + S.sunColor.rgb * 0.3);
    vec3 refl = mix(S.fogColor.rgb, S.skyColor.rgb, 0.4);
    vec3 col = mix(deep, refl, 0.15 + fres * 0.75);
    vec3 h = normalize(l + v);
    col += S.sunColor.rgb * pow(max(dot(n, h), 0.0), 180.0) * 3.0;
    col += pointLights(vWorld, n, vec3(0.3, 0.4, 0.5));
    col = applyFog(col, vWorld);
    outColor = vec4(acesTonemap(col), 0.82 + fres * 0.15);
}

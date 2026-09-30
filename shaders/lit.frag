#version 450
#extension GL_GOOGLE_include_directive : require
#include "scene_ubo.glsl"
#include "common_light.glsl"

layout(set = 0, binding = 1) uniform sampler2DShadow shadowMap;

layout(location = 0) in vec3 vWorld;
layout(location = 1) in vec3 vNormal;
layout(location = 2) in vec3 vColor;
layout(location = 3) in float vEmissive;
layout(location = 4) in vec4 vShadow;
layout(location = 0) out vec4 outColor;

float shadowFactor(float ndl) {
    vec3 p = vShadow.xyz / vShadow.w;
    vec2 uv = p.xy * 0.5 + 0.5;
    if (uv.x < 0.0 || uv.x > 1.0 || uv.y < 0.0 || uv.y > 1.0 || p.z > 1.0) return 1.0;
    float bias = 0.0008 + 0.002 * (1.0 - ndl);
    vec2 texel = 1.0 / vec2(textureSize(shadowMap, 0));
    float sum = 0.0;
    for (int x = -1; x <= 1; x++)
        for (int y = -1; y <= 1; y++)
            sum += texture(shadowMap, vec3(uv + vec2(x, y) * texel * 1.25, p.z - bias));
    // Fade shadows out towards the edge of the shadow map.
    float edge = smoothstep(0.42, 0.5, max(abs(uv.x - 0.5), abs(uv.y - 0.5)));
    return mix(sum / 9.0, 1.0, edge);
}

void main() {
    vec3 n = normalize(vNormal);
    vec3 l = normalize(S.sunDir.xyz);
    vec3 v = normalize(S.camPos.xyz - vWorld);
    float ndl = max(dot(n, l), 0.0);
    float sh = mix(1.0, shadowFactor(ndl), S.sunDir.w);

    vec3 hemi = mix(S.groundColor.rgb, S.skyColor.rgb, n.y * 0.5 + 0.5);
    vec3 col = vColor * (hemi + S.sunColor.rgb * ndl * sh);
    vec3 h = normalize(l + v);
    col += S.sunColor.rgb * pow(max(dot(n, h), 0.0), 40.0) * 0.12 * sh;
    float rim = pow(1.0 - max(dot(n, v), 0.0), 3.0);
    col += vColor * S.skyColor.rgb * rim * 0.35;
    col += pointLights(vWorld, n, vColor);

    if (vEmissive > 0.0) col += vColor * vEmissive * 2.5;

    col = applyFog(col, vWorld);
    outColor = vec4(acesTonemap(col), 1.0);
}

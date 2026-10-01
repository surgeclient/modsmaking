#version 450
#extension GL_GOOGLE_include_directive : require
#include "scene_ubo.glsl"
#include "noise.glsl"
#include "lighting.glsl"
layout(set = 0, binding = 3) uniform sampler2D heightMap;

layout(location = 0) in vec3 vWorld;
layout(location = 1) in vec3 vNormal;
layout(location = 0) out vec4 outColor;

vec3 skyDir(vec3 d) {
    vec3 c = mix(S.fogColor.rgb, S.skyColor.rgb * 1.1, pow(max(d.y, 0.0), 0.45));
    float sd = max(dot(d, normalize(S.skySunDir.xyz)), 0.0);
    c += S.sunColor.rgb * pow(sd, 12.0) * 0.25;
    return c;
}

void main() {
    if (length(vWorld.xz - S.hollowPos.xz) < S.hollowPos.w) discard;
    vec2 p = vWorld.xz;
    float t = S.params.x;
    vec2 uv = (p + S.world.x) / (2.0 * S.world.x);
    float ground = (uv.x > 0.0 && uv.x < 1.0 && uv.y > 0.0 && uv.y < 1.0) ? texture(heightMap, uv).r : -30.0;
    float depth = max(vWorld.y - ground, 0.0);

    // Detail ripples from two scrolling noise layers.
    float e = 0.15;
    vec2 q1 = p * 0.55 + vec2(t * 0.35, t * 0.2), q2 = p * 1.3 - vec2(t * 0.3, -t * 0.42);
    float dx = (vnoise(q1 + vec2(e, 0)) - vnoise(q1 - vec2(e, 0))) + 0.5 * (vnoise(q2 + vec2(e, 0)) - vnoise(q2 - vec2(e, 0)));
    float dz = (vnoise(q1 + vec2(0, e)) - vnoise(q1 - vec2(0, e))) + 0.5 * (vnoise(q2 + vec2(0, e)) - vnoise(q2 - vec2(0, e)));
    float dist = length(S.camPos.xyz - vWorld);
    float detail = 1.0 - smoothstep(60.0, 300.0, dist);
    vec3 n = normalize(vNormal + vec3(-dx, 0.0, -dz) * 0.6 * detail);

    vec3 v = normalize(S.camPos.xyz - vWorld);
    vec3 l = normalize(S.sunDir.xyz);
    float fres = 0.02 + 0.98 * pow(1.0 - max(dot(n, v), 0.0), 5.0);
    vec3 r = reflect(-v, n);
    r.y = abs(r.y);
    vec3 refl = skyDir(r);

    vec3 shallow = vec3(0.03, 0.32, 0.30), deep = vec3(0.004, 0.035, 0.07);
    float depthT = 1.0 - exp(-depth * 0.22);
    vec3 body = mix(shallow, deep, depthT);
    float sh = shadowFactor(vWorld, vec3(0.0, 1.0, 0.0), 1.0);
    vec3 light = S.skyColor.rgb * 0.8 + S.sunColor.rgb * max(dot(vec3(0, 1, 0), l), 0.0) * 0.35 * sh;
    vec3 col = body * light;
    col = mix(col, refl, fres);
    vec3 h = normalize(l + v);
    col += S.sunColor.rgb * pow(max(dot(n, h), 0.0), 400.0) * 6.0 * sh;
    col += pointLights(vWorld, n, vec3(0.2, 0.3, 0.35));

    // Shoreline foam.
    float foamNoise = vnoise(p * 1.8 + vec2(t * 0.4, 0.0)) * 0.6 + vnoise(p * 4.0 - vec2(0.0, t * 0.5)) * 0.4;
    float shore = 1.0 - smoothstep(0.0, 0.9 + 0.5 * sin(t * 1.3 + p.x * 0.05), depth);
    float foam = clamp(shore * (0.6 + foamNoise) - 0.25, 0.0, 1.0);
    foam += smoothstep(0.58, 0.78, vWorld.y + foamNoise * 0.15) * smoothstep(0.55, 0.8, foamNoise) * 0.35;
    col = mix(col, light * 1.3 + S.sunColor.rgb * 0.2 * sh, clamp(foam, 0.0, 1.0));

    col = applyFog(col, vWorld);
    float alpha = mix(0.25, 0.95, smoothstep(0.0, 4.0, depth));
    alpha = max(alpha, fres);
    alpha = max(alpha, foam);
    outColor = vec4(col, clamp(alpha, 0.0, 1.0));
}

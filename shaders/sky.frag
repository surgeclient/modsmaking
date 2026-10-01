#version 450
#extension GL_GOOGLE_include_directive : require
#include "scene_ubo.glsl"
#include "noise.glsl"

layout(location = 0) in vec2 vNdc;
layout(location = 0) out vec4 outColor;

void main() {
    vec4 wp = S.invViewProj * vec4(vNdc, 1.0, 1.0);
    vec3 d = normalize(wp.xyz / wp.w - S.camPos.xyz);
    float up = max(d.y, 0.0);
    vec3 col = mix(S.fogColor.rgb, S.skyColor.rgb * 1.15, pow(up, 0.42));
    if (d.y < 0.0) col = S.fogColor.rgb * (1.0 + d.y * 0.3);

    vec3 sun = normalize(S.skySunDir.xyz);
    float sd = max(dot(d, sun), 0.0);
    float sunUp = smoothstep(-0.12, 0.04, sun.y);
    vec3 sunTint = mix(vec3(1.0, 0.42, 0.12), vec3(1.0, 0.93, 0.82), smoothstep(0.0, 0.35, sun.y));
    col += sunTint * (pow(sd, 6.0) * 0.25 + pow(sd, 48.0) * 0.6) * sunUp;
    col += sunTint * smoothstep(0.99955, 0.99975, sd) * 60.0 * sunUp;

    float night = S.params.y;
    vec3 moon = normalize(S.moonDir.xyz);
    float md = dot(d, moon);
    vec3 moonCol = mix(vec3(0.85, 0.9, 1.0), vec3(1.0, 0.3, 0.22), S.params.z);
    float disk = smoothstep(0.9993, 0.9995, md);
    float crater = vnoise(d.xy * 900.0) * 0.35;
    col += moonCol * (disk * (3.0 - crater * 3.0) + pow(max(md, 0.0), 40.0) * 0.2) * night;

    // Stars with twinkle and a faint milky band.
    vec3 sp = floor(d * 420.0);
    float st = hash13(sp);
    float star = step(0.9978, st) * (0.6 + 0.4 * sin(S.params.x * 3.0 + st * 100.0));
    float band = pow(1.0 - abs(dot(d, normalize(vec3(0.3, 0.2, 1.0)))), 6.0) * vnoise(d.xz * 18.0);
    col += (vec3(star) * 2.0 + vec3(0.25, 0.25, 0.4) * band * 0.25) * night * smoothstep(0.0, 0.25, d.y);

    // Volumetric-looking cloud layer.
    if (d.y > 0.0) {
        float tp = 900.0 / (d.y + 0.08);
        vec2 cuv = (S.camPos.xz + d.xz * tp) * 0.0009 + vec2(S.params.x * 0.004, S.params.x * 0.0015);
        float base = fbm2(cuv, 6);
        float cover = smoothstep(0.42, 0.78, base);
        float towardSun = fbm2(cuv + sun.xz * 0.025, 5);
        float lit = clamp(0.55 + (base - towardSun) * 4.0, 0.25, 1.4);
        vec3 shadowCol = S.skyColor.rgb * 0.7 + S.fogColor.rgb * 0.3;
        vec3 litCol = S.sunColor.rgb * 0.45 + vec3(0.25) * (1.0 - night) + S.skyColor.rgb * 0.3;
        vec3 cloud = mix(shadowCol, litCol, lit * 0.8);
        cloud += sunTint * pow(sd, 10.0) * 0.8 * sunUp * (1.0 - cover);  // silver lining
        cloud = mix(cloud, moonCol * 0.08 + S.skyColor.rgb, night * 0.6);
        float fade = smoothstep(0.0, 0.18, d.y);
        col = mix(col, cloud, cover * 0.92 * fade);
    }
    outColor = vec4(col, 10000.0);
}

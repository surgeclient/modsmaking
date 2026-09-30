#version 450
#extension GL_GOOGLE_include_directive : require
#include "scene_ubo.glsl"
#include "common_light.glsl"

layout(location = 0) in vec2 vNdc;
layout(location = 0) out vec4 outColor;

float hash(vec3 p) {
    p = fract(p * 0.3183099 + 0.1);
    p *= 17.0;
    return fract(p.x * p.y * p.z * (p.x + p.y + p.z));
}

void main() {
    vec4 wp = S.invViewProj * vec4(vNdc, 1.0, 1.0);
    vec3 d = normalize(wp.xyz / wp.w - S.camPos.xyz);
    float up = max(d.y, 0.0);
    vec3 col = mix(S.fogColor.rgb, S.skyColor.rgb * 1.1, pow(up, 0.45));
    if (d.y < 0.0) col = S.fogColor.rgb;

    vec3 sun = normalize(S.skySunDir.xyz);
    float sd = max(dot(d, sun), 0.0);
    float sunUp = smoothstep(-0.1, 0.05, sun.y);
    vec3 sunTint = mix(vec3(1.0, 0.45, 0.15), vec3(1.0, 0.95, 0.85), smoothstep(0.0, 0.35, sun.y));
    col += sunTint * (pow(sd, 900.0) * 30.0 + pow(sd, 12.0) * 0.35) * sunUp;

    float night = S.params.y;
    vec3 moon = normalize(S.moonDir.xyz);
    float md = dot(d, moon);
    vec3 moonCol = mix(vec3(0.9, 0.92, 1.0), vec3(1.0, 0.3, 0.22), S.params.z);
    col += moonCol * (smoothstep(0.9994, 0.9996, md) * 2.5 + pow(max(md, 0.0), 60.0) * 0.25) * night;

    // Stars.
    vec3 sp = floor(d * 380.0);
    float st = hash(sp);
    col += vec3(step(0.9975, st) * (0.6 + 0.4 * sin(S.params.x * 3.0 + st * 100.0))) * night * smoothstep(0.0, 0.2, d.y);

    // Clouds: cheap layered bands.
    if (d.y > 0.0) {
        vec2 cuv = d.xz / (d.y + 0.15) * 1.5 + vec2(S.params.x * 0.01, 0.0);
        float c = sin(cuv.x * 1.7) * sin(cuv.y * 2.3) + sin(cuv.x * 3.1 + 1.3) * sin(cuv.y * 2.9 + 0.7) * 0.5;
        c = smoothstep(0.35, 1.1, c) * smoothstep(0.0, 0.25, d.y);
        vec3 cloudCol = S.sunColor.rgb * 0.6 + S.skyColor.rgb * 0.6 + vec3(0.05);
        col = mix(col, cloudCol, c * 0.55);
    }
    outColor = vec4(acesTonemap(col), 1.0);
}

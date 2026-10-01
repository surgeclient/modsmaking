#version 450
#extension GL_GOOGLE_include_directive : require
#include "scene_ubo.glsl"
#include "noise.glsl"
#include "lighting.glsl"
#include "cards.glsl"
layout(push_constant) uniform PC { vec4 v; } pc;

layout(location = 0) in vec3 vWorld;
layout(location = 1) in vec3 vNormal;
layout(location = 2) in vec3 vColor;
layout(location = 3) in float vEmis;
layout(location = 4) in float vAO;
layout(location = 5) in vec3 vObj;
layout(location = 6) in vec2 vMat;
layout(location = 7) in vec2 vUV;
layout(location = 8) in float vCard;
layout(location = 9) in float vGlossV;
layout(location = 0) out vec4 outColor;

// Cellular noise: distance to nearest and second nearest feature point.
vec2 voronoi(vec3 p, out vec3 cellPos) {
    vec3 i = floor(p), f = fract(p);
    float d1 = 8.0, d2 = 8.0;
    cellPos = vec3(0.0);
    for (int z = -1; z <= 1; z++)
        for (int y = -1; y <= 1; y++)
            for (int x = -1; x <= 1; x++) {
                vec3 g = vec3(x, y, z);
                vec3 o = vec3(hash13(i + g), hash13(i + g + 17.0), hash13(i + g + 41.0));
                vec3 r = g + o - f;
                float d = dot(r, r);
                if (d < d1) {
                    d2 = d1;
                    d1 = d;
                    cellPos = r;
                } else if (d < d2) {
                    d2 = d;
                }
            }
    return vec2(sqrt(d1), sqrt(d2));
}

void main() {
    vec3 n = normalize(vNormal);
    int card = int(vCard + 0.5);
    float cardShade = 1.0;
    gl_SampleMask[0] = -1;  // the mask is written statically, so give every path a defined (full) value
    if (card > 0) {
        float a = cardAlpha(card, vUV, cardShade);
        int samples = int(pc.v.z + 0.5);
        if (samples > 1) {
            float dither = hash12(gl_FragCoord.xy);
            int cov = int(clamp(a * float(samples) + dither - 0.5, 0.0, float(samples)));
            if (cov == 0) discard;
            gl_SampleMask[0] = (1 << cov) - 1;
        } else if (a < 0.5) {
            discard;
        }
    } else if (!gl_FrontFacing) {
        n = -n;
    }
    vec3 albedo = vColor * cardShade;
    float gloss = max(vMat.x, vGlossV);
    int pattern = int(vMat.y + 0.5);
    float translucency = card > 0 ? 0.75 : 0.0;
    float dist = length(S.camPos.xyz - vWorld);
    float detail = 1.0 - smoothstep(25.0, 80.0, dist);
    vec3 v = normalize(S.camPos.xyz - vWorld);
    float sheen = 0.0;
    if (detail > 0.0 && card == 0 && vGlossV < 0.5) {
        if (pattern == 1) {  // overlapping scales
            vec3 cp;
            vec2 vd = voronoi(vObj * 11.0, cp);
            float edge = smoothstep(0.0, 0.12, vd.y - vd.x);
            albedo *= mix(1.0, 0.62 + 0.45 * edge + 0.12 * hash13(floor(vObj * 11.0)), detail);
            n = normalize(n + normalize(-cp + vec3(0.0, 0.001, 0.0)) * 0.35 * detail * (1.0 - edge * 0.5));
        } else if (pattern == 2) {  // fur
            float f = vnoise3(vObj * vec3(36.0, 9.0, 36.0)) * 0.6 + vnoise3(vObj * 90.0) * 0.4;
            albedo *= mix(1.0, 0.72 + 0.5 * f, detail);
            sheen = 0.45;
            translucency = 0.15;
        } else if (pattern == 3) {  // body feathers in rows
            float row = vObj.y * 22.0 + vObj.z * 14.0;
            float scallop = abs(fract(row + sin(vObj.x * 30.0) * 0.25) - 0.5);
            albedo *= mix(1.0, 0.8 + 0.35 * smoothstep(0.0, 0.5, scallop), detail);
            sheen = 0.25;
            translucency = 0.2;
        } else if (pattern == 4) {  // foliage mass, or bark on the woody parts
            float f = vnoise3(vObj * 3.0) * 0.6 + vnoise3(vObj * 11.0) * 0.4;
            if (vColor.r > vColor.g * 1.08) {
                // Vertical bark ridges: stretched noise darkens the furrows and bends the normal.
                vec3 q = vObj * vec3(13.0, 1.6, 13.0);
                float ridge = vnoise3(q) * 0.65 + vnoise3(q * 2.3) * 0.35;
                float furrow = smoothstep(0.35, 0.65, ridge);
                albedo *= mix(1.0, (0.55 + 0.6 * furrow) * (0.85 + 0.3 * f), detail);
                float e = 0.12;
                vec3 g = vec3(vnoise3(q + vec3(e, 0, 0)) - vnoise3(q - vec3(e, 0, 0)), 0.0, vnoise3(q + vec3(0, 0, e)) - vnoise3(q - vec3(0, 0, e)));
                n = normalize(n - g * 1.8 * detail);
            } else {
                albedo *= 0.7 + 0.55 * f;
                translucency = 0.5;
            }
        } else if (pattern == 5) {  // rock / bark
            float f = vnoise3(vObj * 2.0) * 0.5 + vnoise3(vObj * 7.0) * 0.3 + vnoise3(vObj * 23.0) * 0.2;
            albedo *= 0.65 + 0.6 * f;
            float e = 0.08;
            vec3 g = vec3(vnoise3(vObj * 9.0 + vec3(e, 0, 0)) - vnoise3(vObj * 9.0 - vec3(e, 0, 0)), vnoise3(vObj * 9.0 + vec3(0, e, 0)) - vnoise3(vObj * 9.0 - vec3(0, e, 0)),
                          vnoise3(vObj * 9.0 + vec3(0, 0, e)) - vnoise3(vObj * 9.0 - vec3(0, 0, e)));
            n = normalize(n - g * 2.5 * detail);
        } else if (pattern == 6) {  // skin
            albedo *= 0.9 + 0.15 * vnoise3(vObj * 18.0);
            translucency = 0.12;
            sheen = 0.1;
        }
    }
    vec3 col = shade(albedo, n, vWorld, vAO, gloss, translucency);
    // Soft sheen at grazing angles for fur and feathers.
    if (sheen > 0.0) col += albedo * (S.skyColor.rgb + S.sunColor.rgb * 0.25) * pow(1.0 - max(dot(n, v), 0.0), 2.5) * sheen;
    col += albedo * vEmis * 4.0;
    col = applyFog(col, vWorld);
    outColor = vec4(col, dist);
}

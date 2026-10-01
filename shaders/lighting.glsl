// Lighting helpers for the HDR pass (include after scene_ubo.glsl).
layout(set = 0, binding = 1) uniform sampler2DShadow shadowMap;

float shadowCascade(int c, vec3 wp, vec3 n, float ndl) {
    vec4 lp = S.lightViewProj[c] * vec4(wp + n * (0.04 + 0.12 * float(c)), 1.0);
    vec3 p = lp.xyz / lp.w;
    vec2 uv = p.xy * 0.5 + 0.5;
    if (uv.x < 0.01 || uv.x > 0.99 || uv.y < 0.01 || uv.y > 0.99 || p.z > 1.0) return -1.0;
    float bias = (c == 0 ? 0.0003 : 0.0009) * (1.0 + 2.0 * (1.0 - ndl));
    vec2 texel = 1.0 / vec2(textureSize(shadowMap, 0));
    vec2 base = vec2((uv.x + float(c)) * 0.5, uv.y);
    const vec2 poisson[12] = vec2[](vec2(-0.326, -0.406), vec2(-0.840, -0.074), vec2(-0.696, 0.457), vec2(-0.203, 0.621), vec2(0.962, -0.195),
                                    vec2(0.473, -0.480), vec2(0.519, 0.767), vec2(0.185, -0.893), vec2(0.507, 0.064), vec2(0.896, 0.412),
                                    vec2(-0.322, -0.933), vec2(-0.792, -0.598));
    float ang = fract(sin(dot(wp.xz, vec2(12.9898, 78.233))) * 43758.5453) * 6.2831;
    mat2 rot = mat2(cos(ang), sin(ang), -sin(ang), cos(ang));
    float radius = c == 0 ? 2.2 : 1.4;
    float sum = 0.0;
    for (int i = 0; i < 12; i++) sum += texture(shadowMap, vec3(base + rot * poisson[i] * texel * radius, p.z - bias));
    float s = sum / 12.0;
    if (c == 1) s = mix(s, 1.0, smoothstep(0.4, 0.49, max(abs(uv.x - 0.5), abs(uv.y - 0.5))));
    return s;
}

float shadowFactor(vec3 wp, vec3 n, float ndl) {
    float s = shadowCascade(0, wp, n, ndl);
    if (s < 0.0) s = shadowCascade(1, wp, n, ndl);
    if (s < 0.0) s = 1.0;
    return mix(1.0, s, S.sunDir.w);
}

vec3 pointLights(vec3 world, vec3 n, vec3 albedo) {
    vec3 sum = vec3(0.0);
    int count = int(S.params.w);
    for (int i = 0; i < count; i++) {
        vec3 d = S.lightPos[i].xyz - world;
        float dist = length(d);
        float att = clamp(1.0 - dist / S.lightPos[i].w, 0.0, 1.0);
        att *= att;
        float ndl = max(dot(n, d / max(dist, 0.001)), 0.0) * 0.85 + 0.15;
        sum += albedo * S.lightColor[i].rgb * S.lightColor[i].w * att * ndl;
    }
    vec3 hd = world - S.hollowPos.xyz;
    float hglow = S.params.z * exp(-length(hd.xz) * 0.02) * 1.6;
    sum += albedo * vec3(1.0, 0.12, 0.05) * hglow;
    return sum;
}

vec3 applyFog(vec3 col, vec3 world) {
    float dist = length(S.camPos.xyz - world);
    float fog = 1.0 - exp(-dist * S.fogColor.w);
    float low = exp(-max(world.y, 0.0) * 0.045) * 0.3;
    fog = clamp(fog + low * (1.0 - exp(-dist * 0.003)), 0.0, 1.0);
    // Sun-tinted in-scattering when looking towards the sun.
    vec3 v = normalize(world - S.camPos.xyz);
    float sunAmt = pow(max(dot(v, normalize(S.skySunDir.xyz)), 0.0), 8.0) * (1.0 - S.params.y);
    vec3 fogCol = S.fogColor.rgb + S.sunColor.rgb * 0.12 * sunAmt;
    return mix(col, fogCol, fog);
}

vec3 shade(vec3 albedo, vec3 n, vec3 world, float ao, float gloss, float translucency) {
    vec3 l = normalize(S.sunDir.xyz);
    vec3 v = normalize(S.camPos.xyz - world);
    float ndl = dot(n, l);
    float sh = shadowFactor(world, n, max(ndl, 0.0));
    vec3 hemi = mix(S.groundColor.rgb, S.skyColor.rgb, n.y * 0.5 + 0.5);
    vec3 col = albedo * hemi * ao;
    col += albedo * S.sunColor.rgb * max(ndl, 0.0) * sh;
    // Foliage / thin membranes let light through.
    col += albedo * S.sunColor.rgb * translucency * max(-ndl, 0.0) * sh * 0.6;
    vec3 h = normalize(l + v);
    float fres = 0.04 + 0.96 * pow(1.0 - max(dot(n, v), 0.0), 5.0);
    float specPow = mix(12.0, 160.0, gloss);
    col += S.sunColor.rgb * pow(max(dot(n, h), 0.0), specPow) * (0.04 + gloss * 0.6) * sh * max(ndl, 0.0) * (specPow + 8.0) / 40.0;
    col += hemi * fres * gloss * 0.25 * ao;
    col += pointLights(world, n, albedo);
    return col;
}

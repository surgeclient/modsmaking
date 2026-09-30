vec3 acesTonemap(vec3 x) {
    const float a = 2.51, b = 0.03, c = 2.43, d = 0.59, e = 0.14;
    return clamp((x * (a * x + b)) / (x * (c * x + d) + e), 0.0, 1.0);
}

vec3 applyFog(vec3 col, vec3 world) {
    float dist = length(S.camPos.xyz - world);
    float fog = 1.0 - exp(-dist * S.fogColor.w);
    // Thicker fog hugging the ground and around the Hollow at night.
    float low = exp(-max(world.y, 0.0) * 0.05) * 0.25;
    fog = clamp(fog + low * (1.0 - exp(-dist * 0.004)), 0.0, 1.0);
    return mix(col, S.fogColor.rgb, fog);
}

vec3 pointLights(vec3 world, vec3 n, vec3 albedo) {
    vec3 sum = vec3(0.0);
    int count = int(S.params.w);
    for (int i = 0; i < count; i++) {
        vec3 d = S.lightPos[i].xyz - world;
        float dist = length(d);
        float att = clamp(1.0 - dist / S.lightPos[i].w, 0.0, 1.0);
        att *= att;
        float ndl = max(dot(n, d / max(dist, 0.001)), 0.0) * 0.8 + 0.2;
        sum += albedo * S.lightColor[i].rgb * S.lightColor[i].w * att * ndl;
    }
    // The Hollow bleeds a red glow over everything nearby at night.
    vec3 hd = world - S.hollowPos.xyz;
    float hdist = length(hd.xz);
    float hglow = S.params.z * exp(-hdist * 0.018) * 1.4;
    sum += albedo * vec3(1.0, 0.12, 0.05) * hglow;
    return sum;
}

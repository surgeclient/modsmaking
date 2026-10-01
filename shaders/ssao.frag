#version 450
// Screen-space ambient occlusion from the linear depth stored in the HDR alpha channel.
layout(set = 0, binding = 0) uniform sampler2D scene;
layout(push_constant) uniform PC { vec4 a; vec4 b; } pc;  // a: tanX, tanY, radius, intensity  b: texel, time
layout(location = 0) in vec2 vUV;
layout(location = 0) out vec4 outColor;

vec3 viewPos(vec2 uv, float d) {
    vec3 dir = normalize(vec3((uv.x * 2.0 - 1.0) * pc.a.x, -(uv.y * 2.0 - 1.0) * pc.a.y, -1.0));
    return dir * d;
}

void main() {
    float d0 = texture(scene, vUV).a;
    if (pc.a.w <= 0.0 || d0 > 300.0) {
        outColor = vec4(1.0);
        return;
    }
    vec3 P = viewPos(vUV, d0);
    vec2 t = pc.b.xy * 2.0;
    vec3 Px = viewPos(vUV + vec2(t.x, 0.0), texture(scene, vUV + vec2(t.x, 0.0)).a);
    vec3 Py = viewPos(vUV + vec2(0.0, t.y), texture(scene, vUV + vec2(0.0, t.y)).a);
    vec3 N = normalize(cross(Px - P, Py - P));
    if (dot(N, -P) < 0.0) N = -N;
    float radius = clamp(0.5 + d0 * 0.012, 0.5, 2.5) * pc.a.z;
    float screenR = radius / (d0 * pc.a.y) * 0.5;
    float noise = fract(sin(dot(gl_FragCoord.xy, vec2(12.9898, 78.233))) * 43758.5453);
    float occ = 0.0;
    const int SAMPLES = 14;
    for (int i = 0; i < SAMPLES; i++) {
        float fi = float(i) + noise;
        float ang = fi * 2.399963 + noise * 6.2831;
        float r = sqrt((fi + 0.5) / float(SAMPLES));
        vec2 off = vec2(cos(ang), sin(ang) * pc.a.x / pc.a.y) * r * screenR;
        vec2 suv = vUV + off;
        float sd = texture(scene, suv).a;
        vec3 S = viewPos(suv, sd);
        vec3 v = S - P;
        float dist = length(v);
        float ndv = dot(N, v) / max(dist, 1e-3);
        occ += max(ndv - 0.08, 0.0) * (1.0 - smoothstep(radius * 0.5, radius * 1.6, dist));
    }
    float ao = clamp(1.0 - pc.a.w * occ / float(SAMPLES) * 1.8, 0.0, 1.0);
    outColor = vec4(ao, ao, ao, 1.0);
}

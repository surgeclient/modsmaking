#version 450
layout(set = 0, binding = 0) uniform sampler2D scene;
layout(set = 0, binding = 1) uniform sampler2D bloomTex;
layout(set = 0, binding = 2) uniform sampler2D aoTex;
layout(set = 0, binding = 3) uniform sampler2D raysTex;
// a: exposure, bloom, saturation, vignette   b: sun uv, rays strength, ao strength   c: time, sharpen, texel
layout(push_constant) uniform PC { vec4 a; vec4 b; vec4 c; } pc;
layout(location = 0) in vec2 vUV;
layout(location = 0) out vec4 outColor;

vec3 aces(vec3 x) {
    const float a = 2.51, b = 0.03, c = 2.43, d = 0.59, e = 0.14;
    return clamp((x * (a * x + b)) / (x * (c * x + d) + e), 0.0, 1.0);
}

void main() {
    vec2 t = pc.c.zw;
    // Subtle chromatic aberration towards the frame edges.
    vec2 q = vUV - 0.5;
    float ca = dot(q, q) * 0.006;
    vec4 center = texture(scene, vUV);
    vec3 hdr = vec3(texture(scene, vUV + q * ca).r, center.g, texture(scene, vUV - q * ca).b);
    // Sharpen (unsharp mask) to keep detail crisp after MSAA resolve.
    vec3 blur = (texture(scene, vUV + vec2(t.x, 0)).rgb + texture(scene, vUV - vec2(t.x, 0)).rgb + texture(scene, vUV + vec2(0, t.y)).rgb +
                 texture(scene, vUV - vec2(0, t.y)).rgb) * 0.25;
    hdr = max(hdr + (hdr - blur) * pc.c.y, vec3(0.0));
    // Ambient occlusion (softened with a small blur), fading with distance.
    float ao = 0.0;
    for (int i = 0; i < 4; i++) {
        vec2 o = vec2((i & 1) == 0 ? -1.0 : 1.0, (i & 2) == 0 ? -1.0 : 1.0) * t * 1.5;
        ao += texture(aoTex, vUV + o).r;
    }
    ao *= 0.25;
    float depth = center.a;
    ao = mix(1.0, ao, pc.b.w * (1.0 - smoothstep(80.0, 250.0, depth)));
    vec3 c = hdr * ao;
    c += texture(raysTex, vUV).rgb;
    c += texture(bloomTex, vUV).rgb * pc.a.y;
    c *= pc.a.x;
    c = aces(c);
    float lum = dot(c, vec3(0.2126, 0.7152, 0.0722));
    c = mix(vec3(lum), c, pc.a.z);
    c = mix(c, c * c * (3.0 - 2.0 * c), 0.25);
    c *= mix(vec3(1.0), vec3(1.04, 1.0, 0.93), smoothstep(0.4, 1.0, lum));
    c *= mix(vec3(1.0), vec3(0.93, 0.98, 1.07), 1.0 - smoothstep(0.0, 0.35, lum));
    c *= 1.0 - pc.a.w * pow(length(q) * 1.35, 2.4);
    float grain = fract(sin(dot(vUV * 1000.0 + pc.c.x, vec2(12.9898, 78.233))) * 43758.5453) - 0.5;
    c += grain * 0.01;
    outColor = vec4(max(c, vec3(0.0)), 1.0);
}

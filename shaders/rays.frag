#version 450
// Screen-space god rays: march from each pixel towards the sun, accumulating bright sky.
layout(set = 0, binding = 0) uniform sampler2D scene;
layout(push_constant) uniform PC { vec4 a; vec4 b; } pc;  // a: sun uv, strength, decay  b: sun colour
layout(location = 0) in vec2 vUV;
layout(location = 0) out vec4 outColor;

void main() {
    if (pc.a.z <= 0.0) {
        outColor = vec4(0.0);
        return;
    }
    const int N = 40;
    vec2 delta = (vUV - pc.a.xy) / float(N) * 0.9;
    vec2 coord = vUV;
    float decay = 1.0, illum = 0.0;
    float noise = fract(sin(dot(gl_FragCoord.xy, vec2(12.9898, 78.233))) * 43758.5453);
    coord -= delta * noise;
    for (int i = 0; i < N; i++) {
        coord -= delta;
        vec4 s = texture(scene, clamp(coord, vec2(0.001), vec2(0.999)));
        float sky = step(5000.0, s.a);
        float lum = min(dot(s.rgb, vec3(0.3, 0.59, 0.11)), 6.0);
        illum += sky * lum * decay;
        decay *= pc.a.w;
    }
    illum /= float(N);
    float falloff = 1.0 - smoothstep(0.0, 0.9, length((vUV - pc.a.xy) * vec2(1.6, 1.0)));
    outColor = vec4(pc.b.rgb * illum * pc.a.z * falloff, 1.0);
}

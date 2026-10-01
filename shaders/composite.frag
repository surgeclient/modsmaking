#version 450
layout(set = 0, binding = 0) uniform sampler2D scene;
layout(set = 0, binding = 1) uniform sampler2D bloomTex;
layout(push_constant) uniform PC { vec4 v; } pc;  // x exposure, y bloom, z saturation, w vignette
layout(location = 0) in vec2 vUV;
layout(location = 0) out vec4 outColor;

vec3 aces(vec3 x) {
    const float a = 2.51, b = 0.03, c = 2.43, d = 0.59, e = 0.14;
    return clamp((x * (a * x + b)) / (x * (c * x + d) + e), 0.0, 1.0);
}

void main() {
    vec3 hdr = texture(scene, vUV).rgb;
    vec3 bloom = texture(bloomTex, vUV).rgb;
    vec3 c = hdr + bloom * pc.v.y;
    c *= pc.v.x;
    c = aces(c);
    float lum = dot(c, vec3(0.2126, 0.7152, 0.0722));
    c = mix(vec3(lum), c, pc.v.z);
    c = mix(c, c * c * (3.0 - 2.0 * c), 0.2);           // gentle S-curve
    c *= mix(vec3(1.0), vec3(1.03, 1.0, 0.95), smoothstep(0.4, 1.0, lum));  // warm highlights
    c *= mix(vec3(1.0), vec3(0.95, 0.98, 1.05), 1.0 - smoothstep(0.0, 0.35, lum));  // cool shadows
    vec2 q = vUV - 0.5;
    c *= 1.0 - pc.v.w * pow(length(q) * 1.35, 2.4);
    float grain = fract(sin(dot(vUV * 1000.0, vec2(12.9898, 78.233))) * 43758.5453) - 0.5;
    c += grain * 0.012;
    outColor = vec4(max(c, vec3(0.0)), 1.0);
}

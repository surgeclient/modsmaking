#version 450
layout(set = 0, binding = 0) uniform sampler2D src;
layout(push_constant) uniform PC { vec4 v; } pc;  // xy texel size of src, z = prefilter
layout(location = 0) in vec2 vUV;
layout(location = 0) out vec4 outColor;

void main() {
    vec2 t = pc.v.xy;
    vec3 a = texture(src, vUV + t * vec2(-1.0, -1.0)).rgb;
    vec3 b = texture(src, vUV + t * vec2(1.0, -1.0)).rgb;
    vec3 c = texture(src, vUV + t * vec2(-1.0, 1.0)).rgb;
    vec3 d = texture(src, vUV + t * vec2(1.0, 1.0)).rgb;
    vec3 e = texture(src, vUV).rgb;
    vec3 col = (a + b + c + d) * 0.125 + e * 0.5;
    if (pc.v.z > 0.5) {
        col = min(col, vec3(60.0));
        float br = max(col.r, max(col.g, col.b));
        const float threshold = 1.1, knee = 0.6;
        float soft = clamp(br - threshold + knee, 0.0, 2.0 * knee);
        soft = soft * soft / (4.0 * knee + 1e-4);
        col *= max(soft, br - threshold) / max(br, 1e-4);
    }
    outColor = vec4(col, 1.0);
}

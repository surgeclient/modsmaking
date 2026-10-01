#version 450
layout(set = 0, binding = 0) uniform sampler2D src;
layout(push_constant) uniform PC { vec4 v; } pc;
layout(location = 0) in vec2 vUV;
layout(location = 0) out vec4 outColor;

void main() {
    vec2 t = pc.v.xy;
    vec3 s = texture(src, vUV + t * vec2(-1, -1)).rgb + texture(src, vUV + t * vec2(1, -1)).rgb +
             texture(src, vUV + t * vec2(-1, 1)).rgb + texture(src, vUV + t * vec2(1, 1)).rgb;
    s += 2.0 * (texture(src, vUV + t * vec2(0, -1)).rgb + texture(src, vUV + t * vec2(0, 1)).rgb +
                texture(src, vUV + t * vec2(-1, 0)).rgb + texture(src, vUV + t * vec2(1, 0)).rgb);
    s += 4.0 * texture(src, vUV).rgb;
    outColor = vec4(s / 16.0, 1.0);
}

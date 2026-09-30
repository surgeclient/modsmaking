#version 450
layout(location = 0) in vec4 rect;
layout(location = 1) in vec4 color;
layout(push_constant) uniform PC { vec4 screen; } pc;
layout(location = 0) out vec4 vColor;

const vec2 corners[6] = vec2[](vec2(0, 0), vec2(1, 0), vec2(1, 1), vec2(0, 0), vec2(1, 1), vec2(0, 1));

void main() {
    vec2 p = rect.xy + corners[gl_VertexIndex] * rect.zw;
    vColor = color;
    gl_Position = vec4(p / pc.screen.xy * 2.0 - 1.0, 0.0, 1.0);
}

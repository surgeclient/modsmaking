#version 450
layout(location = 0) in vec4 rect;
layout(location = 1) in vec4 color;
layout(location = 2) in vec4 extra;
layout(push_constant) uniform PC { vec4 screen; } pc;
layout(location = 0) out vec4 vColor;
layout(location = 1) out vec2 vPix;
layout(location = 2) flat out vec4 vRect;
layout(location = 3) flat out vec4 vExtra;

const vec2 corners[6] = vec2[](vec2(0, 0), vec2(1, 0), vec2(1, 1), vec2(0, 0), vec2(1, 1), vec2(0, 1));

void main() {
    vec2 c = corners[gl_VertexIndex];
    vec2 p;
    if (extra.x < 0.5) {  // rounded rectangle, expanded for the soft edge
        float pad = extra.z + 1.5;
        p = rect.xy - pad + c * (rect.zw + 2.0 * pad);
    } else {  // capsule line segment
        vec2 a = rect.xy, b = rect.zw;
        vec2 d = b - a;
        float len = length(d);
        vec2 dir = len > 1e-4 ? d / len : vec2(1.0, 0.0);
        vec2 nrm = vec2(-dir.y, dir.x);
        float r = extra.y * 0.5 + 1.5;
        p = a - dir * r + dir * (len + 2.0 * r) * c.x + nrm * (c.y * 2.0 - 1.0) * r;
    }
    vColor = color;
    vPix = p;
    vRect = rect;
    vExtra = extra;
    gl_Position = vec4(p / pc.screen.xy * 2.0 - 1.0, 0.0, 1.0);
}

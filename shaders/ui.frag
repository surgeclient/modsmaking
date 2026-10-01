#version 450
layout(location = 0) in vec4 vColor;
layout(location = 1) in vec2 vPix;
layout(location = 2) flat in vec4 vRect;
layout(location = 3) flat in vec4 vExtra;
layout(location = 0) out vec4 outColor;

void main() {
    float d;
    if (vExtra.x < 0.5) {
        vec2 hs = vRect.zw * 0.5;
        vec2 q = abs(vPix - (vRect.xy + hs)) - hs + vExtra.y;
        d = length(max(q, 0.0)) + min(max(q.x, q.y), 0.0) - vExtra.y;
    } else {
        vec2 a = vRect.xy, b = vRect.zw;
        vec2 pa = vPix - a, ba = b - a;
        float h = clamp(dot(pa, ba) / max(dot(ba, ba), 1e-4), 0.0, 1.0);
        d = length(pa - ba * h) - vExtra.y * 0.5;
    }
    float soft = max(vExtra.z, 0.0) + 0.6;
    float cov = clamp(0.5 - d / soft, 0.0, 1.0);
    if (vExtra.z > 0.0) cov = 1.0 - smoothstep(-vExtra.z, vExtra.z, d);
    outColor = vec4(pow(vColor.rgb, vec3(2.2)), vColor.a * cov);
}

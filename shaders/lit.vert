#version 450
#extension GL_GOOGLE_include_directive : require
#include "scene_ubo.glsl"
#include "mesh_inputs.glsl"

void main() {
    mat4 M = mat4(m0, m1, m2, m3);
    vec4 wp = M * vec4(inPos, 1.0);
    float sway = inSway * iParams.z;
    if (sway > 0.0) {
        float t = S.params.x;
        vec2 w = vec2(sin(t * 1.3 + wp.x * 0.15 + wp.z * 0.1), cos(t * 1.1 + wp.z * 0.13)) * 0.6;
        w += vec2(sin(t * 3.7 + wp.x * 0.9), cos(t * 3.1 + wp.z * 0.8)) * 0.15;
        wp.xz += w * sway * 0.3 * S.world.z;
    }
    mat3 N = transpose(inverse(mat3(M)));
    emitOutputs(wp.xyz, normalize(N * inNormal), inPos, 0.0);
    gl_Position = S.viewProj * wp;
}

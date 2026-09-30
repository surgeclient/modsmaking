// Shared uniform block (keep in sync with SceneUBO in src/renderer.h)
layout(set = 0, binding = 0) uniform Scene {
    mat4 viewProj;
    mat4 invViewProj;
    mat4 lightViewProj;
    vec4 camPos;
    vec4 sunDir;
    vec4 sunColor;
    vec4 skyColor;
    vec4 groundColor;
    vec4 fogColor;
    vec4 params;
    vec4 moonDir;
    vec4 skySunDir;
    vec4 hollowPos;
    vec4 lightPos[16];
    vec4 lightColor[16];
} S;

// Shared uniform block (keep in sync with SceneUBO in src/renderer.h)
layout(set = 0, binding = 0) uniform Scene {
    mat4 viewProj;
    mat4 invViewProj;
    mat4 lightViewProj[2];
    vec4 camPos;
    vec4 sunDir;
    vec4 sunColor;
    vec4 skyColor;
    vec4 groundColor;
    vec4 fogColor;
    vec4 params;      // x time, y night, z hollow glow, w light count
    vec4 moonDir;
    vec4 skySunDir;
    vec4 hollowPos;
    vec4 camRight;
    vec4 camUp;
    vec4 playerPos;
    vec4 post;
    vec4 world;       // x terrain half size, y grass grid, z wind strength
    vec4 lightPos[16];
    vec4 lightColor[16];
} S;

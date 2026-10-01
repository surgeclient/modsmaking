layout(location = 0) in vec3 inPos;
layout(location = 1) in vec3 inNormal;
layout(location = 2) in vec4 inColor;
layout(location = 3) in vec4 inSkin;
layout(location = 4) in float inSway;
layout(location = 5) in vec4 m0;
layout(location = 6) in vec4 m1;
layout(location = 7) in vec4 m2;
layout(location = 8) in vec4 m3;
layout(location = 9) in vec4 iColor;
layout(location = 10) in vec4 iParams;
layout(location = 11) in vec2 inUV;
layout(location = 12) in float inCard;

layout(location = 0) out vec3 vWorld;
layout(location = 1) out vec3 vNormal;
layout(location = 2) out vec3 vColor;
layout(location = 3) out float vEmis;
layout(location = 4) out float vAO;
layout(location = 5) out vec3 vObj;
layout(location = 6) out vec2 vMat;
layout(location = 7) out vec2 vUV;
layout(location = 8) out float vCard;
layout(location = 9) out float vGlossV;

void emitOutputs(vec3 world, vec3 normal, vec3 obj, float glossV) {
    vUV = inUV;
    vCard = inCard;
    vGlossV = glossV;
    vWorld = world;
    vNormal = normal;
    vColor = pow(max(inColor.rgb * iColor.rgb, vec3(0.0)), vec3(2.2));
    vEmis = inColor.a * iColor.a;
    vAO = inSkin.w;
    vObj = obj;
    vMat = iParams.xy;
}

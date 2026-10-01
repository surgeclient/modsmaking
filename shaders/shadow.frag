#version 450
#extension GL_GOOGLE_include_directive : require
#include "cards.glsl"
layout(location = 0) in vec2 vUV;
layout(location = 1) in float vCard;

void main() {
    int card = int(vCard + 0.5);
    if (card > 0) {
        float shade;
        if (cardAlpha(card, vUV, shade) < 0.5) discard;
    }
}

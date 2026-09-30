#pragma once
#include "math.h"

inline float hash2(int x, int y, int seed = 0) {
    uint32_t h = (uint32_t)x * 374761393u + (uint32_t)y * 668265263u + (uint32_t)seed * 2246822519u;
    h = (h ^ (h >> 13)) * 1274126177u;
    h ^= h >> 16;
    return (h & 0xFFFFFF) / 16777216.0f;
}

inline float valueNoise(float x, float y, int seed = 0) {
    int xi = (int)std::floor(x), yi = (int)std::floor(y);
    float xf = x - xi, yf = y - yi;
    float u = xf * xf * (3 - 2 * xf), v = yf * yf * (3 - 2 * yf);
    float a = hash2(xi, yi, seed), b = hash2(xi + 1, yi, seed);
    float c = hash2(xi, yi + 1, seed), d = hash2(xi + 1, yi + 1, seed);
    return lerpf(lerpf(a, b, u), lerpf(c, d, u), v) * 2 - 1;
}

inline float fbm(float x, float y, int octaves, int seed = 0) {
    float sum = 0, amp = 0.5f, freq = 1, norm = 0;
    for (int i = 0; i < octaves; i++) {
        sum += valueNoise(x * freq, y * freq, seed + i * 17) * amp;
        norm += amp;
        amp *= 0.5f;
        freq *= 2.03f;
    }
    return sum / norm;
}

inline float ridged(float x, float y, int octaves, int seed = 0) {
    float sum = 0, amp = 0.5f, freq = 1, norm = 0;
    for (int i = 0; i < octaves; i++) {
        float n = 1 - std::fabs(valueNoise(x * freq, y * freq, seed + i * 31));
        sum += n * n * amp;
        norm += amp;
        amp *= 0.5f;
        freq *= 2.1f;
    }
    return sum / norm;
}

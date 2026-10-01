// Procedural alpha-cut cards: leaf clusters, pine needles, palm/fern fronds, feathers.
// Returns coverage 0..1; 'shade' receives a brightness variation for the surface.
float leaf(vec2 p, vec2 c, float ang, vec2 size, inout float shade, float id) {
    vec2 d = p - c;
    float s = sin(ang), co = cos(ang);
    d = vec2(co * d.x - s * d.y, s * d.x + co * d.y) / size;
    float w = (1.0 - d.y * d.y) * 0.95;
    float edge = abs(d.x) - w;
    float a = (1.0 - smoothstep(-0.08, 0.08, edge)) * step(abs(d.y), 1.0);
    if (a > 0.0) shade = (0.78 + 0.3 * fract(id * 7.31)) * (0.85 + 0.15 * smoothstep(0.0, 0.25, abs(d.x)));  // darker midrib
    return a;
}

float cardAlpha(int type, vec2 uv, out float shade) {
    shade = 1.0;
    if (type == 1) {
        float a = 0.0;
        a = max(a, leaf(uv, vec2(0.5, 0.52), 0.0, vec2(0.13, 0.22), shade, 1.0));
        a = max(a, leaf(uv, vec2(0.27, 0.36), 0.95, vec2(0.12, 0.2), shade, 2.0));
        a = max(a, leaf(uv, vec2(0.73, 0.34), -0.9, vec2(0.12, 0.2), shade, 3.0));
        a = max(a, leaf(uv, vec2(0.29, 0.72), 2.3, vec2(0.12, 0.2), shade, 4.0));
        a = max(a, leaf(uv, vec2(0.71, 0.71), -2.25, vec2(0.12, 0.2), shade, 5.0));
        a = max(a, leaf(uv, vec2(0.52, 0.17), 0.15, vec2(0.1, 0.16), shade, 6.0));
        a = max(a, leaf(uv, vec2(0.48, 0.86), 3.0, vec2(0.1, 0.15), shade, 7.0));
        a = max(a, leaf(uv, vec2(0.13, 0.55), 1.6, vec2(0.09, 0.14), shade, 8.0));
        a = max(a, leaf(uv, vec2(0.87, 0.53), -1.6, vec2(0.09, 0.14), shade, 9.0));
        return a;
    }
    if (type == 2) {  // conifer sprig
        float x = uv.x - 0.5;
        float stem = (1.0 - smoothstep(0.02, 0.045, abs(x))) * step(uv.y, 0.96);
        float t = fract(uv.y * 15.0 + abs(x) * 4.5);
        float needle = (1.0 - smoothstep(0.18, 0.32, t)) * (1.0 - smoothstep(0.4, 0.48, abs(x) + uv.y * 0.18));
        shade = 0.75 + 0.35 * (1.0 - abs(x) * 2.0);
        return max(stem, needle);
    }
    if (type == 3) {  // frond with leaflets
        float x = uv.x - 0.5;
        float stem = 1.0 - smoothstep(0.025, 0.05, abs(x));
        float t = fract(uv.y * 16.0 - abs(x) * 2.4);
        float width = 0.48 * sin(clamp(uv.y, 0.0, 1.0) * 3.0) + 0.04;
        float leaflet = (1.0 - smoothstep(0.42, 0.62, t)) * (1.0 - smoothstep(width - 0.04, width, abs(x)));
        shade = 0.8 + 0.25 * fract(floor(uv.y * 16.0) * 0.37);
        return max(stem, leaflet);
    }
    if (type == 4) {  // feather
        float x = uv.x - 0.5;
        float width = 0.5 * (1.0 - pow(uv.y, 5.0)) * (0.55 + 0.45 * smoothstep(0.0, 0.25, uv.y));
        float vane = 1.0 - smoothstep(width - 0.05, width, abs(x));
        float notch = step(0.45, uv.y) * step(abs(fract(uv.y * 5.0 + abs(x) * 1.2) - 0.5), 0.04) * step(0.35, abs(x) / max(width, 0.01));
        float shaft = 1.0 - smoothstep(0.015, 0.035, abs(x));
        shade = mix(0.85 + 0.15 * sin(uv.y * 140.0 + abs(x) * 50.0), 0.55, shaft);
        return clamp(vane - notch * 0.9, 0.0, 1.0);
    }
    return 1.0;
}

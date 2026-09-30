// Small self-contained vector / matrix library (column-major, Vulkan clip space).
#pragma once
#include <cmath>
#include <cstdint>
#include <algorithm>

constexpr float PI = 3.14159265358979f;
constexpr float TAU = 6.28318530717959f;

struct vec2 {
    float x = 0, y = 0;
    vec2() = default;
    constexpr vec2(float x_, float y_) : x(x_), y(y_) {}
    vec2 operator+(vec2 o) const { return {x + o.x, y + o.y}; }
    vec2 operator-(vec2 o) const { return {x - o.x, y - o.y}; }
    vec2 operator*(float s) const { return {x * s, y * s}; }
};

struct vec3 {
    float x = 0, y = 0, z = 0;
    vec3() = default;
    constexpr vec3(float x_, float y_, float z_) : x(x_), y(y_), z(z_) {}
    vec3 operator+(vec3 o) const { return {x + o.x, y + o.y, z + o.z}; }
    vec3 operator-(vec3 o) const { return {x - o.x, y - o.y, z - o.z}; }
    vec3 operator*(float s) const { return {x * s, y * s, z * s}; }
    vec3 operator*(vec3 o) const { return {x * o.x, y * o.y, z * o.z}; }
    vec3 operator/(float s) const { return {x / s, y / s, z / s}; }
    vec3 operator-() const { return {-x, -y, -z}; }
    vec3& operator+=(vec3 o) { x += o.x; y += o.y; z += o.z; return *this; }
    vec3& operator-=(vec3 o) { x -= o.x; y -= o.y; z -= o.z; return *this; }
    vec3& operator*=(float s) { x *= s; y *= s; z *= s; return *this; }
};

struct vec4 {
    float x = 0, y = 0, z = 0, w = 0;
    vec4() = default;
    constexpr vec4(float x_, float y_, float z_, float w_) : x(x_), y(y_), z(z_), w(w_) {}
    constexpr vec4(vec3 v, float w_) : x(v.x), y(v.y), z(v.z), w(w_) {}
    vec3 xyz() const { return {x, y, z}; }
};

inline float dot(vec3 a, vec3 b) { return a.x * b.x + a.y * b.y + a.z * b.z; }
inline vec3 cross(vec3 a, vec3 b) { return {a.y * b.z - a.z * b.y, a.z * b.x - a.x * b.z, a.x * b.y - a.y * b.x}; }
inline float length(vec3 v) { return std::sqrt(dot(v, v)); }
inline float length2(vec3 v) { return dot(v, v); }
inline vec3 normalize(vec3 v) { float l = length(v); return l > 1e-6f ? v / l : vec3(0, 1, 0); }
inline float lengthXZ(vec3 v) { return std::sqrt(v.x * v.x + v.z * v.z); }
inline float distXZ(vec3 a, vec3 b) { return lengthXZ(a - b); }
inline vec3 lerp(vec3 a, vec3 b, float t) { return a + (b - a) * t; }
inline float lerpf(float a, float b, float t) { return a + (b - a) * t; }
inline float clampf(float v, float lo, float hi) { return v < lo ? lo : (v > hi ? hi : v); }
inline float saturate(float v) { return clampf(v, 0.f, 1.f); }
inline float smoothstep(float e0, float e1, float x) { float t = saturate((x - e0) / (e1 - e0)); return t * t * (3 - 2 * t); }
inline float wrapAngle(float a) { while (a > PI) a -= TAU; while (a < -PI) a += TAU; return a; }
inline float approachAngle(float cur, float target, float maxStep) {
    float d = wrapAngle(target - cur);
    if (d > maxStep) d = maxStep;
    if (d < -maxStep) d = -maxStep;
    return wrapAngle(cur + d);
}
inline vec3 dirFromYaw(float yaw) { return {std::sin(yaw), 0, std::cos(yaw)}; }
inline float yawOf(vec3 d) { return std::atan2(d.x, d.z); }

struct mat4 {
    float m[16];  // column-major: m[col*4 + row]
    static mat4 identity() {
        mat4 r{};
        r.m[0] = r.m[5] = r.m[10] = r.m[15] = 1;
        return r;
    }
    float& at(int row, int col) { return m[col * 4 + row]; }
    float at(int row, int col) const { return m[col * 4 + row]; }
    mat4 operator*(const mat4& b) const {
        mat4 r{};
        for (int c = 0; c < 4; c++)
            for (int rr = 0; rr < 4; rr++) {
                float s = 0;
                for (int k = 0; k < 4; k++) s += at(rr, k) * b.at(k, c);
                r.m[c * 4 + rr] = s;
            }
        return r;
    }
    vec3 transformPoint(vec3 p) const {
        return {m[0] * p.x + m[4] * p.y + m[8] * p.z + m[12],
                m[1] * p.x + m[5] * p.y + m[9] * p.z + m[13],
                m[2] * p.x + m[6] * p.y + m[10] * p.z + m[14]};
    }
    vec4 transform(vec4 p) const {
        return {m[0] * p.x + m[4] * p.y + m[8] * p.z + m[12] * p.w,
                m[1] * p.x + m[5] * p.y + m[9] * p.z + m[13] * p.w,
                m[2] * p.x + m[6] * p.y + m[10] * p.z + m[14] * p.w,
                m[3] * p.x + m[7] * p.y + m[11] * p.z + m[15] * p.w};
    }
    vec3 transformDir(vec3 p) const {
        return {m[0] * p.x + m[4] * p.y + m[8] * p.z,
                m[1] * p.x + m[5] * p.y + m[9] * p.z,
                m[2] * p.x + m[6] * p.y + m[10] * p.z};
    }
};

inline mat4 translate(vec3 t) { mat4 r = mat4::identity(); r.m[12] = t.x; r.m[13] = t.y; r.m[14] = t.z; return r; }
inline mat4 scale(vec3 s) { mat4 r = mat4::identity(); r.m[0] = s.x; r.m[5] = s.y; r.m[10] = s.z; return r; }
inline mat4 rotateX(float a) { mat4 r = mat4::identity(); float c = std::cos(a), s = std::sin(a); r.m[5] = c; r.m[6] = s; r.m[9] = -s; r.m[10] = c; return r; }
inline mat4 rotateY(float a) { mat4 r = mat4::identity(); float c = std::cos(a), s = std::sin(a); r.m[0] = c; r.m[2] = -s; r.m[8] = s; r.m[10] = c; return r; }
inline mat4 rotateZ(float a) { mat4 r = mat4::identity(); float c = std::cos(a), s = std::sin(a); r.m[0] = c; r.m[1] = s; r.m[4] = -s; r.m[5] = c; return r; }

// Matrix whose columns are the given basis vectors plus translation.
inline mat4 basis(vec3 x, vec3 y, vec3 z, vec3 t) {
    mat4 r = mat4::identity();
    r.m[0] = x.x; r.m[1] = x.y; r.m[2] = x.z;
    r.m[4] = y.x; r.m[5] = y.y; r.m[6] = y.z;
    r.m[8] = z.x; r.m[9] = z.y; r.m[10] = z.z;
    r.m[12] = t.x; r.m[13] = t.y; r.m[14] = t.z;
    return r;
}

// Right-handed perspective for Vulkan: depth 0..1, Y flipped.
inline mat4 perspective(float fovY, float aspect, float zn, float zf) {
    mat4 r{};
    float f = 1.0f / std::tan(fovY * 0.5f);
    r.m[0] = f / aspect;
    r.m[5] = -f;
    r.m[10] = zf / (zn - zf);
    r.m[11] = -1;
    r.m[14] = zn * zf / (zn - zf);
    return r;
}

inline mat4 ortho(float l, float r_, float b, float t, float zn, float zf) {
    mat4 r = mat4::identity();
    r.m[0] = 2 / (r_ - l);
    r.m[5] = -2 / (t - b);
    r.m[10] = -1 / (zf - zn);
    r.m[12] = -(r_ + l) / (r_ - l);
    r.m[13] = (t + b) / (t - b);
    r.m[14] = -zn / (zf - zn);
    return r;
}

inline mat4 lookAt(vec3 eye, vec3 target, vec3 up) {
    vec3 f = normalize(target - eye);
    vec3 s = normalize(cross(f, up));
    vec3 u = cross(s, f);
    mat4 r = mat4::identity();
    r.m[0] = s.x; r.m[4] = s.y; r.m[8] = s.z;
    r.m[1] = u.x; r.m[5] = u.y; r.m[9] = u.z;
    r.m[2] = -f.x; r.m[6] = -f.y; r.m[10] = -f.z;
    r.m[12] = -dot(s, eye);
    r.m[13] = -dot(u, eye);
    r.m[14] = dot(f, eye);
    return r;
}

inline mat4 inverse(const mat4& a) {
    const float* m = a.m;
    float inv[16];
    inv[0] = m[5] * m[10] * m[15] - m[5] * m[11] * m[14] - m[9] * m[6] * m[15] + m[9] * m[7] * m[14] + m[13] * m[6] * m[11] - m[13] * m[7] * m[10];
    inv[4] = -m[4] * m[10] * m[15] + m[4] * m[11] * m[14] + m[8] * m[6] * m[15] - m[8] * m[7] * m[14] - m[12] * m[6] * m[11] + m[12] * m[7] * m[10];
    inv[8] = m[4] * m[9] * m[15] - m[4] * m[11] * m[13] - m[8] * m[5] * m[15] + m[8] * m[7] * m[13] + m[12] * m[5] * m[11] - m[12] * m[7] * m[9];
    inv[12] = -m[4] * m[9] * m[14] + m[4] * m[10] * m[13] + m[8] * m[5] * m[14] - m[8] * m[6] * m[13] - m[12] * m[5] * m[10] + m[12] * m[6] * m[9];
    inv[1] = -m[1] * m[10] * m[15] + m[1] * m[11] * m[14] + m[9] * m[2] * m[15] - m[9] * m[3] * m[14] - m[13] * m[2] * m[11] + m[13] * m[3] * m[10];
    inv[5] = m[0] * m[10] * m[15] - m[0] * m[11] * m[14] - m[8] * m[2] * m[15] + m[8] * m[3] * m[14] + m[12] * m[2] * m[11] - m[12] * m[3] * m[10];
    inv[9] = -m[0] * m[9] * m[15] + m[0] * m[11] * m[13] + m[8] * m[1] * m[15] - m[8] * m[3] * m[13] - m[12] * m[1] * m[11] + m[12] * m[3] * m[9];
    inv[13] = m[0] * m[9] * m[14] - m[0] * m[10] * m[13] - m[8] * m[1] * m[14] + m[8] * m[2] * m[13] + m[12] * m[1] * m[10] - m[12] * m[2] * m[9];
    inv[2] = m[1] * m[6] * m[15] - m[1] * m[7] * m[14] - m[5] * m[2] * m[15] + m[5] * m[3] * m[14] + m[13] * m[2] * m[7] - m[13] * m[3] * m[6];
    inv[6] = -m[0] * m[6] * m[15] + m[0] * m[7] * m[14] + m[4] * m[2] * m[15] - m[4] * m[3] * m[14] - m[12] * m[2] * m[7] + m[12] * m[3] * m[6];
    inv[10] = m[0] * m[5] * m[15] - m[0] * m[7] * m[13] - m[4] * m[1] * m[15] + m[4] * m[3] * m[13] + m[12] * m[1] * m[7] - m[12] * m[3] * m[5];
    inv[14] = -m[0] * m[5] * m[14] + m[0] * m[6] * m[13] + m[4] * m[1] * m[14] - m[4] * m[2] * m[13] - m[12] * m[1] * m[6] + m[12] * m[2] * m[5];
    inv[3] = -m[1] * m[6] * m[11] + m[1] * m[7] * m[10] + m[5] * m[2] * m[11] - m[5] * m[3] * m[10] - m[9] * m[2] * m[7] + m[9] * m[3] * m[6];
    inv[7] = m[0] * m[6] * m[11] - m[0] * m[7] * m[10] - m[4] * m[2] * m[11] + m[4] * m[3] * m[10] + m[8] * m[2] * m[7] - m[8] * m[3] * m[6];
    inv[11] = -m[0] * m[5] * m[11] + m[0] * m[7] * m[9] + m[4] * m[1] * m[11] - m[4] * m[3] * m[9] - m[8] * m[1] * m[7] + m[8] * m[3] * m[5];
    inv[15] = m[0] * m[5] * m[10] - m[0] * m[6] * m[9] - m[4] * m[1] * m[10] + m[4] * m[2] * m[9] + m[8] * m[1] * m[6] - m[8] * m[2] * m[5];
    float det = m[0] * inv[0] + m[1] * inv[4] + m[2] * inv[8] + m[3] * inv[12];
    mat4 r{};
    if (std::fabs(det) < 1e-12f) return mat4::identity();
    det = 1.0f / det;
    for (int i = 0; i < 16; i++) r.m[i] = inv[i] * det;
    return r;
}

// Deterministic PRNG (xorshift) so the island is identical every launch.
struct Rng {
    uint32_t s;
    explicit Rng(uint32_t seed = 1337) : s(seed ? seed : 1) {}
    uint32_t next() { s ^= s << 13; s ^= s >> 17; s ^= s << 5; return s; }
    float f() { return (next() & 0xFFFFFF) / 16777216.0f; }
    float range(float a, float b) { return a + (b - a) * f(); }
    int irange(int a, int b) { return a + (int)(next() % (uint32_t)(b - a + 1)); }
    bool chance(float p) { return f() < p; }
};

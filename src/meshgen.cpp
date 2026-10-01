#include "meshgen.h"
#include "noise.h"
#include <thread>

// ---------------------------------------------------------------------------
// Noise
// ---------------------------------------------------------------------------
static float hash3(int x, int y, int z, int seed) {
    uint32_t h = (uint32_t)x * 374761393u + (uint32_t)y * 668265263u + (uint32_t)z * 2147483647u + (uint32_t)seed * 2246822519u;
    h = (h ^ (h >> 13)) * 1274126177u;
    h ^= h >> 16;
    return (h & 0xFFFFFF) / 16777216.0f;
}

float noise3(vec3 p, int seed) {
    int xi = (int)std::floor(p.x), yi = (int)std::floor(p.y), zi = (int)std::floor(p.z);
    float xf = p.x - xi, yf = p.y - yi, zf = p.z - zi;
    float u = xf * xf * (3 - 2 * xf), v = yf * yf * (3 - 2 * yf), w = zf * zf * (3 - 2 * zf);
    float c000 = hash3(xi, yi, zi, seed), c100 = hash3(xi + 1, yi, zi, seed);
    float c010 = hash3(xi, yi + 1, zi, seed), c110 = hash3(xi + 1, yi + 1, zi, seed);
    float c001 = hash3(xi, yi, zi + 1, seed), c101 = hash3(xi + 1, yi, zi + 1, seed);
    float c011 = hash3(xi, yi + 1, zi + 1, seed), c111 = hash3(xi + 1, yi + 1, zi + 1, seed);
    float x00 = lerpf(c000, c100, u), x10 = lerpf(c010, c110, u), x01 = lerpf(c001, c101, u), x11 = lerpf(c011, c111, u);
    return lerpf(lerpf(x00, x10, v), lerpf(x01, x11, v), w) * 2 - 1;
}

float fbm3(vec3 p, int octaves, int seed) {
    float sum = 0, amp = 0.5f, norm = 0;
    for (int i = 0; i < octaves; i++) {
        sum += noise3(p, seed + i * 13) * amp;
        norm += amp;
        amp *= 0.5f;
        p = p * 2.03f + vec3(17.1f, 3.7f, 9.2f);
    }
    return sum / norm;
}

// ---------------------------------------------------------------------------
// Shapes
// ---------------------------------------------------------------------------
Prim capsule(vec3 a, vec3 b, float ra, float rb, vec3 color, int bone, float blend) {
    Prim p;
    p.type = Prim::CAPSULE;
    p.a = a;
    p.b = b;
    p.ra = ra;
    p.rb = rb;
    p.color = color;
    p.bone = bone;
    p.blend = blend;
    return p;
}

Prim ellipsoid(vec3 c, vec3 radii, vec3 color, int bone, float blend) {
    Prim p;
    p.type = Prim::ELLIPSOID;
    p.a = c;
    p.radii = radii;
    p.color = color;
    p.bone = bone;
    p.blend = blend;
    return p;
}

Prim ellipsoidRot(vec3 c, vec3 radii, float yaw, float pitch, float roll, vec3 color, int bone, float blend) {
    Prim p = ellipsoid(c, radii, color, bone, blend);
    mat4 r = rotateY(yaw) * rotateX(pitch) * rotateZ(roll);
    p.ax = r.transformDir(vec3(1, 0, 0));
    p.ay = r.transformDir(vec3(0, 1, 0));
    p.az = r.transformDir(vec3(0, 0, 1));
    return p;
}

Prim roundBox(vec3 c, vec3 halfExt, float round, vec3 color, int bone, float blend) {
    Prim p;
    p.type = Prim::BOX;
    p.a = c;
    p.radii = halfExt;
    p.ra = round;
    p.color = color;
    p.bone = bone;
    p.blend = blend;
    return p;
}

static float sdRoundCone(vec3 p, vec3 a, vec3 b, float r1, float r2) {
    vec3 ba = b - a;
    float l2 = dot(ba, ba);
    float rr = r1 - r2;
    if (l2 < 1e-8f || rr * rr >= l2) {  // degenerate: fall back to the bigger sphere
        return r1 >= r2 ? length(p - a) - r1 : length(p - b) - r2;
    }
    float a2 = l2 - rr * rr;
    float il2 = 1.0f / l2;
    vec3 pa = p - a;
    float y = dot(pa, ba);
    float z = y - l2;
    vec3 xv = pa * l2 - ba * y;
    float x2 = dot(xv, xv);
    float y2 = y * y * l2;
    float z2 = z * z * l2;
    float k = (rr > 0 ? 1.0f : -1.0f) * rr * rr * x2;
    if ((z > 0 ? 1.0f : -1.0f) * a2 * z2 > k) return std::sqrt(x2 + z2) * il2 - r2;
    if ((y > 0 ? 1.0f : -1.0f) * a2 * y2 < k) return std::sqrt(x2 + y2) * il2 - r1;
    return (std::sqrt(x2 * a2 * il2) + y * rr) * il2 - r1;
}

static float sdEllipsoid(vec3 q, vec3 r) {
    vec3 a(q.x / r.x, q.y / r.y, q.z / r.z);
    vec3 b(q.x / (r.x * r.x), q.y / (r.y * r.y), q.z / (r.z * r.z));
    float k0 = length(a), k1 = length(b);
    if (k1 < 1e-8f) return -std::min(r.x, std::min(r.y, r.z));
    return k0 * (k0 - 1.0f) / k1;
}

static float sdRoundBox(vec3 q, vec3 b, float r) {
    vec3 d(std::fabs(q.x) - b.x + r, std::fabs(q.y) - b.y + r, std::fabs(q.z) - b.z + r);
    vec3 m(std::max(d.x, 0.0f), std::max(d.y, 0.0f), std::max(d.z, 0.0f));
    return length(m) + std::min(std::max(d.x, std::max(d.y, d.z)), 0.0f) - r;
}

static float smin(float a, float b, float k) {
    if (k <= 1e-5f) return std::min(a, b);
    float h = std::max(k - std::fabs(a - b), 0.0f) / k;
    return std::min(a, b) - h * h * k * 0.25f;
}

namespace {

struct Box3 {
    vec3 lo, hi;
    bool contains(vec3 p) const { return p.x >= lo.x && p.y >= lo.y && p.z >= lo.z && p.x <= hi.x && p.y <= hi.y && p.z <= hi.z; }
};

struct Field {
    const SdfModel& m;
    std::vector<Box3> boxes;
    float margin;

    Field(const SdfModel& model, float marginIn) : m(model), margin(marginIn) {
        for (const auto& p : m.prims) {
            float r = 0;
            vec3 lo, hi;
            switch (p.type) {
                case Prim::CAPSULE:
                    r = std::max(p.ra, p.rb);
                    lo = vec3(std::min(p.a.x, p.b.x), std::min(p.a.y, p.b.y), std::min(p.a.z, p.b.z));
                    hi = vec3(std::max(p.a.x, p.b.x), std::max(p.a.y, p.b.y), std::max(p.a.z, p.b.z));
                    break;
                case Prim::ELLIPSOID:
                    r = std::max(p.radii.x, std::max(p.radii.y, p.radii.z));
                    lo = hi = p.a;
                    break;
                case Prim::BOX:
                    r = length(p.radii);
                    lo = hi = p.a;
                    break;
            }
            float e = r + p.blend + p.disp + margin;
            boxes.push_back({lo - vec3(e, e, e), hi + vec3(e, e, e)});
        }
    }

    float prim(const Prim& p, vec3 q) const {
        float d = 0;
        switch (p.type) {
            case Prim::CAPSULE: d = sdRoundCone(q, p.a, p.b, p.ra, p.rb); break;
            case Prim::ELLIPSOID: {
                vec3 l = q - p.a;
                d = sdEllipsoid(vec3(dot(l, p.ax), dot(l, p.ay), dot(l, p.az)), p.radii);
                break;
            }
            case Prim::BOX: {
                vec3 l = q - p.a;
                d = sdRoundBox(vec3(dot(l, p.ax), dot(l, p.ay), dot(l, p.az)), p.radii, p.ra);
                break;
            }
        }
        if (p.disp > 0) d += p.disp * fbm3(q * p.dispFreq, 3, m.seed);
        return d;
    }

    float eval(vec3 q) const {
        float d = 1e9f;
        for (size_t i = 0; i < m.prims.size(); i++) {
            if (!boxes[i].contains(q)) continue;
            const Prim& p = m.prims[i];
            float di = prim(p, q);
            if (p.subtract) d = -smin(-d, di, p.blend);
            else d = smin(d, di, p.blend);
        }
        return d > 1e8f ? margin : d;
    }

    vec3 normal(vec3 q, float e) const {
        return normalize(vec3(eval(q + vec3(e, 0, 0)) - eval(q - vec3(e, 0, 0)), eval(q + vec3(0, e, 0)) - eval(q - vec3(0, e, 0)),
                              eval(q + vec3(0, 0, e)) - eval(q - vec3(0, 0, e))));
    }
};

}  // namespace

void buildSdf(const SdfModel& m, float voxel, MeshData& out) {
    if (m.prims.empty()) return;
    Field f(m, voxel * 3.0f);
    vec3 lo(1e9f, 1e9f, 1e9f), hi(-1e9f, -1e9f, -1e9f);
    for (size_t i = 0; i < m.prims.size(); i++) {
        if (m.prims[i].subtract) continue;
        const Box3& b = f.boxes[i];
        lo = vec3(std::min(lo.x, b.lo.x), std::min(lo.y, b.lo.y), std::min(lo.z, b.lo.z));
        hi = vec3(std::max(hi.x, b.hi.x), std::max(hi.y, b.hi.y), std::max(hi.z, b.hi.z));
    }
    int nx = (int)std::ceil((hi.x - lo.x) / voxel) + 1;
    int ny = (int)std::ceil((hi.y - lo.y) / voxel) + 1;
    int nz = (int)std::ceil((hi.z - lo.z) / voxel) + 1;
    auto P = [&](int i, int j, int k) { return lo + vec3(i * voxel, j * voxel, k * voxel); };
    auto I = [&](int i, int j, int k) { return ((size_t)k * (ny + 1) + j) * (nx + 1) + i; };

    // 1. Sample the field (multi-threaded over z slices).
    std::vector<float> grid((size_t)(nx + 1) * (ny + 1) * (nz + 1));
    unsigned threads = std::max(1u, std::min(16u, std::thread::hardware_concurrency()));
    {
        std::vector<std::thread> pool;
        for (unsigned t = 0; t < threads; t++)
            pool.emplace_back([&, t]() {
                for (int k = (int)t; k <= nz; k += (int)threads)
                    for (int j = 0; j <= ny; j++)
                        for (int i = 0; i <= nx; i++) grid[I(i, j, k)] = f.eval(P(i, j, k));
            });
        for (auto& th : pool) th.join();
    }

    // 2. One vertex per surface-crossing cell (naive surface nets).
    std::vector<int> cellVert((size_t)nx * ny * nz, -1);
    std::vector<vec3> verts;
    const int corner[8][3] = {{0, 0, 0}, {1, 0, 0}, {0, 1, 0}, {1, 1, 0}, {0, 0, 1}, {1, 0, 1}, {0, 1, 1}, {1, 1, 1}};
    const int edges[12][2] = {{0, 1}, {2, 3}, {4, 5}, {6, 7}, {0, 2}, {1, 3}, {4, 6}, {5, 7}, {0, 4}, {1, 5}, {2, 6}, {3, 7}};
    for (int k = 0; k < nz; k++)
        for (int j = 0; j < ny; j++)
            for (int i = 0; i < nx; i++) {
                float v[8];
                int inside = 0;
                for (int c = 0; c < 8; c++) {
                    v[c] = grid[I(i + corner[c][0], j + corner[c][1], k + corner[c][2])];
                    if (v[c] < 0) inside++;
                }
                if (inside == 0 || inside == 8) continue;
                vec3 sum;
                int n = 0;
                for (auto& e : edges) {
                    float a = v[e[0]], b = v[e[1]];
                    if ((a < 0) == (b < 0)) continue;
                    float t = a / (a - b);
                    vec3 pa((float)corner[e[0]][0], (float)corner[e[0]][1], (float)corner[e[0]][2]);
                    vec3 pb((float)corner[e[1]][0], (float)corner[e[1]][1], (float)corner[e[1]][2]);
                    sum += lerp(pa, pb, t);
                    n++;
                }
                cellVert[((size_t)k * ny + j) * nx + i] = (int)verts.size();
                verts.push_back(P(i, j, k) + (sum / (float)n) * voxel);
            }

    // 3. Quads across every sign-changing grid edge.
    std::vector<uint32_t> quads;
    auto C = [&](int i, int j, int k) { return cellVert[((size_t)k * ny + j) * nx + i]; };
    auto emit = [&](int a, int b, int c, int d, bool flip) {
        if (a < 0 || b < 0 || c < 0 || d < 0) return;
        if (flip) std::swap(b, d);
        quads.insert(quads.end(), {(uint32_t)a, (uint32_t)b, (uint32_t)c, (uint32_t)d});
    };
    for (int k = 0; k <= nz; k++)
        for (int j = 0; j <= ny; j++)
            for (int i = 0; i <= nx; i++) {
                float d0 = grid[I(i, j, k)];
                if (i < nx && j >= 1 && k >= 1 && j < ny && k < nz) {
                    float d1 = grid[I(i + 1, j, k)];
                    if ((d0 < 0) != (d1 < 0)) emit(C(i, j - 1, k - 1), C(i, j, k - 1), C(i, j, k), C(i, j - 1, k), !(d0 < 0));
                }
                if (j < ny && i >= 1 && k >= 1 && i < nx && k < nz) {
                    float d1 = grid[I(i, j + 1, k)];
                    if ((d0 < 0) != (d1 < 0)) emit(C(i - 1, j, k - 1), C(i - 1, j, k), C(i, j, k), C(i, j, k - 1), !(d0 < 0));
                }
                if (k < nz && i >= 1 && j >= 1 && i < nx && j < ny) {
                    float d1 = grid[I(i, j, k + 1)];
                    if ((d0 < 0) != (d1 < 0)) emit(C(i - 1, j - 1, k), C(i, j - 1, k), C(i, j, k), C(i - 1, j, k), !(d0 < 0));
                }
            }

    // 4. Refine, shade, weight (multi-threaded over vertices).
    size_t base = out.v.size();
    out.v.resize(base + verts.size());
    int boneCount = 0;
    for (auto& p : m.prims) boneCount = std::max(boneCount, p.bone + 1);
    {
        std::vector<std::thread> pool;
        for (unsigned t = 0; t < threads; t++)
            pool.emplace_back([&, t]() {
                std::vector<float> boneW(boneCount);
                for (size_t vi = t; vi < verts.size(); vi += threads) {
                    vec3 p = verts[vi];
                    vec3 n = f.normal(p, voxel * 0.5f);
                    for (int it = 0; it < 2; it++) p -= n * f.eval(p);  // project onto the surface
                    n = f.normal(p, voxel * 0.5f);

                    // Colour, emissive & bones: soft-blend the shapes nearest to this point.
                    float dmin = 1e9f;
                    std::vector<float> dist(m.prims.size(), 1e9f);
                    for (size_t i = 0; i < m.prims.size(); i++) {
                        if (m.prims[i].subtract || !f.boxes[i].contains(p)) continue;
                        dist[i] = f.prim(m.prims[i], p);
                        dmin = std::min(dmin, dist[i]);
                    }
                    float sigma = std::max(voxel * 1.2f, 0.004f) / m.colorSharpness;
                    vec3 col;
                    float emis = 0, wsum = 0;
                    std::fill(boneW.begin(), boneW.end(), 0.0f);
                    for (size_t i = 0; i < m.prims.size(); i++) {
                        if (dist[i] > 1e8f) continue;
                        float w = std::exp(-(dist[i] - dmin) / sigma);
                        const Prim& pr = m.prims[i];
                        col += pr.color * w;
                        emis += pr.emissive * w;
                        wsum += w;
                        float bw = std::exp(-(dist[i] - dmin) / (sigma * 3.0f + pr.blend));
                        boneW[pr.bone] += bw;
                    }
                    if (wsum > 0) {
                        col = col / wsum;
                        emis /= wsum;
                    }
                    int b0 = 0, b1 = 0;
                    for (int b = 0; b < boneCount; b++)
                        if (boneW[b] > boneW[b0]) b0 = b;
                    b1 = b0;
                    for (int b = 0; b < boneCount; b++)
                        if (b != b0 && (b1 == b0 || boneW[b] > boneW[b1])) b1 = b;
                    float w1 = (b1 != b0 && boneW[b0] + boneW[b1] > 0) ? boneW[b1] / (boneW[b0] + boneW[b1]) : 0.0f;

                    // Ambient occlusion from the distance field.
                    float h = voxel * 1.5f, occ = 0, decay = 1;
                    for (int s = 1; s <= 5; s++) {
                        float dd = f.eval(p + n * (h * s));
                        occ += (h * s - dd) * decay;
                        decay *= 0.6f;
                    }
                    float ao = clampf(1.0f - occ / (h * 3.0f), 0.25f, 1.0f);
                    if (m.colorFn) col = m.colorFn(p, n, col, ao);
                    float sway = m.swayFn ? m.swayFn(p) : 0.0f;
                    out.v[base + vi] = {p, n, vec4(col, emis), vec4((float)b0, (float)b1, w1, ao), sway};
                }
            });
        for (auto& th : pool) th.join();
    }

    for (size_t q = 0; q + 3 < quads.size(); q += 4) {
        uint32_t a = quads[q] + (uint32_t)base, b = quads[q + 1] + (uint32_t)base;
        uint32_t c = quads[q + 2] + (uint32_t)base, d = quads[q + 3] + (uint32_t)base;
        // Split along the shorter diagonal.
        if (length2(out.v[a].pos - out.v[c].pos) < length2(out.v[b].pos - out.v[d].pos))
            out.idx.insert(out.idx.end(), {a, b, c, a, c, d});
        else
            out.idx.insert(out.idx.end(), {a, b, d, b, c, d});
    }
}

// ---------------------------------------------------------------------------
// Explicit geometry
// ---------------------------------------------------------------------------
static Vertex mkV(vec3 p, vec3 n, vec3 c, float e, int bone, float sway = 0, float ao = 1) {
    return {p, n, vec4(c, e), vec4((float)bone, (float)bone, 0, ao), sway};
}

static void frameFor(vec3 dir, vec3& x, vec3& z) {
    vec3 ref = std::fabs(dir.y) < 0.95f ? vec3(0, 1, 0) : vec3(1, 0, 0);
    x = normalize(cross(ref, dir));
    z = cross(x, dir);
}

void addSphere(MeshData& m, vec3 c, vec3 radii, vec3 color, int bone, float emissive, int segs) {
    uint32_t base = (uint32_t)m.v.size();
    int rings = std::max(4, segs * 2 / 3);
    for (int r = 0; r <= rings; r++) {
        float phi = PI * r / rings;
        for (int s = 0; s <= segs; s++) {
            float th = TAU * s / segs;
            vec3 n(std::sin(phi) * std::sin(th), std::cos(phi), std::sin(phi) * std::cos(th));
            vec3 p = c + n * radii;
            vec3 nn = normalize(vec3(n.x / radii.x, n.y / radii.y, n.z / radii.z));
            m.v.push_back(mkV(p, nn, color, emissive, bone));
        }
    }
    for (int r = 0; r < rings; r++)
        for (int s = 0; s < segs; s++) {
            uint32_t a = base + r * (segs + 1) + s, b = a + segs + 1;
            m.idx.insert(m.idx.end(), {a, b, a + 1, a + 1, b, b + 1});
        }
}

void addCylinder(MeshData& m, vec3 a, vec3 b, float ra, float rb, vec3 color, int bone, int segs, float sway) {
    vec3 dir = normalize(b - a), x, z;
    frameFor(dir, x, z);
    uint32_t base = (uint32_t)m.v.size();
    for (int s = 0; s <= segs; s++) {
        float th = TAU * s / segs;
        vec3 radial = x * std::cos(th) + z * std::sin(th);
        m.v.push_back(mkV(a + radial * ra, radial, color, 0, bone, 0));
        m.v.push_back(mkV(b + radial * rb, radial, color, 0, bone, sway));
    }
    for (int s = 0; s < segs; s++) {
        uint32_t i0 = base + s * 2;
        m.idx.insert(m.idx.end(), {i0, i0 + 1, i0 + 2, i0 + 1, i0 + 3, i0 + 2});
    }
}

void addCone(MeshData& m, vec3 baseP, vec3 tip, float r, vec3 color, int bone, float emissive, int segs, float sway) {
    vec3 dir = normalize(tip - baseP), x, z;
    frameFor(dir, x, z);
    float len = length(tip - baseP);
    float slope = std::atan2(r, len);
    for (int s = 0; s < segs; s++) {
        float t0 = TAU * s / segs, t1 = TAU * (s + 1) / segs, tm = (t0 + t1) * 0.5f;
        vec3 r0 = x * std::cos(t0) + z * std::sin(t0), r1 = x * std::cos(t1) + z * std::sin(t1), rm = x * std::cos(tm) + z * std::sin(tm);
        uint32_t b = (uint32_t)m.v.size();
        m.v.push_back(mkV(baseP + r0 * r, normalize(r0 * std::cos(slope) + dir * std::sin(slope)), color, emissive, bone));
        m.v.push_back(mkV(tip, normalize(rm * std::cos(slope) + dir * std::sin(slope)), color, emissive, bone, sway));
        m.v.push_back(mkV(baseP + r1 * r, normalize(r1 * std::cos(slope) + dir * std::sin(slope)), color, emissive, bone));
        m.idx.insert(m.idx.end(), {b, b + 1, b + 2});
    }
}

void addPolygon(MeshData& m, const std::vector<vec3>& pts, vec3 color, int bone, float emissive, float sway, float thickness) {
    if (pts.size() < 3) return;
    vec3 n;
    for (size_t i = 0; i < pts.size(); i++) {
        const vec3& a = pts[i];
        const vec3& b = pts[(i + 1) % pts.size()];
        n.x += (a.y - b.y) * (a.z + b.z);
        n.y += (a.z - b.z) * (a.x + b.x);
        n.z += (a.x - b.x) * (a.y + b.y);
    }
    n = normalize(n);
    for (int side = 0; side < 2; side++) {
        vec3 nn = side == 0 ? n : -n;
        uint32_t base = (uint32_t)m.v.size();
        for (auto& p : pts) m.v.push_back(mkV(p + nn * (thickness * 0.5f), nn, color, emissive, bone, sway));
        for (uint32_t i = 1; i + 1 < pts.size(); i++) {
            if (side == 0) m.idx.insert(m.idx.end(), {base, base + i, base + i + 1});
            else m.idx.insert(m.idx.end(), {base, base + i + 1, base + i});
        }
    }
}

void addCrystal(MeshData& m, vec3 baseP, vec3 dir, float r, float len, vec3 color, float emissive) {
    dir = normalize(dir);
    vec3 x, z;
    frameFor(dir, x, z);
    vec3 top = baseP + dir * (len * 0.75f), tip = baseP + dir * len;
    for (int s = 0; s < 6; s++) {
        float t0 = TAU * s / 6, t1 = TAU * (s + 1) / 6;
        vec3 r0 = x * std::cos(t0) + z * std::sin(t0), r1 = x * std::cos(t1) + z * std::sin(t1);
        vec3 fn = normalize(r0 + r1);
        uint32_t b = (uint32_t)m.v.size();
        vec3 c = color * (0.85f + 0.15f * (s % 2));
        m.v.push_back(mkV(baseP + r0 * r, fn, c, emissive, 0));
        m.v.push_back(mkV(baseP + r1 * r, fn, c, emissive, 0));
        m.v.push_back(mkV(top + r1 * r, fn, c, emissive, 0));
        m.v.push_back(mkV(top + r0 * r, fn, c, emissive, 0));
        m.idx.insert(m.idx.end(), {b, b + 2, b + 1, b, b + 3, b + 2});
        vec3 tn = normalize(fn + dir * 0.6f);
        uint32_t t = (uint32_t)m.v.size();
        m.v.push_back(mkV(top + r0 * r, tn, c * 1.1f, emissive * 1.3f, 0));
        m.v.push_back(mkV(top + r1 * r, tn, c * 1.1f, emissive * 1.3f, 0));
        m.v.push_back(mkV(tip, tn, c * 1.2f, emissive * 1.6f, 0));
        m.idx.insert(m.idx.end(), {t, t + 2, t + 1});
    }
}

void addCard(MeshData& m, vec3 p0, vec3 p1, vec3 p2, vec3 p3, vec2 uv0, vec2 uv1, vec2 uv2, vec2 uv3, vec3 normal, vec3 color, float type, int bone,
             float sway, float ao, float emissive) {
    uint32_t b = (uint32_t)m.v.size();
    vec3 pts[4] = {p0, p1, p2, p3};
    vec2 uvs[4] = {uv0, uv1, uv2, uv3};
    for (int i = 0; i < 4; i++) {
        Vertex v{pts[i], normal, vec4(color, emissive), vec4((float)bone, (float)bone, 0, ao), sway};
        v.uv = uvs[i];
        v.card = type;
        m.v.push_back(v);
    }
    m.idx.insert(m.idx.end(), {b, b + 1, b + 2, b, b + 2, b + 3});
}

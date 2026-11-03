#pragma once
// Small 2D math kit. World space = board pixels (board.png is 2390x1050), y grows downward.

#include <algorithm>
#include <cmath>

namespace lc {

struct Vec2 {
    float x = 0.f, y = 0.f;

    constexpr Vec2() = default;
    constexpr Vec2(float x_, float y_) : x(x_), y(y_) {}

    constexpr Vec2 operator+(Vec2 o) const { return {x + o.x, y + o.y}; }
    constexpr Vec2 operator-(Vec2 o) const { return {x - o.x, y - o.y}; }
    constexpr Vec2 operator*(float s) const { return {x * s, y * s}; }
    constexpr Vec2 operator/(float s) const { return {x / s, y / s}; }
    Vec2& operator+=(Vec2 o) { x += o.x; y += o.y; return *this; }
    Vec2& operator-=(Vec2 o) { x -= o.x; y -= o.y; return *this; }
    float length() const { return std::sqrt(x * x + y * y); }
};

struct Rect {
    float x = 0.f, y = 0.f, w = 0.f, h = 0.f;

    static constexpr Rect fromCenter(Vec2 c, Vec2 size) { return {c.x - size.x * .5f, c.y - size.y * .5f, size.x, size.y}; }
    constexpr Vec2 center() const { return {x + w * .5f, y + h * .5f}; }
    constexpr Vec2 size() const { return {w, h}; }
    constexpr bool contains(Vec2 p) const { return p.x >= x && p.x <= x + w && p.y >= y && p.y <= y + h; }
    constexpr float right() const { return x + w; }
    constexpr float bottom() const { return y + h; }
    Rect inflated(float d) const { return {x - d, y - d, w + 2 * d, h + 2 * d}; }
    float overlapArea(const Rect& o) const {
        const float ix = std::max(0.f, std::min(right(), o.right()) - std::max(x, o.x));
        const float iy = std::max(0.f, std::min(bottom(), o.bottom()) - std::max(y, o.y));
        return ix * iy;
    }
};

struct Color {
    float r = 1.f, g = 1.f, b = 1.f, a = 1.f;
    constexpr Color withAlpha(float na) const { return {r, g, b, na}; }
    static constexpr Color rgb(unsigned hex, float a = 1.f) {
        return {((hex >> 16) & 0xff) / 255.f, ((hex >> 8) & 0xff) / 255.f, (hex & 0xff) / 255.f, a};
    }
};

/// Column-major 4x4 for GL uniforms.
struct Mat4 {
    float m[16] = {1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1};
};

/// Framerate-independent exponential approach: move `cur` toward `target` with the given sharpness.
inline float damp(float cur, float target, float sharpness, float dt) {
    return target + (cur - target) * std::exp(-sharpness * dt);
}
inline Vec2 damp(Vec2 cur, Vec2 target, float sharpness, float dt) {
    return {damp(cur.x, target.x, sharpness, dt), damp(cur.y, target.y, sharpness, dt)};
}

constexpr float kPi = 3.14159265358979f;

} // namespace lc

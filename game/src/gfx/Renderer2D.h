#pragma once
#include "core/Math.h"
#include "gfx/Shader.h"

namespace lc {

/// Immediate-mode quad renderer. Every primitive is a rotated quad whose fragment shader
/// evaluates a rounded-rectangle SDF, giving anti-aliased rounded card corners, soft
/// shadows and glow outlines from one shader. A few hundred draws per frame is nothing
/// for the GPU; batching can come later if the scene ever grows.
class Renderer2D {
public:
    Renderer2D() = default;
    ~Renderer2D();
    Renderer2D(const Renderer2D&) = delete;
    Renderer2D& operator=(const Renderer2D&) = delete;

    bool init();

    /// `unitsPerPixel` = world units covered by one screen pixel (drives edge anti-aliasing).
    void begin(const Mat4& viewProj, float unitsPerPixel);
    void end();

    struct Quad {
        Vec2 center;
        Vec2 size;
        float rotation = 0.f;  // radians
        float scaleX = 1.f;    // horizontal squash (card flip animation)
        GLuint texture = 0;    // 0 => flat colour
        Color tint{1, 1, 1, 1};
        float radius = 0.f;    // corner radius, world units
    };
    void draw(const Quad& q);

    /// Soft drop shadow under a rounded rect.
    void shadow(Vec2 center, Vec2 size, float rotation, float radius, float blur, Color color);

    /// Ring of `thickness` just inside the rounded rect edge; `softness` widens the falloff (glow).
    void outline(Vec2 center, Vec2 size, float rotation, float radius, float thickness, Color color,
                 float softness = 0.f);

    void fill(Vec2 center, Vec2 size, Color color, float radius = 0.f) {
        draw({center, size, 0.f, 1.f, 0, color, radius});
    }

private:
    void submit(Vec2 center, Vec2 quadSize, Vec2 shapeHalf, float rotation, float scaleX, GLuint tex,
                Color color, float radius, float feather, float border);

    Shader shader_;
    GLuint vao_ = 0, vbo_ = 0;
    float unitsPerPixel_ = 1.f;

    struct {
        GLint viewProj, center, size, rot, scaleX, useTex, color, half, radius, feather, border;
    } u_{};
};

} // namespace lc

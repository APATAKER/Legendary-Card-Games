#pragma once
#include "core/Math.h"

namespace lc {

/// 2D camera over the world (board pixels). Screen space is logical window points, y down.
/// Zoom and pan are animated toward targets so wheel zooming feels smooth.
class Camera {
public:
    void setViewport(Vec2 sizePoints) { viewport_ = sizePoints; }
    Vec2 viewport() const { return viewport_; }

    Vec2 worldToScreen(Vec2 w) const { return (w - center_) * zoom_ + viewport_ * 0.5f; }
    Vec2 screenToWorld(Vec2 s) const { return (s - viewport_ * 0.5f) / zoom_ + center_; }
    float zoom() const { return zoom_; }

    /// Frame `r` with `marginPx` of space around it. `instant` skips the animation.
    void fit(const Rect& r, float marginPx, bool instant = false);
    /// Multiply zoom by `factor`, keeping the world point under `screenPt` fixed.
    void zoomAt(Vec2 screenPt, float factor);
    /// Drag the world by a screen-space delta.
    void panBy(Vec2 screenDelta);
    void setLimits(const Rect& world) { limits_ = world; }

    void update(float dt);

    /// Projection * view: world -> clip space.
    Mat4 viewProj() const;
    /// Plain screen-space ortho (for overlays drawn in points).
    Mat4 screenProj() const;

private:
    void clampTarget();

    Vec2 viewport_{1280, 720};
    Vec2 center_{0, 0}, targetCenter_{0, 0};
    float zoom_ = 0.5f, targetZoom_ = 0.5f;
    float minZoom_ = 0.1f, maxZoom_ = 4.f;
    Rect limits_{-1e6f, -1e6f, 2e6f, 2e6f};
};

} // namespace lc

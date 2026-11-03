#include "game/Camera.h"

#include <algorithm>

namespace lc {

void Camera::fit(const Rect& r, float marginPx, bool instant) {
    const float zx = (viewport_.x - 2 * marginPx) / r.w;
    const float zy = (viewport_.y - 2 * marginPx) / r.h;
    targetZoom_ = std::max(0.05f, std::min(zx, zy));
    minZoom_ = targetZoom_ * 0.35f;
    maxZoom_ = std::max(4.f, targetZoom_ * 6.f);
    targetCenter_ = r.center();
    if (instant) {
        zoom_ = targetZoom_;
        center_ = targetCenter_;
    }
}

void Camera::zoomAt(Vec2 screenPt, float factor) {
    // Keep the point under the cursor fixed relative to the *target* state so repeated
    // wheel ticks during an animation compose correctly.
    const Vec2 anchor = (screenPt - viewport_ * 0.5f) / targetZoom_ + targetCenter_;
    targetZoom_ = std::clamp(targetZoom_ * factor, minZoom_, maxZoom_);
    targetCenter_ = anchor - (screenPt - viewport_ * 0.5f) / targetZoom_;
    clampTarget();
}

void Camera::panBy(Vec2 screenDelta) {
    targetCenter_ -= screenDelta / targetZoom_;
    center_ -= screenDelta / zoom_; // panning tracks the cursor 1:1, no lag
    clampTarget();
    center_.x = std::clamp(center_.x, limits_.x, limits_.right());
    center_.y = std::clamp(center_.y, limits_.y, limits_.bottom());
}

void Camera::clampTarget() {
    targetCenter_.x = std::clamp(targetCenter_.x, limits_.x, limits_.right());
    targetCenter_.y = std::clamp(targetCenter_.y, limits_.y, limits_.bottom());
}

void Camera::update(float dt) {
    // Interpolate zoom in log space; derive the centre so the zoom anchor stays put.
    const float k = 1.f - std::exp(-16.f * dt);
    zoom_ = std::exp(std::log(zoom_) + (std::log(targetZoom_) - std::log(zoom_)) * k);
    center_ = center_ + (targetCenter_ - center_) * k;
    if (std::abs(zoom_ - targetZoom_) < 1e-4f) zoom_ = targetZoom_;
}

Mat4 Camera::screenProj() const {
    Mat4 m;
    const float w = viewport_.x, h = viewport_.y;
    m.m[0] = 2.f / w;
    m.m[5] = -2.f / h;
    m.m[10] = -1.f;
    m.m[12] = -1.f;
    m.m[13] = 1.f;
    return m;
}

Mat4 Camera::viewProj() const {
    // screen = (world - center) * zoom + viewport/2, then screen -> clip
    Mat4 m = screenProj();
    const float tx = viewport_.x * 0.5f - center_.x * zoom_;
    const float ty = viewport_.y * 0.5f - center_.y * zoom_;
    Mat4 r;
    r.m[0] = m.m[0] * zoom_;
    r.m[5] = m.m[5] * zoom_;
    r.m[10] = m.m[10];
    r.m[12] = m.m[0] * tx + m.m[12];
    r.m[13] = m.m[5] * ty + m.m[13];
    return r;
}

} // namespace lc

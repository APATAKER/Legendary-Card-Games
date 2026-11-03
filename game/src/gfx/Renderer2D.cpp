#include "gfx/Renderer2D.h"

namespace lc {

namespace {

constexpr const char* kVert = R"(#version 330 core
layout(location = 0) in vec2 a_pos;     // unit quad, -0.5 .. 0.5
uniform mat4  u_viewProj;
uniform vec2  u_center;
uniform vec2  u_size;                   // quad extent (shape + padding for blur)
uniform float u_rot;
uniform float u_scaleX;
out vec2 v_local;                       // unrotated offset from centre, world units
out vec2 v_uv;
void main() {
    vec2 local = a_pos * u_size;
    v_local = local;
    v_uv = a_pos + 0.5;
    vec2 l = vec2(local.x * u_scaleX, local.y);
    float c = cos(u_rot), s = sin(u_rot);
    vec2 world = u_center + vec2(c * l.x - s * l.y, s * l.x + c * l.y);
    gl_Position = u_viewProj * vec4(world, 0.0, 1.0);
}
)";

constexpr const char* kFrag = R"(#version 330 core
in vec2 v_local;
in vec2 v_uv;
uniform sampler2D u_tex;
uniform int   u_useTex;
uniform vec4  u_color;                  // straight alpha
uniform vec2  u_half;                   // half extent of the rounded-rect shape
uniform float u_radius;
uniform float u_feather;
uniform float u_border;                 // > 0 => outline ring
out vec4 o_color;

float sdRoundRect(vec2 p, vec2 b, float r) {
    vec2 q = abs(p) - b + r;
    return length(max(q, 0.0)) + min(max(q.x, q.y), 0.0) - r;
}

void main() {
    float d = sdRoundRect(v_local, u_half, u_radius);
    float cov = 1.0 - smoothstep(-u_feather, u_feather, d);
    if (u_border > 0.0) cov *= smoothstep(-u_feather, u_feather, d + u_border);
    vec4 c = vec4(u_color.rgb * u_color.a, u_color.a);   // premultiply
    if (u_useTex == 1) c *= texture(u_tex, v_uv);        // textures are premultiplied on load
    o_color = c * cov;
}
)";

} // namespace

Renderer2D::~Renderer2D() {
    if (vbo_) glDeleteBuffers(1, &vbo_);
    if (vao_) glDeleteVertexArrays(1, &vao_);
}

bool Renderer2D::init() {
    if (!shader_.build(kVert, kFrag)) return false;

    const float verts[] = {-0.5f, -0.5f, 0.5f, -0.5f, -0.5f, 0.5f, 0.5f, 0.5f};
    glGenVertexArrays(1, &vao_);
    glGenBuffers(1, &vbo_);
    glBindVertexArray(vao_);
    glBindBuffer(GL_ARRAY_BUFFER, vbo_);
    glBufferData(GL_ARRAY_BUFFER, sizeof verts, verts, GL_STATIC_DRAW);
    glEnableVertexAttribArray(0);
    glVertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE, 2 * sizeof(float), nullptr);
    glBindVertexArray(0);

    u_.viewProj = shader_.uniform("u_viewProj");
    u_.center = shader_.uniform("u_center");
    u_.size = shader_.uniform("u_size");
    u_.rot = shader_.uniform("u_rot");
    u_.scaleX = shader_.uniform("u_scaleX");
    u_.useTex = shader_.uniform("u_useTex");
    u_.color = shader_.uniform("u_color");
    u_.half = shader_.uniform("u_half");
    u_.radius = shader_.uniform("u_radius");
    u_.feather = shader_.uniform("u_feather");
    u_.border = shader_.uniform("u_border");
    shader_.use();
    glUniform1i(shader_.uniform("u_tex"), 0);
    return true;
}

void Renderer2D::begin(const Mat4& viewProj, float unitsPerPixel) {
    unitsPerPixel_ = unitsPerPixel;
    glEnable(GL_BLEND);
    glBlendFunc(GL_ONE, GL_ONE_MINUS_SRC_ALPHA); // premultiplied alpha
    glDisable(GL_DEPTH_TEST);
    glDisable(GL_CULL_FACE);
    shader_.use();
    glUniformMatrix4fv(u_.viewProj, 1, GL_FALSE, viewProj.m);
    glActiveTexture(GL_TEXTURE0);
    glBindVertexArray(vao_);
}

void Renderer2D::end() {
    glBindVertexArray(0);
}

void Renderer2D::submit(Vec2 center, Vec2 quadSize, Vec2 shapeHalf, float rotation, float scaleX, GLuint tex,
                        Color color, float radius, float feather, float border) {
    glUniform2f(u_.center, center.x, center.y);
    glUniform2f(u_.size, quadSize.x, quadSize.y);
    glUniform1f(u_.rot, rotation);
    glUniform1f(u_.scaleX, scaleX);
    glUniform1i(u_.useTex, tex ? 1 : 0);
    if (tex) glBindTexture(GL_TEXTURE_2D, tex);
    glUniform4f(u_.color, color.r, color.g, color.b, color.a);
    glUniform2f(u_.half, shapeHalf.x, shapeHalf.y);
    glUniform1f(u_.radius, radius);
    glUniform1f(u_.feather, feather);
    glUniform1f(u_.border, border);
    glDrawArrays(GL_TRIANGLE_STRIP, 0, 4);
}

void Renderer2D::draw(const Quad& q) {
    submit(q.center, q.size, q.size * 0.5f, q.rotation, q.scaleX, q.texture, q.tint, q.radius,
           unitsPerPixel_ * 0.75f, 0.f);
}

void Renderer2D::shadow(Vec2 center, Vec2 size, float rotation, float radius, float blur, Color color) {
    const Vec2 pad{blur * 2.f, blur * 2.f};
    submit(center, size + pad * 2.f, size * 0.5f, rotation, 1.f, 0, color, radius, blur, 0.f);
}

void Renderer2D::outline(Vec2 center, Vec2 size, float rotation, float radius, float thickness, Color color,
                         float softness) {
    const float feather = std::max(unitsPerPixel_ * 0.75f, softness);
    const Vec2 pad{feather * 2.f, feather * 2.f};
    submit(center, size + pad * 2.f, size * 0.5f, rotation, 1.f, 0, color, radius, feather, thickness);
}

} // namespace lc

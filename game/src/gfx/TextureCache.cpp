#include "gfx/TextureCache.h"

#include "core/Log.h"

#include <stb_image.h>

#include <algorithm>
#include <fstream>

#ifndef GL_TEXTURE_MAX_ANISOTROPY
#define GL_TEXTURE_MAX_ANISOTROPY 0x84FE
#endif
#ifndef GL_MAX_TEXTURE_MAX_ANISOTROPY
#define GL_MAX_TEXTURE_MAX_ANISOTROPY 0x84FF
#endif

namespace lc {

TextureCache::TextureCache(std::filesystem::path root) : root_(std::move(root)) {
    const unsigned n = std::clamp(std::thread::hardware_concurrency() / 2u, 1u, 4u);
    for (unsigned i = 0; i < n; ++i) threads_.emplace_back([this] { worker(); });
}

TextureCache::~TextureCache() {
    {
        std::lock_guard lk(mtx_);
        quit_ = true;
    }
    cv_.notify_all();
    for (auto& t : threads_) t.join();
    for (auto& [_, tex] : textures_) glDeleteTextures(1, &tex.id);
}

bool TextureCache::decode(const std::filesystem::path& file, Decoded& out) {
    // Read through std::ifstream (wide paths on Windows) and decode from memory.
    std::ifstream f(file, std::ios::binary);
    if (!f) return false;
    std::vector<unsigned char> bytes((std::istreambuf_iterator<char>(f)), std::istreambuf_iterator<char>());
    int w = 0, h = 0, comp = 0;
    unsigned char* px = stbi_load_from_memory(bytes.data(), static_cast<int>(bytes.size()), &w, &h, &comp, 4);
    if (!px) return false;
    out.w = w;
    out.h = h;
    out.rgba.assign(px, px + static_cast<size_t>(w) * h * 4);
    stbi_image_free(px);
    if (comp == 4) { // premultiply so bilinear filtering never bleeds transparent colour
        for (size_t i = 0; i < out.rgba.size(); i += 4) {
            const unsigned a = out.rgba[i + 3];
            out.rgba[i + 0] = static_cast<unsigned char>(out.rgba[i + 0] * a / 255);
            out.rgba[i + 1] = static_cast<unsigned char>(out.rgba[i + 1] * a / 255);
            out.rgba[i + 2] = static_cast<unsigned char>(out.rgba[i + 2] * a / 255);
        }
    }
    return true;
}

Texture TextureCache::upload(const Decoded& d) {
    Texture t;
    t.w = d.w;
    t.h = d.h;
    glGenTextures(1, &t.id);
    glBindTexture(GL_TEXTURE_2D, t.id);
    glPixelStorei(GL_UNPACK_ALIGNMENT, 1);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, d.w, d.h, 0, GL_RGBA, GL_UNSIGNED_BYTE, d.rgba.data());
    glGenerateMipmap(GL_TEXTURE_2D);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR_MIPMAP_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    GLfloat maxAniso = 1.f;
    glGetFloatv(GL_MAX_TEXTURE_MAX_ANISOTROPY, &maxAniso);
    glTexParameterf(GL_TEXTURE_2D, GL_TEXTURE_MAX_ANISOTROPY, std::min(8.f, maxAniso));
    glGetError(); // anisotropy is core in 4.6 but tolerate drivers that reject it
    return t;
}

Texture TextureCache::loadNow(const std::string& rel) {
    if (auto it = textures_.find(rel); it != textures_.end()) return it->second;
    Decoded d;
    d.rel = rel;
    if (!decode(root_ / rel, d)) {
        LC_ERROR("failed to load texture %s", (root_ / rel).string().c_str());
        return {};
    }
    Texture t = upload(d);
    textures_[rel] = t;
    return t;
}

Texture TextureCache::request(const std::string& rel) {
    if (auto it = textures_.find(rel); it != textures_.end()) return it->second;
    if (!inFlight_[rel]) {
        inFlight_[rel] = true;
        {
            std::lock_guard lk(mtx_);
            queue_.push_back(rel);
        }
        cv_.notify_one();
    }
    return {};
}

void TextureCache::pump(int maxUploads) {
    for (int i = 0; i < maxUploads; ++i) {
        Decoded d;
        {
            std::lock_guard lk(mtx_);
            if (done_.empty()) return;
            d = std::move(done_.front());
            done_.pop_front();
        }
        if (d.rgba.empty()) {
            LC_ERROR("failed to decode %s", d.rel.c_str());
            textures_[d.rel] = {}; // remember the failure; don't retry every frame
            continue;
        }
        textures_[d.rel] = upload(d);
        inFlight_.erase(d.rel);
    }
}

size_t TextureCache::pendingCount() const {
    std::lock_guard lk(mtx_);
    return queue_.size() + done_.size();
}

void TextureCache::worker() {
    for (;;) {
        std::string rel;
        {
            std::unique_lock lk(mtx_);
            cv_.wait(lk, [&] { return quit_ || !queue_.empty(); });
            if (quit_) return;
            rel = std::move(queue_.front());
            queue_.pop_front();
        }
        Decoded d;
        d.rel = rel;
        decode(root_ / rel, d);
        std::lock_guard lk(mtx_);
        done_.push_back(std::move(d));
    }
}

} // namespace lc

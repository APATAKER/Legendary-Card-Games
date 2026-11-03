#pragma once
#include <glad/glad.h>

#include <condition_variable>
#include <deque>
#include <filesystem>
#include <mutex>
#include <string>
#include <thread>
#include <unordered_map>
#include <vector>

namespace lc {

struct Texture {
    GLuint id = 0;
    int w = 0, h = 0;
    explicit operator bool() const { return id != 0; }
};

/// Loads textures by asset-relative path. Card art is decoded on worker threads and
/// uploaded on the GL thread in `pump()`, so the frame never stalls on JPEG decoding.
/// All textures are stored with premultiplied alpha and full mip chains.
class TextureCache {
public:
    explicit TextureCache(std::filesystem::path root);
    ~TextureCache();
    TextureCache(const TextureCache&) = delete;
    TextureCache& operator=(const TextureCache&) = delete;

    /// Blocking load (startup assets: board, card back).
    Texture loadNow(const std::string& rel);

    /// Non-blocking: returns the texture if resident, otherwise queues it and returns an empty one.
    Texture request(const std::string& rel);

    /// Upload finished decodes. Call once per frame on the GL thread.
    void pump(int maxUploads = 6);

    size_t residentCount() const { return textures_.size(); }
    size_t pendingCount() const;

private:
    struct Decoded {
        std::string rel;
        int w = 0, h = 0;
        std::vector<unsigned char> rgba; // empty => failed
    };

    static bool decode(const std::filesystem::path& file, Decoded& out);
    static Texture upload(const Decoded& d);
    void worker();

    std::filesystem::path root_;
    std::unordered_map<std::string, Texture> textures_;
    std::unordered_map<std::string, bool> inFlight_; // GL thread only

    mutable std::mutex mtx_;
    std::condition_variable cv_;
    std::deque<std::string> queue_;  // guarded by mtx_
    std::deque<Decoded> done_;       // guarded by mtx_
    bool quit_ = false;              // guarded by mtx_
    std::vector<std::thread> threads_;
};

} // namespace lc

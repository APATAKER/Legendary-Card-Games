#pragma once
#include <glad/glad.h>

#include <string_view>

namespace lc {

/// Owns a linked GL program. Compile errors are logged and leave the shader invalid.
class Shader {
public:
    Shader() = default;
    ~Shader();
    Shader(const Shader&) = delete;
    Shader& operator=(const Shader&) = delete;

    bool build(std::string_view vertexSrc, std::string_view fragmentSrc);
    void use() const { glUseProgram(program_); }
    GLint uniform(const char* name) const { return glGetUniformLocation(program_, name); }
    bool valid() const { return program_ != 0; }

private:
    GLuint program_ = 0;
};

} // namespace lc

#include "gfx/Shader.h"

#include "core/Log.h"

#include <string>

namespace lc {

namespace {

GLuint compile(GLenum type, std::string_view src) {
    GLuint s = glCreateShader(type);
    const char* p = src.data();
    const GLint len = static_cast<GLint>(src.size());
    glShaderSource(s, 1, &p, &len);
    glCompileShader(s);
    GLint ok = 0;
    glGetShaderiv(s, GL_COMPILE_STATUS, &ok);
    if (!ok) {
        char log[2048];
        glGetShaderInfoLog(s, sizeof log, nullptr, log);
        LC_ERROR("%s shader compile failed:\n%s", type == GL_VERTEX_SHADER ? "vertex" : "fragment", log);
        glDeleteShader(s);
        return 0;
    }
    return s;
}

} // namespace

Shader::~Shader() {
    if (program_) glDeleteProgram(program_);
}

bool Shader::build(std::string_view vertexSrc, std::string_view fragmentSrc) {
    GLuint vs = compile(GL_VERTEX_SHADER, vertexSrc);
    GLuint fs = compile(GL_FRAGMENT_SHADER, fragmentSrc);
    if (!vs || !fs) {
        if (vs) glDeleteShader(vs);
        if (fs) glDeleteShader(fs);
        return false;
    }
    GLuint prog = glCreateProgram();
    glAttachShader(prog, vs);
    glAttachShader(prog, fs);
    glLinkProgram(prog);
    glDeleteShader(vs);
    glDeleteShader(fs);
    GLint ok = 0;
    glGetProgramiv(prog, GL_LINK_STATUS, &ok);
    if (!ok) {
        char log[2048];
        glGetProgramInfoLog(prog, sizeof log, nullptr, log);
        LC_ERROR("shader link failed:\n%s", log);
        glDeleteProgram(prog);
        return false;
    }
    if (program_) glDeleteProgram(program_);
    program_ = prog;
    return true;
}

} // namespace lc

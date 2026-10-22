#include "gfx/shader.h"

#include <cstdio>
#include <string>

namespace pb {

namespace {
GLuint compile(GLenum type, const char* src) {
  const GLuint s = gl::CreateShader(type);
  gl::ShaderSource(s, 1, &src, nullptr);
  gl::CompileShader(s);
  GLint ok = 0;
  gl::GetShaderiv(s, GL_COMPILE_STATUS, &ok);
  if (!ok) {
    char log[4096];
    gl::GetShaderInfoLog(s, sizeof log, nullptr, log);
    std::fprintf(stderr, "shader compile error:\n%s\n", log);
    gl::DeleteShader(s);
    return 0;
  }
  return s;
}
}  // namespace

GLuint buildProgram(const char* vertexSrc, const char* fragmentSrc) {
  const GLuint vs = compile(GL_VERTEX_SHADER, vertexSrc);
  const GLuint fs = compile(GL_FRAGMENT_SHADER, fragmentSrc);
  if (!vs || !fs) return 0;
  const GLuint p = gl::CreateProgram();
  gl::AttachShader(p, vs);
  gl::AttachShader(p, fs);
  gl::LinkProgram(p);
  gl::DeleteShader(vs);
  gl::DeleteShader(fs);
  GLint ok = 0;
  gl::GetProgramiv(p, GL_LINK_STATUS, &ok);
  if (!ok) {
    char log[4096];
    gl::GetProgramInfoLog(p, sizeof log, nullptr, log);
    std::fprintf(stderr, "shader link error:\n%s\n", log);
    gl::DeleteProgram(p);
    return 0;
  }
  return p;
}

}  // namespace pb

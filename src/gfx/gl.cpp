#include "gfx/gl.h"

#include <cstdio>

namespace pb::gl {

#define PB_GL_DEFINE(type, name) type name = nullptr;
PB_GL_FUNCTIONS(PB_GL_DEFINE)
#undef PB_GL_DEFINE

bool load(void* (*getProc)(const char* name)) {
  bool ok = true;
#define PB_GL_LOAD(type, name)                                   \
  name = reinterpret_cast<type>(getProc("gl" #name));            \
  if (!name) {                                                   \
    std::fprintf(stderr, "OpenGL function gl%s missing\n", #name); \
    ok = false;                                                  \
  }
  PB_GL_FUNCTIONS(PB_GL_LOAD)
#undef PB_GL_LOAD
  return ok;
}

}  // namespace pb::gl

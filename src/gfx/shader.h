#pragma once

#include "gfx/gl.h"

namespace pb {

// Compiles and links a vertex+fragment program; 0 on failure (log on stderr).
GLuint buildProgram(const char* vertexSrc, const char* fragmentSrc);

}  // namespace pb

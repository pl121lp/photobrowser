#pragma once

// OpenGL 4.1 core function pointers, loaded at runtime (no GL loader library).
#include <GL/glcorearb.h>

namespace pb::gl {

#define PB_GL_FUNCTIONS(X)                                         \
  X(PFNGLGETSTRINGPROC, GetString)                                 \
  X(PFNGLGETERRORPROC, GetError)                                   \
  X(PFNGLGETINTEGERVPROC, GetIntegerv)                             \
  X(PFNGLVIEWPORTPROC, Viewport)                                   \
  X(PFNGLCLEARCOLORPROC, ClearColor)                               \
  X(PFNGLCLEARPROC, Clear)                                         \
  X(PFNGLENABLEPROC, Enable)                                       \
  X(PFNGLDISABLEPROC, Disable)                                     \
  X(PFNGLBLENDFUNCPROC, BlendFunc)                                 \
  X(PFNGLGENTEXTURESPROC, GenTextures)                             \
  X(PFNGLDELETETEXTURESPROC, DeleteTextures)                       \
  X(PFNGLBINDTEXTUREPROC, BindTexture)                             \
  X(PFNGLTEXIMAGE2DPROC, TexImage2D)                               \
  X(PFNGLTEXSUBIMAGE2DPROC, TexSubImage2D)                         \
  X(PFNGLTEXPARAMETERIPROC, TexParameteri)                         \
  X(PFNGLTEXPARAMETERFPROC, TexParameterf)                         \
  X(PFNGLPIXELSTOREIPROC, PixelStorei)                             \
  X(PFNGLREADPIXELSPROC, ReadPixels)                               \
  X(PFNGLACTIVETEXTUREPROC, ActiveTexture)                         \
  X(PFNGLCREATESHADERPROC, CreateShader)                           \
  X(PFNGLSHADERSOURCEPROC, ShaderSource)                           \
  X(PFNGLCOMPILESHADERPROC, CompileShader)                         \
  X(PFNGLGETSHADERIVPROC, GetShaderiv)                             \
  X(PFNGLGETSHADERINFOLOGPROC, GetShaderInfoLog)                   \
  X(PFNGLDELETESHADERPROC, DeleteShader)                           \
  X(PFNGLCREATEPROGRAMPROC, CreateProgram)                         \
  X(PFNGLATTACHSHADERPROC, AttachShader)                           \
  X(PFNGLLINKPROGRAMPROC, LinkProgram)                             \
  X(PFNGLGETPROGRAMIVPROC, GetProgramiv)                           \
  X(PFNGLGETPROGRAMINFOLOGPROC, GetProgramInfoLog)                 \
  X(PFNGLDELETEPROGRAMPROC, DeleteProgram)                         \
  X(PFNGLUSEPROGRAMPROC, UseProgram)                               \
  X(PFNGLGETUNIFORMLOCATIONPROC, GetUniformLocation)               \
  X(PFNGLUNIFORM1IPROC, Uniform1i)                                 \
  X(PFNGLUNIFORM2FPROC, Uniform2f)                                 \
  X(PFNGLGENVERTEXARRAYSPROC, GenVertexArrays)                     \
  X(PFNGLBINDVERTEXARRAYPROC, BindVertexArray)                     \
  X(PFNGLDELETEVERTEXARRAYSPROC, DeleteVertexArrays)               \
  X(PFNGLGENBUFFERSPROC, GenBuffers)                               \
  X(PFNGLBINDBUFFERPROC, BindBuffer)                               \
  X(PFNGLBUFFERDATAPROC, BufferData)                               \
  X(PFNGLBUFFERSUBDATAPROC, BufferSubData)                         \
  X(PFNGLDELETEBUFFERSPROC, DeleteBuffers)                         \
  X(PFNGLMAPBUFFERRANGEPROC, MapBufferRange)                       \
  X(PFNGLUNMAPBUFFERPROC, UnmapBuffer)                             \
  X(PFNGLFENCESYNCPROC, FenceSync)                                 \
  X(PFNGLCLIENTWAITSYNCPROC, ClientWaitSync)                       \
  X(PFNGLDELETESYNCPROC, DeleteSync)                               \
  X(PFNGLVERTEXATTRIBPOINTERPROC, VertexAttribPointer)             \
  X(PFNGLENABLEVERTEXATTRIBARRAYPROC, EnableVertexAttribArray)     \
  X(PFNGLVERTEXATTRIBDIVISORPROC, VertexAttribDivisor)             \
  X(PFNGLDRAWARRAYSINSTANCEDPROC, DrawArraysInstanced)

#define PB_GL_DECLARE(type, name) extern type name;
PB_GL_FUNCTIONS(PB_GL_DECLARE)
#undef PB_GL_DECLARE

// Loads all functions above; false if any is missing.
bool load(void* (*getProc)(const char* name));

}  // namespace pb::gl

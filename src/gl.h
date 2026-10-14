// Tiny OpenGL 3.3 core loader: types, the constants we use, and function
// pointers resolved at runtime through SDL_GL_GetProcAddress.
#pragma once
#include <cstddef>

typedef unsigned int GLenum;
typedef unsigned int GLuint;
typedef int GLint;
typedef int GLsizei;
typedef float GLfloat;
typedef unsigned char GLboolean;
typedef unsigned int GLbitfield;
typedef char GLchar;
typedef ptrdiff_t GLsizeiptr;
typedef ptrdiff_t GLintptr;
typedef unsigned char GLubyte;
typedef unsigned long long GLuint64;

enum : GLenum {
    GL_FALSE_ = 0, GL_TRUE_ = 1,
    GL_POINTS = 0x0000, GL_LINES = 0x0001, GL_TRIANGLES = 0x0004,
    GL_DEPTH_BUFFER_BIT = 0x0100, GL_COLOR_BUFFER_BIT = 0x4000,
    GL_LEQUAL = 0x0203, GL_LESS = 0x0201,
    GL_SRC_ALPHA = 0x0302, GL_ONE_MINUS_SRC_ALPHA = 0x0303,
    GL_CULL_FACE = 0x0B44, GL_DEPTH_TEST = 0x0B71, GL_BLEND = 0x0BE2,
    GL_UNPACK_ALIGNMENT = 0x0CF5, GL_PACK_ALIGNMENT = 0x0D05,
    GL_TEXTURE_2D = 0x0DE1, GL_TEXTURE_BORDER_COLOR = 0x1004,
    GL_UNSIGNED_BYTE = 0x1401, GL_UNSIGNED_INT = 0x1405, GL_FLOAT = 0x1406,
    GL_DEPTH_COMPONENT = 0x1902, GL_RED = 0x1903, GL_RGB = 0x1907,
    GL_VENDOR = 0x1F00, GL_RENDERER = 0x1F01, GL_VERSION = 0x1F02,
    GL_NEAREST = 0x2600, GL_LINEAR = 0x2601,
    GL_TEXTURE_MAG_FILTER = 0x2800, GL_TEXTURE_MIN_FILTER = 0x2801,
    GL_TEXTURE_WRAP_S = 0x2802, GL_TEXTURE_WRAP_T = 0x2803,
    GL_POLYGON_OFFSET_FILL = 0x8037, GL_MULTISAMPLE = 0x809D,
    GL_CLAMP_TO_BORDER = 0x812D, GL_CLAMP_TO_EDGE = 0x812F,
    GL_DEPTH_COMPONENT24 = 0x81A6, GL_R8 = 0x8229,
    GL_TEXTURE0 = 0x84C0,
    GL_TEXTURE_COMPARE_MODE = 0x884C, GL_TEXTURE_COMPARE_FUNC = 0x884D, GL_COMPARE_REF_TO_TEXTURE = 0x884E,
    GL_ARRAY_BUFFER = 0x8892, GL_ELEMENT_ARRAY_BUFFER = 0x8893,
    GL_STREAM_DRAW = 0x88E0, GL_STATIC_DRAW = 0x88E4, GL_DYNAMIC_DRAW = 0x88E8,
    GL_FRAGMENT_SHADER = 0x8B30, GL_VERTEX_SHADER = 0x8B31,
    GL_COMPILE_STATUS = 0x8B81, GL_LINK_STATUS = 0x8B82,
    GL_PROGRAM_POINT_SIZE = 0x8642,
    GL_FRAMEBUFFER_COMPLETE = 0x8CD5, GL_DEPTH_ATTACHMENT = 0x8D00, GL_FRAMEBUFFER = 0x8D40,
    GL_NONE = 0, GL_BACK = 0x0405,
    GL_ONE = 1, GL_RGBA = 0x1908, GL_HALF_FLOAT = 0x140B, GL_RG = 0x8227, GL_R16F = 0x822D,
    GL_RGBA16F = 0x881A, GL_RGBA32F = 0x8814, GL_R11F_G11F_B10F = 0x8C3A, GL_DEPTH_COMPONENT32F = 0x8CAC,
    GL_READ_FRAMEBUFFER = 0x8CA8, GL_DRAW_FRAMEBUFFER = 0x8CA9, GL_COLOR_ATTACHMENT0 = 0x8CE0,
    GL_RENDERBUFFER = 0x8D41, GL_MAX_SAMPLES = 0x8D57,
    // Texture arrays and filtering (the ground's photo-scanned materials).
    GL_TEXTURE_2D_ARRAY = 0x8C1A, GL_SRGB8_ALPHA8 = 0x8C43, GL_RGBA8 = 0x8058, GL_REPEAT = 0x2901,
    GL_LINEAR_MIPMAP_LINEAR = 0x2703, GL_TEXTURE_MAX_ANISOTROPY = 0x84FE, GL_MAX_TEXTURE_MAX_ANISOTROPY = 0x84FF,
    GL_TEXTURE_MAX_LEVEL = 0x813D,
    // Instanced props (propdraw.cpp): vertex formats, cut-out leaves, GPU timing.
    GL_BYTE = 0x1400, GL_SHORT = 0x1402, GL_UNSIGNED_SHORT = 0x1403, GL_SRGB8 = 0x8C41,
    GL_SAMPLE_ALPHA_TO_COVERAGE = 0x809E, GL_TIME_ELAPSED = 0x88BF, GL_QUERY_RESULT = 0x8866,
    GL_QUERY_RESULT_AVAILABLE = 0x8867, GL_LINEAR_MIPMAP_NEAREST = 0x2701,
};

#define CZ_GL_FUNCS(X)                                                                         \
    X(void, Clear, (GLbitfield))                                                               \
    X(void, ClearColor, (GLfloat, GLfloat, GLfloat, GLfloat))                                  \
    X(void, Viewport, (GLint, GLint, GLsizei, GLsizei))                                        \
    X(void, Enable, (GLenum))                                                                  \
    X(void, Disable, (GLenum))                                                                 \
    X(void, DepthFunc, (GLenum))                                                               \
    X(void, DepthMask, (GLboolean))                                                            \
    X(void, BlendFunc, (GLenum, GLenum))                                                       \
    X(void, PolygonOffset, (GLfloat, GLfloat))                                                 \
    X(void, PixelStorei, (GLenum, GLint))                                                      \
    X(void, ReadPixels, (GLint, GLint, GLsizei, GLsizei, GLenum, GLenum, void*))               \
    X(const GLubyte*, GetString, (GLenum))                                                     \
    X(GLuint, CreateShader, (GLenum))                                                          \
    X(void, ShaderSource, (GLuint, GLsizei, const GLchar* const*, const GLint*))               \
    X(void, CompileShader, (GLuint))                                                           \
    X(void, GetShaderiv, (GLuint, GLenum, GLint*))                                             \
    X(void, GetShaderInfoLog, (GLuint, GLsizei, GLsizei*, GLchar*))                            \
    X(void, DeleteShader, (GLuint))                                                            \
    X(GLuint, CreateProgram, (void))                                                           \
    X(void, AttachShader, (GLuint, GLuint))                                                    \
    X(void, LinkProgram, (GLuint))                                                             \
    X(void, GetProgramiv, (GLuint, GLenum, GLint*))                                            \
    X(void, GetProgramInfoLog, (GLuint, GLsizei, GLsizei*, GLchar*))                           \
    X(void, UseProgram, (GLuint))                                                              \
    X(GLint, GetUniformLocation, (GLuint, const GLchar*))                                      \
    X(void, Uniform1i, (GLint, GLint))                                                         \
    X(void, Uniform1f, (GLint, GLfloat))                                                       \
    X(void, Uniform2f, (GLint, GLfloat, GLfloat))                                              \
    X(void, Uniform3f, (GLint, GLfloat, GLfloat, GLfloat))                                     \
    X(void, Uniform4f, (GLint, GLfloat, GLfloat, GLfloat, GLfloat))                            \
    X(void, UniformMatrix4fv, (GLint, GLsizei, GLboolean, const GLfloat*))                     \
    X(void, UniformMatrix3fv, (GLint, GLsizei, GLboolean, const GLfloat*))                     \
    X(void, Uniform4fv, (GLint, GLsizei, const GLfloat*))                                      \
    X(void, GenVertexArrays, (GLsizei, GLuint*))                                               \
    X(void, BindVertexArray, (GLuint))                                                         \
    X(void, GenBuffers, (GLsizei, GLuint*))                                                    \
    X(void, BindBuffer, (GLenum, GLuint))                                                      \
    X(void, BufferData, (GLenum, GLsizeiptr, const void*, GLenum))                             \
    X(void, BufferSubData, (GLenum, GLintptr, GLsizeiptr, const void*))                        \
    X(void, VertexAttribPointer, (GLuint, GLint, GLenum, GLboolean, GLsizei, const void*))     \
    X(void, EnableVertexAttribArray, (GLuint))                                                 \
    X(void, DrawArrays, (GLenum, GLint, GLsizei))                                              \
    X(void, DrawElements, (GLenum, GLsizei, GLenum, const void*))                              \
    X(void, GenTextures, (GLsizei, GLuint*))                                                   \
    X(void, BindTexture, (GLenum, GLuint))                                                     \
    X(void, ActiveTexture, (GLenum))                                                           \
    X(void, TexImage2D, (GLenum, GLint, GLint, GLsizei, GLsizei, GLint, GLenum, GLenum, const void*)) \
    X(void, TexSubImage2D, (GLenum, GLint, GLint, GLint, GLsizei, GLsizei, GLenum, GLenum, const void*)) \
    X(void, TexParameteri, (GLenum, GLenum, GLint))                                            \
    X(void, TexParameterfv, (GLenum, GLenum, const GLfloat*))                                  \
    X(void, GenFramebuffers, (GLsizei, GLuint*))                                               \
    X(void, BindFramebuffer, (GLenum, GLuint))                                                 \
    X(void, FramebufferTexture2D, (GLenum, GLenum, GLenum, GLuint, GLint))                     \
    X(GLenum, CheckFramebufferStatus, (GLenum))                                                \
    X(void, DrawBuffer, (GLenum))                                                              \
    X(void, ReadBuffer, (GLenum))                                                              \
    X(void, DeleteTextures, (GLsizei, const GLuint*))                                          \
    X(void, DeleteFramebuffers, (GLsizei, const GLuint*))                                      \
    X(void, GenRenderbuffers, (GLsizei, GLuint*))                                              \
    X(void, DeleteRenderbuffers, (GLsizei, const GLuint*))                                     \
    X(void, BindRenderbuffer, (GLenum, GLuint))                                                \
    X(void, RenderbufferStorageMultisample, (GLenum, GLsizei, GLenum, GLsizei, GLsizei))       \
    X(void, FramebufferRenderbuffer, (GLenum, GLenum, GLenum, GLuint))                         \
    X(void, BlitFramebuffer, (GLint, GLint, GLint, GLint, GLint, GLint, GLint, GLint, GLbitfield, GLenum)) \
    X(void, GetIntegerv, (GLenum, GLint*))                                                     \
    X(void, GetFloatv, (GLenum, GLfloat*))                                                     \
    X(void, TexImage3D, (GLenum, GLint, GLint, GLsizei, GLsizei, GLsizei, GLint, GLenum, GLenum, const void*)) \
    X(void, TexSubImage3D, (GLenum, GLint, GLint, GLint, GLint, GLsizei, GLsizei, GLsizei, GLenum, GLenum, const void*)) \
    X(void, TexParameterf, (GLenum, GLenum, GLfloat))                                          \
    X(void, GenerateMipmap, (GLenum))                                                          \
    X(void, VertexAttribDivisor, (GLuint, GLuint))                                             \
    X(void, DrawElementsInstanced, (GLenum, GLsizei, GLenum, const void*, GLsizei))            \
    X(void, DrawArraysInstanced, (GLenum, GLint, GLsizei, GLsizei))                            \
    X(void, DeleteBuffers, (GLsizei, const GLuint*))                                           \
    X(void, DeleteVertexArrays, (GLsizei, const GLuint*))                                      \
    X(void, GenQueries, (GLsizei, GLuint*))                                                    \
    X(void, BeginQuery, (GLenum, GLuint))                                                      \
    X(void, EndQuery, (GLenum))                                                                \
    X(void, GetQueryObjectiv, (GLuint, GLenum, GLint*))                                        \
    X(void, GetQueryObjectui64v, (GLuint, GLenum, GLuint64*))

namespace gl {
#define CZ_DECLARE(ret, name, args) \
    typedef ret(*PFN_##name) args;  \
    extern PFN_##name name;
CZ_GL_FUNCS(CZ_DECLARE)
#undef CZ_DECLARE

// Resolves every function; returns the name of the first missing one, or nullptr.
const char* load(void* (*getProc)(const char*));
// Uniform names are linked-program constants; avoid repeated driver lookups per wheel/pass.
GLint uniformLocation(GLuint program, const char* name);
}  // namespace gl

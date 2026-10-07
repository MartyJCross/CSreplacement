// Tiny OpenGL 3.3 core loader: only the functions this prototype uses. No system GL headers needed.
#pragma once
#include <cstddef>
#include <cstdint>

#if defined(_WIN32)
#define GLAPIENTRY __stdcall
#else
#define GLAPIENTRY
#endif

typedef unsigned int GLenum;
typedef unsigned int GLuint;
typedef int GLint;
typedef int GLsizei;
typedef unsigned int GLbitfield;
typedef unsigned char GLboolean;
typedef float GLfloat;
typedef char GLchar;
typedef unsigned char GLubyte;
typedef std::ptrdiff_t GLsizeiptr;
typedef std::ptrdiff_t GLintptr;

#define GL_FALSE 0
#define GL_TRUE 1
#define GL_DEPTH_BUFFER_BIT 0x00000100
#define GL_COLOR_BUFFER_BIT 0x00004000
#define GL_TRIANGLES 0x0004
#define GL_LEQUAL 0x0203
#define GL_LESS 0x0201
#define GL_SRC_ALPHA 0x0302
#define GL_ONE_MINUS_SRC_ALPHA 0x0303
#define GL_BACK 0x0405
#define GL_CULL_FACE 0x0B44
#define GL_DEPTH_TEST 0x0B71
#define GL_BLEND 0x0BE2
#define GL_UNPACK_ALIGNMENT 0x0CF5
#define GL_PACK_ALIGNMENT 0x0D05
#define GL_TEXTURE_2D 0x0DE1
#define GL_UNSIGNED_BYTE 0x1401
#define GL_FLOAT 0x1406
#define GL_RED 0x1903
#define GL_RGBA 0x1908
#define GL_VENDOR 0x1F00
#define GL_RENDERER 0x1F01
#define GL_VERSION 0x1F02
#define GL_NEAREST 0x2600
#define GL_TEXTURE_MAG_FILTER 0x2800
#define GL_TEXTURE_MIN_FILTER 0x2801
#define GL_TEXTURE_WRAP_S 0x2802
#define GL_TEXTURE_WRAP_T 0x2803
#define GL_CLAMP_TO_EDGE 0x812F
#define GL_TEXTURE0 0x84C0
#define GL_ARRAY_BUFFER 0x8892
#define GL_STREAM_DRAW 0x88E0
#define GL_STATIC_DRAW 0x88E4
#define GL_DYNAMIC_DRAW 0x88E8
#define GL_FRAGMENT_SHADER 0x8B30
#define GL_VERTEX_SHADER 0x8B31
#define GL_COMPILE_STATUS 0x8B81
#define GL_LINK_STATUS 0x8B82
#define GL_R8 0x8229

#define GL_FUNCTIONS(X)                                                                              \
    X(const GLubyte*, GetString, (GLenum name))                                                      \
    X(void, Viewport, (GLint x, GLint y, GLsizei w, GLsizei h))                                      \
    X(void, ClearColor, (GLfloat r, GLfloat g, GLfloat b, GLfloat a))                                \
    X(void, Clear, (GLbitfield mask))                                                                \
    X(void, Finish, (void))                                                                          \
    X(void, Enable, (GLenum cap))                                                                    \
    X(void, Disable, (GLenum cap))                                                                   \
    X(void, DepthFunc, (GLenum func))                                                                \
    X(void, DepthMask, (GLboolean flag))                                                             \
    X(void, ColorMask, (GLboolean r, GLboolean g, GLboolean b, GLboolean a))                         \
    X(void, BlendFunc, (GLenum s, GLenum d))                                                         \
    X(void, CullFace, (GLenum mode))                                                                 \
    X(void, PixelStorei, (GLenum pname, GLint param))                                                \
    X(void, ReadPixels, (GLint x, GLint y, GLsizei w, GLsizei h, GLenum fmt, GLenum type, void* px)) \
    X(void, GenTextures, (GLsizei n, GLuint * t))                                                    \
    X(void, BindTexture, (GLenum target, GLuint t))                                                  \
    X(void, TexImage2D, (GLenum target, GLint level, GLint ifmt, GLsizei w, GLsizei h, GLint border, \
                         GLenum fmt, GLenum type, const void* px))                                   \
    X(void, TexParameteri, (GLenum target, GLenum pname, GLint param))                               \
    X(void, ActiveTexture, (GLenum tex))                                                             \
    X(void, DrawArrays, (GLenum mode, GLint first, GLsizei count))                                   \
    X(void, DrawArraysInstanced, (GLenum mode, GLint first, GLsizei count, GLsizei instances))       \
    X(void, GenVertexArrays, (GLsizei n, GLuint * a))                                                \
    X(void, BindVertexArray, (GLuint a))                                                             \
    X(void, GenBuffers, (GLsizei n, GLuint * b))                                                     \
    X(void, BindBuffer, (GLenum target, GLuint b))                                                   \
    X(void, BufferData, (GLenum target, GLsizeiptr size, const void* data, GLenum usage))            \
    X(void, BufferSubData, (GLenum target, GLintptr off, GLsizeiptr size, const void* data))         \
    X(void, VertexAttribPointer, (GLuint i, GLint size, GLenum type, GLboolean norm, GLsizei stride, \
                                  const void* ptr))                                                  \
    X(void, EnableVertexAttribArray, (GLuint i))                                                     \
    X(void, VertexAttribDivisor, (GLuint i, GLuint divisor))                                         \
    X(GLuint, CreateShader, (GLenum type))                                                           \
    X(void, ShaderSource, (GLuint s, GLsizei n, const GLchar* const* src, const GLint* len))         \
    X(void, CompileShader, (GLuint s))                                                               \
    X(void, GetShaderiv, (GLuint s, GLenum pname, GLint * out))                                      \
    X(void, GetShaderInfoLog, (GLuint s, GLsizei max, GLsizei * len, GLchar * log))                  \
    X(void, DeleteShader, (GLuint s))                                                                \
    X(GLuint, CreateProgram, (void))                                                                 \
    X(void, AttachShader, (GLuint p, GLuint s))                                                      \
    X(void, LinkProgram, (GLuint p))                                                                 \
    X(void, GetProgramiv, (GLuint p, GLenum pname, GLint * out))                                     \
    X(void, GetProgramInfoLog, (GLuint p, GLsizei max, GLsizei * len, GLchar * log))                 \
    X(void, UseProgram, (GLuint p))                                                                  \
    X(GLint, GetUniformLocation, (GLuint p, const GLchar* name))                                     \
    X(void, Uniform1i, (GLint loc, GLint v))                                                         \
    X(void, Uniform2f, (GLint loc, GLfloat a, GLfloat b))                                            \
    X(void, Uniform3f, (GLint loc, GLfloat a, GLfloat b, GLfloat c))                                 \
    X(void, UniformMatrix4fv, (GLint loc, GLsizei n, GLboolean transpose, const GLfloat* m))

#define GL_DECLARE(ret, name, args) \
    typedef ret(GLAPIENTRY* PFN_gl##name) args; \
    extern PFN_gl##name gl##name;
GL_FUNCTIONS(GL_DECLARE)
#undef GL_DECLARE

// Loads all functions using the given proc-address getter. Returns the name of the first missing
// function, or nullptr on success.
const char* loadGL(void* (*getProc)(const char*));

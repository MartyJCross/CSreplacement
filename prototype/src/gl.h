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
typedef uint64_t GLuint64;
typedef struct __GLsync* GLsync;

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
#define GL_RGB 0x1907
#define GL_RGB8 0x8051
#define GL_LINEAR 0x2601
#define GL_LINEAR_MIPMAP_LINEAR 0x2703
#define GL_REPEAT 0x2901
#define GL_TEXTURE1 0x84C1
#define GL_TEXTURE_2D_ARRAY 0x8C1A
#define GL_TEXTURE_MAX_ANISOTROPY 0x84FE      // EXT_texture_filter_anisotropic (everywhere in practice)
#define GL_MAX_TEXTURE_MAX_ANISOTROPY 0x84FF
#define GL_NONE 0
#define GL_FRONT 0x0404
#define GL_UNSIGNED_SHORT 0x1403
#define GL_UNSIGNED_INT 0x1405
#define GL_DEPTH_COMPONENT 0x1902
#define GL_DEPTH_COMPONENT16 0x81A5
#define GL_DEPTH_COMPONENT24 0x81A6
#define GL_DEPTH24_STENCIL8 0x88F0
#define GL_RGBA8 0x8058
#define GL_TEXTURE2 0x84C2
#define GL_TEXTURE3 0x84C3
#define GL_TEXTURE_BORDER_COLOR 0x1004
#define GL_CLAMP_TO_BORDER 0x812D
#define GL_TEXTURE_COMPARE_MODE 0x884C
#define GL_TEXTURE_COMPARE_FUNC 0x884D
#define GL_COMPARE_REF_TO_TEXTURE 0x884E
#define GL_POLYGON_OFFSET_FILL 0x8037
#define GL_MAX_TEXTURE_SIZE 0x0D33
#define GL_MAX_SAMPLES 0x8D57
#define GL_FRAMEBUFFER 0x8D40
#define GL_READ_FRAMEBUFFER 0x8CA8
#define GL_DRAW_FRAMEBUFFER 0x8CA9
#define GL_RENDERBUFFER 0x8D41
#define GL_COLOR_ATTACHMENT0 0x8CE0
#define GL_DEPTH_ATTACHMENT 0x8D00
#define GL_DEPTH_STENCIL_ATTACHMENT 0x821A
#define GL_FRAMEBUFFER_COMPLETE 0x8CD5
#define GL_SYNC_GPU_COMMANDS_COMPLETE 0x9117
#define GL_SYNC_FLUSH_COMMANDS_BIT 0x00000001

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
    X(void, TexParameterf, (GLenum target, GLenum pname, GLfloat param))                             \
    X(void, TexImage3D, (GLenum target, GLint level, GLint internal, GLsizei w, GLsizei h, GLsizei d, \
                         GLint border, GLenum fmt, GLenum type, const void* px))                     \
    X(void, GenerateMipmap, (GLenum target))                                                         \
    X(void, GetFloatv, (GLenum pname, GLfloat * data))                                               \
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
    X(void, Uniform1f, (GLint loc, GLfloat v))                                                       \
    X(void, Uniform2f, (GLint loc, GLfloat a, GLfloat b))                                            \
    X(void, Uniform3f, (GLint loc, GLfloat a, GLfloat b, GLfloat c))                                 \
    X(void, Uniform4f, (GLint loc, GLfloat a, GLfloat b, GLfloat c, GLfloat d))                      \
    X(void, UniformMatrix4fv, (GLint loc, GLsizei n, GLboolean transpose, const GLfloat* m))        \
    X(GLsync, FenceSync, (GLenum condition, GLbitfield flags))                                       \
    X(GLenum, ClientWaitSync, (GLsync sync, GLbitfield flags, GLuint64 timeout))                     \
    X(void, DeleteSync, (GLsync sync))                                                               \
    X(void, GetIntegerv, (GLenum pname, GLint * data))                                               \
    X(void, DeleteTextures, (GLsizei n, const GLuint* t))                                            \
    X(void, DeleteBuffers, (GLsizei n, const GLuint* b))                                             \
    X(void, DeleteVertexArrays, (GLsizei n, const GLuint* a))                                        \
    X(void, TexParameterfv, (GLenum target, GLenum pname, const GLfloat* v))                         \
    X(void, PolygonOffset, (GLfloat factor, GLfloat units))                                          \
    X(void, DrawBuffer, (GLenum buf))                                                                \
    X(void, ReadBuffer, (GLenum buf))                                                                \
    X(void, GenFramebuffers, (GLsizei n, GLuint * f))                                                \
    X(void, DeleteFramebuffers, (GLsizei n, const GLuint* f))                                        \
    X(void, BindFramebuffer, (GLenum target, GLuint f))                                              \
    X(void, FramebufferTexture2D, (GLenum target, GLenum att, GLenum textarget, GLuint t, GLint lvl)) \
    X(GLenum, CheckFramebufferStatus, (GLenum target))                                               \
    X(void, GenRenderbuffers, (GLsizei n, GLuint * r))                                               \
    X(void, DeleteRenderbuffers, (GLsizei n, const GLuint* r))                                       \
    X(void, BindRenderbuffer, (GLenum target, GLuint r))                                             \
    X(void, RenderbufferStorage, (GLenum target, GLenum ifmt, GLsizei w, GLsizei h))                 \
    X(void, RenderbufferStorageMultisample, (GLenum target, GLsizei samples, GLenum ifmt, GLsizei w, \
                                             GLsizei h))                                             \
    X(void, FramebufferRenderbuffer, (GLenum target, GLenum att, GLenum rbtarget, GLuint r))         \
    X(void, BlitFramebuffer, (GLint sx0, GLint sy0, GLint sx1, GLint sy1, GLint dx0, GLint dy0,      \
                              GLint dx1, GLint dy1, GLbitfield mask, GLenum filter))

#define GL_DECLARE(ret, name, args) \
    typedef ret(GLAPIENTRY* PFN_gl##name) args; \
    extern PFN_gl##name gl##name;
GL_FUNCTIONS(GL_DECLARE)
#undef GL_DECLARE

// Loads all functions using the given proc-address getter. Returns the name of the first missing
// function, or nullptr on success.
const char* loadGL(void* (*getProc)(const char*));

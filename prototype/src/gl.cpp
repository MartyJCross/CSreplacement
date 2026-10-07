#include "gl.h"

#define GL_DEFINE(ret, name, args) PFN_gl##name gl##name = nullptr;
GL_FUNCTIONS(GL_DEFINE)
#undef GL_DEFINE

const char* loadGL(void* (*getProc)(const char*)) {
#define GL_LOAD(ret, name, args)                                         \
    gl##name = reinterpret_cast<PFN_gl##name>(getProc("gl" #name));      \
    if (!gl##name) return "gl" #name;
    GL_FUNCTIONS(GL_LOAD)
#undef GL_LOAD
    return nullptr;
}

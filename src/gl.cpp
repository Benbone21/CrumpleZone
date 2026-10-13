#include "gl.h"

namespace gl {
#define CZ_DEFINE(ret, name, args) PFN_##name name = nullptr;
CZ_GL_FUNCS(CZ_DEFINE)
#undef CZ_DEFINE

const char* load(void* (*getProc)(const char*)) {
#define CZ_LOAD(ret, name, args)                                  \
    name = reinterpret_cast<PFN_##name>(getProc("gl" #name));     \
    if (!name) return "gl" #name;
    CZ_GL_FUNCS(CZ_LOAD)
#undef CZ_LOAD
    return nullptr;
}
}  // namespace gl

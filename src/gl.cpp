#include "gl.h"
#include <map>
#include <cstring>
#include <string>

namespace gl {
namespace {
std::map<GLuint, std::map<std::string, GLint, std::less<>>> uniformLocations;
}
#define CZ_DEFINE(ret, name, args) PFN_##name name = nullptr;
CZ_GL_FUNCS(CZ_DEFINE)
CZ_GL_OPTIONAL(CZ_DEFINE)
#undef CZ_DEFINE

const char* load(void* (*getProc)(const char*)) {
    uniformLocations.clear();
#define CZ_LOAD(ret, name, args)                                  \
    name = reinterpret_cast<PFN_##name>(getProc("gl" #name));     \
    if (!name) return "gl" #name;
    CZ_GL_FUNCS(CZ_LOAD)
#undef CZ_LOAD
#define CZ_LOAD_OPTIONAL(ret, name, args) name = reinterpret_cast<PFN_##name>(getProc("gl" #name));
    CZ_GL_OPTIONAL(CZ_LOAD_OPTIONAL)
#undef CZ_LOAD_OPTIONAL
    return nullptr;
}

float maxTextureAnisotropy() {
    GLint count = 0;
    GetIntegerv(GL_NUM_EXTENSIONS, &count);
    for (GLint i = 0; i < count; i++) {
        const char* name = (const char*)GetStringi(GL_EXTENSIONS, (GLuint)i);
        if (name && (std::strcmp(name, "GL_EXT_texture_filter_anisotropic") == 0 ||
                     std::strcmp(name, "GL_ARB_texture_filter_anisotropic") == 0)) {
            GLfloat limit = 1;
            GetFloatv(GL_MAX_TEXTURE_MAX_ANISOTROPY, &limit);
            return limit > 1 ? limit : 1;
        }
    }
    return 1;
}

GLint uniformLocation(GLuint program, const char* name) {
    auto& locations = uniformLocations[program];
    const auto it = locations.find(name);
    if (it != locations.end()) return it->second;
    const GLint location = GetUniformLocation(program, name);
    locations.emplace(name, location);
    return location;
}
}  // namespace gl

#include "gl.h"
#include <map>
#include <string>

namespace gl {
namespace {
std::map<GLuint, std::map<std::string, GLint, std::less<>>> uniformLocations;
}
#define CZ_DEFINE(ret, name, args) PFN_##name name = nullptr;
CZ_GL_FUNCS(CZ_DEFINE)
#undef CZ_DEFINE

const char* load(void* (*getProc)(const char*)) {
    uniformLocations.clear();
#define CZ_LOAD(ret, name, args)                                  \
    name = reinterpret_cast<PFN_##name>(getProc("gl" #name));     \
    if (!name) return "gl" #name;
    CZ_GL_FUNCS(CZ_LOAD)
#undef CZ_LOAD
    return nullptr;
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

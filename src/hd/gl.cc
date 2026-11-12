#include "hd/gl.h"
#include <cstring>

namespace fallout::hd {
bool hdGlLoad()
{
    const char* version = reinterpret_cast<const char*>(glGetString(GL_VERSION));
    if (version == nullptr || std::strstr(version, "OpenGL ES") == nullptr) return false;
    GLint major = 0, minor = 0;
    glGetIntegerv(GL_MAJOR_VERSION, &major);
    glGetIntegerv(GL_MINOR_VERSION, &minor);
    return major > 3 || (major == 3 && minor >= 1);
}
bool hdGlHasBc7()
{
    GLint count = 0;
    glGetIntegerv(GL_NUM_EXTENSIONS, &count);
    for (int i = 0; i < count; i++) {
        const char* ext = reinterpret_cast<const char*>(glGetStringi(GL_EXTENSIONS, i));
        if (ext && std::strcmp(ext, "GL_EXT_texture_compression_bptc") == 0) return true;
    }
    return false;
}
} // namespace fallout::hd

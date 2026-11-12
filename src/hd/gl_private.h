#ifndef FALLOUT_HD_GL_PRIVATE_H
#define FALLOUT_HD_GL_PRIVATE_H
#include "hd/gl.h"
#include <string>
#include <vector>
namespace fallout::hd {
// Raw GL shares SDL's context. Save every binding/state our passes change,
// including indexed ranges and pixel transfer state (SDL caches its state).
class GlState {
    GLint program, active, vao, drawFbo, readFbo, viewport[4], scissor[4], imageCount;
    GLint tex[12][3], images[7][6], ssbo[4], ssboGeneric, packBuffer, unpackBuffer;
    GLint pack[4], unpack[6], blend[6];
    GLint64 ssboStart[4], ssboSize[4];
    GLboolean enabled[5], mask[4];
    static constexpr GLenum caps[5] = { GL_BLEND, GL_DEPTH_TEST, GL_STENCIL_TEST, GL_SCISSOR_TEST, GL_CULL_FACE };

public:
    GlState();
    ~GlState();
};
GLuint glCompute(const std::string& source);
GLuint glProgram(const char* vertex, const char* fragment);
GLuint glTexture(int width, int height, GLenum format, GLenum type, const void* pixels = nullptr);
void glUpload(GLuint texture, int x, int y, int w, int h, GLenum format, GLenum type, const void* data);
void glBuffer(GLuint buffer, const void* data, size_t bytes);
} // namespace fallout::hd
#endif

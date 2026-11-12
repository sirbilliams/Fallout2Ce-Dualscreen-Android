#ifndef FALLOUT_HD_COMPUTE_H_
#define FALLOUT_HD_COMPUTE_H_
#include "hd/gl.h"
#include <cstdio>
#include <cstdlib>
#include <string>
#include <vector>
namespace fallout::hd::compute {
// All resources belong to the current GLES context. Call shutdown before
// destroying that context. The presentation owner restores SDL's GL state.
inline GLuint program(const char* source, const char* entry)
{
    std::string text(source);
    text += std::string("\nvoid main(){ ") + entry + "(gl_GlobalInvocationID); }\n";
    GLuint shader = glCreateShader(GL_COMPUTE_SHADER);
    const char* p = text.c_str();
    glShaderSource(shader, 1, &p, nullptr);
    glCompileShader(shader);
    GLint ok = 0;
    glGetShaderiv(shader, GL_COMPILE_STATUS, &ok);
    char log[8192];
    if (!ok) {
        glGetShaderInfoLog(shader, sizeof(log), nullptr, log);
        std::fprintf(stderr, "HD %s: %s\n", entry, log);
        glDeleteShader(shader);
        return 0;
    }
    GLuint result = glCreateProgram();
    glAttachShader(result, shader);
    glLinkProgram(result);
    glDeleteShader(shader);
    glGetProgramiv(result, GL_LINK_STATUS, &ok);
    if (!ok) {
        glGetProgramInfoLog(result, sizeof(log), nullptr, log);
        std::fprintf(stderr, "HD %s: %s\n", entry, log);
        glDeleteProgram(result);
        return 0;
    }
    return result;
}
struct Texture {
    GLuint id = 0;
    int width = 0, height = 0, layers = 1;
    GLenum format = GL_RGBA16F;
    Texture() = default;
    Texture(const Texture&) = delete;
    Texture& operator=(const Texture&) = delete;
    ~Texture()
    {
        glDeleteTextures(1, &id);
    }
    GLenum target() const
    {
        return layers > 1 ? GL_TEXTURE_2D_ARRAY : GL_TEXTURE_2D;
    }
    bool ensure(int w, int h, GLenum fmt = GL_RGBA16F, int count = 1)
    {
        if (id && width == w && height == h && format == fmt && layers == count) return true;
        glDeleteTextures(1, &id);
        glGenTextures(1, &id);
        width = w;
        height = h;
        format = fmt;
        layers = count;
        glBindTexture(target(), id);
        if (count > 1)
            glTexStorage3D(target(), 1, fmt, w, h, count);
        else
            glTexStorage2D(target(), 1, fmt, w, h);
        glTexParameteri(target(), GL_TEXTURE_MIN_FILTER, (fmt == GL_R32UI || fmt == GL_R32F) ? GL_NEAREST : GL_LINEAR);
        glTexParameteri(target(), GL_TEXTURE_MAG_FILTER, (fmt == GL_R32UI || fmt == GL_R32F) ? GL_NEAREST : GL_LINEAR);
        glTexParameteri(target(), GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
        glTexParameteri(target(), GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
        return glGetError() == GL_NO_ERROR;
    }
};
inline void sample(int slot, GLuint id, GLenum target = GL_TEXTURE_2D)
{
    glActiveTexture(GL_TEXTURE0 + slot);
    glBindTexture(target, id);
    glBindSampler(slot, 0);
}
inline void image(int slot, const Texture& t)
{
    glBindImageTexture(slot, t.id, 0, t.layers > 1, 0, GL_WRITE_ONLY, t.format);
}
inline void buffer(GLuint& id, GLuint slot, const void* data, size_t bytes)
{
    if (!id) glGenBuffers(1, &id);
    glBindBuffer(GL_SHADER_STORAGE_BUFFER, id);
    glBufferData(GL_SHADER_STORAGE_BUFFER, bytes ? bytes : 16, data, GL_STREAM_DRAW);
    glBindBufferBase(GL_SHADER_STORAGE_BUFFER, slot, id);
}
// Optional synchronous benchmark only; production dispatch never waits.
// EXT_disjoint_timer_query is resolved by the desktop harness; timing is
// performed there around individual dispatches, without changing the API.
inline void dispatch(int w, int h, int z = 1)
{
    glDispatchCompute((w + 7) / 8, (h + 7) / 8, z);
    glMemoryBarrier(GL_SHADER_IMAGE_ACCESS_BARRIER_BIT | GL_TEXTURE_FETCH_BARRIER_BIT | GL_FRAMEBUFFER_BARRIER_BIT);
}
inline bool read(GLuint tex, int w, int h, std::vector<float>& rgba)
{
    GLint previous = 0;
    glGetIntegerv(GL_READ_FRAMEBUFFER_BINDING, &previous);
    GLuint fbo;
    glGenFramebuffers(1, &fbo);
    glBindFramebuffer(GL_READ_FRAMEBUFFER, fbo);
    glFramebufferTexture2D(GL_READ_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, tex, 0);
    bool ok = glCheckFramebufferStatus(GL_READ_FRAMEBUFFER) == GL_FRAMEBUFFER_COMPLETE;
    if (ok) {
        rgba.resize(size_t(w) * h * 4);
        glReadPixels(0, 0, w, h, GL_RGBA, GL_FLOAT, rgba.data());
        ok = glGetError() == GL_NO_ERROR;
    }
    glBindFramebuffer(GL_READ_FRAMEBUFFER, previous);
    glDeleteFramebuffers(1, &fbo);
    return ok;
}
} // namespace fallout::hd::compute
#endif
